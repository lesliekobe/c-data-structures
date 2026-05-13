/*
 * mbox_example.c - Mailbox example applications
 *
 * Demonstrates:
 * 1. Simple send/receive between two processes
 * 2. Using poll/select for async notification
 * 3. Broadcast messaging
 *
 * Build: qcc -o mbox_example mbox_example.c -L../lib -lmbox -lpthread
 * Run:
 *   Terminal 1: ./mbox_example server
 *   Terminal 2: ./mbox_example client
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/poll.h>

#include "../include/mbox.h"

/* ========================================================================
 * Configuration
 * ======================================================================== */

#define MAILBOX_NAME    "test_mbox"
#define MAX_PAYLOAD     256
#define SERVER_DEPTH    16

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

static const char *timestamp(void)
{
    static char buf[32];
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    snprintf(buf, sizeof(buf), "%ld.%06ld", ts.tv_sec, ts.tv_nsec / 1000);
    return buf;
}

static void print_stats(mbox_stats_t *stats)
{
    printf("  sent:      %u\n", stats->messages_sent);
    printf("  recv:      %u\n", stats->messages_recv);
    printf("  dropped:   %u\n", stats->messages_dropped);
    printf("  current:   %u / %u\n", stats->current_depth, stats->max_depth);
    printf("  bytes:     sent=%llu recv=%llu\n",
           (unsigned long long)stats->bytes_sent,
           (unsigned long long)stats->bytes_recv);
}

/* ========================================================================
 * Server Mode
 * ======================================================================== */

static int run_server(void)
{
    int mbox;
    int ret;
    mbox_stats_t stats;
    uint8_t channel;
    char buf[MAX_PAYLOAD];

    printf("[Server] Opening mailbox '%s' as receiver...\n", MAILBOX_NAME);

    mbox = mbox_open(MAILBOX_NAME, MBOX_O_RDWR | MBOX_O_CREATE);
    if (mbox < 0) {
        fprintf(stderr, "[Server] mbox_open failed: %s\n",
                mbox_strerror(-mbox));
        return 1;
    }

    /* Get and print initial stats */
    mbox_stats(mbox, &stats);
    printf("[Server] Mailbox opened:\n");
    print_stats(&stats);

    printf("[Server] Waiting for messages (timeout=5s)...\n\n");

    /* Main receive loop */
    while (1) {
        memset(buf, 0, sizeof(buf));

        /* Receive with timeout */
        ret = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf) - 1, 5000);

        if (ret == -MBOX_ERR_TIMEOUT) {
            /* Just print a dot to show we're alive */
            printf(".");
            fflush(stdout);
            continue;
        }

        if (ret < 0) {
            fprintf(stderr, "[Server] mbox_recv error: %s\n",
                    mbox_strerror(-ret));
            break;
        }

        /* Print received message */
        printf("\n[Server] RECEIVED: %d bytes on channel %d: '%s'\n",
               ret, channel, buf);

        /* Check for quit */
        if (strcmp(buf, "quit") == 0) {
            printf("[Server] Quit requested, exiting.\n");
            break;
        }

        /* Print updated stats */
        mbox_stats(mbox, &stats);
        printf("[Server] Stats after recv:\n");
        print_stats(&stats);
    }

    mbox_close(mbox);
    return 0;
}

/* ========================================================================
 * Client Mode
 * ======================================================================== */

