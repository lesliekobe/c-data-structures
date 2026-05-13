// SPDX-License-Identifier: GPL-2.0
/*
 * mbox_kern.c - Shared Memory Mailbox Kernel Module
 *
 * Implements a kernel-level mailbox framework using shared memory
 * for inter-process communication. Messages are stored in a ring buffer
 * allocated via mmap() and accessed atomically using lock-free algorithms.
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/shmem_fs.h>
#include <linux/file.h>
#include <linux/pid_namespace.h>
#include <linux/pagemap.h>

#include "mbox.h"

/* ========================================================================
 * Constants and Limits
 * ======================================================================== */

#define DRV_NAME    "mbox"
#define DRV_VERSION "1.0.0"

#define MAX_MAILBOXES    64
#define MBOX_MIN_DEPTH   2
#define MBOX_MAX_DEPTH   4096
#define MBOX_MIN_SIZE    64
#define MBOX_MAX_SIZE    (1024 * 1024)

#define MBOX_SHMEM_SIZE(mbox) \
    (sizeof(struct mbox_queue) + ((mbox)->depth * (sizeof(struct mbox_msg_entry) + (mbox)->msg_size)))

/* ========================================================================
 * Data Structures
 * ======================================================================== */

/* Per-message entry in the ring buffer */
struct mbox_msg_entry {
    uint32_t msg_id;
    uint32_t timestamp_sec;
    uint32_t timestamp_nsec;
    uint32_t sender_pid;
    uint8_t  channel;
    uint8_t  flags;
    uint16_t size;
    uint8_t  payload[];  /* Variable size payload */
};

/* Mailbox queue header (at start of shared memory) */
struct mbox_queue {
    /* Producer state */
    uint32_t head ____cacheline_aligned;   /* Write index */
    uint32_t tail;                          /* Read index (for consumer) */

    /* Consumer state (per reader) */
    uint32_t reader_tail[16];               /* Per-channel read indices */

    /* Queue configuration */
    uint32_t depth;                         /* Max messages */
    uint32_t msg_size;                      /* Max payload size */
    uint32_t count;                         /* Current message count */
    uint32_t total_sent;                    /* Total messages sent */
    uint32_t total_recv;                    /* Total messages received */
    uint32_t messages_dropped;              /* Dropped due to overflow */
    uint64_t bytes_sent;
    uint64_t bytes_recv;

    /* Synchronization */
    uint32_t lock;                          /* Spinlock hint */
    uint32_t flags;                         /* Queue flags */

    /* Padding to align payload area */
    uint8_t  reserved[48];

    /* Message entries follow immediately after this header */
};

/* Mailbox instance (kernel-side) */
struct mbox_instance {
    char name[MBOX_NAME_LEN];
    int  id;

    /* Shared memory backing */
    void *shmem;                            /* Kernel virtual address */
    size_t shmem_size;                      /* Total size */
    dma_addr_t shmem_phys;                  /* Physical (for DMA) */

    /* Queue pointers */
    struct mbox_queue *queue;
    struct mbox_msg_entry *entries;         /* Points into shmem after queue */

    /* Reference counting */
    atomic_t refcount;
    atomic_t open_count;

    /* Synchronization */
    spinlock_t lock;
    wait_queue_head_t waitq;

    /* Character device association */
    struct cdev cdev;
    dev_t devno;
    struct device *device;
};

/* Global state */
static struct {
    struct class *class;
    dev_t devno;
    int major;
    struct mutex instances_lock;
    struct mbox_instance *instances[MAX_MAILBOXES];
    int next_id;
} mbox_global;

/* Per-file private data */
struct mbox_filedata {
    struct mbox_instance *mbox;
    uint32_t channel_mask;      /* Which channels this fd listens to */
    uint32_t flags;
    int reader_idx;             /* Reader index for mmap reader tracking */
};

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

static inline bool mbox_queue_full(struct mbox_queue *q)
{
    return q->count >= q->depth;
}

static inline bool mbox_queue_empty(struct mbox_queue *q)
{
    return q->count == 0;
}

