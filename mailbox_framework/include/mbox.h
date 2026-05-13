/*
 * mbox.h - Shared Memory Mailbox Userspace API
 *
 * Header for the shared memory mailbox library.
 * Provides send/receive interface for inter-process mail messaging.
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#ifndef MBOX_H_
#define MBOX_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Mailbox Configuration
 * ======================================================================== */

#define MBOX_NAME_LEN       32
#define MBOX_MAX_CHANNELS   16
#define MBOX_MAX_MSG_SIZE   4096
#define MBOX_DEFAULT_DEPTH  64

/* Mailbox identifier */
typedef struct {
    char name[MBOX_NAME_LEN];
    int  id;
} mbox_t;

/* Message structure */
typedef struct {
    uint32_t magic;          /* MBOX_MSG_MAGIC */
    uint32_t msg_id;         /* Auto-incrementing message ID */
    uint32_t timestamp_sec;  /* Send timestamp (seconds) */
    uint32_t timestamp_nsec; /* Send timestamp (nanoseconds) */
    uint32_t sender_pid;     /* Sender process ID */
    uint32_t channel;        /* Message channel (0-15) */
    uint32_t size;           /* Payload size */
    uint32_t flags;          /* Message flags */
    uint8_t  payload[];      /* Flexible array member */
} mbox_msg_t;

/* Message flags */
#define MBOX_MSG_FLAG_URGENT    (1 << 0)   /* High priority message */
#define MBOX_MSG_FLAG_BROADCAST  (1 << 1)   /* Broadcast to all readers */
#define MBOX_MSG_FLAG_PEEK       (1 << 2)   /* Don't remove on read */

/* Mailbox statistics */
typedef struct {
    uint32_t messages_sent;
    uint32_t messages_recv;
    uint32_t messages_dropped;
    uint32_t current_depth;
    uint32_t max_depth;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
} mbox_stats_t;

/* Open flags */
#define MBOX_O_RDONLY   0x01   /* Open for receiving */
#define MBOX_O_WRONLY   0x02   /* Open for sending */
#define MBOX_O_RDWR     0x03   /* Open for both */
#define MBOX_O_CREATE   0x10   /* Create mailbox if not exists */
#define MBOX_O_NONBLOCK 0x20   /* Non-blocking operations */

/* IOCTL commands */
#define MBOX_IOC_MAGIC      'M'
#define MBOX_IOC_CREATE     _IOW(MBOX_IOC_MAGIC, 0, struct mbox_create_req)
#define MBOX_IOC_OPEN       _IOW(MBOX_IOC_MAGIC, 1, struct mbox_open_req)
#define MBOX_IOC_CLOSE      _IO (MBOX_IOC_MAGIC, 2)
#define MBOX_IOC_SEND       _IOW(MBOX_IOC_MAGIC, 3, struct mbox_send_req)
#define MBOX_IOC_RECV       _IOWR(MBOX_IOC_MAGIC, 4, struct mbox_recv_req)
#define MBOX_IOC_PEEK       _IOWR(MBOX_IOC_MAGIC, 5, struct mbox_recv_req)
#define MBOX_IOC_FLUSH      _IO (MBOX_IOC_MAGIC, 6)
#define MBOX_IOC_STATS      _IOR(MBOX_IOC_MAGIC, 7, mbox_stats_t)
#define MBOX_IOC_SETFLAGS   _IOW(MBOX_IOC_MAGIC, 8, uint32_t)
#define MBOX_IOC_GETFLAGS   _IOR(MBOX_IOC_MAGIC, 9, uint32_t)

/* Request structures for IOCTL */
struct mbox_create_req {
    char name[MBOX_NAME_LEN];
    uint32_t queue_depth;
    uint32_t msg_size;
    uint32_t flags;
};

struct mbox_open_req {
    char name[MBOX_NAME_LEN];
    uint32_t flags;
};

struct mbox_send_req {
    uint8_t  channel;
    uint32_t size;
    uint32_t flags;
    /* payload follows in separate copy */
};

struct mbox_recv_req {
    uint32_t channel_mask;   /* Which channels to check */
    uint32_t timeout_ms;     /* 0 = nonblock, -1 = infinite */
    uint32_t out_size;       /* Return: actual msg size */
    uint32_t out_msg_id;      /* Return: message ID */
    uint32_t out_channel;     /* Return: channel */
    uint32_t out_flags;      /* Return: message flags */
};

