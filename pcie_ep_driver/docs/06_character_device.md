# 第六章：字符设备与 ioctl API

## 6.1 Linux 设备模型回顾

```
应用层
  │
  │ open("/dev/xilinx_ep0")
  │ read() write() ioctl() mmap()
  ▼
┌─────────────────────────────────────┐
│         Kernel                     │
│                                     │
│  ┌───────────────────────────────┐ │
│  │  Character Device             │ │
│  │  /dev/xilinx_ep0              │ │
│  │  (cdev)                       │ │
│  └───────────────┬───────────────┘ │
│                  │                   │
│  ┌───────────────▼───────────────┐ │
│  │  file_operations              │ │
│  │  .open = ep_open              │ │
│  │  .read = ep_read              │ │
│  │  .write = ep_write            │ │
│  │  .unlocked_ioctl = ep_ioctl   │ │
│  │  .mmap = ep_mmap              │ │
│  └───────────────┬───────────────┘ │
│                  │                   │
│  ┌───────────────▼───────────────┐ │
│  │  Driver Private Data          │ │
│  │  (struct xilinx_ep_dev)       │ │
│  └───────────────────────────────┘ │
└─────────────────────────────────────┘
        │ PCI 总线
        ▼
    FPGA Endpoint
```

## 6.2 字符设备注册流程

```c
// 1. 模块初始化时分配设备号
static int xilinx_ep_init(void)
{
    dev_t devno;
    int ret;

    // alloc_chrdev_region(&devno, first_minor, count, name)
    // firstminor: 起始次设备号
    // count: 设备数量
    ret = alloc_chrdev_region(&devno, 0, max_devices, DRV_NAME);
    if (ret) {
        pr_err("Failed to allocate chrdev region\n");
        return ret;
    }
    ep_major = MAJOR(devno);  // 保存主设备号

    // 创建 /sys/class/xxx
    ep_class = class_create("xilinx_ep");

    // 注册 PCI 驱动（probe 中会创建设备节点）
    pci_register_driver(&xilinx_ep_driver);

    return 0;
}

// 2. probe 中添加字符设备
static int xilinx_ep_probe(struct pci_dev *pdev, ...)
{
    struct xilinx_ep_dev *ep;
    int minor;

    // 分配次设备号
    minor = find_first_zero_bit(dev_minor, max_devices);
    set_bit(minor, dev_minor);
    ep->devno = MKDEV(ep_major, minor);

    // 初始化 cdev
    cdev_init(&ep->cdev, &ep_fops);
    ep->cdev.owner = THIS_MODULE;

    // 添加到系统
    ret = cdev_add(&ep->cdev, ep->devno, 1);
    if (ret)
        goto err_bit;

    // 创建设备节点 /dev/xilinx_ep0
    ep->device = device_create(ep_class, &pdev->dev,
                               ep->devno, NULL,
                               "xilinx_ep%d", minor);
    if (IS_ERR(ep->device)) {
        ret = PTR_ERR(ep->device);
        goto err_cdev;
    }

    return 0;
}
```

## 6.3 文件操作实现

```c
// ep_open - 设备打开
static int ep_open(struct inode *inode, struct file *filp)
{
    struct xilinx_ep_dev *ep;

    // 从 inode 获取私有数据
    // container_of() 是宏，用于从成员指针反推结构体指针
    ep = container_of(inode->i_cdev, struct xilinx_ep_dev, cdev);
    filp->private_data = ep;

    // 增加引用计数
    atomic_inc(&ep->refcount);

    return 0;
}

// ep_release - 设备关闭
static int ep_release(struct inode *inode, struct file *filp)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    atomic_dec(&ep->refcount);
    return 0;
}
```

## 6.4 ioctl 设计

ioctl 是最灵活的系统调用，用于设备特定的控制命令：

