# 第一章：整体架构

## 1.1 什么是 Mailbox Framework

Mailbox Framework 是一个基于共享内存的进程间通信（IPC）机制，用于在 Linux 系统中的不同进程之间高效传递消息报文。

```
传统 IPC vs Mailbox Framework

管道/消息队列:                         Mailbox Framework:
┌─────────┐      ┌─────────┐          ┌─────────┐      ┌─────────┐
│ Process │ msg │ Process │          │ Process │ msg │ Process │
│    A   │────►│   B    │          │    A   │────►│   B    │
└─────────┘      └─────────┘          └────┬────┘      └─────────┘
     │                                     │
     │ kernel 复制数据                      │ 共享内存映射
     ▼                                     ▼
  ┌──────┐                           ┌──────────────┐
  │ Kernel│                           │ Shared Memory│
  │ Buffer│                           │ (Ring Buffer)│
  └──────┘                           └──────────────┘
```

**核心区别**：传统 IPC 需要内核复制数据，Mailbox Framework 通过共享内存实现零拷贝。

## 1.2 系统架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        Application Layer                          │
│                                                                  │
│   ┌──────────────┐                    ┌──────────────┐         │
│   │ Producer App │                    │ Consumer App │         │
│   └──────┬───────┘                    └──────┬───────┘         │
│          │ mbox_send()                          │ mbox_recv()   │
│          │                                      │               │
│          ▼                                      ▼               │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │              Userspace Library (libmbox)                 │   │
│   │                                                          │   │
│   │   mbox_open()  mbox_send()  mbox_recv()  mbox_close() │   │
│   │   mbox_get_fd()  mbox_stats()  mbox_flush()           │   │
│   │                                                          │   │
│   │   - File descriptor abstraction                         │   │
│   │   - poll()/select() integration                        │   │
│   │   - Error mapping和重试逻辑                              │   │
│   └─────────────────────────┬───────────────────────────────┘   │
└──────────────────────────────┼───────────────────────────────────┘
                               │ read() / write() / ioctl() / poll()
┌──────────────────────────────┼───────────────────────────────────┐
│                              ▼            Kernel Layer           │
│   ┌─────────────────────────────────────────────────────────┐    │
│   │              Mailbox Kernel Module (mbox)               │    │
│   │                                                          │    │
│   │   ┌─────────────────────────────────────────────────┐   │    │
│   │   │          Mailbox Instance (per mailbox)          │   │    │
│   │   │                                                  │   │    │
│   │   │   struct mbox_instance {                        │   │    │
│   │   │     char name[32];           // Mailbox name    │   │    │
│   │   │     void *shmem;             // 共享内存指针    │   │    │
│   │   │     struct mbox_queue *queue; // Ring buffer    │   │    │
│   │   │     spinlock_t lock;         // 保护队列        │   │    │
│   │   │     wait_queue_head_t waitq; // 阻塞等待        │   │    │
│   │   │     atomic_t refcount;        // 引用计数        │   │    │
│   │   │   }                                                  │   │    │
│   │   └─────────────────────────────────────────────────┘   │    │
│   │                                                          │    │
│   │   ┌─────────────────────────────────────────────────┐   │    │
│   │   │          Character Device Layer                 │   │    │
│   │   │                                                  │   │    │
│   │   │   /dev/mbox-<name>                             │   │    │
│   │   │   file_operations:                              │   │    │
│   │   │     .open = mbox_open                          │   │    │
│   │   │     .read = mbox_read                          │   │    │
│   │   │     .write = mbox_write                        │   │    │
│   │   │     .poll = mbox_poll                          │   │    │
│   │   │     .mmap = mbox_mmap                          │   │    │
│   │   └─────────────────────────────────────────────────┘   │    │
│   └─────────────────────────┬───────────────────────────────┘    │
└─────────────────────────────┼────────────────────────────────────┘
                              │