static inline struct mbox_msg_entry *mbox_get_entry(struct mbox_instance *mbox, uint32_t idx)
{
    return &mbox->entries[idx % mbox->queue->depth];
}

/* ========================================================================
 * Mailbox Queue Operations
 * ======================================================================== */

/* Add message to queue (producer side) - lock-free for single producer */
static int mbox_enqueue(struct mbox_instance *mbox,
                        uint8_t channel,
                        const void *payload,
                        size_t size,
                        uint32_t flags)
{
    struct mbox_queue *q = mbox->queue;
    struct mbox_msg_entry *entry;
    unsigned long irq_flags;
    uint32_t idx;
    int ret = -1;

    if (size > mbox->queue->msg_size) {
        return -MBOX_ERR_TOOBIG;
    }

    spin_lock_irqsave(&mbox->lock, irq_flags);

    if (mbox_queue_full(q)) {
        /* Drop oldest message if urgent */
        if (flags & MBOX_MSG_FLAG_URGENT) {
            q->messages_dropped++;
            /* Move head back to overwrite */
        }
        ret = -MBOX_ERR_NOBUFS;
        goto unlock;
    }

    /* Get entry at head */
    idx = q->head;
    entry = mbox_get_entry(mbox, idx);

    /* Fill entry */
    entry->msg_id = q->total_sent + 1;
    entry->timestamp_sec = 0;  /* TODO: real time */
    entry->timestamp_nsec = 0;
    entry->sender_pid = current->pid;
    entry->channel = channel;
    entry->flags = flags & 0xFF;
    entry->size = size;

    if (payload && size > 0) {
        memcpy(entry->payload, payload, size);
    }

    /* Memory barrier to ensure data is visible before updating head */
    smp_wmb();

    /* Update head */
    q->head = idx + 1;
    q->count++;
    q->total_sent++;
    q->bytes_sent += size;

    ret = entry->msg_id;

unlock:
    spin_unlock_irqrestore(&mbox->lock, irq_flags);

    /* Wake up waiters */
    if (ret >= 0) {
        wake_up_interruptible(&mbox->waitq);
    }

    return ret;
}

/* Remove message from queue (consumer side) */
static int mbox_dequeue(struct mbox_instance *mbox,
                        uint32_t channel_mask,
                        void *payload,
                        size_t max_size,
                        int timeout_ms,
                        uint32_t *out_msg_id,
                        uint8_t *out_channel,
                        uint32_t *out_flags,
                        size_t *out_size)
{
    struct mbox_queue *q = mbox->queue;
    struct mbox_msg_entry *entry;
    unsigned long irq_flags;
    uint32_t idx;
    int ret = -1;
    int remaining_ms;
    ktime_t start, now;

    if (timeout_ms > 0) {
        start = ktime_get();
        remaining_ms = timeout_ms;
    }

    do {
        spin_lock_irqsave(&mbox->lock, irq_flags);

        /* Find next message matching channel mask */
        idx = q->tail;
        int tries = 0;

        while (tries < q->depth && idx != q->head) {
            entry = mbox_get_entry(mbox, idx);

            if ((1 << entry->channel) & channel_mask) {
                /* Found matching message */
                uint32_t size = entry->size;

                /* Return data */
                if (payload && size > 0 && size <= max_size) {
                    memcpy(payload, entry->payload, size);
                }

                if (out_msg_id) *out_msg_id = entry->msg_id;
                if (out_channel) *out_channel = entry->channel;
                if (out_flags) *out_flags = entry->flags;
                if (out_size) *out_size = size;

                /* Remove from queue (advance tail) */
                q->tail = idx + 1;
                q->count--;
                q->total_recv++;
                q->bytes_recv += size;

                ret = size;
                spin_unlock_irqrestore(&mbox->lock, irq_flags);
                goto done;
            }

            idx++;
            tries++;
        }

        spin_unlock_irqrestore(&mbox->lock, irq_flags);

        if (timeout_ms == 0) {
            /* Non-blocking - no message available */
            ret = -MBOX_ERR_WOULDBLOCK;
            break;
        }

        if (timeout_ms > 0) {
            now = ktime_get();
            remaining_ms = timeout_ms - (int)ktime_to_ms(ktime_sub(now, start));
            if (remaining_ms <= 0) {
                ret = -MBOX_ERR_TIMEOUT;
                break;
            }
        }

        /* Wait for message to arrive */
        ret = wait_event_interruptible_timeout(
            mbox->waitq,
            !mbox_queue_empty(q),
            msecs_to_jiffies(remaining_ms > 100 ? 100 : remaining_ms)
        );

        if (ret < 0) {
            /* Signal interrupted */
            ret = -MBOX_ERR_TIMEOUT;
            break;
        }

    } while (ret != 0);  /* ret == 0 means timeout expired */

done:
    return ret;
}

