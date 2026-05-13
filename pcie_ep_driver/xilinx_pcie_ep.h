# SPDX-License-Identifier: GPL-2.0
/*
 * xilinx_pcie_ep.h - Xilinx Spartan-6 PCIe Endpoint public header
 *
 * Userspace applications can include this header for ioctl definitions.
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#ifndef __XILINX_PCIE_EP_H__
#define __XILINX_PCIE_EP_H__

#include <linux/ioctl.h>
#include <linux/types.h>

#define XILINX_EP_IOC_MAGIC   'X'

/* Control ioctls */
#define XILINX_EP_IOC_START       _IO(XILINX_EP_IOC_MAGIC, 0)
#define XILINX_EP_IOC_STOP        _IO(XILINX_EP_IOC_MAGIC, 1)
#define XILINX_EP_IOC_RESET       _IO(XILINX_EP_IOC_MAGIC, 2)

/* Configuration ioctl */
struct xilinx_ep_config {
    __u32 dma_ring_size;
    __u32 irq_count;
    __u32 reserved[4];
};
#define XILINX_EP_IOC_CONFIG  _IOW(XILINX_EP_IOC_MAGIC, 3, struct xilinx_ep_config)

/* Status ioctl */
struct xilinx_ep_status {
    __u32 ctrl;
    __u32 status;
    __u32 dma_active;
    __u32 irq_count;
    __u64 rx_bytes;
    __u64 tx_bytes;
};
#define XILINX_EP_IOC_STATUS  _IOR(XILINX_EP_IOC_MAGIC, 4, struct xilinx_ep_status)

/* DMA ioctls */
struct xilinx_ep_dma_desc {
    __u64 user_addr;   /* userspace buffer virtual address */
    __u64 dev_addr;    /* device DMA address (filled by driver) */
    __u32 size;        /* transfer size in bytes */
    __u32 flags;       /* transfer direction: 0=write, 1=read */
};
#define XILINX_EP_IOC_DMA_ALLOC   _IOWR(XILINX_EP_IOC_MAGIC, 5, struct xilinx_ep_dma_desc)
#define XILINX_EP_IOC_DMA_FREE    _IOWR(XILINX_EP_IOC_MAGIC, 6, struct xilinx_ep_dma_desc)
#define XILINX_EP_IOC_DMA_START   _IOWR(XILINX_EP_IOC_MAGIC, 7, struct xilinx_ep_dma_desc)
#define XILINX_EP_IOC_DMA_STOP    _IO(XILINX_EP_IOC_MAGIC, 8)

/* Device path (auto-created by udev rules) */
#define XILINX_EP_DEVICE_PATH "/dev/xilinx_ep0"

/* Ioctl range check */
#define XILINX_EP_IOC_MAXNR  8

#endif /* __XILINX_PCIE_EP_H__ */