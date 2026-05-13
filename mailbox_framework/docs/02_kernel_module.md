# 第二章：内核模块设计

## 2.1 模块结构概览

```c
// mbox_kern.c 主要组成部分

// 1. 头文件和宏定义
#define DRV_NAME    "mbox"
#define MAX_MAILBOXES    64
#define MBOX_SHMEM_SIZE(mbox) ...

// 2. 数据结构
struct mbox_msg_entry { ... };     // 单条消息
struct mbox_queue { ... };         // 队列头
struct mbox_instance { ... };      // mailbox 实例
struct mbox_filedata { ... };      // 打开文件私有数据

// 3. 文件操作
static int mbox_open(...)
static int mbox_release(...)
static long mbox_ioctl(...)
static ssize_t mbox_read(...)
static ssize_t mbox_write(...)
static unsigned int mbox_poll(...)
static int mbox_mmap(...)

// 4. 核心队列操作
static int mbox_enqueue(...)
static int mbox_dequeue(...)
static int mbox_peek_nolock(...)

// 5. 模块生命周期
static int __init mbox_init(void)
static void __exit mbox_exit(void)
```

## 2.2 核心数据结构

### struct mbox_msg_entry（消息条目）

```c
struct mbox_msg_entry {
    uint32_t msg_id;          // 全局唯一消息 ID
    uint32_t timestamp_sec;   // 发送时间（秒）
    uint32_t timestamp_nsec;  // 发送时间（纳秒）
    uint32_t sender_pid;      // 发送者进程 ID
    uint8_t  channel;         // 目标通道 0-15
    uint8_t  flags;           // 消息标志
    uint16_t size;            // 负载长度
    uint8_t  payload[];       // 可变长负载（C99 flexible array）
};
```

### struct mbox_queue（队列头）

```c
struct mbox_queue {
    // 生产者状态
    uint32_t head;            // 下一写入位置

    // 消费者状态
    uint32_t tail;            // 下一读取位置

    // 通道读索引（未来扩展）
    uint32_t reader_tail[16];

    // 配置
    uint32_t depth;           // 队列深度
    uint32_t msg_size;        // 单消息最大负载

    // 统计
    uint32_t count;           // 当前消息数
    uint32_t total_sent;      // 累计发送
    uint32_t total_recv;      // 累计接收
    uint32_t messages_dropped; // 因队列满丢弃的消息

    // 同步
    uint32_t lock;            // 自旋锁
    uint32_t flags;           // 队列标志

    // 填充对齐
    uint8_t  reserved[48];

    // 消息条目从这之后开始
};
```

### struct mbox_instance（ mailbox 实例）

```c
struct mbox_instance {
    char name[32];             // mailbox 名称
    int  id;                  // 全局唯一 ID

    // 共享内存
    void *shmem;              // 内核虚拟地址
    size_t shmem_size;        // 总大小
    dma_addr_t shmem_phys;    // 物理地址（给 DMA 用）

    // 队列指针
    struct mbox_queue *queue;
    struct mbox_msg_entry *entries;  // 指向 shmem 中的消息区

    // 引用计数
    atomic_t refcount;        // 文件引用数
    atomic_t open_count;      // 打开次数

    // 同步
    spinlock_t lock;          // 保护队列
    wait_queue_head_t waitq;  // 阻塞等待队列

    // 字符设备
    struct cdev cdev;
    dev_t devno;
    struct device *device;
};
```

## 2.3 消息入队（ mbox_enqueue）

```c
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

    // 1. 长度检查
    if (size > mbox->queue->msg_size) {
        return -MBOX_ERR_TOOBIG;
    }

    // 2. 获取锁并检查队列状态
    spin_lock_irqsave(&mbox->lock, irq_flags);

    if (mbox_queue_full(q)) {
        // 队列满时的处理策略：
        // - 如果是 URGENT 消息，可以选择覆盖最旧的
        if (flags & MBOX_MSG_FLAG_URGENT) {
            q->messages_dropped++;
            // 覆盖最旧消息：什么都不做，head++ 会自动覆盖
        }
        ret = -MBOX_ERR_NOBUFS;
        goto unlock;
    }

    // 3. 获取当前 head 位置的条目
    idx = q->head;
    entry = mbox_get_entry(mbox, idx);

    // 4. 填充条目
    entry->msg_id = q->total_sent + 1;
    entry->timestamp_sec = 0;          // TODO: 真实时间戳
    entry->timestamp_nsec = 0;
    entry->sender_pid = current->pid;
    entry->channel = channel;
    entry->flags = flags & 0xFF;
    entry->size = size;

    // 5. 复制负载数据
    if (payload && size > 0) {
        memcpy(entry->payload, payload, size);
    }

    // 6. 内存屏障：确保数据写入在索引更新之前
    smp_wmb();

    // 7. 更新生产者状态
    q->head = idx + 1;
    q->count++;
    q->total_sent++;
    q->bytes_sent += size;

    ret = entry->msg_id;

unlock:
    spin_unlock_irqrestore(&mbox->lock, irq_flags);

    // 8. 唤醒等待中的消费者
    if (ret >= 0) {
        wake_up_interruptible(&mbox->waitq);
    }

    return ret;
}
```

**关键点**：
- `smp_wmb()` 确保在 head 更新之前数据已复制完成
- `spin_lock_irqsave` 在持有锁时禁用中断，防止死锁
- `wake_up_interruptible` 只唤醒可中断等待的进程

## 2.4 消息出队（ mbox_dequeue）