```c
// 定义 ioctl 编号
// 格式: _IOR/MAGIC/SEQ/TYPE
//  I = Ioctl, R/W = Read/Write, O = Direction

#define XILINX_EP_IOC_MAGIC   'X'

// 无参数命令
#define XILINX_EP_IOC_START   _IO(XILINX_EP_IOC_MAGIC, 0)
#define XILINX_EP_IOC_STOP    _IO(XILINX_EP_IOC_MAGIC, 1)
#define XILINX_EP_IOC_RESET   _IO(XILINX_EP_IOC_MAGIC, 2)

// 带输出参数的查询命令
#define XILINX_EP_IOC_STATUS  _IOR(XILINX_EP_IOC_MAGIC, 4, struct ep_status)

// 带输入参数的配置命令
#define XILINX_EP_IOC_CONFIG  _IOW(XILINX_EP_IOC_MAGIC, 3, struct ep_config)

// 带输入输出参数的 DMA 命令
#define XILINX_EP_IOC_DMA_START  _IOWR(XILINX_EP_IOC_MAGIC, 7, struct ep_dma_desc)
```

## 6.5 ioctl 实现

```c
static long ep_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    int ret = 0;
    u32 tmp;
    unsigned long flags;

    // 参数验证（可选但推荐）
    if (_IOC_TYPE(cmd) != XILINX_EP_IOC_MAGIC) {
        return -ENOTTY;
    }
    if (_IOC_NR(cmd) > XILINX_EP_IOC_MAXNR) {
        return -ENOTTY;
    }

    // 锁保护（防止并发访问寄存器）
    spin_lock_irqsave(&ep->lock, flags);

    switch (cmd) {
    case XILINX_EP_IOC_START:
        dev_info(&ep->pdev->dev, "Starting EP\n");
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp | CTRL_START);
        break;

    case XILINX_EP_IOC_STOP:
        dev_info(&ep->pdev->dev, "Stopping EP\n");
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp & ~CTRL_START);
        break;

    case XILINX_EP_IOC_RESET:
        dev_info(&ep->pdev->dev, "Resetting EP\n");
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp | CTRL_RESET);
        udelay(10);  // 复位脉冲宽度
        ep_write32(ep, REG_CTRL, tmp & ~CTRL_RESET);
        break;

    case XILINX_EP_IOC_STATUS: {
        struct ep_status status;
        // 先解锁再 copy_to_user（睡眠操作不能在锁内）
        spin_unlock_irqrestore(&ep->lock, flags);
        status.ctrl = ep_read32(ep, REG_CTRL);
        status.status = ep_read32(ep, REG_STATUS);
        status.dma_active = (status.ctrl & CTRL_START) ? 1 : 0;
        if (copy_to_user((void __user *)arg, &status, sizeof(status)))
            ret = -EFAULT;
        return ret;
    }

    case XILINX_EP_IOC_CONFIG: {
        struct ep_config cfg;
        // copy_from_user 必须在锁外
        spin_unlock_irqrestore(&ep->lock, flags);
        if (copy_from_user(&cfg, (void __user *)arg, sizeof(cfg))) {
            return -EFAULT;
        }
        spin_lock_irqsave(&ep->lock, flags);
        ep_write32(ep, REG_RING_SIZE, cfg.dma_ring_size);
        dev_info(&ep->pdev->dev, "Ring size: %d\n", cfg.dma_ring_size);
        break;
    }

    case XILINX_EP_IOC_DMA_START: {
        struct ep_dma_desc desc;
        spin_unlock_irqrestore(&ep->lock, flags);
        if (copy_from_user(&desc, (void __user *)arg, sizeof(desc))) {
            return -EFAULT;
        }
        spin_lock_irqsave(&ep->lock, flags);
        ep_write64(ep, REG_RING_BASE, REG_RING_BASE_HI, desc.dev_addr);
        tmp = ep_read32(ep, REG_CTRL);
        ep_write32(ep, REG_CTRL, tmp | CTRL_START | CTRL_IRQ_EN);
        dev_info(&ep->pdev->dev, "DMA: %d bytes from 0x%llx\n",
                 desc.size, (unsigned long long)desc.dev_addr);
        break;
    }

    default:
        ret = -ENOTTY;  // 无效命令
        break;
    }

    spin_unlock_irqrestore(&ep->lock, flags);
    return ret;
}
```

## 6.6 数据结构定义

