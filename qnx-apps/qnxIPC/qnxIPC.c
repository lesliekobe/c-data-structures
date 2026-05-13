/*
 * qnxIPC.c - QNX Interprocess Communication examples
 *
 * Demonstrates core QNX IPC primitives:
 * 1. Channels and Connections (ChannelCreate/ConnectAttach)
 * 2. Messages (MsgSend/MsgReceive/MsgReply)
 * 3. Pulses (procmgr_dmcreate/Dispatch)
 * 4. Mutexes and Condition Variables (pthread_mutex/cond)
 * 5. Semaphores (sem_*)
 *
 * Build: qcc -o qnxIPC qnxIPC.c -lpthread
 * Run: ./qnxIPC [server|client|both]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/procmgr.h>
#include <sys/dispatch.h>
#include <sys/slog.h>
#include <sys/slogf.h>

#include <pthread.h>
#include <semaphore.h>

/* ========================================================================
 * Constants
 * ======================================================================== */

#define SERVER_NAME     "/IPC_server"
#define MAX_MSG_SIZE    1024

/* Message types */
#define MSG_ECHO        1
#define MSG_GET_STATS   2
#define MSG_QUIT        3

/* ========================================================================
 * Message Structures
 * ======================================================================== */

typedef struct {
    int type;
    int pid;
    int data_len;
    char data[MAX_MSG_SIZE];
} ipc_msg_t;

typedef struct {
    int total_messages;
    int total_bytes;
    int active_clients;
} stats_t;

typedef struct {
    int status;
    stats_t stats;
} reply_t;

/* ========================================================================
 * Server Implementation
 * ======================================================================== */

/* Server context */
static int server_chid = -1;
static stats_t global_stats;
static pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Handle incoming message */
static int handle_message(ipc_msg_t *msg, reply_t *reply)
{
    int ret = 0;

    pthread_mutex_lock(&stats_mutex);

    switch (msg->type) {
    case MSG_ECHO:
        printf("[Server] ECHO from pid=%d: %s\n", msg->pid, msg->data);
        global_stats.total_messages++;
        global_stats.total_bytes += msg->data_len;
        ret = 0;
        break;

    case MSG_GET_STATS:
        printf("[Server] STATS request from pid=%d\n", msg->pid);
        reply->stats = global_stats;
        ret = 0;
        break;

    case MSG_QUIT:
        printf("[Server] QUIT from pid=%d\n", msg->pid);
        ret = 1;  /* Signal to exit */
        break;

    default:
        printf("[Server] Unknown message type: %d\n", msg->type);
        ret = -1;
        break;
    }

    pthread_mutex_unlock(&stats_mutex);

    reply->status = ret;
    return ret;
}

/* Server main loop */
static void *server_thread(void *arg)
{
    ipc_msg_t msg;
    reply_t reply;
    int rcvid;
    int done = 0;

    printf("[Server] Started, chid=%d\n", server_chid);

    while (!done) {
        /* Receive message - blocks until message arrives */
        rcvid = MsgReceive(server_chid, &msg, sizeof(msg), NULL);

        if (rcvid == -1) {
            fprintf(stderr, "[Server] MsgReceive failed: %s\n",
                    strerror(errno));
            continue;
        }

        /* Handle the message */
        int exit_requested = handle_message(&msg, &reply);

        /* Reply to client
         * rcvid: from MsgReceive
         * status: return code for client
         * reply: reply buffer
         */
        MsgReply(rcvid, exit_requested, &reply, sizeof(reply));

        if (exit_requested) {
            done = 1;
        }
    }

    printf("[Server] Exiting\n");
    return NULL;
}

/* Start the server */
static int start_server(void)
{
    pthread_t tid;

    /* Create a channel for this server */
    server_chid = ChannelCreate(_NTO_CHF_DISCONNECT |
                                  _NTO_CHF_UNBLOCK |
                                  _NTO_CHF_SENDER_LEN);
    if (server_chid == -1) {
        fprintf(stderr, "[Server] ChannelCreate failed: %s\n",
                strerror(errno));
        return -1;
    }

    /* Start server thread */
    if (pthread_create(&tid, NULL, server_thread, NULL) != EOK) {
        fprintf(stderr, "[Server] pthread_create failed\n");
        ChannelDestroy(server_chid);
        return -1;
    }

    printf("[Server] Started on chid=%d\n", server_chid);
    return 0;
}

