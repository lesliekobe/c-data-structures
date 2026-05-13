# SPDX-License-Identifier: GPL-2.0
/*
 * xilinx_pcie_ep.c - Xilinx Spartan-6 PCIe Endpoint Linux Driver
 *
 * Generic framework for Xilinx FPGA PCIe Endpoint devices.
 * Based on Spartan-6 FPGA Integrated Endpoint Block.
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/delay.h>

#define DRV_NAME    "xilinx_pcie_ep"
#define DRV_VERSION "1.0.0"

#define EP_VENDOR_ID   0x10EE   /* Xilinx */
#define EP_DEVICE_ID   0x0007   /* Spartan-6 PCIe EP default */

/* BAR definitions */
#define BAR_0_SIZE     0x10000  /* 64KB - Config/CSR space */
#define BAR_1_SIZE     0x100000 /* 1MB  - DMA buffer space */
#define BAR_2_SIZE     0x1000   /* 4KB  - MSI/MSG space */

/* CSR register offsets (BAR0) */
#define REG_CTRL       0x00    /* Control register */
#define REG_STATUS     0x04    /* Status register */
#define REG_RING_BASE  0x08    /* DMA ring base address (low) */
#define REG_RING_BASE_HI 0x0C /* DMA ring base address (high) */
#define REG_RING_SIZE  0x10    /* DMA ring size */
#define REG_DEV_CMD    0x14    /* Device command */
#define REG_DEV_STATUS 0x18    /* Device status */
#define REG_INT_ENABLE 0x1C    /* Interrupt enable */
#define REG_INT_STATUS 0x20    /* Interrupt status */
#define REG_FLR        0x24    /* Function level reset */

/* Control register bits */
#define CTRL_START     BIT(0)
#define CTRL_STOP      BIT(1)
#define CTRL_RESET     BIT(2)
#define CTRL_IRQ_EN    BIT(3)

/* Status register bits */
#define STATUS_IDLE        BIT(0)
#define STATUS_RUNNING     BIT(1)
#define STATUS_ERROR       BIT(2)
#define STATUS_IRQ_PENDING BIT(3)
#define STATUS_DMA_DONE    BIT(4)
#define STATUS_DMA_ERR     BIT(5)

/* Device command register bits */
#define DEV_CMD_WRITE_EN   BIT(0)
#define DEV_CMD_READ_EN    BIT(1)
#define DEV_CMD_DMA_EN     BIT(2)

/* IOCTL definitions */
#define XILINX_EP_IOC_MAGIC   'X'
#define XILINX_EP_IOC_START   _IO(XILINX_EP_IOC_MAGIC, 0)
#define XILINX_EP_IOC_STOP    _IO(XILINX_EP_IOC_MAGIC, 1)
#define XILINX_EP_IOC_RESET   _IO(XILINX_EP_IOC_MAGIC, 2)
#define XILINX_EP_IOC_CONFIG  _IOW(XILINX_EP_IOC_MAGIC, 3, struct ep_config)
#define XILINX_EP_IOC_STATUS  _IOR(XILINX_EP_IOC_MAGIC, 4, struct ep_status)
#define XILINX_EP_IOC_DMA_ALLOC   _IOWR(XILINX_EP_IOC_MAGIC, 5, struct ep_dma_desc)
#define XILINX_EP_IOC_DMA_FREE    _IOWR(XILINX_EP_IOC_MAGIC, 6, struct ep_dma_desc)
#define XILINX_EP_IOC_DMA_START   _IOWR(XILINX_EP_IOC_MAGIC, 7, struct ep_dma_desc)
#define XILINX_EP_IOC_DMA_STOP    _IO(XILINX_EP_IOC_MAGIC, 8)

struct ep_config {
    __u32 dma_ring_size;
    __u32 irq_count;
    __u32 reserved[4];
};

struct ep_status {
    __u32 ctrl;
    __u32 status;
    __u32 dma_active;
    __u32 irq_count;
    __u64 rx_bytes;
    __u64 tx_bytes;
};

struct ep_dma_desc {
    __u64 user_addr;   /* user-space buffer address */
    __u64 dev_addr;    /* device-side physical address */
    __u32 size;        /* transfer size */
    __u32 flags;      /* read/write flags */
};

/* Per-device private data */
struct xilinx_ep_dev {
    struct pci_dev *pdev;
    void __iomem *bar0;    /* Config/CSR space */
    void __iomem *bar1;    /* DMA buffer space */

    /* Character device */
    struct cdev cdev;
    dev_t devno;
    struct class *class;
    struct device *device;

    /* DMA */
    struct device_node *dma_node;
    dma_addr_t dma_phys;
    void *dma_virt;
    size_t dma_size;

    /* Interrupt */
    unsigned int irq;
    bool irq_enabled;

    /* Ref count */
    atomic_t refcount;

