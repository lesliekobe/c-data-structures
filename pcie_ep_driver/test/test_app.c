#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <string.h>
#include "xilinx_pcie_ep.h"

int main(int argc, char *argv[])
{
    int fd;
    struct xilinx_ep_status status;
    struct xilinx_ep_config config;
    int ret;

    fd = open("/dev/xilinx_ep0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }

    /* Get status */
    ret = ioctl(fd, XILINX_EP_IOC_STATUS, &status);
    if (ret < 0) {
        perror("IOC_STATUS failed");
        close(fd);
        return 1;
    }

    printf("EP Status:\n");
    printf("  Control:   0x%08x\n", status.ctrl);
    printf("  Status:    0x%08x\n", status.status);
    printf("  DMA Active: %d\n", status.dma_active);
    printf("  IRQs:      %u\n", status.irq_count);
    printf("  RX: %llu bytes\n", status.rx_bytes);
    printf("  TX: %llu bytes\n", status.tx_bytes);

    /* Configure DMA ring size */
    config.dma_ring_size = 256;
    config.irq_count = 1;
    memset(config.reserved, 0, sizeof(config.reserved));

    ret = ioctl(fd, XILINX_EP_IOC_CONFIG, &config);
    if (ret < 0) {
        perror("IOC_CONFIG failed");
    } else {
        printf("\nDMA configured with ring size %d\n", config.dma_ring_size);
    }

    /* Start EP */
    ret = ioctl(fd, XILINX_EP_IOC_START);
    if (ret < 0) {
        perror("IOC_START failed");
    } else {
        printf("EP engine started\n");
    }

    /* Stop EP */
    sleep(1);
    ret = ioctl(fd, XILINX_EP_IOC_STOP);
    if (ret < 0) {
        perror("IOC_STOP failed");
    } else {
        printf("EP engine stopped\n");
    }

    /* Reset EP */
    ret = ioctl(fd, XILINX_EP_IOC_RESET);
    if (ret < 0) {
        perror("IOC_RESET failed");
    } else {
        printf("EP reset complete\n");
    }

    close(fd);
    return 0;
}