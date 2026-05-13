# 第六章：调试

## 6.1 dmesg 和内核日志

加载模块后，所有内核 printk 输出可以通过 dmesg 查看：

```bash
# 加载模块
sudo insmod mbox_kern.ko

# 查看内核日志
dmesg | grep -i mbox

# 输出示例：
# [ 123.456] mbox: Mailbox driver loaded (major=243)
# [ 123.789] mbox: Mailbox 'test' created: depth=64, msg_size=4096
# [ 124.012] mbox: Device opened, refcount=1
```

## 6.2 /proc 和 /sys 接口

### 查看模块参数

```bash
# 查看可调参数（如果模块支持）
cat /sys/module/mbox_kern/parameters/*

# 查看模块信息
modinfo mbox_kern.ko
```

### 查看设备

```bash
# 查看已创建的 mailbox 设备
ls -la /sys/class/mbox/

# 查看特定 mailbox 的属性
ls -la /sys/class/mbox/mbox-*

# 查看设备详情
cat /sys/class/mbox/mbox-*/uevent
```

## 6.3 strace 追踪系统调用

追踪用户态程序和内核的交互：

```bash
# 追踪 open/close
strace -e open,close ./my_app 2>&1 | grep mbox

# 追踪 read/write
strace -e read,write ./my_app 2>&1

# 追踪 ioctl
strace -e ioctl ./my_app 2>&1

# 完整追踪（会产生很多输出）
strace ./my_app 2>&1 | head -100
```

### 常见 strace 输出分析

```
// 成功打开 mailbox
open("/dev/mbox-chat", O_RDWR) = 3

// 读取消息
read(3, "Hello", 4096) = 5

// 写入消息
write(3, "World", 5) = 5

// poll 等待
poll([{fd=3, events=POLLIN}], 1, 5000) = 1

// ioctl 获取统计
ioctl(3, MBOX_IOC_STATS, {messages_sent=100, ...}) = 0

// 非阻塞读取无数据
read(3, 0x7fff..., 4096) = -1 EAGAIN (Resource temporarily unavailable)
```

## 6.4 gdb 调试用户态

```bash
# 编译时加调试符号
gcc -g -o my_app my_app.c -I../include -L../lib -lmbox

# 启动 gdb
gdb ./my_app

# 设置断点
(gdb) break mbox_send
(gdb) break mbox_recv

# 运行
(gdb) run

# 单步调试
(gdb) next

# 查看变量
(gdb) print msg_id
(gdb) print size

# 查看调用栈
(gdb) bt

# 继续执行
(gdb) continue
```

## 6.5 内核调试

### 启用内核调试选项

```bash
# 在内核配置中启用
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_INFO=y
CONFIG_KGDB=y
```

### 动态调试（如果内核支持）

```bash
# 查看可用的动态调试项
cat /sys/kernel/debug/dynamic_debug/control | grep mbox

# 启用 mbox 调试
echo "file mbox_kern.c +p" > /sys/kernel/debug/dynamic_debug/control
```

### 添加内核 printk 调试

在代码中添加临时调试输出：

```c
// 在内核模块中添加
pr_debug("mbox: enqueue msg_id=%u, channel=%u, size=%u\n",
         msg_id, channel, size);

pr_debug("mbox: queue state: head=%u, tail=%u, count=%u\n",
         q->head, q->tail, q->count);
```

## 6.6 常见问题排查

### 问题 1：打开 mailbox 失败（ENOENT）

```bash
# 现象
open("/dev/mbox-test") failed: No such file or directory

# 原因
- mailbox 尚未创建
- 设备节点不存在

# 解决
# 服务端需要先用 MBOX_O_CREATE 标志打开
mbox_open("test", MBOX_O_RDWR | MBOX_O_CREATE);
```

### 问题 2：发送消息返回 -MBOX_ERR_NOBUFS

```bash
# 现象
mbox_send() = -5 (No free message slots)

# 原因
- 队列已满
- 消费者没有及时读取

# 解决
# 1. 增加队列深度（创建时）
# 2. 检查消费者是否正常工作
# 3. 使用 poll 确认队列状态
```

### 问题 3：poll 返回但 read 无数据

```bash
# 现象
poll() returned 1 (POLLIN)
read() returned -1 EAGAIN

# 原因
- 队列中有消息但被其他 channel 过滤了
- channel_mask 不正确

# 解决
# 使用 0xFFFF 监听所有 channel
```

### 问题 4：数据错乱或乱序

```bash
# 现象
- 消息内容不正确
- 消息 ID 不连续

# 原因
- 多进程同时发送（单生产者假设被违反）
- 没有外部同步

# 解决
# 1. 只允许一个生产者
# 2. 使用外部锁保护 mbox_send
```

### 问题 5：mmap 失败