    /* Spinlock for register access */
    spinlock_t lock;
};

static int max_devices = 4;
module_param(max_devices, int, 0444);
MODULE_PARM_DESC(max_devices, "Maximum number of devices");

static struct class *ep_class;
static int ep_major;
static DECLARE_BITMAP(dev_minor, 256);

/* Utility: read/write CSR registers */
static inline u32 ep_read32(struct xilinx_ep_dev *ep, int offset)
{
    return ioread32(ep->bar0 + offset);
}

static inline void ep_write32(struct xilinx_ep_dev *ep, int offset, u32 val)
{
    iowrite32(val, ep->bar0 + offset);
}

static inline u64 ep_read64(struct xilinx_ep_dev *ep, int offset_lo, int offset_hi)
{
    u32 lo = ep_read32(ep, offset_lo);
    u32 hi = ep_read32(ep, offset_hi);
    return ((u64)hi << 32) | lo;
}

static inline void ep_write64(struct xilinx_ep_dev *ep, int offset_lo, int offset_hi, u64 val)
{
    ep_write32(ep, offset_lo, (u32)(val & 0xFFFFFFFF));
    ep_write32(ep, offset_hi, (u32)(val >> 32));
}

/* Interrupt handler */
static irqreturn_t xilinx_ep_irq_handler(int irq, void *dev_id)
{
    struct xilinx_ep_dev *ep = dev_id;
    u32 status;

    spin_lock(&ep->lock);
    status = ep_read32(ep, REG_INT_STATUS);
    if (status == 0) {
        spin_unlock(&ep->lock);
        return IRQ_NONE;
    }

    /* Clear interrupt status (write 1 to clear) */
    ep_write32(ep, REG_INT_STATUS, status);

    /* Handle DMA completion */
    if (status & STATUS_DMA_DONE) {
        dev_info(&ep->pdev->dev, "DMA transfer completed\n");
    }

    /* Handle error */
    if (status & STATUS_DMA_ERR) {
        dev_err(&ep->pdev->dev, "DMA error occurred\n");
    }

    spin_unlock(&ep->lock);
    return IRQ_HANDLED;
}

/* File operations */
static int ep_open(struct inode *inode, struct file *filp)
{
    struct xilinx_ep_dev *ep = container_of(inode->i_cdev, struct xilinx_ep_dev, cdev);
    filp->private_data = ep;
    atomic_inc(&ep->refcount);
    return 0;
}

static int ep_release(struct inode *inode, struct file *filp)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    atomic_dec(&ep->refcount);
    return 0;
}

static long ep_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    int ret = 0;
    u32 tmp;
    unsigned long flags;

    spin_lock_irqsave(&ep->lock, flags);

    switch (cmd) {
    case XILINX_EP_IOC_START:
        dev_info(&ep->pdev->dev, "Starting EP engine\n");
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp | CTRL_START);
        break;

    case XILINX_EP_IOC_STOP:
        dev_info(&ep->pdev->dev, "Stopping EP engine\n");
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp & ~CTRL_START);
        break;

    case XILINX_EP_IOC_RESET:
        dev_info(&ep->pdev->dev, "Resetting EP\n");
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp | CTRL_RESET);
        udelay(10);
        ep_write32(ep, REG_CTRL, tmp & ~CTRL_RESET);
        break;

    case XILINX_EP_IOC_STATUS: {
        struct ep_status status;
        status.ctrl = ep_read32(ep, REG_CTRL);
        status.status = ep_read32(ep, REG_STATUS);
        status.dma_active = (status.ctrl & CTRL_START) ? 1 : 0;
        status.irq_count = 0;
        status.rx_bytes = 0;
        status.tx_bytes = 0;
        spin_unlock_irqrestore(&ep->lock, flags);
        if (copy_to_user((void __user *)arg, &status, sizeof(status)))
            ret = -EFAULT;
        return ret;
    }

    case XILINX_EP_IOC_CONFIG: {
        struct ep_config cfg;
        if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg))) {
            ret = -EFAULT;
            break;
        }
        ep_write32(ep, REG_RING_SIZE, cfg.dma_ring_size);
        dev_info(&ep->pdev->dev, "Configured with ring size %d\n", cfg.dma_ring_size);
        break;
    }

    case XILINX_EP_IOC_DMA_ALLOC: {
        struct ep_dma_desc desc;
        if (copy_from_user(&desc, (void __user *)arg, sizeof(desc))) {
            ret = -EFAULT;
            break;
        }
        /* For real implementation, allocate physically contiguous DMA buffer */
        desc.dev_addr = ep->dma_phys;
        spin_unlock_irqrestore(&ep->lock, flags);
        if (copy_to_user((void __user *)arg, &desc, sizeof(desc)))
            ret = -EFAULT;
        return ret;
    }

    case XILINX_EP_IOC_DMA_START: {
        struct ep_dma_desc desc;
        if (copy_from_user(&desc, (void __user *)arg, sizeof(desc))) {
            ret = -EFAULT;
            break;
        }
        ep_write64(ep, REG_RING_BASE, REG_RING_BASE_HI, desc.dev_addr);
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp | CTRL_START | CTRL_IRQ_EN);
        dev_info(&ep->pdev->dev, "DMA started: %d bytes from 0x%llx\n",
                 desc.size, (unsigned long long)desc.dev_addr);
        break;
    }

    case XILINX_EP_IOC_DMA_STOP:
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp & ~CTRL_START);
        dev_info(&ep->pdev->dev, "DMA stopped\n");
        break;

    default:
        ret = -ENOTTY;
        break;
    }

    spin_unlock_irqrestore(&ep->lock, flags);
    return ret;
}