```c
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
    ktime_t start, now;
    int remaining_ms;

    if (timeout_ms > 0) {
        start = ktime_get();
        remaining_ms = timeout_ms;
    }

    do {
        // 1. 在队列中查找匹配 channel 的消息
        spin_lock_irqsave(&mbox->lock, irq_flags);

        idx = q->tail;
        int tries = 0;

        while (tries < q->depth && idx != q->head) {
            entry = mbox_get_entry(mbox, idx);

            // 检查 channel 是否匹配
            if ((1 << entry->channel) & channel_mask) {
                // 找到匹配消息
                uint32_t size = entry->size;

                // 复制数据到用户缓冲区
                if (payload && size > 0 && size <= max_size) {
                    memcpy(payload, entry->payload, size);
                }

                // 返回元数据
                if (out_msg_id)   *out_msg_id   = entry->msg_id;
                if (out_channel)  *out_channel  = entry->channel;
                if (out_flags)    *out_flags    = entry->flags;
                if (out_size)     *out_size     = size;

                // 从队列中移除：简单移动 tail
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

        // 2. 非阻塞模式：立即返回
        if (timeout_ms == 0) {
            ret = -MBOX_ERR_WOULDBLOCK;
            break;
        }

        // 3. 计算剩余时间
        if (timeout_ms > 0) {
            now = ktime_get();
            remaining_ms = timeout_ms - (int)ktime_to_ms(ktime_sub(now, start));
            if (remaining_ms <= 0) {
                ret = -MBOX_ERR_TIMEOUT;
                break;
            }
        }

        // 4. 等待消息到达
        ret = wait_event_interruptible_timeout(
            mbox->waitq,
            !mbox_queue_empty(q),  // 条件：队列非空
            msecs_to_jiffies(remaining_ms > 100 ? 100 : remaining_ms)
        );

        if (ret < 0) {
            // 被信号中断
            ret = -MBOX_ERR_TIMEOUT;
            break;
        }

    } while (ret != 0);  // ret == 0 表示超时

done:
    return ret;
}
```

**关键点**：
- 使用 `wait_event_interruptible_timeout` 实现可中断的阻塞等待
- 以 100ms 为单位分批等待，避免一次性等待过久
- Channel 匹配使用位掩码，支持同时监听多个通道

## 2.5 文件操作实现

### mbox_poll（支持 select/epoll）

```c
static unsigned int mbox_poll(struct file *filp, struct poll_table_struct *wait)
{
    struct mbox_filedata *fd = filp->private_data;
    struct mbox_instance *mbox = fd->mbox;
    unsigned int mask = 0;
    unsigned long irq_flags;

    // 注册到 poll 等待队列
    poll_wait(filp, &mbox->waitq, wait);

    spin_lock_irqsave(&mbox->lock, irq_flags);

    // 有消息可读
    if (mbox->queue->count > 0) {
        mask |= POLLIN | POLLRDNORM;
    }

    // 可以发送（队列未满）
    if (mbox->queue->count < mbox->queue->depth) {
        mask |= POLLOUT | POLLWRNORM;
    }

    spin_unlock_irqrestore(&mbox->lock, irq_flags);

    return mask;
}
```

### mbox_mmap（共享内存映射）

```c
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

    // 非缓存映射（设备寄存器必须，共享内存推荐）
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    // 获取共享内存的物理页帧号
    pfn = vmalloc_to_pfn(mbox->shmem);
    if (!pfn)
        return -EFAULT;

    // 建立页表映射
    if (remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}
```

## 2.6 模块生命周期

### 初始化（mbox_init）

```c
static int __init mbox_init(void)
{
    int ret;

    // 1. 初始化全局状态
    memset(&mbox_global, 0, sizeof(mbox_global));
    mutex_init(&mbox_global.instances_lock);

    // 2. 分配字符设备主设备号
    ret = alloc_chrdev_region(&mbox_global.devno, 0, MAX_MAILBOXES, DRV_NAME);
    if (ret) {
        pr_err("Failed to allocate chrdev region: %d\n", ret);
        return ret;
    }
    mbox_global.major = MAJOR(mbox_global.devno);

    // 3. 创建设备类（/sys/class/mbox）
    mbox_global.class = class_create(DRV_NAME);
    if (IS_ERR(mbox_global.class)) {
        pr_err("Failed to create class\n");
        unregister_chrdev_region(mbox_global.devno, MAX_MAILBOXES);
        return PTR_ERR(mbox_global.class);
    }

    pr_info("Mailbox driver loaded (major=%d)\n", mbox_global.major);
    return 0;
}
```

### 清理（mbox_exit）

```c
static void __exit mbox_exit(void)
{
    int i;

    // 销毁所有 mailbox 实例
    for (i = 0; i < MAX_MAILBOXES; i++) {
        if (mbox_global.instances[i]) {
            mbox_destroy(mbox_global.instances[i]);
        }
    }

    // 销毁设备类
    class_destroy(mbox_global.class);

    // 释放设备号
    unregister_chrdev_region(mbox_global.devno, MAX_MAILBOXES);

    pr_info("Mailbox driver unloaded\n");
}
```

## 2.7 设备节点创建

```bash
# 加载模块后，/sys/class/mbox/ 下会创建目录
# /dev/ 下会创建设备节点

ls -la /sys/class/mbox/
# drwxr-xr--- 1 root root    0 Jan  1 00:00 mbox-test_mbox -> ../../devices/...

ls -la /dev/mbox-*
# crw-rw-rw- 1 root root 243, 0 Jan  1 00:00 mbox-test_mbox
```

## 2.8 下一步

下一章：[第三章：共享内存布局](./03_shared_memory.md)