/* ========================================================================
 * Client Implementation
 * ======================================================================== */

static int client_coid = -1;

/* Connect to server */
static int client_connect(const char *server_name)
{
    /* Attach to server's channel
     * ND_LOCAL_NODE: same node
     * 0: same process (actually server's pid would be used)
     * server_chid: channel to attach to
     */
    client_coid = ConnectAttach(ND_LOCAL_NODE, 0, server_chid,
                                 _NTO_COF_CLOEXEC, 0);
    if (client_coid == -1) {
        fprintf(stderr, "[Client] ConnectAttach failed: %s\n",
                strerror(errno));
        return -1;
    }

    printf("[Client] Connected to server, coid=%d\n", client_coid);
    return 0;
}

/* Disconnect from server */
static void client_disconnect(void)
{
    if (client_coid != -1) {
        ConnectDetach(client_coid);
        client_coid = -1;
    }
}

/* Send message and wait for reply */
static int client_send_msg(ipc_msg_t *msg, reply_t *reply)
{
    int status;

    /* MsgSend - synchronous message send
     * coid: server connection
     * sbuf: message to send
     * slen: send buffer length
     * rbuf: reply buffer
     * rlen: reply buffer length
     *
     * Returns: 0 on success, -1 on error
     * Note: reply status is in reply->status
     */
    status = MsgSend(client_coid, msg, sizeof(*msg), reply, sizeof(*reply));

    if (status == -1) {
        fprintf(stderr, "[Client] MsgSend failed: %s\n", strerror(errno));
        return -1;
    }

    printf("[Client] Sent type=%d, server status=%d\n",
           msg->type, reply->status);

    return reply->status;
}

/* Client main loop */
static void client_main(int num_messages)
{
    ipc_msg_t msg;
    reply_t reply;
    int i;

    printf("[Client] Sending %d messages to server\n", num_messages);

    for (i = 0; i < num_messages; i++) {
        /* Prepare message */
        msg.type = MSG_ECHO;
        msg.pid = getpid();
        msg.data_len = snprintf(msg.data, sizeof(msg.data),
                                 "Hello #%d from client", i);

        /* Send */
        if (client_send_msg(&msg, &reply) != 0) {
            fprintf(stderr, "[Client] Server returned error\n");
            break;
        }

        /* Brief delay */
        usleep(10000);  /* 10ms */
    }

    /* Request server stats */
    printf("\n[Client] Requesting server stats...\n");
    msg.type = MSG_GET_STATS;
    msg.pid = getpid();
    msg.data_len = 0;

    if (client_send_msg(&msg, &reply) == 0) {
        printf("[Client] Server stats:\n");
        printf("  total_messages: %d\n", reply.stats.total_messages);
        printf("  total_bytes: %d\n", reply.stats.total_bytes);
        printf("  active_clients: %d\n", reply.stats.active_clients);
    }

    /* Send quit */
    printf("\n[Client] Sending QUIT to server...\n");
    msg.type = MSG_QUIT;
    msg.pid = getpid();
    msg.data_len = 0;
    client_send_msg(&msg, &reply);
}

/* ========================================================================
 * Pulse/Notification Demo
 * ======================================================================== */