```bash
# 现象
mmap() failed: Cannot allocate memory

# 原因
- 共享内存大小为 0
- mbox 实例未正确初始化

# 解决
# 检查 mbox_open 是否成功
# 检查 /proc/sys/vm/max_map_count
```

## 6.7 验证工具

创建一个简单的验证程序：

```c
// verify.c - 基础功能验证
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/mbox.h"

#define MBOX_NAME "verify_test"

void test_basic_send_recv(void)
{
    printf("Test: basic send/recv... ");

    int mbox = mbox_open(MBOX_NAME, MBOX_O_RDWR | MBOX_O_CREATE);
    assert(mbox >= 0);

    const char *msg = "hello";
    int msg_id = mbox_send(mbox, 0, msg, strlen(msg), 0);
    assert(msg_id >= 0);

    char buf[256];
    int size = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf), 1000);
    assert(size > 0);
    assert(size == strlen(msg));
    assert(memcmp(buf, msg, size) == 0);

    mbox_close(mbox);
    printf("PASS\n");
}

void test_channel_filter(void)
{
    printf("Test: channel filter... ");

    int mbox = mbox_open(MBOX_NAME, MBOX_O_RDWR | MBOX_O_CREATE);
    assert(mbox >= 0);

    // 发送两条不同 channel 的消息
    mbox_send(mbox, 0, "chan0", 5, 0);
    mbox_send(mbox, 1, "chan1", 5, 0);

    // 只接收 channel 0
    char buf[256];
    int size = mbox_recv(mbox, 0x0001, buf, sizeof(buf), 1000);
    assert(size == 5);
    assert(memcmp(buf, "chan0", 5) == 0);

    // 只接收 channel 1
    size = mbox_recv(mbox, 0x0002, buf, sizeof(buf), 1000);
    assert(size == 5);
    assert(memcmp(buf, "chan1", 5) == 0);

    mbox_close(mbox);
    printf("PASS\n");
}

void test_stats(void)
{
    printf("Test: statistics... ");

    int mbox = mbox_open(MBOX_NAME, MBOX_O_RDWR | MBOX_O_CREATE);
    assert(mbox >= 0);

    mbox_send(mbox, 0, "a", 1, 0);
    mbox_send(mbox, 0, "b", 1, 0);
    mbox_send(mbox, 0, "c", 1, 0);

    char buf[256];
    mbox_recv(mbox, 0xFFFF, buf, sizeof(buf), 0);
    mbox_recv(mbox, 0xFFFF, buf, sizeof(buf), 0);

    mbox_stats_t stats;
    mbox_stats(mbox, &stats);
    assert(stats.messages_sent == 3);
    assert(stats.messages_recv == 2);
    assert(stats.current_depth == 1);

    mbox_close(mbox);
    printf("PASS\n");
}

int main(void)
{
    printf("Running mailbox verification tests...\n\n");

    test_basic_send_recv();
    test_channel_filter();
    test_stats();

    printf("\nAll tests passed!\n");
    return 0;
}
```

## 6.8 性能测试

```c
// benchmark.c - 吞吐量测试
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/mbox.h"

#define MBOX_NAME "bench_mbox"
#define ITERATIONS 100000

int main(void)
{
    int mbox = mbox_open(MBOX_NAME, MBOX_O_RDWR | MBOX_O_CREATE);
    if (mbox < 0) {
        perror("open");
        return 1;
    }

    char msg[64];
    char recv_buf[64];

    // 预热
    for (int i = 0; i < 1000; i++) {
        mbox_send(mbox, 0, msg, sizeof(msg), 0);
        mbox_recv(mbox, 0xFFFF, recv_buf, sizeof(recv_buf), 0);
    }

    // 发送测试
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < ITERATIONS; i++) {
        mbox_send(mbox, 0, msg, sizeof(msg), 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec - start.tv_sec) +
                    (end.tv_nsec - start.tv_nsec) / 1e9;
    double throughput = ITERATIONS / elapsed;

    printf("Send throughput: %.2f msg/s (%.2f MB/s)\n",
           throughput, throughput * sizeof(msg) / 1024 / 1024);

    // 接收测试
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < ITERATIONS; i++) {
        mbox_recv(mbox, 0xFFFF, recv_buf, sizeof(recv_buf), 0);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    elapsed = (end.tv_sec - start.tv_sec) +
              (end.tv_nsec - start.tv_nsec) / 1e9;
    throughput = ITERATIONS / elapsed;

    printf("Recv throughput: %.2f msg/s (%.2f MB/s)\n",
           throughput, throughput * sizeof(msg) / 1024 / 1024);

    mbox_close(mbox);
    return 0;
}
```

## 6.9 下一步

下一章：[第七章：性能优化](./07_performance.md)