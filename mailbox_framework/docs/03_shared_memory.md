# 第三章：共享内存布局

## 3.1 内存布局概览

```
共享内存区域 (mmap 到用户空间)
┌─────────────────────────────────────────────────────────────────┐
│ 0x0000 │                                                          │
│        │  ┌─────────────────────────────────────────────────┐    │
│        │  │  struct mbox_queue (128 bytes)                    │    │
│        │  │                                                   │    │
│        │  │  head:          0x00000042   // 写入位置          │    │
│        │  │  tail:          0x0000003F   // 读取位置          │    │
│        │  │  reader_tail[0]: 0x0000003F                       │    │
│        │  │  reader_tail[1]: 0x0000003E                       │    │
│        │  │  ...                                             │    │
│        │  │  depth:         64            // 队列深度         │    │
│        │  │  msg_size:      4096          // 最大负载         │    │
│        │  │  count:         3             // 当前消息数       │    │
│        │  │  total_sent:    100           // 累计发送         │    │
│        │  │  total_recv:    97            // 累计接收         │    │
│        │  │  messages_dropped: 0           // 丢弃数           │    │
│        │  │  bytes_sent:     0x00012340    // 累计字节         │    │
│        │  │  bytes_recv:     0x00011F80                       │    │
│        │  │  lock:           0             // 自旋锁           │    │
│        │  │  flags:          0                                │    │
│        │  │  reserved[48]:   0                                │    │
│        │  └─────────────────────────────────────────────────┘    │
│        │                                                          │
│        │  ┌─────────────────────────────────────────────────┐    │
│        │  │  struct mbox_msg_entry[0] (4104 bytes)          │    │
│        │  │                                                   │    │
│        │  │  msg_id:         100         // 消息 ID          │    │
│        │  │  timestamp_sec:  1704067200   // 2024-01-01     │    │
│        │  │  timestamp_nsec: 123456789                      │    │
│        │  │  sender_pid:     1234         // 发送者 PID      │    │
│        │  │  channel:        0             // 通道 0         │    │
│        │  │  flags:          0             // 无标志         │    │
│        │  │  size:           256           // 负载 256B     │    │
│        │  │  payload[0..255]: "Hello..."   // 负载数据       │    │
│        │  └─────────────────────────────────────────────────┘    │
│        │                                                          │
│        │  ┌─────────────────────────────────────────────────┐    │
│        │  │  struct mbox_msg_entry[1]                       │    │
│        │  └─────────────────────────────────────────────────┘    │
│        │  ...                                                     │
│        │                                                          │
│        │  ┌─────────────────────────────────────────────────┐    │
│        │  │  struct mbox_msg_entry[63]                      │    │
│        │  └─────────────────────────────────────────────────┘    │
│        │                                                          │
│ 0xFFFF │                                                          │
└─────────────────────────────────────────────────────────────────┘
```

## 3.2 Ring Buffer 算法

Ring Buffer（环形缓冲区）是一种固定大小的缓冲区，使用模运算实现循环：

```c
// 计算实际数组索引
index = position % depth

// 示例：
depth = 64
head = 100
entry_idx = 100 % 64 = 36  // 写入/读取第 36 个 slot
```

### 写入过程

```
初始状态：
head=0, tail=0, count=0

写入 msg1 (256B):
head=1, count=1

写入 msg2 (128B):
head=2, count=2

写入 msg3 (64B):
head=3, count=3

队列状态：
[ msg1 ][ msg2 ][ msg3 ][ empty ][ empty ]...
    0       1       2       3       ...

tail=0（指向下一条可读消息）
head=3（指向下一条可写位置）
```

### 读取过程

```
读取 msg1:
- 读取 msg1 数据
- tail=1, count=2

再读取 msg2:
- 读取 msg2 数据
- tail=2, count=1

队列状态：
[ empty ][ empty ][ msg3 ][ empty ][ empty ]...
    0       1       2       3       ...

tail=2（跳过空槽）
head=3
```

### 队列满时

```
队列已满 (count=depth=64):
head=66, tail=2, count=64

下一条消息写入 head=66，实际位置 66%64=2，会覆盖 msg3

覆盖后：
head=67, tail=3, count=64（不变，因为是覆盖而非增加）
```

