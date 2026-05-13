/*
 * shmem_demo.c - QNX Shared Memory and Message Passing Demo
 *
 * Demonstrates QNX interprocess communication:
 * 1. shm_open() / mmap() for shared memory
 * 2. MsgSend() / ChannelCreate() / ConnectAttach() for messages
 *
 * This is a single program that acts as both server (writer) and
 * client (reader). In real usage, these would be separate programs.
 *
 * Build: qcc -o shmem_demo shmem_demo.c
 * Run: ./shmem_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmgr.h>

/* Shared memory region name */
#define SHMEM_NAME  "/test_shmem"
#define SHMEM_SIZE  (1024 * 1024)  /* 1 MB */

/* Message types */
#define MSG_TYPE_WRITE   1
#define MSG_TYPE_READ     2
#define MSG_TYPE_QUIT     3

/* Message structure */
typedef struct {
    int type;
    int offset;
    int size;
    char data[256];
} msg_t;

/* Shared memory context */
typedef struct {
    int shm_fd;
    void *virt;
    char *name;
} shmem_ctx_t;

/* Server context */
typedef struct {
    int chid;
    int coid;
} server_ctx_t;

/* Initialize shared memory */
int shmem_init(shmem_ctx_t *ctx, const char *name, size_t size)
{
    int fd;
    void *virt;

    /* Create shared memory object */
    fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0666);
    if (fd == -1) {
        /* Try opening existing */
        fd = shm_open(name, O_RDWR, 0666);
        if (fd == -1) {
            fprintf(stderr, "shm_open failed: %s\n", strerror(errno));
            return -1;
        }
        printf("Opened existing shared memory: %s\n", name);
    } else {
        printf("Created new shared memory: %s\n", name);
        /* Set size */
        if (ftruncate(fd, size) == -1) {
            fprintf(stderr, "ftruncate failed: %s\n", strerror(errno));
            close(fd);
            return -1;
        }
    }

    /* Map shared memory */
    virt = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (virt == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %s\n", strerror(errno));
        close(fd);
        return -1;
    }

    ctx->shm_fd = fd;
    ctx->virt = virt;
    ctx->name = (char *)name;

    printf("Shared memory mapped: fd=%d, virt=%p, size=%zu\n",
           fd, virt, size);

    return 0;
}

/* Clean up shared memory */
void shmem_fini(shmem_ctx_t *ctx)
{
    if (ctx->virt) {
        munmap(ctx->virt, SHMEM_SIZE);
    }
    if (ctx->shm_fd != -1) {
        close(ctx->shm_fd);
    }
    shm_unlink(ctx->name);
}

/* Create a message server (channel) */
int server_init(server_ctx_t *ctx)
{
    /* Create a channel for receiving messages */
    ctx->chid = ChannelCreate(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK);
    if (ctx->chid == -1) {
        fprintf(stderr, "ChannelCreate failed: %s\n", strerror(errno));
        return -1;
    }

    printf("Server channel created: chid=%d\n", ctx->chid);
    return 0;
}

/* Create a connection to a server */
int client_connect(server_ctx_t *ctx, int chid)
{
    /* Attach to server's channel from this process */
    ctx->coid = ConnectAttach(ND_LOCAL_NODE, 0, chid,
                               _NTO_COF_CLOEXEC, 0);
    if (ctx->coid == -1) {
        fprintf(stderr, "ConnectAttach failed: %s\n", strerror(errno));
        return -1;
    }

    printf("Client connected to server: coid=%d\n", ctx->coid);
    return 0;
}

