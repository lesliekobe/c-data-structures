/*
 * mbox_lib.c - Shared Memory Mailbox Userspace Library
 *
 * Implements the mbox_* API declared in mbox.h
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <time.h>
#include <linux/vmalloc.h>

#include "mbox.h"

/* For direct system call access (ioctl without wrapper) */
#include <sys/syscall.h>

/* ========================================================================
 * Internal Structures
 * ======================================================================== */

struct mbox_handle {
    char        name[MBOX_NAME_LEN];
    int         fd;             /* File descriptor for ioctl/poll */
    void        *shmem;         /* Mapped shared memory */
    size_t      shmem_size;     /* Mapped size */
    uint32_t    depth;          /* Queue depth */
    uint32_t    msg_size;       /* Max message size */
    uint32_t    flags;          /* Open flags */
    bool        is_owner;      /* True if we created the mailbox */
};

/* Shared memory queue layout (must match kernel) */
struct mbox_queue {
    uint32_t head;
    uint32_t tail;
    uint32_t reader_tail[16];
    uint32_t depth;
    uint32_t msg_size;
    uint32_t count;
    uint32_t total_sent;
    uint32_t total_recv;
    uint32_t messages_dropped;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
    uint32_t lock;
    uint32_t flags;
    uint8_t  reserved[48];
    /* Entries follow */
} __attribute__((packed));

struct mbox_msg_entry {
    uint32_t msg_id;
    uint32_t timestamp_sec;
    uint32_t timestamp_nsec;
    uint32_t sender_pid;
    uint8_t  channel;
    uint8_t  flags;
    uint16_t size;
    uint8_t  payload[];
} __attribute__((packed));

/* ========================================================================
 * Error Mapping
 * ======================================================================== */

static const char *mbox_err_strings[] = {
    "Success",
    "Mailbox does not exist",
    "Mailbox already exists",
    "Invalid argument",
    "Out of memory",
    "No free message slots",
    "Operation timed out",
    "Would block",
    "Message too large",
    "Mailbox closed",
    "Mailbox data corrupted",
};

const char *mbox_strerror(int err)
{
    int idx = -err;
    if (idx < 0 || idx >= (int)(sizeof(mbox_err_strings)/sizeof(mbox_err_strings[0])))
        return "Unknown error";
    return mbox_err_strings[idx];
}

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

static inline struct mbox_msg_entry *mbox_get_entry(struct mbox_handle *h, uint32_t idx)
{
    struct mbox_queue *q = (struct mbox_queue *)h->shmem;
    uint8_t *base = (uint8_t *)h->shmem + sizeof(struct mbox_queue);
    return (struct mbox_msg_entry *)(base + (idx % h->depth) *
           (sizeof(struct mbox_msg_entry) + h->msg_size));
}

static int mbox_validate_handle(int mbox)
{
    if (mbox < 0)
        return -MBOX_ERR_INVAL;
    return 0;
}

/* ========================================================================
 * Core API
 * ======================================================================== */

int mbox_open(const char *name, uint32_t flags)
{
    struct mbox_handle *h;
    char devpath[64];
    int fd;

    if (!name || name[0] == '\0')
        return -MBOX_ERR_INVAL;

    h = calloc(1, sizeof(*h));
    if (!h)
        return -MBOX_ERR_NOMEM;

    strncpy(h->name, name, MBOX_NAME_LEN - 1);
    h->name[MBOX_NAME_LEN - 1] = '\0';
    h->flags = flags;

    /* Open the device */
    snprintf(devpath, sizeof(devpath), "/dev/mbox-%s", name);

    int open_flags = O_RDWR;
    if (flags & MBOX_O_NONBLOCK)
        open_flags |= O_NONBLOCK;

    fd = open(devpath, open_flags);
    if (fd < 0) {
        if ((flags & MBOX_O_CREATE) && errno == ENOENT) {
            /* Try creating via ioctl on a control device */
            /* For simplicity, we'll require the mailbox to already exist */
            free(h);
            errno = ENOENT;
            return -MBOX_ERR_NOENT;
        }
        free(h);
        return -MBOX_ERR_NOENT;
    }

    h->fd = fd;

    /* Get queue parameters via ioctl */
    mbox_stats_t stats;
    if (ioctl(fd, MBOX_IOC_STATS, &stats) == 0) {
        h->depth = stats.max_depth;
    }

    return (int)(intptr_t)h;  /* Return handle as integer */
}