## 3.3 内存计算

```c
// 共享内存总大小计算
size_t total_size = sizeof(struct mbox_queue) +
                    (depth * (sizeof(struct mbox_msg_entry) + msg_size));

// 实际条目大小（考虑对齐）
size_t entry_size = sizeof(struct mbox_msg_entry) + msg_size;
// 由于 C 的 flexible array member，实际是：
// sizeof(struct mbox_msg_entry) = 24 字节（不含 payload）
// entry_size = 24 + msg_size（按实际大小）

// 示例计算
depth = 64, msg_size = 4096
total = 128 + (64 * (24 + 4096))
     = 128 + (64 * 4120)
     = 128 + 263680
     = 263808 字节 ≈ 258 KB
```

## 3.4 变长消息支持

虽然 `msg_size` 是固定的（创建时指定），但实际消息可以小于 `msg_size`：

```c
// 发送不同大小的消息
mbox_send(mbox, 0, "Hi", 2, 0);           // 2 字节
mbox_send(mbox, 0, large_buf, 4096, 0); // 4096 字节

// entry->size 记录实际负载长度
// 读取时返回实际大小，而非配置的 msg_size
```

## 3.5 跨进程共享原理

```
进程 A (Producer)                          进程 B (Consumer)
     │                                          │
     │ mmap("/dev/mbox-test", PROT_RDWR)        │
     │       │                                   │
     │       ▼                                   │
     │  ┌─────────────────────────┐              │
     │  │  共享内存页表项          │              │
     │  │  (进程 A 的页表)        │              │
     │  └───────────┬─────────────┘              │
     │              │                           │
     │              │ (同一物理页)               │
     │              │                           │
     │              ▼                           │
     │  ┌─────────────────────────┐              │
     │  │  struct mbox_queue      │◄─────────────┤
     │  │  struct mbox_entry[]    │              │
     │  │  (物理内存页)            │              │
     │  └─────────────────────────┘              │
     │                                           │
     │  ┌─────────────────────────┐              │
     │  │  共享内存页表项          │              │
     │  │  (进程 B 的页表)        │◄─────────────┤
     │  └───────────┬─────────────┘              │
     │              │                           │
     └──────────────┴───────────────────────────┘
```

内核使用 `vmalloc` 分配连续的虚拟内存区域，然后通过 `remap_pfn_range` 将其映射到用户态。多个进程映射同一物理页，实现共享。

## 3.6 缓存一致性

```c
// 入队时
entry->payload = data;    // 1. 写入数据
smp_wmb();               // 2. 内存屏障：确保数据写完
q->head = new_head;      // 3. 更新索引

// 出队时
if (q->head != q->tail) {    // 1. 检查有数据
    rmb();                        // 2. 内存屏障：确保索引已更新
    data = entry->payload;        // 3. 读取数据
}
```

**为什么需要内存屏障？**
- 现代 CPU 有 store buffer 和 load reorder
- 编译器也可能重排代码
- `smp_wmb()` 确保写操作按程序顺序对其他 CPU 可见

## 3.7 对齐和填充

```c
struct mbox_msg_entry {
    uint32_t msg_id;          // 4 bytes
    uint32_t timestamp_sec;   // 4 bytes
    uint32_t timestamp_nsec;  // 4 bytes
    uint32_t sender_pid;      // 4 bytes
    uint8_t  channel;         // 1 byte
    uint8_t  flags;           // 1 byte
    uint16_t size;            // 2 bytes
    //                     ------
    //                     20 bytes (加上 padding)
    uint8_t  payload[];       // flexible array, 可变长度
};
```

C 会自动在 `payload[]` 之前添加填充以满足对齐要求，所以实际大小是 24 字节（4 的倍数）。

## 3.8 内存布局调整

如果需要修改消息结构，需要重新编译内核模块和用户态库，并确保共享内存格式一致。

**不兼容变更**：
- 添加/删除字段
- 修改字段类型大小
- 改变 `depth` 或 `msg_size`

**兼容变更**：
- 在结构末尾添加新字段（需预留空间）
- 使用 version 字段区分不同格式

## 3.9 下一步

下一章：[第四章：API 参考](./04_api_reference.md)