static int run_client(int count, int interval_ms)
{
    int mbox;
    int msg_id;
    int ret;
    char buf[MAX_PAYLOAD];
    int i;

    printf("[Client] Opening mailbox '%s' as sender...\n", MAILBOX_NAME);

    mbox = mbox_open(MAILBOX_NAME, MBOX_O_RDWR);
    if (mbox < 0) {
        fprintf(stderr, "[Client] mbox_open failed: %s\n",
                mbox_strerror(-mbox));
        return 1;
    }

    printf("[Client] Mailbox opened, sending %d messages...\n", count);

    for (i = 0; i < count; i++) {
        /* Build message */
        snprintf(buf, sizeof(buf), "Message #%d from PID %d at %s",
                 i, getpid(), timestamp());

        /* Send on channel 0 */
        msg_id = mbox_send(mbox, 0, buf, strlen(buf), 0);
        if (msg_id < 0) {
            fprintf(stderr, "[Client] mbox_send failed: %s\n",
                    mbox_strerror(-msg_id));
            break;
        }

        printf("[Client] Sent msg_id=%d: '%s'\n", msg_id, buf);

        /* Rate limiting */
        if (interval_ms > 0 && i < count - 1) {
            usleep(interval_ms * 1000);
        }
    }

    /* Send quit message */
    printf("\n[Client] Sending quit message...\n");
    mbox_send(mbox, 0, "quit", 4, 0);

    /* Print final stats */
    mbox_stats_t stats;
    mbox_stats(mbox, &stats);
    printf("\n[Client] Final stats:\n");
    print_stats(&stats);

    mbox_close(mbox);
    return 0;
}

/* ========================================================================
 * Poll/Select Demo
 * ======================================================================== */

static int run_poll_demo(void)
{
    int mbox;
    struct pollfd pfd;
    int ret;
    char buf[MAX_PAYLOAD];

    printf("[PollDemo] Opening mailbox with poll...\n");

    mbox = mbox_open(MAILBOX_NAME, MBOX_O_RDWR | MBOX_O_CREATE);
    if (mbox < 0) {
        fprintf(stderr, "[PollDemo] mbox_open failed: %s\n",
                mbox_strerror(-mbox));
        return 1;
    }

    /* Get poll file descriptor */
    int fd = mbox_get_fd(mbox);
    if (fd < 0) {
        fprintf(stderr, "[PollDemo] mbox_get_fd failed\n");
        return 1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN;  /* Wait for readability */

    printf("[PollDemo] Waiting for messages...\n");

    while (1) {
        ret = poll(&pfd, 1, 5000);  /* 5 second timeout */

        if (ret < 0) {
            perror("[PollDemo] poll");
            break;
        }

        if (ret == 0) {
            printf("[PollDemo] poll timeout...\n");
            continue;
        }

        if (pfd.revents & POLLIN) {
            /* Message available - receive it */
            memset(buf, 0, sizeof(buf));
            ret = mbox_recv(mbox, 0xFFFF, buf, sizeof(buf) - 1, 0);
            if (ret >= 0) {
                printf("[PollDemo] Got: %d bytes: '%s'\n", ret, buf);
                if (strcmp(buf, "quit") == 0)
                    break;
            }
        }

        if (pfd.revents & (POLLHUP | POLLERR)) {
            printf("[PollDemo] Hangup or error\n");
            break;
        }
    }

    mbox_close(mbox);
    return 0;
}

/* ========================================================================
 * Main
 * ======================================================================== */

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [server|client|poll] [args]\n", prog);
    fprintf(stderr, "  server          - Run as receiver\n");
    fprintf(stderr, "  client [count]  - Send N messages (default: 10)\n");
    fprintf(stderr, "  poll            - Use poll() instead of blocking recv\n");
}

int main(int argc, char *argv[])
{
    const char *mode = "server";

    printf("Mailbox Example Application\n");
    printf("============================\n\n");

    if (argc >= 2) {
        mode = argv[1];
    }

    if (strcmp(mode, "server") == 0) {
        return run_server();
    }
    else if (strcmp(mode, "client") == 0) {
        int count = 10;
        int interval = 100;  /* ms between messages */

        if (argc >= 3)
            count = atoi(argv[2]);

        return run_client(count, interval);
    }
    else if (strcmp(mode, "poll") == 0) {
        return run_poll_demo();
    }
    else {
        usage(argv[0]);
        return 1;
    }
}