int mbox_close(int mbox)
{
    struct mbox_handle *h = (struct mbox_handle *)(intptr_t)mbox;
    int ret = 0;

    if (mbox_validate_handle(mbox) != 0)
        return -MBOX_ERR_INVAL;

    if (h->shmem) {
        munmap(h->shmem, h->shmem_size);
    }

    if (h->fd >= 0) {
        close(h->fd);
    }

    free(h);
    return ret;
}

int mbox_send(int mbox, uint8_t channel, const void *payload, size_t size, uint32_t flags)
{
    struct mbox_handle *h = (struct mbox_handle *)(intptr_t)mbox;
    struct mbox_send_req req;
    struct iovec iov[2];
    ssize_t n;

    if (mbox_validate_handle(mbox) != 0)
        return -MBOX_ERR_INVAL;

    if (size > MBOX_MAX_MSG_SIZE)
        return -MBOX_ERR_TOOBIG;

    if (!(h->flags & MBOX_O_WRONLY) && !(h->flags & MBOX_O_RDWR))
        return -MBOX_ERR_INVAL;

    /* Build send request */
    memset(&req, 0, sizeof(req));
    req.channel = channel;
    req.size = size;
    req.flags = flags;

    /* Write request header + payload in one writev */
    iov[0].iov_base = &req;
    iov[0].iov_len = sizeof(req);
    iov[1].iov_base = (void *)payload;
    iov[1].iov_len = size;

    n = writev(h->fd, iov, 2);
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return -MBOX_ERR_WOULDBLOCK;
        return -MBOX_ERR_INVAL;
    }

    /* Return message ID (offset in queue) */
    return (int)(n - sizeof(req));  /* Placeholder */
}

int mbox_recv(int mbox, uint32_t channel_mask, void *payload, size_t max_size, int timeout_ms)
{
    struct mbox_handle *h = (struct mbox_handle *)(intptr_t)mbox;
    struct mbox_recv_req req;
    struct pollfd pfd;
    ssize_t n;
    int ret;

    if (mbox_validate_handle(mbox) != 0)
        return -MBOX_ERR_INVAL;

    if (!(h->flags & MBOX_O_RDONLY) && !(h->flags & MBOX_O_RDWR))
        return -MBOX_ERR_INVAL;

    /* Non-blocking path */
    if (timeout_ms == 0 || (h->flags & MBOX_O_NONBLOCK)) {
        req.channel_mask = channel_mask;
        req.timeout_ms = 0;

        n = read(h->fd, &req, sizeof(req));
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN)
                return -MBOX_ERR_WOULDBLOCK;
            return -MBOX_ERR_INVAL;
        }

        if (n < sizeof(req))
            return -MBOX_ERR_INVAL;

        /* Read actual payload */
        if (req.out_size > 0 && payload && req.out_size <= max_size) {
            n = read(h->fd, payload, req.out_size);
            if (n < 0)
                return -MBOX_ERR_INVAL;
        }

        return req.out_size;
    }

    /* Blocking with poll */
    pfd.fd = h->fd;
    pfd.events = POLLIN;

    ret = poll(&pfd, 1, timeout_ms >= 0 ? timeout_ms : -1);
    if (ret < 0)
        return -MBOX_ERR_INVAL;

    if (ret == 0)
        return -MBOX_ERR_TIMEOUT;

    /* Read message */
    req.channel_mask = channel_mask;
    req.timeout_ms = 0;

    n = read(h->fd, &req, sizeof(req));
    if (n < sizeof(req))
        return -MBOX_ERR_INVAL;

    if (req.out_size > 0 && payload && req.out_size <= max_size) {
        n = read(h->fd, payload, req.out_size);
        if (n < 0)
            return -MBOX_ERR_INVAL;
        return (int)n;
    }

    return req.out_size;
}