/* Server: receive and handle message */
int server_handle_msg(server_ctx_t *ctx, shmem_ctx_t *shmem, msg_t *msg)
{
    int status;

    /* Receive message
     * Returns when a MsgSend() is received
     * rcvid: connection ID to reply to
     */
    int rcvid = MsgReceive(ctx->chid, msg, sizeof(msg_t), NULL);

    if (rcvid == -1) {
        fprintf(stderr, "MsgReceive failed: %s\n", strerror(errno));
        return -1;
    }

    printf("Server received: type=%d, offset=%d, size=%d\n",
           msg->type, msg->offset, msg->size);

    /* Handle message */
    switch (msg->type) {
    case MSG_TYPE_WRITE:
        /* Write data to shared memory */
        if (shmem->virt && msg->offset + msg->size < SHMEM_SIZE) {
            memcpy(shmem->virt + msg->offset, msg->data, msg->size);
            printf("  Wrote %d bytes at offset %d\n", msg->size, msg->offset);
            status = 0;
        } else {
            status = -1;
        }
        break;

    case MSG_TYPE_READ:
        /* Read data from shared memory */
        if (shmem->virt && msg->offset + msg->size < SHMEM_SIZE) {
            memcpy(msg->data, shmem->virt + msg->offset, msg->size);
            status = 0;
        } else {
            status = -1;
        }
        break;

    case MSG_TYPE_QUIT:
        printf("  Quit requested\n");
        return 1;  /* Signal to exit */

    default:
        status = -1;
        break;
    }

    /* Reply to client
     * rcvid: from MsgReceive
     * status: return value to client
     */
    MsgReply(rcvid, status, msg, sizeof(msg_t));

    return 0;
}

/* Client: send message to server */
int client_send_msg(server_ctx_t *ctx, msg_t *msg)
{
    int status;

    /* Send message to server
     * coid: from ConnectAttach
     * sndbuf: message to send
     * rcvbuf: reply buffer
     */
    status = MsgSend(ctx->coid, msg, sizeof(msg_t), msg, sizeof(msg_t));

    if (status == -1) {
        fprintf(stderr, "MsgSend failed: %s\n", strerror(errno));
        return -1;
    }

    printf("Client sent: type=%d, status=%d\n", msg->type, status);
    return 0;
}

int main(int argc, char *argv[])
{
    shmem_ctx_t shmem;
    server_ctx_t server;
    server_ctx_t client;
    msg_t msg;
    int mode = 0;  /* 0=both, 1=server-only, 2=client-only */
    int done = 0;

    printf("QNX Shared Memory + Message Passing Demo\n");
    printf("==========================================\n\n");

    /* Parse args */
    if (argc >= 2) {
        if (strcmp(argv[1], "server") == 0) mode = 1;
        else if (strcmp(argv[1], "client") == 0) mode = 2;
    }

    /* Initialize shared memory */
    if (shmem_init(&shmem, SHMEM_NAME, SHMEM_SIZE) != 0) {
        return EXIT_FAILURE;
    }

    /* Initialize server */
    if (server_init(&server) != 0) {
        shmem_fini(&shmem);
        return EXIT_FAILURE;
    }

    /* In "both" mode, also create a client connection to ourselves */
    if (mode == 0 || mode == 2) {
        if (client_connect(&client, server.chid) != 0) {
            shmem_fini(&shmem);
            ChannelDestroy(server.chid);
            return EXIT_FAILURE;
        }
    }

    /* Initialize pattern in shared memory */
    memset(shmem.virt, 0xAA, SHMEM_SIZE);

    printf("\n");

    /* Main loop */
    while (!done) {
        if (mode == 0 || mode == 2) {
            /* Client: send some messages */
            static int counter = 0;

            msg.type = MSG_TYPE_WRITE;
            msg.offset = (counter * 256) % SHMEM_SIZE;
            msg.size = snprintf(msg.data, sizeof(msg.data),
                                "Message #%d at offset %d", counter, msg.offset);
            client_send_msg(&client, &msg);

            counter++;
            delay(100);

            if (counter >= 10 && mode == 2) {
                msg.type = MSG_TYPE_QUIT;
                client_send_msg(&client, &msg);
                done = 1;
            }
        }

        if (mode == 0 || mode == 1) {
            /* Server: process messages */
            if (server_handle_msg(&server, &shmem, &msg) != 0) {
                done = 1;
            }
        }

        if (mode == 0 && counter >= 10) {
            done = 1;
        }
    }

    printf("\nCleaning up...\n");

    /* Cleanup */
    if (mode == 0 || mode == 2) {
        ConnectDetach(client.coid);
    }

    ChannelDestroy(server.chid);
    shmem_fini(&shmem);

    printf("Done.\n");
    return 0;
}