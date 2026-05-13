# 第七章：性能优化

## 7.1 性能基线

典型硬件上的基准测试结果（单核，CPU 3.0GHz）：

| 操作 | 延迟 | 吞吐量 |
|------|------|--------|
| mbox_send() | ~0.5-2 μs | ~500K-1M msg/s |
| mbox_recv() | ~0.5-2 μs | ~500K-1M msg/s |
| round-trip (send+recv) | ~2-4 μs | ~250K-500K msg/s |

**影响因素**：
- CPU 频率和架构
- 消息大小（ larger = 更高 MB/s，更低 msg/s）
- 队列深度（太浅会增加锁竞争）
- 是否使用 mmap 零拷贝

## 7.2 减少系统调用

### 批量操作

一次系统调用发送/接收多条消息：

```c
// 批量发送
struct batch_send {
    int count;
    struct {
        uint8_t channel;
        uint32_t size;
        const void *data;
    } msgs[16];
};

// 效率低：16 次系统调用
for (int i = 0; i < 16; i++) {
    mbox_send(mbox, 0, buffers[i], sizes[i], 0);
}

// 效率高：1 次系统调用（需要自定义实现）
// 目前库不支持，需要修改内核模块或使用 mmap
```

### 使用 mmap 减少 copy

通过 mmap 直接访问共享内存：

```c
// 获取共享内存指针
struct mbox_queue *q = mbox_get_shmem(mbox, NULL);

// 直接写入（绕过内核 copy）
// 警告：需要外部同步保证原子性
```

## 7.3 锁竞争优化

### 减少临界区

```c
// 优化前：整个 dequeue 在锁内
spin_lock_irqsave(&lock, flags);
while (search_queue()) {
    // 耗时的搜索
}
copy_data();
spin_unlock_irqrestore(&lock, flags);

// 优化后：减少锁内操作
spin_lock_irqsave(&lock, flags);
find_entry_and_copy();  // 快速找到并复制
spin_unlock_irqrestore(&lock, flags);
```

### 无锁读取（单消费者）

如果只有一个消费者，可以去掉出队的锁：

```c
// 消费者无锁读取（生产者有锁）
static int mbox_peek_nolock(struct mbox_instance *mbox, ...)
{
    struct mbox_queue *q = mbox->queue;

    // 只检查，不修改 tail
    if (q->count == 0) {
        return -MBOX_ERR_WOULDBLOCK;
    }

    entry = mbox_get_entry(mbox, q->tail);
    // 复制数据...

    return size;
}
```

## 7.4 内存布局优化

### cache line 对齐

确保队列头和消息条目各自对齐到 cache line（通常是 64 字节）：

```c
struct mbox_queue {
    // ...
} __attribute__((aligned(64)));  // 64 字节对齐

struct mbox_msg_entry {
    // ...
} __attribute__((aligned(64)));  // 64 字节对齐
```

### NUMA 优化

在 NUMA 系统上，在创建 mailbox 时指定节点：

```c
// 需要修改内核模块以支持 NUMA 节点参数
int mbox = mbox_open_numa("my_mbox", flags, numa_node_id);
```

## 7.5 中断和上下文切换

### 减少阻塞

非阻塞操作比阻塞操作快：

```c
// 慢：每次等待
for (int i = 0; i < N; i++) {
    mbox_recv(mbox, mask, buf, size, -1);  // 阻塞
}

// 快：轮询
for (int i = 0; i < N; i++) {
    int ret = mbox_recv(mbox, mask, buf, size, 0);
    while (ret == -MBOX_ERR_WOULDBLOCK) {
        cpu_relax();  // 或 sched_yield()
        ret = mbox_recv(mbox, mask, buf, size, 0);
    }
}
```

### poll 阈值调整

对于高频消息，使用 poll 而不是阻塞 recv：

```c
// 获取 fd 并使用 epoll
int fd = mbox_get_fd(mbox);
struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);

// epoll_wait() 比忙等更高效
```

## 7.6 消息大小优化

### 选择合适的 msg_size

消息大小影响内存使用和性能：

| msg_size | 内存/条目 | 适用场景 |
|----------|---------|---------|
| 64-256 | 低 | 控制消息、短命令 |
| 1024-4096 | 中 | 一般数据 |
| 64K-1M | 高 | 大块数据传输 |

### 使用合适的负载大小

避免发送过小的消息（每条消息有固定开销）：

```c
// 效率低：1 字节消息 × 10000
for (int i = 0; i < 10000; i++) {
    char c = 'a';
    mbox_send(mbox, 0, &c, 1, 0);
}

// 效率高：打包成 100 条消息
struct packet { char data[100]; };
for (int i = 0; i < 100; i++) {
    struct packet p = { .data = {...} };
    mbox_send(mbox, 0, &p, sizeof(p), 0);
}
```

## 7.7 多线程注意事项

### 多生产者

单 mailbox 支持多个生产者发送，但需要外部同步：

```c
// 方案 1：每个生产者独立 channel
pthread_mutex_t send_lock;
pthread_mutex_lock(&send_lock);
mbox_send(mbox, my_channel, buf, size, 0);
pthread_mutex_unlock(&send_lock);

// 方案 2：使用一个 mailbox 队列 + mutex
// （不是真正的无锁）
```

### 多消费者

单 mailbox 只支持一个消费者正确读取（否则消息会被争抢）。多消费者需要：

```c
// 方案：每个消费者独立的 mailbox
int consumer_mbox = mbox_open("my_mbox_c0", MBOX_O_RDONLY);
int consumer1 = mbox_open("my_mbox_c1", MBOX_O_RDONLY);
// 生产者向所有消费者发送：
mbox_send(mbox_c0, ...);
mbox_send(mbox_c1, ...);
```

## 7.8 性能测试工具

```bash
# 编译 benchmark
gcc -O2 -o bench bench.c -I../include -L../lib -lmbox -lpthread

# 运行测试
./bench

# 输出示例：
# Send throughput: 850000.00 msg/s (323.09 MB/s)
# Recv throughput: 820000.00 msg/s (311.43 MB/s)
```

## 7.9 优化检查清单

| 优化项 | 方法 | 预期提升 |
|--------|------|---------|
| 减少系统调用 | 批量操作 | 2-10x |
| mmap 零拷贝 | 直接访问共享内存 | 1.5-3x |
| 减少锁竞争 | 缩小临界区 | 1.2-2x |
| 非阻塞轮询 | poll/epoll 替代阻塞 | 1.5-5x |
| 合适的 msg_size | 减少内存分配 | 1.1-2x |
| cache line 对齐 | 对齐结构体 | 1.1-1.5x |

## 7.10 下一步

文档撰写完毕。如果需要更多内容，请告诉我。