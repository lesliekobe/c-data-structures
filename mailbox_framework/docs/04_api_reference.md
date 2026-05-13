# 第四章：API 参考

## 4.1 函数概览

| 函数 | 说明 |
|------|------|
| `mbox_open()` | 打开或创建 mailbox |
| `mbox_close()` | 关闭 mailbox |
| `mbox_send()` | 发送消息 |
| `mbox_recv()` | 接收消息（阻塞或超时） |
| `mbox_peek()` | 查看消息但不删除 |
| `mbox_flush()` | 清空队列中所有消息 |
| `mbox_stats()` | 获取统计信息 |
| `mbox_get_fd()` | 获取 poll/select 用的文件描述符 |
| `mbox_get_shmem()` | 获取共享内存直接访问指针 |
| `mbox_strerror()` | 获取错误描述字符串 |

## 4.2 mbox_open

```c
int mbox_open(const char *name, uint32_t flags);
```

**功能**：打开或创建一个 mailbox。

**参数**：
- `name`： mailbox 名称，最多 31 个字符
- `flags`：打开标志，位掩码

**标志**：
```c
MBOX_O_RDONLY    = 0x01   // 只读（接收消息）
MBOX_O_WRONLY    = 0x02   // 只写（发送消息）
MBOX_O_RDWR      = 0x03   // 读写
MBOX_O_CREATE     = 0x10   // 不存在时创建
MBOX_O_NONBLOCK   = 0x20   // 非阻塞模式
```

**返回值**：成功返回 mailbox 句柄（>= 0），失败返回负数错误码。

**示例**：
```c
// 只读打开（客户端）
int mbox = mbox_open("my_mbox", MBOX_O_RDONLY);

// 读写打开并创建（服务端）
int mbox = mbox_open("my_mbox", MBOX_O_RDWR | MBOX_O_CREATE);

// 非阻塞打开
int mbox = mbox_open("my_mbox", MBOX_O_RDWR | MBOX_O_NONBLOCK);
```

## 4.3 mbox_close

```c
int mbox_close(int mbox);
```

**功能**：关闭 mailbox，释放相关资源。

**返回值**：成功返回 0，失败返回负数错误码。

**示例**：
```c
int ret = mbox_close(mbox);
if (ret < 0) {
    fprintf(stderr, "close failed: %s\n", mbox_strerror(-ret));
}
```

## 4.4 mbox_send

```c
int mbox_send(int mbox, uint8_t channel, const void *payload,
              size_t size, uint32_t flags);
```

**功能**：发送一条消息到指定通道。

**参数**：
- `mbox`： mailbox 句柄
- `channel`：目标通道（0-15）
- `payload`：消息数据指针
- `size`：消息长度（字节）
- `flags`：消息标志

**消息标志**：
```c
MBOX_MSG_FLAG_URGENT     = (1 << 0)   // 高优先级，队列满时可覆盖
MBOX_MSG_FLAG_BROADCAST = (1 << 1)   // 广播（当前未实现）
MBOX_MSG_FLAG_PEEK      = (1 << 2)   // 保留，peek 使用 mbox_peek()
```

**返回值**：成功返回消息 ID（>= 1），失败返回负数错误码。

**错误码**：
- `MBOX_ERR_INVAL`：无效参数
- `MBOX_ERR_TOOBIG`：消息过大（超过配置的 msg_size）
- `MBOX_ERR_NOBUFS`：队列已满
- `MBOX_ERR_WOULDBLOCK`：非阻塞模式下队列满

**示例**：
```c
// 发送普通消息
char buf[] = "Hello, world!";
int msg_id = mbox_send(mbox, 0, buf, strlen(buf), 0);
if (msg_id < 0) {
    fprintf(stderr, "send failed: %s\n", mbox_strerror(-msg_id));
} else {
    printf("Sent message, id=%d\n", msg_id);
}

// 发送高优先级消息
mbox_send(mbox, 0, urgent_data, sizeof(urgent_data),
          MBOX_MSG_FLAG_URGENT);
```

## 4.5 mbox_recv

```c
int mbox_recv(int mbox, uint32_t channel_mask, void *payload,
              size_t max_size, int timeout_ms);
```

**功能**：从队列中接收一条消息。

**参数**：
- `mbox`： mailbox 句柄
- `channel_mask`：要监听的通道掩码（位或）
- `payload`：接收缓冲区指针
- `max_size`：缓冲区最大大小
- `timeout_ms`：超时时间（毫秒）

**channel_mask 示例**：
```c
0x0001   // 只监听 channel 0
0x0003   // 监听 channel 0 和 1
0xFFFF   // 监听所有 channel
```

**timeout_ms**：
- `0`：非阻塞，有消息立即返回，无消息返回 -MBOX_ERR_WOULDBLOCK
- `-1`：无限等待，直到收到消息
- `> 0`：等待指定毫秒，超时返回 -MBOX_ERR_TIMEOUT

**返回值**：成功返回消息大小（>= 0），失败返回负数错误码。

**示例**：
```c
char buf[4096];
int timeout = 5000;  // 5 秒超时

// 阻塞接收（无限等待）
int size = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf), -1);
if (size < 0) {
    fprintf(stderr, "recv failed: %s\n", mbox_strerror(-size));
} else {
    printf("Received %d bytes: %.*s\n", size, size, buf);
}

// 非阻塞接收
size = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf), 0);
if (size == -MBOX_ERR_WOULDBLOCK) {
    printf("No message available\n");
}

// 带超时的接收
size = mbox_recv(mbox, 0x0001, buf, sizeof(buf), 2000);  // 2 秒
if (size == -MBOX_ERR_TIMEOUT) {
    printf("Timeout, no message\n");
}
```