static int ep_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    unsigned long pfn;
    size_t size = vma->vm_end - vma->vm_start;

    if (vma->vm_pgoff == 0) {
        /* Map BAR0 - CSR space */
        if (size > BAR_0_SIZE)
            return -EINVAL;
        pfn = pci_resource_start(ep->pdev, 0) >> PAGE_SHIFT;
    } else if (vma->vm_pgoff == 1) {
        /* Map BAR1 - DMA space */
        if (size > BAR_1_SIZE)
            return -EINVAL;
        pfn = pci_resource_start(ep->pdev, 1) >> PAGE_SHIFT;
    } else {
        return -EINVAL;
    }

    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    if (io_remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}

static const struct file_operations ep_fops = {
    .owner          = THIS_MODULE,
    .open           = ep_open,
    .release        = ep_release,
    .unlocked_ioctl = ep_ioctl,
    .mmap           = ep_mmap,
};

/* PCI driver probe */
static int xilinx_ep_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct xilinx_ep_dev *ep;
    int ret;
    int minor;

    ep = kzalloc(sizeof(struct xilinx_ep_dev), GFP_KERNEL);
    if (!ep)
        return -ENOMEM;

    ep->pdev = pdev;
    atomic_set(&ep->refcount, 0);
    spin_lock_init(&ep->lock);

    /* Enable PCI device */
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device: %d\n", ret);
        goto err_free;
    }

    /* Request BAR regions */
    ret = pci_request_regions(pdev, DRV_NAME);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request PCI regions: %d\n", ret);
        goto err_disable;
    }

    /* Enable bus mastering */
    pci_set_master(pdev);

    /* Map BAR0 - CSR/Config space */
    ep->bar0 = pci_iomap(pdev, 0, BAR_0_SIZE);
    if (!ep->bar0) {
        dev_err(&pdev->dev, "Failed to map BAR0\n");
        ret = -ENOMEM;
        goto err_release;
    }

    /* Map BAR1 - DMA space (optional) */
    if (pci_resource_len(pdev, 1) > 0) {
        ep->bar1 = pci_iomap(pdev, 1, BAR_1_SIZE);
        if (!ep->bar1)
            dev_warn(&pdev->dev, "BAR1 not available\n");
    }

    /* Setup DMA mask */
    ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        dev_err(&pdev->dev, "Failed to set DMA mask: %d\n", ret);
        goto err_unmap;
    }

    /* Allocate DMA buffer */
    ep->dma_size = BAR_1_SIZE;
    ep->dma_virt = dma_alloc_coherent(&pdev->dev, ep->dma_size,
                                      &ep->dma_phys, GFP_KERNEL);
    if (!ep->dma_virt) {
        dev_warn(&pdev->dev, "Failed to allocate DMA buffer, using fallback\n");
        ep->dma_virt = kzalloc(ep->dma_size, GFP_KERNEL);
        ep->dma_phys = virt_to_phys(ep->dma_virt);
    }

    /* Request IRQ */
    ep->irq = pdev->irq;
    ret = request_irq(ep->irq, xilinx_ep_irq_handler,
                     IRQF_SHARED, DRV_NAME, ep);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ %d: %d\n", ep->irq, ret);
        goto err_dma;
    }

    /* Clear and enable interrupts */
    ep_write32(ep, REG_INT_STATUS, 0xFFFFFFFF);
    ep_write32(ep, REG_INT_ENABLE, 0x07);

    /* Allocate character device number */
    minor = find_first_zero_bit(dev_minor, max_devices);
    if (minor >= max_devices) {
        ret = -ENODEV;
        goto err_irq;
    }
    set_bit(minor, dev_minor);

    ep->devno = MKDEV(ep_major, minor);
    cdev_init(&ep->cdev, &ep_fops);
    ep->cdev.owner = THIS_MODULE;

    ret = cdev_add(&ep->cdev, ep->devno, 1);
    if (ret) {
        dev_err(&pdev->dev, "Failed to add cdev: %d\n", ret);
        goto err_bit;
    }

    /* Create device node */
    ep->device = device_create(ep_class, &pdev->dev, ep->devno,
                               NULL, "xilinx_ep%d", minor);
    if (IS_ERR(ep->device)) {
        ret = PTR_ERR(ep->device);
        goto err_cdev;
    }

    pci_set_drvdata(pdev, ep);
    dev_info(&pdev->dev, "Xilinx PCIe EP probed: bar0=0x%p, irq=%d\n",
             ep->bar0, ep->irq);

    return 0;

