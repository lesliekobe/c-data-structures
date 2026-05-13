# 第五章：使用示例

## 5.1 基础模式：点对点通信

最简单的模式：一个生产者，一个消费者。

### 服务端（消费者）

```c
// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "../include/mbox.h"

#define MAILBOX_NAME "chat"

static volatile int running = 1;

void signal_handler(int sig) {
    (void)sig;
    running = 0;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    int mbox;
    char buf[4096];

    printf("Starting server...\n");

    // 安装信号处理器
    signal(SIGINT, signal_handler);

    // 打开 mailbox（不存在则创建）
    mbox = mbox_open(MAILBOX_NAME, MBOX_O_RDWR | MBOX_O_CREATE);
    if (mbox < 0) {
        fprintf(stderr, "Failed to open mailbox: %s\n",
                mbox_strerror(-mbox));
        return 1;
    }

    printf("Server ready, waiting for messages (Ctrl+C to exit)...\n\n");

    while (running) {
        // 阻塞接收，5 秒超时
        int size = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf) - 1, 5000);

        if (size < 0) {
            if (size == -MBOX_ERR_TIMEOUT) {
                // 超时，打印心跳
                printf(".");
                fflush(stdout);
                continue;
            }
            fprintf(stderr, "recv error: %s\n", mbox_strerror(-size));
            break;
        }

        // 确保字符串以 null 结尾
        buf[size] = '\0';
        printf("\n[Received %d bytes]: %s\n", size, buf);

        // 检查退出命令
        if (strcmp(buf, "quit") == 0) {
            printf("Quit command received.\n");
            break;
        }
    }

    mbox_close(mbox);
    printf("Server stopped.\n");
    return 0;
}
```

### 客户端（生产者）

```c
// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../include/mbox.h"

#define MAILBOX_NAME "chat"

static const char *timestamp(void)
{
    static char buf[32];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(buf, sizeof(buf), "%ld.%03ld", ts.tv_sec, ts.tv_nsec / 1000000);
    return buf;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    int mbox;
    char buf[4096];
    int count = 10;
    int interval_ms = 1000;

    printf("Starting client...\n");

    // 打开 mailbox（只写）
    mbox = mbox_open(MAILBOX_NAME, MBOX_O_WRONLY);
    if (mbox < 0) {
        fprintf(stderr, "Failed to open mailbox: %s\n",
                mbox_strerror(-mbox));
        return 1;
    }

    printf("Connected to mailbox, sending %d messages...\n\n", count);

    for (int i = 0; i < count; i++) {
        snprintf(buf, sizeof(buf), "Message #%d from client at %s",
                 i, timestamp());

        int msg_id = mbox_send(mbox, 0, buf, strlen(buf), 0);
        if (msg_id < 0) {
            fprintf(stderr, "send failed: %s\n", mbox_strerror(-msg_id));
            break;
        }

        printf("[Sent msg_id=%d]: %s\n", msg_id, buf);

        if (interval_ms > 0 && i < count - 1) {
            usleep(interval_ms * 1000);
        }
    }

    // 发送退出消息
    printf("\nSending quit command...\n");
    mbox_send(mbox, 0, "quit", 4, 0);

    mbox_close(mbox);
    printf("Client done.\n");
    return 0;
}
```

### 编译和运行

```bash
# 编译
gcc -o server server.c -I../include -L../lib -lmbox -lpthread
gcc -o client client.c -I../include -L../lib -lmbox -lpthread

# 运行
# 终端 1:
./server

# 终端 2:
./client
```

## 5.2 多通道模式

使用多个通道分离不同类型的消息。

```c
// 通道定义
#define CHAN_CONTROL  0   // 控制命令
#define CHAN_DATA     1   // 数据消息
#define CHAN_HIGH     15  // 高优先级

// 发送不同类型的消息
mbox_send(mbox, CHAN_CONTROL, "reset", 5, 0);
mbox_send(mbox, CHAN_DATA, large_buffer, buffer_size, 0);
mbox_send(mbox, CHAN_HIGH, alert, sizeof(alert), MBOX_MSG_FLAG_URGENT);

// 消费者监听多个通道
// 只接收控制消息
mbox_recv(mbox, (1 << CHAN_CONTROL), buf, size, timeout);

// 接收控制和高层消息（高优先级）
mbox_recv(mbox, (1 << CHAN_CONTROL) | (1 << CHAN_HIGH), buf, size, timeout);

// 接收所有消息
mbox_recv(mbox, 0xFFFF, buf, size, timeout);
```

## 5.3 轮询模式（ poll/select）

用于同时监听多个文件描述符，或实现事件驱动的架构。

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include "../include/mbox.h"

#define MAILBOX_NAME "event_mbox"