## 4.6 mbox_peek

```c
int mbox_peek(int mbox, uint32_t channel_mask, void *payload,
              size_t max_size, uint32_t *msg_id, uint8_t *out_channel,
              uint32_t *out_flags);
```

**功能**：查看下一条消息但不移除。

**参数**：
- `mbox`： mailbox 句柄
- `channel_mask`：要检查的通道掩码
- `payload`：缓冲区（可为 NULL）
- `max_size`：缓冲区大小
- `msg_id`：输出：消息 ID
- `out_channel`：输出：所属通道
- `out_flags`：输出：消息标志

**返回值**：成功返回消息大小，失败返回负数错误码，无消息返回 -MBOX_ERR_WOULDBLOCK。

**示例**：
```c
uint32_t msg_id;
uint8_t channel;
uint32_t flags;

int size = mbox_peek(mbox, 0xFFFF, NULL, 0, &msg_id, &channel, &flags);
if (size >= 0) {
    printf("Next message: id=%u, channel=%u, size=%d, flags=0x%x\n",
           msg_id, channel, size, flags);
} else if (size == -MBOX_ERR_WOULDBLOCK) {
    printf("Queue is empty\n");
}
```

## 4.7 mbox_flush

```c
int mbox_flush(int mbox);
```

**功能**：丢弃队列中所有消息，重置 head 和 tail。

**返回值**：成功返回 0，失败返回负数错误码。

**示例**：
```c
int ret = mbox_flush(mbox);
if (ret < 0) {
    fprintf(stderr, "flush failed: %s\n", mbox_strerror(-ret));
}
```

## 4.8 mbox_stats

```c
int mbox_stats(int mbox, mbox_stats_t *stats);
```

**功能**：获取 mailbox 统计信息。

**参数**：
- `mbox`： mailbox 句柄
- `stats`：输出统计结构体指针

**stats 结构体**：
```c
typedef struct {
    uint32_t messages_sent;     // 累计发送消息数
    uint32_t messages_recv;     // 累计接收消息数
    uint32_t messages_dropped;  // 因队列满丢弃的消息数
    uint32_t current_depth;     // 当前队列深度
    uint32_t max_depth;         // 配置的最大深度
    uint64_t bytes_sent;         // 累计发送字节数
    uint64_t bytes_recv;         // 累计接收字节数
} mbox_stats_t;
```

**示例**：
```c
mbox_stats_t stats;
if (mbox_stats(mbox, &stats) == 0) {
    printf("Mailbox stats:\n");
    printf("  sent: %u\n", stats.messages_sent);
    printf("  recv: %u\n", stats.messages_recv);
    printf("  dropped: %u\n", stats.messages_dropped);
    printf("  current: %u / %u\n", stats.current_depth, stats.max_depth);
}
```

## 4.9 mbox_get_fd

```c
int mbox_get_fd(int mbox);
```

**功能**：获取用于 poll/select 的文件描述符。

**返回值**：文件描述符，失败返回 -1。

**示例**：
```c
int fd = mbox_get_fd(mbox);
if (fd < 0) {
    fprintf(stderr, "get_fd failed\n");
    return;
}

struct pollfd pfd = { fd, POLLIN, 0 };

while (1) {
    int ret = poll(&pfd, 1, 5000);  // 5 秒超时

    if (ret < 0) {
        perror("poll");
        break;
    }

    if (ret == 0) {
        printf("Timeout...\n");
        continue;
    }

    if (pfd.revents & POLLIN) {
        // 有消息可读
        char buf[4096];
        int size = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf), 0);
        if (size > 0) {
            printf("Got: %.*s\n", size, buf);
        }
    }
}
```

## 4.10 mbox_get_shmem

```c
void *mbox_get_shmem(int mbox, size_t *size);
```

**功能**：获取直接访问共享内存的指针。

**参数**：
- `mbox`： mailbox 句柄
- `size`：输出：共享内存大小（可为 NULL）

**返回值**：共享内存起始地址，失败返回 NULL。

**警告**：直接访问共享内存会绕过库函数的锁保护。只在确定没有并发访问时使用。

**示例**：
```c
size_t shmem_size;
struct mbox_queue *q = mbox_get_shmem(mbox, &shmem_size);
if (q) {
    printf("Shared memory at %p, size=%zu\n", q, shmem_size);
    printf("Current depth: %u / %u\n", q->count, q->depth);
    printf("Total sent: %u\n", q->total_sent);
}
```

## 4.11 错误码汇总

| 错误码 | 值 | 说明 |
|--------|-----|------|
| `MBOX_ERR_OK` | 0 | 成功 |
| `MBOX_ERR_NOENT` | -1 | mailbox 不存在 |
| `MBOX_ERR_EXIST` | -2 | mailbox 已存在 |
| `MBOX_ERR_INVAL` | -3 | 无效参数 |
| `MBOX_ERR_NOMEM` | -4 | 内存不足 |
| `MBOX_ERR_NOBUFS` | -5 | 队列已满 |
| `MBOX_ERR_TIMEOUT` | -6 | 操作超时 |
| `MBOX_ERR_WOULDBLOCK` | -7 | 非阻塞模式下阻塞 |
| `MBOX_ERR_TOOBIG` | -8 | 消息过大 |
| `MBOX_ERR_CLOSED` | -9 | mailbox 已关闭 |
| `MBOX_ERR_CORRUPT` | -10 | 数据损坏 |

## 4.12 下一步

下一章：[第五章：使用示例](./05_examples.md)