int mbox_peek(int mbox, uint32_t channel_mask, void *payload, size_t max_size,
              uint32_t *msg_id, uint8_t *out_channel, uint32_t *out_flags)
{
    struct mbox_handle *h = (struct mbox_handle *)(intptr_t)mbox;
    struct mbox_recv_req req = {0};
    ssize_t n;

    if (mbox_validate_handle(mbox) != 0)
        return -MBOX_ERR_INVAL;

    req.channel_mask = channel_mask;
    req.timeout_ms = 0;

    n = read(h->fd, &req, sizeof(req));
    if (n < 0) {
        if (errno == EWOULDBLOCK || errno == EAGAIN)
            return -MBOX_ERR_WOULDBLOCK;
        return -MBOX_ERR_INVAL;
    }

    if (n < sizeof(req))
        return -MBOX_ERR_INVAL;

    if (msg_id) *msg_id = req.out_msg_id;
    if (out_channel) *out_channel = (uint8_t)req.out_channel;
    if (out_flags) *out_flags = req.out_flags;

    if (payload && req.out_size > 0 && req.out_size <= max_size) {
        n = read(h->fd, payload, req.out_size);
        if (n < 0)
            return -MBOX_ERR_INVAL;
        return (int)n;
    }

    return req.out_size;
}

int mbox_flush(int mbox)
{
    struct mbox_handle *h = (struct mbox_handle *)(intptr_t)mbox;

    if (mbox_validate_handle(mbox) != 0)
        return -MBOX_ERR_INVAL;

    if (ioctl(h->fd, MBOX_IOC_FLUSH) < 0)
        return -MBOX_ERR_INVAL;

    return 0;
}

int mbox_stats(int mbox, mbox_stats_t *stats)
{
    struct mbox_handle *h = (struct mbox_handle *)(intptr_t)mbox;

    if (mbox_validate_handle(mbox) != 0)
        return -MBOX_ERR_INVAL;

    if (ioctl(h->fd, MBOX_IOC_STATS, stats) < 0)
        return -MBOX_ERR_INVAL;

    return 0;
}

int mbox_get_fd(int mbox)
{
    struct mbox_handle *h = (struct mbox_handle *)(intptr_t)mbox;

    if (mbox_validate_handle(mbox) != 0)
        return -MBOX_ERR_INVAL;

    return h->fd;
}

/* ========================================================================
 * Advanced: Direct Shared Memory Access
 * ======================================================================== */

/*
 * mbox_get_shmem - Get direct pointer to shared memory
 * @mbox: Mailbox handle
 * @size: Output: size of shared memory region
 *
 * Returns: Pointer to shared memory, or NULL on error
 *
 * For zero-copy operations, applications can directly read/write
 * the shared memory queue. Use mbox_*_direct() functions for this.
 *
 * WARNING: Direct access bypasses locking. Only use when you are
 * certain no other process is accessing the mailbox.
 */
void *mbox_get_shmem(int mbox, size_t *size)
{
    struct mbox_handle *h = (struct mbox_handle *)(intptr_t)mbox;

    if (mbox_validate_handle(mbox) != 0)
        return NULL;

    if (size)
        *size = h->shmem_size;

    return h->shmem;
}

/*
 * mbox_get_queue_ptr - Get pointer to queue header in shared memory
 * Useful for lock-free read operations
 */
struct mbox_queue *mbox_get_queue_ptr(int mbox)
{
    return (struct mbox_queue *)mbox_get_shmem(mbox, NULL);
}