```c
// 配置结构
struct ep_config {
    __u32 dma_ring_size;
    __u32 irq_count;
    __u32 reserved[4];
};

// 状态结构
struct ep_status {
    __u32 ctrl;
    __u32 status;
    __u32 dma_active;
    __u32 irq_count;
    __u64 rx_bytes;
    __u64 tx_bytes;
};

// DMA 描述符
struct ep_dma_desc {
    __u64 user_addr;   // 用户空间缓冲区虚拟地址
    __u64 dev_addr;    // 设备 DMA 地址（驱动填充）
    __u32 size;        // 传输大小
    __u32 flags;       // 方向：0=写，1=读
};
```

## 6.7 read/write 实现（可选）

```c
static ssize_t ep_read(struct file *filp, char __user *buf,
                       size_t count, loff_t *ppos)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    ssize_t ret = 0;
    unsigned long flags;

    spin_lock_irqsave(&ep->lock, flags);

    // 检查有数据可读
    if (ep->rx_count == 0) {
        // 没有数据，等待或返回 -EAGAIN
        spin_unlock_irqrestore(&ep->lock, flags);
        return -EAGAIN;
    }

    // 复制到用户空间
    if (count > ep->rx_count)
        count = ep->rx_count;

    if (copy_to_user(buf, ep->rx_buffer, count)) {
        ret = -EFAULT;
    } else {
        ret = count;
        ep->rx_count -= count;
    }

    spin_unlock_irqrestore(&ep->lock, flags);
    return ret;
}

static ssize_t ep_write(struct file *filp, const char __user *buf,
                        size_t count, loff_t *ppos)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    ssize_t ret = 0;
    unsigned long flags;

    spin_lock_irqsave(&ep->lock, flags);

    // 复制用户数据
    if (count > ep->tx_buffer_size) {
        spin_unlock_irqrestore(&ep->lock, flags);
        return -EINVAL;
    }

    if (copy_from_user(ep->tx_buffer, buf, count)) {
        ret = -EFAULT;
    } else {
        ret = count;
        // 启动 DMA 发送
        ep->tx_pending = count;
        // 设置 DMA 寄存器...
    }

    spin_unlock_irqrestore(&ep->lock, flags);
    return ret;
}
```

## 6.8 poll 实现（支持 select/poll）

```c
static unsigned int ep_poll(struct file *filp, poll_table *wait)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    unsigned int mask = 0;
    unsigned long flags;

    // 注册等待队列（让 select 能感知）
    poll_wait(filp, &ep->wait_queue, wait);

    spin_lock_irqsave(&ep->lock, flags);

    // 有数据可读？
    if (ep->rx_count > 0)
        mask |= POLLIN | POLLRDNORM;

    // 可写？
    if (ep->tx_pending == 0)
        mask |= POLLOUT | POLLWRNORM;

    spin_unlock_irqrestore(&ep->lock, flags);

    return mask;
}
```

## 6.9 设备节点权限

udev 规则控制设备节点权限：

```
# /etc/udev/rules.d/99-xilinx-pcie-ep.rules

# 创建设备节点，0666 所有用户可读写
SUBSYSTEM=="xilinx_ep", MODE="0666", GROUP="users", \
    SYMLINK+="xilinx_ep%n"
```

## 6.10 用户空间代码示例

```c
#include "xilinx_pcie_ep.h"
#include <fcntl.h>
#include <unistd.h>

int main()
{
    int fd = open("/dev/xilinx_ep0", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    // 启动设备
    ioctl(fd, XILINX_EP_IOC_START);

    // 配置 DMA
    struct ep_config cfg = { .dma_ring_size = 256, .irq_count = 1 };
    ioctl(fd, XILINX_EP_IOC_CONFIG, &cfg);

    // 启动 DMA 传输
    struct ep_dma_desc desc = {
        .user_addr = (uint64_t)user_buffer,
        .dev_addr = 0,  // 驱动填充
        .size = 1024,
        .flags = 0
    };
    ioctl(fd, XILINX_EP_IOC_DMA_START, &desc);

    // 查询状态
    struct ep_status status;
    ioctl(fd, XILINX_EP_IOC_STATUS, &status);
    printf("Status: 0x%08x\n", status.status);

    close(fd);
    return 0;
}
```

## 6.11 下一步

下一章：[第七章：内存映射与 DMA 缓冲区管理](./07_memory_mapping.md)