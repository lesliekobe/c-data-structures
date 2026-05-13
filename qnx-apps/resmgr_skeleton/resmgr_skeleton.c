/*
 * resmgr_skeleton.c - QNX Resource Manager skeleton
 *
 * A minimal resource manager that serves as a template for
 * writing device drivers and file system extensions in QNX.
 *
 * QNX resource managers handle open/read/write/ioctl/close
 * operations by implementing _resmgr_open(), _resmgr_read(), etc.
 *
 * Build: qcc -o resmgr resmgr_skeleton.c -lresmgr
 * Run: ./resmgr &
 *      cat /dev/sample
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/resmgr.h>
#include <sys/iofunc.h>

/* Structure to hold our device context */
typedef struct {
    iofunc_attr_t attr;    /* Standard I/O attribute structure */
    int data;              /* Device-specific data */
} device_ctx_t;

/* Open handler - called when device is opened */
int resmgr_open(resmgr_context_t *ctp, io_open_t *msg, void *extra, void *reserved) {
    /* Default open handler - allocates context, initializes */
    return iofunc_open_default(ctp, msg, extra, reserved);
}

/* Read handler - called when data is read from device */
int resmgr_read(resmgr_context_t *ctp, io_read_t *msg, void *reserved) {
    int status;
    int nbytes;
    char buf[64];

    /* Must be root to open resource manager in this way typically */
    status = iofunc_read_verify(ctp, msg, NULL, 0);
    if (status != EOK) {
        return status;
    }

    /* Build response */
    snprintf(buf, sizeof(buf), "Sample read, %d bytes available\n", (int)strlen(buf));
    nbytes = strlen(buf);

    /* Set number of bytes to return */
    _IO_SET_READ_NBYTES(ctp, nbytes);

    /* If this is first read (offset 0), send data */
    if (msg->i.nbytes > 0) {
        return _RESMGR_NPARTS(0);  /* Use default data in part */
    }

    /* Return data buffer */
    return _RESMGR_PTR(ctp, buf, nbytes);
}

/* Write handler - called when data is written to device */
int resmgr_write(resmgr_context_t *ctp, io_write_t *msg, void *reserved) {
    char buf[256];
    int status;

    /* Verify write access */
    status = iofunc_write_verify(ctp, msg, NULL, 0);
    if (status != EOK) {
        return status;
    }

    /* Read data from message */
    if (msg->i.nbytes > sizeof(buf) - 1) {
        return EINVAL;
    }

    /* Copy data (would use MsgRead in real scenario) */
    /* For now just acknowledge write */
    _IO_SET_WRITE_NBYTES(ctp, msg->i.nbytes);

    return _RESMGR_NPARTS(0);
}

/* Ioctl handler - device control */
int resmgr_ioctl(resmgr_context_t *ctp, io_ioctl_t *msg, void *reserved) {
    int status;

    /* Must be root for ioctls */
    status = iofunc_ioctl_verify(ctp, msg, NULL, 0);
    if (status != EOK) {
        return status;
    }

    /* Handle specific ioctls */
    switch (msg->i.com) {
        default:
            return ENOSYS;
    }
}

/* Close handler - cleanup when device closed */
int resmgr_close(resmgr_context_t *ctp, void *reserved) {
    /* Default close handles reference counting */
    return EOK;
}

/* Attach our resource manager to a path in the namespace */
static int resmgr_attach(resmgr_context_t *ctp, char *path) {
    resmgr_attr_t resmgr_attr;
    iofunc_attr_t iofunc_attr;
    device_ctx_t ctx;

    /* Initialize resource manager attributes */
    memset(&resmgr_attr, 0, sizeof(resmgr_attr));
    resmgr_attr.msg_max_size = 2048;

    /* Initialize I/O function attributes */
    iofunc_attr_init(&ctx.attr, S_IFCHR | 0666, NULL, NULL);

    /* Attach to filesystem */
    return resmgr_attach(ctp, &resmgr_attr, path, _FTYPE_ANY,
                         _RESMGR_FLAG_DIR | _RESMGR_FLAG_OPEN,
                         &resmgr_open, &resmgr_write, &ctx);
}

int main(int argc, char *argv[]) {
    resmgr_context_t *ctp;
    int chid;
    int rcvid;
    int status;

    /* Create a channel for messages */
    chid = ChannelCreate(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK);
    if (chid == -1) {
        fprintf(stderr, "ChannelCreate failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Fill in resource manager context */
    ctp = resmgr_context_alloc(chd);
    if (!ctp) {
        fprintf(stderr, "resmgr_context_alloc failed\n");
        return EXIT_FAILURE;
    }

    /* Attach to device path */
    if (resmgr_attach(ctp, "/dev/sample") == -1) {
        fprintf(stderr, "resmgr_attach failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    printf("Resource manager started on /dev/sample\n");

    /* Main message loop */
    while (1) {
        /* Context unblock next rcvid with timeout */
        ctp = resmgr_context_run(ctp, _NTO_TIMEOUT_MAX, -1);

        /* Handle messages */
        while ((rcvid = MsgReceive(ctp->chid, &ctp->msg, sizeof(ctp->msg), NULL)) != -1) {
            /* Try to intercept as resource manager first */
            status = resmgr_handler(ctp, rcvid);
            if (status != EBAD_MSG) {
                continue;  /* Message handled */
            }

            /* Fallback: iofunc */
            iofunc_dispatch_default(ctp, rcvid);
        }
    }

    /* Should never reach here */
    resmgr_context_free(ctp);
    return 0;
}