err_cdev:
    cdev_del(&ep->cdev);
err_bit:
    clear_bit(minor, dev_minor);
err_irq:
    free_irq(ep->irq, ep);
err_dma:
    if (ep->dma_virt) {
        if (dma_free_coherent(&pdev->dev, ep->dma_size, ep->dma_virt, ep->dma_phys))
            kfree(ep->dma_virt);
    }
err_unmap:
    pci_iounmap(pdev, ep->bar0);
    if (ep->bar1)
        pci_iounmap(pdev, ep->bar1);
err_release:
    pci_release_regions(pdev);
err_disable:
    pci_disable_device(pdev);
err_free:
    kfree(ep);
    return ret;
}

static void xilinx_ep_remove(struct pci_dev *pdev)
{
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);
    int minor = MINOR(ep->devno);

    device_destroy(ep_class, ep->devno);
    cdev_del(&ep->cdev);
    clear_bit(minor, dev_minor);

    free_irq(ep->irq, ep);

    if (ep->dma_virt) {
        if (dma_free_coherent(&pdev->dev, ep->dma_size, ep->dma_virt, ep->dma_phys))
            kfree(ep->dma_virt);
    }

    pci_iounmap(pdev, ep->bar0);
    if (ep->bar1)
        pci_iounmap(pdev, ep->bar1);

    pci_release_regions(pdev);
    pci_disable_device(pdev);

    kfree(ep);
    dev_info(&pdev->dev, "Xilinx PCIe EP removed\n");
}

static void xilinx_ep_shutdown(struct pci_dev *pdev)
{
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);
    u32 tmp;

    /* Stop DMA engine */
    spin_lock(&ep->lock);
    tmp = ep_read32(ep, REG_CTRL);
    ep_write32(ep, REG_CTRL, tmp & ~CTRL_START);
    spin_unlock(&ep->lock);

    /* Disable interrupts */
    ep_write32(ep, REG_INT_ENABLE, 0);

    synchronize_irq(ep->irq);
}

/* PCI device ID table - add your specific Device ID here */
static const struct pci_device_id xilinx_ep_ids[] = {
    { PCI_DEVICE(EP_VENDOR_ID, EP_DEVICE_ID) },
    { 0, }
};
MODULE_DEVICE_TABLE(pci, xilinx_ep_ids);

static struct pci_driver xilinx_ep_driver = {
    .name     = DRV_NAME,
    .id_table = xilinx_ep_ids,
    .probe    = xilinx_ep_probe,
    .remove   = xilinx_ep_remove,
    .shutdown = xilinx_ep_shutdown,
};

static int __init xilinx_ep_init(void)
{
    int ret;
    dev_t devno;

    /* Allocate character device major number */
    ret = alloc_chrdev_region(&devno, 0, max_devices, DRV_NAME);
    if (ret) {
        pr_err("Failed to allocate chrdev region: %d\n", ret);
        return ret;
    }
    ep_major = MAJOR(devno);

    /* Create device class */
    ep_class = class_create("xilinx_ep");
    if (IS_ERR(ep_class)) {
        pr_err("Failed to create class: %ld\n", PTR_ERR(ep_class));
        ret = PTR_ERR(ep_class);
        goto err_unreg;
    }

    /* Register PCI driver */
    ret = pci_register_driver(&xilinx_ep_driver);
    if (ret) {
        pr_err("Failed to register PCI driver: %d\n", ret);
        goto err_class;
    }

    pr_info("Xilinx PCIe EP driver loaded (major=%d)\n", ep_major);
    return 0;

err_class:
    class_destroy(ep_class);
err_unreg:
    unregister_chrdev_region(devno, max_devices);
    return ret;
}

static void __exit xilinx_ep_exit(void)
{
    pci_unregister_driver(&xilinx_ep_driver);
    class_destroy(ep_class);
    unregister_chrdev_region(MKDEV(ep_major, 0), max_devices);
    pr_info("Xilinx PCIe EP driver unloaded\n");
}

module_init(xilinx_ep_init);
module_exit(xilinx_ep_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OpenClaw Agent");
MODULE_DESCRIPTION("Xilinx Spartan-6 PCIe Endpoint Driver");
MODULE_VERSION(DRV_VERSION);