/* Peek at message without removing */
static int mbox_peek_nolock(struct mbox_instance *mbox,
                            uint32_t channel_mask,
                            void *payload,
                            size_t max_size,
                            uint32_t *out_msg_id,
                            uint8_t *out_channel,
                            uint32_t *out_flags,
                            size_t *out_size)
{
    struct mbox_queue *q = mbox->queue;
    struct mbox_msg_entry *entry;
    uint32_t idx = q->tail;
    int tries = 0;

    while (tries < q->depth && idx != q->head) {
        entry = mbox_get_entry(mbox, idx);

        if ((1 << entry->channel) & channel_mask) {
            if (payload && entry->size <= max_size) {
                memcpy(payload, entry->payload, entry->size);
            }
            if (out_msg_id) *out_msg_id = entry->msg_id;
            if (out_channel) *out_channel = entry->channel;
            if (out_flags) *out_flags = entry->flags;
            if (out_size) *out_size = entry->size;
            return entry->size;
        }

        idx++;
        tries++;
    }

    return -MBOX_ERR_WOULDBLOCK;
}

/* ========================================================================
 * File Operations
 * ======================================================================== */

static int mbox_open(struct inode *inode, struct file *filp)
{
    struct mbox_instance *mbox = container_of(inode->i_cdev, struct mbox_instance, cdev);
    struct mbox_filedata *filedata;

    filedata = kzalloc(sizeof(*filedata), GFP_KERNEL);
    if (!filedata)
        return -ENOMEM;

    filedata->mbox = mbox;
    filedata->channel_mask = 0xFFFF;  /* All channels by default */
    filedata->reader_idx = atomic_inc_return(&mbox->open_count);

    atomic_inc(&mbox->refcount);
    filp->private_data = filedata;

    return 0;
}

static int mbox_release(struct inode *inode, struct file *filp)
{
    struct mbox_filedata *filedata = filp->private_data;

    if (filedata) {
        if (filedata->mbox) {
            atomic_dec(&filedata->mbox->refcount);
        }
        kfree(filedata);
    }

    return 0;
}