/* ========================================================================
 * Library API
 * ======================================================================== */

/*
 * mbox_open - Open or create a mailbox
 * @name: Mailbox name (up to MBOX_NAME_LEN-1 chars)
 * @flags: MBOX_O_* flags
 *
 * Returns: Mailbox handle or -1 on error
 */
int mbox_open(const char *name, uint32_t flags);

/*
 * mbox_close - Close a mailbox
 * @mbox: Mailbox handle from mbox_open()
 *
 * Returns: 0 on success, -1 on error
 */
int mbox_close(int mbox);

/*
 * mbox_send - Send a message
 * @mbox: Mailbox handle
 * @channel: Target channel (0-15)
 * @payload: Message data
 * @size: Payload size (must be <= MBOX_MAX_MSG_SIZE)
 * @flags: MBOX_MSG_FLAG_* flags
 *
 * Returns: Message ID on success, -1 on error
 */
int mbox_send(int mbox, uint8_t channel, const void *payload, size_t size, uint32_t flags);

/*
 * mbox_recv - Receive a message
 * @mbox: Mailbox handle
 * @channel_mask: Bitmask of channels to check
 * @payload: Buffer to receive data
 * @max_size: Maximum payload size
 * @timeout_ms: 0=nonblock, -1=infinite, >0=ms
 *
 * Returns: Message size on success, -1 on error, 0 on no message
 */
int mbox_recv(int mbox, uint32_t channel_mask, void *payload, size_t max_size, int timeout_ms);

/*
 * mbox_peek - Peek at next message without removing
 * @mbox: Mailbox handle
 * @channel_mask: Channels to check
 * @payload: Buffer for message (can be NULL to just get info)
 * @max_size: Max payload size
 * @msg_id: Output: message ID
 * @out_channel: Output: channel number
 * @out_flags: Output: message flags
 *
 * Returns: Message size on success, -1 on error, 0 if empty
 */
int mbox_peek(int mbox, uint32_t channel_mask, void *payload, size_t max_size,
              uint32_t *msg_id, uint8_t *out_channel, uint32_t *out_flags);

/*
 * mbox_flush - Discard all messages in mailbox
 * @mbox: Mailbox handle
 *
 * Returns: 0 on success, -1 on error
 */
int mbox_flush(int mbox);

/*
 * mbox_stats - Get mailbox statistics
 * @mbox: Mailbox handle
 * @stats: Output statistics structure
 *
 * Returns: 0 on success, -1 on error
 */
int mbox_stats(int mbox, mbox_stats_t *stats);

/*
 * mbox_get_fd - Get file descriptor for poll/select
 * @mbox: Mailbox handle
 *
 * Returns: File descriptor, or -1 if not supported
 *
 * Use with poll(): wait for readability when message available
 */
int mbox_get_fd(int mbox);

/* ========================================================================
 * Utility Macros
 * ======================================================================== */

#define MBOX_MSG_MAGIC   0x4D424F58  /* 'MBOX' in ASCII */

/* Helper to get message from payload buffer */
#define MBOX_PAYLOAD_TO_MSG(payload) \
    ((mbox_msg_t *)((uint8_t *)(payload) - offsetof(mbox_msg_t, payload)))

/* Helper to get payload size from message */
#define MBOX_MSG_PAYLOAD_SIZE(msg) ((msg)->size)

/* ========================================================================
 * Error codes
 * ======================================================================== */

#define MBOX_ERR_OK          0
#define MBOX_ERR_NOENT      -1   /* Mailbox does not exist */
#define MBOX_ERR_EXIST       -2   /* Mailbox already exists */
#define MBOX_ERR_INVAL       -3   /* Invalid argument */
#define MBOX_ERR_NOMEM       -4   /* Out of memory */
#define MBOX_ERR_NOBUFS      -5   /* No free message slots */
#define MBOX_ERR_TIMEOUT     -6   /* Operation timed out */
#define MBOX_ERR_WOULDBLOCK  -7   /* Would block (nonblock mode) */
#define MBOX_ERR_TOOBIG      -8   /* Message too large */
#define MBOX_ERR_CLOSED      -9   /* Mailbox closed */
#define MBOX_ERR_CORRUPT    -10   /* Mailbox data corrupted */

/* Get human-readable error string */
const char *mbox_strerror(int err);

#ifdef __cplusplus
}
#endif

#endif /* MBOX_H_ */