┌─────────────────────────────┼────────────────────────────────────┐
│                             ▼            Shared Memory             │
│   ┌─────────────────────────────────────────────────────────┐    │
│   │                   Shared Memory Region                   │    │
│   │                                                          │    │
│   │   ┌─────────────────────────────────────────────────┐  │    │
│   │   │  struct mbox_queue (queue header)               │  │    │
│   │   │    head: 0x00000042     // 下一写入位置         │  │    │
│   │   │    tail: 0x0000003F     // 下一读取位置         │  │    │
│   │   │    depth: 64            // 队列深度             │  │    │
│   │   │    msg_size: 4096       // 单消息最大长度       │  │    │
│   │   │    count: 3             // 当前消息数          │  │    │
│   │   │    total_sent: 100      // 累计发送             │  │    │
│   │   │    total_recv: 97       // 累计接收             │  │    │
│   │   └─────────────────────────────────────────────────┘  │    │
│   │                                                          │    │
│   │   ┌─────────────────────────────────────────────────┐  │    │
│   │   │  struct mbox_msg_entry[0]                        │  │    │
│   │   ├─────────────────────────────────────────────────┤  │    │
│   │   │  struct mbox_msg_entry[1]                        │  │    │
│   │   ├─────────────────────────────────────────────────┤  │    │
│   │   │  ...                                              │  │    │
│   │   ├─────────────────────────────────────────────────┤  │    │
│   │   │  struct mbox_msg_entry[63]                       │  │    │
│   │   └─────────────────────────────────────────────────┘  │    │
│   │                                                          │    │
│   │   Producer ◄─────── mmap ────────────► Consumer        │    │
│   └─────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

## 1.3 消息流

### 发送流程（Producer）

```
1. Producer 调用 mbox_send(mbox, channel, payload, size, flags)

2. 用户态库：
   - 构建 struct mbox_send_req
   - 调用 write(fd, &req, sizeof(req))
   - 调用 write(fd, payload, size)

3. 内核 mbox_write()：
   - 检查队列是否满 (count >= depth)
   - 获取下一个空闲 slot (head % depth)
   - 填充 struct mbox_msg_entry：
     * msg_id = ++total_sent
     * timestamp = 当前时间
     * sender_pid = current->pid
     * channel = 目标通道
     * payload = 消息数据
   - 更新 head++ 和 count++
   - 内存屏障 (smp_wmb)
   - 唤醒等待中的消费者 (wake_up_interruptible)

4. 返回消息 ID 给 Producer
```

### 接收流程（Consumer）

```
1. Consumer 调用 mbox_recv(mbox, channel_mask, buf, max_size, timeout_ms)

2. 库函数：
   - 如果 timeout > 0，先调用 poll(fd, POLLIN)
   - 或直接 read(fd, &req, sizeof(req))

3. 内核 mbox_read() / mbox_dequeue()：
   - 在 channel_mask 匹配的条目中查找消息
   - 从 tail 开始遍历，跳过不匹配的 channel
   - 复制消息数据到用户缓冲区
   - 更新 tail++ 和 count--
   - 更新 total_recv 和 bytes_recv
   - 唤醒可能等待的发送者

4. 返回消息大小给 Consumer
```

## 1.4 Channel 机制

Mailbox 支持 16 个独立的通道（0-15）：

```c
// 发送时指定 channel
mbox_send(mbox, 0, buf, size, 0);    // 发到 channel 0
mbox_send(mbox, 3, buf, size, 0);    // 发到 channel 3

// 接收时可以监听多个 channel
mbox_recv(mbox, 0x0003, ...)        // 监听 channel 0 和 1
mbox_recv(mbox, 0xFFFF, ...)        // 监听所有 16 个 channel
mbox_recv(mbox, 0x0001, ...)        // 只监听 channel 0
```

**用途**：
- 优先级分离（高优先级用独立 channel）
- 消息类型区分（控制消息 vs 数据消息）
- 多客户端复用（每个客户端独立 channel）

## 1.5 关键设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 队列结构 | Ring Buffer | O(1) 入队/出队，无需内存分配 |
| 内存位置 | vmalloc | 内核连续虚拟地址，可映射到用户态 |
| 同步原语 | spinlock + waitqueue | 保护队列 + 阻塞等待 |
| 消息组织 | 变长 payload | 支持不同大小的消息 |
| 文件抽象 | character device | 标准化接口，poll/select 支持 |
| Channel | 16 个独立通道 | 足够大多数场景，避免消息类型混淆 |

## 1.6 与现有 IPC 机制对比

| 机制 | 延迟 | 吞吐量 | 适用场景 |
|------|------|--------|---------|
| pipe | 低 | 中 | 单向字节流 |
| Unix Domain Socket | 低 | 中 | 双向消息，复杂通信 |
| POSIX MQ | 中 | 中 | 标准化消息队列 |
| SHM + mutex | 极低 | 极高 | 高性能，零拷贝场景 |
| **Mailbox** | 低 | 高 | 多通道，报文传递 |

## 1.7 限制与约束

- **单生产者假设**：Ring Buffer 设计假设单一生产者。多进程同时发送需要外部同步。
- **消息顺序**：同一 channel 内保序，不同 channel 间无顺序保证。
- **消息大小**：必须在创建时指定最大值 (msg_size)，运行时不能超过。
- **队列深度**：必须在创建时指定，运行时固定。

## 1.8 下一步

下一章：[第二章：内核模块设计](./02_kernel_module.md)