static long mbox_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct mbox_filedata *fd = filp->private_data;
    struct mbox_instance *mbox = fd->mbox;
    int ret = 0;
    void __user *argp = (void __user *)arg;

    if (!mbox)
        return -ENODEV;

    switch (cmd) {
    case MBOX_IOC_CREATE: {
        /* Create is handled by mbox_create() - not via ioctl on open fd */
        ret = -ENOSYS;
        break;
    }

    case MBOX_IOC_SEND: {
        struct mbox_send_req req;
        void *buf;

        if (copy_from_user(&req, argp, sizeof(req)))
            return -EFAULT;

        if (req.size > MBOX_MAX_MSG_SIZE)
            return -MBOX_ERR_TOOBIG;

        buf = kzalloc(req.size, GFP_KERNEL);
        if (!buf)
            return -ENOMEM;

        /* Get payload from after the ioctl arg (using separate accessor) */
        /* For simplicity, we require payload via a separate write() call */

        ret = -EINVAL;
        kfree(buf);
        break;
    }

    case MBOX_IOC_RECV: {
        struct mbox_recv_req req;
        struct mbox_recv_req req_copy;

        if (copy_from_user(&req_copy, argp, sizeof(req_copy)))
            return -EFAULT;

        req = req_copy;

        /* Use temporary buffer for message payload */
        void *tmp = kzalloc(mbox->queue->msg_size, GFP_KERNEL);
        if (!tmp)
            return -ENOMEM;

        ret = mbox_dequeue(mbox, req.channel_mask, tmp, mbox->queue->msg_size,
                          req.timeout_ms,
                          &req.out_msg_id, &req.out_channel, &req.out_flags,
                          &req.out_size);

        if (ret >= 0 && req.out_size > 0) {
            if (copy_to_user(argp, &req, sizeof(req))) {
                ret = -EFAULT;
            } else {
                /* Actual payload via write to fd or separate mmap */
                ret = req.out_size;
            }
        } else if (ret == -MBOX_ERR_WOULDBLOCK || ret == -MBOX_ERR_TIMEOUT) {
            if (copy_to_user(argp, &req, sizeof(req)))
                ret = -EFAULT;
        }

        kfree(tmp);
        break;
    }

    case MBOX_IOC_FLUSH: {
        unsigned long irq_flags;
        spin_lock_irqsave(&mbox->lock, irq_flags);
        mbox->queue->head = 0;
        mbox->queue->tail = 0;
        mbox->queue->count = 0;
        spin_unlock_irqrestore(&mbox->lock, irq_flags);
        ret = 0;
        break;
    }

    case MBOX_IOC_STATS: {
        mbox_stats_t stats;
        memset(&stats, 0, sizeof(stats));

        stats.messages_sent = mbox->queue->total_sent;
        stats.messages_recv = mbox->queue->total_recv;
        stats.messages_dropped = mbox->queue->messages_dropped;
        stats.current_depth = mbox->queue->count;
        stats.max_depth = mbox->queue->depth;
        stats.bytes_sent = mbox->queue->bytes_sent;
        stats.bytes_recv = mbox->queue->bytes_recv;

        if (copy_to_user(argp, &stats, sizeof(stats)))
            ret = -EFAULT;
        else
            ret = 0;
        break;
    }

    case MBOX_IOC_SETFLAGS:
        fd->channel_mask = (uint32_t)arg;
        ret = 0;
        break;

    case MBOX_IOC_GETFLAGS:
        ret = fd->channel_mask;
        break;

    default:
        ret = -ENOTTY;
        break;
    }

    return ret;
}

/* Write to mailbox (send message) */
static ssize_t mbox_write(struct file *filp, const char __user *buf,
                          size_t count, loff_t *ppos)
{
    struct mbox_filedata *fd = filp->private_data;
    struct mbox_instance *mbox = fd->mbox;
    struct mbox_send_req __user *req;
    struct mbox_msg_entry *entry;
    unsigned long irq_flags;
    uint32_t msg_id;
    size_t payload_size;

    /* Extract send parameters from the buffer */
    if (count < sizeof(struct mbox_send_req)) {
        /* Just treat as simple message with channel 0 */
        payload_size = count;
        msg_id = mbox_enqueue(mbox, 0, buf, payload_size, 0);
    } else {
        /* First sizeof(req) bytes are the send request, rest is payload */
        struct mbox_send_req req_struct;
        if (copy_from_user(&req_struct, buf, sizeof(req_struct)))
            return -EFAULT;

        payload_size = count - sizeof(req_struct);
        if (payload_size > mbox->queue->msg_size)
            return -MBOX_ERR_TOOBIG;

        msg_id = mbox_enqueue(mbox, req_struct.channel,
                             buf + sizeof(req_struct), payload_size,
                             req_struct.flags);
    }

    if (msg_id < 0)
        return msg_id;

    return count;
}