int main(void)
{
    int mbox;
    struct pollfd pfd[2];
    char buf[4096];
    int mbox_fd;
    int running = 1;

    // 打开 mailbox
    mbox = mbox_open(MAILBOX_NAME, MBOX_O_RDWR | MBOX_O_CREATE);
    if (mbox < 0) {
        perror("mbox_open");
        return 1;
    }

    mbox_fd = mbox_get_fd(mbox);

    // 设置 poll 文件描述符
    memset(pfd, 0, sizeof(pfd));
    pfd[0].fd = mbox_fd;           // mailbox
    pfd[0].events = POLLIN;       // 监听可读
    pfd[1].fd = STDIN_FILENO;      // 标准输入
    pfd[1].events = POLLIN;        // 监听可读

    printf("Ready. Type messages to send, incoming messages will be displayed.\n");
    printf("Press Ctrl+D to exit.\n\n");

    while (running) {
        int ret = poll(pfd, 2, 5000);  // 5 秒超时

        if (ret < 0) {
            perror("poll");
            break;
        }

        if (ret == 0) {
            printf("[timeout - still alive]\n");
            continue;
        }

        // 检查 mailbox 是否有消息
        if (pfd[0].revents & POLLIN) {
            int size = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf) - 1, 0);
            if (size > 0) {
                buf[size] = '\0';
                printf("\n[IN ]: %s\n", buf);
            } else if (size < 0 && size != -MBOX_ERR_WOULDBLOCK) {
                fprintf(stderr, "recv error: %s\n", mbox_strerror(-size));
            }
        }

        // 检查标准输入
        if (pfd[1].revents & POLLIN) {
            if (fgets(buf, sizeof(buf), stdin) == NULL) {
                // EOF (Ctrl+D)
                running = 0;
                break;
            }

            // 去掉换行符
            size_t len = strlen(buf);
            if (len > 0 && buf[len-1] == '\n') {
                buf[len-1] = '\0';
            }

            if (len > 1) {  // 忽略空行
                int msg_id = mbox_send(mbox, 0, buf, len - 1, 0);
                if (msg_id >= 0) {
                    printf("[OUT]: %s\n", buf);
                }
            }
        }

        // 检查错误/挂断
        if (pfd[0].revents & (POLLHUP | POLLERR)) {
            printf("Mailbox disconnected.\n");
            break;
        }
    }

    mbox_close(mbox);
    return 0;
}
```

## 5.4 select/epoll 模式

对于高性能服务器，可以使用 epoll：

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include "../include/mbox.h"

#define MAX_EVENTS 10

int main(void)
{
    int epfd = epoll_create1(0);
    int mbox = mbox_open("epoll_mbox", MBOX_O_RDWR | MBOX_O_CREATE);
    int mbox_fd = mbox_get_fd(mbox);

    struct epoll_event ev, events[MAX_EVENTS];

    // 注册 mailbox 到 epoll
    ev.events = EPOLLIN;  // 监听可读事件
    ev.data.fd = mbox_fd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, mbox_fd, &ev);

    printf("epoll-based server started.\n");

    while (1) {
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, 5000);

        if (nfds < 0) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == mbox_fd) {
                // mailbox 有数据
                char buf[4096];
                int size = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf), 0);
                if (size > 0) {
                    printf("Got: %.*s\n", size, buf);
                }
            }
        }
    }

    close(epfd);
    mbox_close(mbox);
    return 0;
}
```

## 5.5 统计监控

```c
#include <stdio.h>
#include "../include/mbox.h"

void print_stats(int mbox)
{
    mbox_stats_t stats;
    if (mbox_stats(mbox, &stats) == 0) {
        printf("=== Mailbox Statistics ===\n");
        printf("  Total sent:     %u\n", stats.messages_sent);
        printf("  Total recv:     %u\n", stats.messages_recv);
        printf("  Dropped:        %u\n", stats.messages_dropped);
        printf("  Current depth:  %u / %u\n", stats.current_depth, stats.max_depth);
        printf("  Bytes sent:     %llu\n", (unsigned long long)stats.bytes_sent);
        printf("  Bytes recv:     %llu\n", (unsigned long long)stats.bytes_recv);

        if (stats.messages_sent > 0) {
            double avg_size = (double)stats.bytes_sent / stats.messages_sent;
            printf("  Avg msg size:   %.2f bytes\n", avg_size);
        }
        printf("==========================\n");
    }
}

int main(void)
{
    int mbox = mbox_open("stats_mbox", MBOX_O_RDWR | MBOX_O_CREATE);

    // 定期打印统计
    while (1) {
        sleep(10);
        print_stats(mbox);
    }

    mbox_close(mbox);
    return 0;
}
```

## 5.6 错误处理最佳实践

```c
int send_with_retry(int mbox, uint8_t channel, const void *buf, size_t size, int max_retries)
{
    int retries = 0;
    int delay_ms = 10;

    while (retries < max_retries) {
        int msg_id = mbox_send(mbox, channel, buf, size, 0);

        if (msg_id >= 0) {
            return msg_id;  // 成功
        }

        if (msg_id == -MBOX_ERR_WOULDBLOCK || msg_id == -MBOX_ERR_NOBUFS) {
            // 队列满，稍后重试
            usleep(delay_ms * 1000);
            delay_ms *= 2;  // 指数退避
            retries++;
            continue;
        }

        // 其他错误，不重试
        return msg_id;
    }

    return -MBOX_ERR_NOBUFS;  // 重试次数用尽
}

int recv_with_timeout_retry(int mbox, uint32_t mask, void *buf, size_t max_size, int timeout_ms)
{
    int remaining = timeout_ms;
    int chunk = 100;  // 每步等待 100ms

    while (remaining > 0) {
        int size = mbox_recv(mbox, mask, buf, max_size, chunk);

        if (size != -MBOX_ERR_TIMEOUT) {
            return size;  // 收到消息或错误
        }

        remaining -= chunk;
    }

    return -MBOX_ERR_TIMEOUT;
}
```

## 5.7 下一步

下一章：[第六章：调试](./06_debugging.md)