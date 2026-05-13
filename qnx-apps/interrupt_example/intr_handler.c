/*
 * interrupt_handler.c - QNX Interrupt handling example
 *
 * Demonstrates how to attach an interrupt handler to handle
 * hardware interrupts from PCIe devices in QNX Neutrino.
 *
 * Build: qcc -o interrupt_handler interrupt_handler.c
 * Run: ./interrupt_handler
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/siginfo.h>
#include <sys/neutrino.h>
#include <sys/syspage.h>

/* Interrupt handler context */
typedef struct {
    int irq;
    int receive_fd;
    int coid;
    int interrupt_id;
} intr_context_t;

/* Forward declarations */
static void *interrupt_thread(void *arg);
static int intr_attach_irq(intr_context_t *ctx, int irq);

/*
 * Interrupt handler function
 * This runs in interrupt context - cannot call blocking functions
 */
static const struct sigevent *intr_handler(void *arg, int id)
{
    intr_context_t *ctx = (intr_context_t *)arg;

    /* In a real driver, you would:
     * 1. Read device interrupt status register
     * 2. Handle the interrupt (DMA done, data ready, etc.)
     * 3. Clear the interrupt source
     * 4. Return interrupt event to wake thread
     */

    /* Signal main thread that interrupt occurred */
    return (struct sigevent *)arg;  /* Placeholder */
}

/* Attach interrupt to IRQ line */
static int intr_attach_irq(intr_context_t *ctx, int irq)
{
    struct sigevent event;
    int rc;

    /* Initialize interrupt event to wake our thread */
    SIGEV_PULSE_INIT(&event, ctx->coid, 10, 0, 0);

    /* Attach interrupt handler
     * _NTO_INTR_FLAGS_TRACK_MASTER: track interrupt owner
     */
    ctx->interrupt_id = InterruptAttachEvent(irq, &event,
                                              _NTO_INTR_FLAGS_TRACK_MASTER);
    if (ctx->interrupt_id == -1) {
        fprintf(stderr, "InterruptAttachEvent failed: %s\n", strerror(errno));
        return -1;
    }

    printf("Attached to IRQ %d, interrupt id=%d\n", irq, ctx->interrupt_id);
    return 0;
}

/* Interrupt handling thread */
static void *interrupt_thread(void *arg)
{
    intr_context_t *ctx = (intr_context_t *)arg;
    struct _pulse pulse;
    int rcvid;

    printf("Interrupt thread started (tid=%d)\n", pthread_self());

    /* Main interrupt handling loop */
    while (1) {
        rcvid = MsgReceive(ctx->receive_fd, &pulse, sizeof(pulse), NULL);

        if (rcvid == -1) {
            fprintf(stderr, "MsgReceive failed: %s\n", strerror(errno));
            continue;
        }

        /* Check for pulse (interrupt signal) */
        if (rcvid == 0 && pulse.code == 10) {
            /* Interrupt occurred */
            static int count = 0;
            count++;

            if (count % 1000 == 0) {
                printf("Interrupt #%d (IRQ=%d)\n", count, ctx->irq);
            }

            /* Re-arm interrupt if needed */
            /* InterruptUnmask(ctx->irq, ctx->interrupt_id); */
        }
    }

    return NULL;
}

/*
 * Wait for interrupt with timeout
 * Returns: 0 on interrupt, -1 on timeout, -2 on error
 */
static int intr_wait_timeout(intr_context_t *ctx, int timeout_ms)
{
    struct _pulse pulse;
    int rcvid;
    struct timespec ts;

    ts.tv_sec = timeout_ms / 1000;
    ts.tv_nsec = (timeout_ms % 1000) * 1000000;

    rcvid = MsgReceivePulseTimeout(ctx->receive_fd, &ts, &pulse, NULL);

    if (rcvid == -1) {
        if (errno == ETIMEDOUT) {
            return -1;  /* Timeout */
        }
        return -2;  /* Error */
    }

    if (rcvid == 0 && pulse.code == 10) {
        return 0;  /* Interrupt received */
    }

    return -2;
}

int main(int argc, char *argv[])
{
    intr_context_t ctx;
    pthread_t tid;
    int irq = 10;  /* Default IRQ - change for your hardware */
    int timeout_ms = 1000;
    int ret = EXIT_SUCCESS;

    printf("QNX Interrupt Handler Example\n");
    printf("==============================\n\n");

    /* Parse command line args */
    if (argc >= 2) {
        irq = atoi(argv[1]);
    }
    printf("Using IRQ %d\n", irq);

    /* Create a channel for interrupt messages */
    ctx.receive_fd = ChannelCreate(_NTO_CHF_DISCONNECT | _NTO_CHF_UNBLOCK);
    if (ctx.receive_fd == -1) {
        fprintf(stderr, "ChannelCreate failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Create a connection to the channel */
    ctx.coid = ConnectAttach(ND_LOCAL_NODE, 0, ctx.receive_fd,
                              _NTO_COF_CLOEXEC, 0);
    if (ctx.coid == -1) {
        fprintf(stderr, "ConnectAttach failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    /* Attach interrupt handler */
    if (intr_attach_irq(&ctx, irq) != 0) {
        fprintf(stderr, "Failed to attach interrupt\n");
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    /* Start interrupt handling thread */
    if (pthread_create(&tid, NULL, interrupt_thread, &ctx) != EOK) {
        fprintf(stderr, "pthread_create failed\n");
        ret = EXIT_FAILURE;
        goto cleanup;
    }

    printf("Waiting for interrupts (timeout=%dms)...\n", timeout_ms);
    printf("Press Ctrl+C to exit\n\n");

    /* Main loop - wait for interrupts with timeout */
    while (1) {
        int status = intr_wait_timeout(&ctx, timeout_ms);

        if (status == 0) {
            /* Interrupt received */
            static int count = 0;
            if (++count % 10 == 0) {
                printf("Interrupt count: %d\n", count);
            }
        } else if (status == -1) {
            /* Timeout - do other work here */
            /* printf("Timeout - no interrupt\n"); */
        } else {
            /* Error */
            fprintf(stderr, "intr_wait error\n");
            break;
        }

        /* Sleep a bit to avoid spinning */
        delay(10);
    }

cleanup:
    /* Cleanup */
    if (ctx.interrupt_id != -1) {
        InterruptDetach(ctx.interrupt_id);
    }

    if (ctx.coid != -1) {
        ConnectDetach(ctx.coid);
    }

    if (ctx.receive_fd != -1) {
        ChannelDestroy(ctx.receive_fd);
    }

    printf("Exiting\n");
    return ret;
}