/* Read from mailbox (receive message) */
static ssize_t mbox_read(struct file *filp, char __user *buf,
                         size_t count, loff_t *ppos)
{
    struct mbox_filedata *fd = filp->private_data;
    struct mbox_instance *mbox = fd->mbox;
    struct mbox_recv_req req;
    void *tmp;
    int ret;

    if (count < sizeof(req)) {
        /* Simple mode: just return message data */
        tmp = kzalloc(mbox->queue->msg_size, GFP_KERNEL);
        ret = mbox_dequeue(mbox, fd->channel_mask, tmp, mbox->queue->msg_size,
                          0, NULL, NULL, NULL, NULL);
        if (ret < 0) {
            kfree(tmp);
            return ret;
        }
        if (copy_to_user(buf, tmp, ret)) {
            kfree(tmp);
            return -EFAULT;
        }
        kfree(tmp);
        return ret;
    }

    /* Full mode: receive into request struct */
    if (copy_from_user(&req, buf, sizeof(req)))
        return -EFAULT;

    tmp = kzalloc(mbox->queue->msg_size, GFP_KERNEL);
    ret = mbox_dequeue(mbox, req.channel_mask, tmp, mbox->queue->msg_size,
                      req.timeout_ms, &req.out_msg_id, &req.out_channel,
                      &req.out_flags, &req.out_size);

    if (ret >= 0) {
        if (copy_to_user(buf, &req, sizeof(req))) {
            ret = -EFAULT;
        } else {
            /* Payload follows the request structure */
            ret = req.out_size;
        }
    }

    kfree(tmp);
    return ret;
}

/* Poll for message availability */
static unsigned int mbox_poll(struct file *filp, struct poll_table_struct *wait)
{
    struct mbox_filedata *fd = filp->private_data;
    struct mbox_instance *mbox = fd->mbox;
    unsigned int mask = 0;
    unsigned long irq_flags;

    poll_wait(filp, &mbox->waitq, wait);

    spin_lock_irqsave(&mbox->lock, irq_flags);
    if (mbox->queue->count > 0) {
        mask = POLLIN | POLLRDNORM;  /* Message available */
    }
    if (mbox->queue->count < mbox->queue->depth) {
        mask |= POLLOUT | POLLWRNORM;  /* Can send */
    }
    spin_unlock_irqrestore(&mbox->lock, irq_flags);

    return mask;
}

/* Memory map for shared memory access */
static int mbox_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct mbox_filedata *fd = filp->private_data;
    struct mbox_instance *mbox = fd->mbox;
    unsigned long pfn;
    size_t size = vma->vm_end - vma->vm_start;

    if (!mbox || !mbox->shmem)
        return -ENODEV;

    if (size > mbox->shmem_size)
        return -EINVAL;

    /* Map kernel memory to user space (COW) */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    /* Get physical address of shared memory */
    pfn = vmalloc_to_pfn(mbox->shmem);
    if (!pfn)
        return -EFAULT;

    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}

static const struct file_operations mbox_fops = {
    .owner          = THIS_MODULE,
    .open           = mbox_open,
    .release        = mbox_release,
    .unlocked_ioctl = mbox_ioctl,
    .read           = mbox_read,
    .write          = mbox_write,
    .poll           = mbox_poll,
    .mmap           = mbox_mmap,
};

/* ========================================================================
 * Mailbox Management
 * ======================================================================== */