/* Timer pulse handler - demonstrates async notifications */
static void *timer_pulse_thread(void *arg)
{
    int rcvid;
    struct _pulse pulse;
    int timer_id;
    struct sigevent event;
    struct itimerspec itime;

    printf("[TimerPulse] Thread started\n");

    /* Create a channel for timer pulses */
    int chid = ChannelCreate(0);
    if (chid == -1) {
        fprintf(stderr, "[TimerPulse] ChannelCreate failed\n");
        return NULL;
    }

    /* Create a connection so we can receive */
    int coid = ConnectAttach(ND_LOCAL_NODE, 0, chid, _NTO_COF_CLOEXEC, 0);
    if (coid == -1) {
        fprintf(stderr, "[TimerPulse] ConnectAttach failed\n");
        return NULL;
    }

    /* Set up pulse to be delivered to our channel */
    SIGEV_PULSE_INIT(&event, coid, 10, 1, 0);

    /* Create a timer */
    timer_id = TimerCreate(CLOCK_MONOTONIC, &event);
    if (timer_id == -1) {
        fprintf(stderr, "[TimerPulse] TimerCreate failed\n");
        return NULL;
    }

    /* Arm timer to fire every 500ms */
    itime.it_value.tv_sec = 0;
    itime.it_value.tv_nsec = 500000000;  /* 500ms */
    itime.it_interval.tv_sec = 0;
    itime.it_interval.tv_nsec = 500000000;  /* 500ms period */

    TimerSet(timer_id, TIMER_ABSTIME, &itime, NULL);
    printf("[TimerPulse] Timer armed (500ms period)\n");

    /* Pulse receive loop */
    int count = 0;
    while (count < 10) {
        rcvid = MsgReceive(chid, &pulse, sizeof(pulse), NULL);

        if (rcvid == 0 && pulse.code == 10) {
            count++;
            printf("[TimerPulse] Pulse #%d received\n", pulse.value.sival_int);

            /* In real code, you would do work here */
        }
    }

    /* Cleanup */
    TimerDestroy(timer_id);
    ConnectDetach(coid);
    ChannelDestroy(chid);

    printf("[TimerPulse] Thread exiting\n");
    return NULL;
}

/* ========================================================================
 * Semaphore Demo
 * ======================================================================== */

static sem_t sem_demo;
static pthread_mutex_t print_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Worker thread that waits on semaphore */
static void *sem_worker(void *arg)
{
    int id = *(int *)arg;

    for (int i = 0; i < 5; i++) {
        /* Wait on semaphore */
        sem_wait(&sem_demo);

        pthread_mutex_lock(&print_mutex);
        printf("[SemWorker %d] Critical section #%d\n", id, i);
        usleep(100000);  /* Simulate work */
        pthread_mutex_unlock(&print_mutex);

        /* Signal we're done */
        sem_post(&sem_demo);

        usleep(50000);  /* Gap between iterations */
    }

    return NULL;
}

/* Run semaphore demo */
static void run_semaphore_demo(void)
{
    pthread_t tid[3];
    int ids[3] = {0, 1, 2};

    printf("\n[SemDemo] Starting semaphore demo\n");

    /* Initialize semaphore to 1 (binary semaphore) */
    sem_init(&sem_demo, 0, 1);

    /* Create worker threads */
    for (int i = 0; i < 3; i++) {
        pthread_create(&tid[i], NULL, sem_worker, &ids[i]);
    }

    /* Wait for all threads */
    for (int i = 0; i < 3; i++) {
        pthread_join(tid[i], NULL);
    }

    sem_destroy(&sem_demo);

    printf("[SemDemo] Completed\n");
}

/* ========================================================================
 * Main
 * ======================================================================== */

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [server|client|both|timer|sem]\n", prog);
}

int main(int argc, char *argv[])
{
    const char *mode = "both";
    int ret = 0;

    if (argc >= 2) {
        mode = argv[1];
    }

    printf("QNX IPC Demo - mode: %s\n", mode);
    printf("==========================\n\n");

    if (strcmp(mode, "timer") == 0) {
        /* Timer + Pulse demo */
        pthread_t tid;
        pthread_create(&tid, NULL, timer_pulse_thread, NULL);
        pthread_join(tid, NULL);
    }
    else if (strcmp(mode, "sem") == 0) {
        /* Semaphore demo */
        run_semaphore_demo();
    }
    else if (strcmp(mode, "server") == 0) {
        /* Server only */
        if (start_server() != 0) {
            return 1;
        }
        /* Wait forever (server mode) */
        pause();
    }
    else if (strcmp(mode, "client") == 0) {
        /* Client only */
        if (client_connect(SERVER_NAME) != 0) {
            return 1;
        }
        client_main(10);
        client_disconnect();
    }
    else {
        /* Both: start server and client */
        if (start_server() != 0) {
            return 1;
        }

        /* Give server time to start */
        usleep(100000);

        /* Start client */
        if (client_connect(SERVER_NAME) != 0) {
            return 1;
        }

        client_main(5);

        /* Brief pause */
        usleep(100000);

        client_disconnect();

        /* Server will exit after quit message */
        sleep(1);
    }

    printf("\nDone.\n");
    return ret;
}