/* Create a new mailbox */
static struct mbox_instance *mbox_create(const char *name,
                                          uint32_t depth,
                                          uint32_t msg_size)
{
    struct mbox_instance *mbox;
    size_t total_size;
    int id;

    if (!name || name[0] == '\0')
        return ERR_PTR(-EINVAL);

    if (depth < MBOX_MIN_DEPTH || depth > MBOX_MAX_DEPTH)
        return ERR_PTR(-EINVAL);

    if (msg_size < MBOX_MIN_SIZE || msg_size > MBOX_MAX_SIZE)
        return ERR_PTR(-EINVAL);

    /* Allocate instance */
    mbox = kzalloc(sizeof(*mbox), GFP_KERNEL);
    if (!mbox)
        return ERR_PTR(-ENOMEM);

    /* Calculate total shared memory size */
    total_size = sizeof(struct mbox_queue) +
                 (depth * (sizeof(struct mbox_msg_entry) + msg_size));

    /* Allocate shared memory using vmalloc (contiguous in kernel, mapped to user) */
    mbox->shmem = vmalloc(total_size);
    if (!mbox->shmem) {
        kfree(mbox);
        return ERR_PTR(-ENOMEM);
    }

    memset(mbox->shmem, 0, total_size);

    /* Initialize instance */
    strncpy(mbox->name, name, MBOX_NAME_LEN - 1);
    mbox->name[MBOX_NAME_LEN - 1] = '\0';

    mbox->shmem_size = total_size;
    mbox->queue = (struct mbox_queue *)mbox->shmem;
    mbox->entries = (struct mbox_msg_entry *)
                    ((uint8_t *)mbox->shmem + sizeof(struct mbox_queue));

    /* Configure queue */
    mbox->queue->depth = depth;
    mbox->queue->msg_size = msg_size;

    /* Initialize synchronization */
    atomic_set(&mbox->refcount, 0);
    atomic_set(&mbox->open_count, 0);
    spin_lock_init(&mbox->lock);
    init_waitqueue_head(&mbox->waitq);

    /* Initialize cdev */
    cdev_init(&mbox->cdev, &mbox_fops);
    mbox->cdev.owner = THIS_MODULE;

    /* Assign device number */
    mutex_lock(&mbox_global.instances_lock);
    id = mbox_global.next_id++;
    mbox->id = id;
    mbox->devno = MKDEV(mbox_global.major, id);

    if (id < MAX_MAILBOXES && !mbox_global.instances[id]) {
        mbox_global.instances[id] = mbox;
    } else {
        mutex_unlock(&mbox_global.instances_lock);
        vfree(mbox->shmem);
        kfree(mbox);
        return ERR_PTR(-ENOMEM);
    }
    mutex_unlock(&mbox_global.instances_lock);

    /* Add cdev to system */
    cdev_add(&mbox->cdev, mbox->devno, 1);

    /* Create device node */
    mbox->device = device_create(mbox_global.class, NULL, mbox->devno,
                                  NULL, "mbox-%s", name);

    dev_info(&mbox->device->parent ? *mbox->device : (struct device){},
             "Mailbox '%s' created: depth=%u, msg_size=%u, size=%zu\n",
             name, depth, msg_size, total_size);

    return mbox;
}

/* Destroy a mailbox */
static void mbox_destroy(struct mbox_instance *mbox)
{
    if (!mbox)
        return;

    device_destroy(mbox_global.class, mbox->devno);
    cdev_del(&mbox->cdev);

    mutex_lock(&mbox_global.instances_lock);
    if (mbox->id >= 0 && mbox->id < MAX_MAILBOXES) {
        mbox_global.instances[mbox->id] = NULL;
    }
    mutex_unlock(&mbox_global.instances_lock);

    if (mbox->shmem)
        vfree(mbox->shmem);

    kfree(mbox);
}

/* ========================================================================
 * Module Initialization
 * ======================================================================== */

static int __init mbox_init(void)
{
    int ret;

    /* Initialize global state */
    memset(&mbox_global, 0, sizeof(mbox_global));
    mutex_init(&mbox_global.instances_lock);

    /* Allocate major number */
    ret = alloc_chrdev_region(&mbox_global.devno, 0, MAX_MAILBOXES, DRV_NAME);
    if (ret) {
        pr_err("Failed to allocate chrdev region: %d\n", ret);
        return ret;
    }
    mbox_global.major = MAJOR(mbox_global.devno);

    /* Create device class */
    mbox_global.class = class_create(DRV_NAME);
    if (IS_ERR(mbox_global.class)) {
        pr_err("Failed to create class\n");
        unregister_chrdev_region(mbox_global.devno, MAX_MAILBOXES);
        return PTR_ERR(mbox_global.class);
    }

    pr_info("Mailbox driver v%s loaded (major=%d)\n",
            DRV_VERSION, mbox_global.major);

    return 0;
}

static void __exit mbox_exit(void)
{
    int i;

    /* Destroy all mailboxes */
    for (i = 0; i < MAX_MAILBOXES; i++) {
        if (mbox_global.instances[i]) {
            mbox_destroy(mbox_global.instances[i]);
        }
    }

    class_destroy(mbox_global.class);
    unregister_chrdev_region(mbox_global.devno, MAX_MAILBOXES);

    pr_info("Mailbox driver unloaded\n");
}

module_init(mbox_init);
module_exit(mbox_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenClaw Agent");
MODULE_DESCRIPTION("Shared Memory Mailbox Framework");
MODULE_VERSION(DRV_VERSION);