# 第二章：驱动整体架构

## 2.1 驱动在系统中的位置

```
┌─────────────────────────────────────────────────────────────┐
│                      Userspace                              │
│                                                             │
│   Application    open() read() ioctl() mmap()              │
│         │              │              │                      │
│         └──────────────┴──────────────┘                      │
│                          │                                   │
│                     /dev/xilinx_ep0                          │
└──────────────────────────│───────────────────────────────────┘
                           │ system call
┌──────────────────────────│───────────────────────────────────┐
│                      Kernel                                  │
│                                                             │
│              ┌─────────────────────┐                        │
│              │  Character Device   │                        │
│              │  (cdev, file_ops)   │                        │
│              └─────────┬───────────┘                        │
│                        │                                     │
│              ┌─────────▼───────────┐                        │
│              │   EP Driver Core    │                        │
│              │  (probe, remove)    │                        │
│              └─────────┬───────────┘                        │
│                        │                                     │
│    ┌───────────────────┼───────────────────┐                │
│    │                   │                   │                 │
│    ▼                   ▼                   ▼                 │
│ ┌──────┐          ┌──────────┐       ┌────────┐             │
│ │ BAR  │          │    IRQ   │       │  DMA   │             │
│ │Mapping│          │ Handler  │       │ Engine │             │
│ └──────┘          └──────────┘       └────────┘             │
│    │                   │                   │                 │
└────│───────────────────│───────────────────│─────────────────┘
     │                   │                   │
     ▼                   ▼                   ▼
┌─────────────────────────────────────────────────────────────┐
│                    PCIe Bus                                 │
│                                                             │
│              Xilinx Spartan-6 FPGA                         │
│              (PCIe Endpoint Block)                         │
└─────────────────────────────────────────────────────────────┘
```

## 2.2 模块结构

驱动的核心文件 `xilinx_pcie_ep.c` 包含以下部分：

```c
// 1. 头文件和宏定义
#define DRV_NAME    "xilinx_pcie_ep"
#define EP_VENDOR_ID   0x10EE
#define EP_DEVICE_ID   0x0007

// BAR 大小定义
#define BAR_0_SIZE     0x10000  // 64KB - CSR 空间
#define BAR_1_SIZE     0x100000 // 1MB  - DMA 空间

// 寄存器偏移
#define REG_CTRL       0x00
#define REG_STATUS     0x04
#define ...

// 2. 数据结构定义
struct xilinx_ep_dev {
    struct pci_dev *pdev;
    void __iomem *bar0;    // CSR 空间映射
    void __iomem *bar1;    // DMA 空间映射
    struct cdev cdev;      // 字符设备
    dev_t devno;           // 设备号
    unsigned int irq;      // 中断号
    void *dma_virt;        // DMA 缓冲区虚拟地址
    dma_addr_t dma_phys;   // DMA 缓冲区物理地址
    spinlock_t lock;       // 自旋锁
    atomic_t refcount;     // 引用计数
};

// 3. 文件操作接口
static int ep_open(struct inode *inode, struct file *filp);
static int ep_release(struct inode *inode, struct file *filp);
static long ep_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static int ep_mmap(struct file *filp, struct vm_area_struct *vma);

// 4. PCI 驱动接口
static int xilinx_ep_probe(struct pci_dev *pdev, const struct pci_device_id *id);
static void xilinx_ep_remove(struct pci_dev *pdev);
static void xilinx_ep_shutdown(struct pci_dev *pdev);

// 5. 中断处理
static irqreturn_t xilinx_ep_irq_handler(int irq, void *dev_id);

// 6. 辅助函数
static inline u32 ep_read32(struct xilinx_ep_dev *ep, int offset);
static inline void ep_write32(struct xilinx_ep_dev *ep, int offset, u32 val);
```

## 2.3 数据流

### 写入操作（CPU → FPGA）

```
应用层 write(data)
    │
    ▼
驱动: copy_from_user() 把用户数据拷贝到 kernel
    │
    ▼
ep_write32(REG_CTRL, val)  // 写控制寄存器
    │
    ▼
DMA 描述符写入 BAR1
    │
    ▼
启动 DMA: iowrite32(bar0 + REG_CTRL, CTRL_START)
    │
    ▼
FPGA 的 DMA 引擎从 PC 内存读取数据，写入 FPGA 或外部设备
```

### 读取操作（FPGA → CPU）

```
FPGA DMA 引擎把数据写入 PC DDR
    │
    ▼
FPGA 发送 MSI 中断
    │
    ▼
Linux 中断处理: xilinx_ep_irq_handler()
    │
    ▼
唤醒等待的进程 或 调用回调
    │
    ▼
应用层 read() 返回数据
```

## 2.4 关键设计决策

### 为什么要用字符设备而不是直接映射？

```c
// 字符设备接口
ioctl(fd, XILINX_EP_IOC_START);  // 有类型安全，清晰
ioctl(fd, XILINX_EP_IOC_DMA_START, &desc);

// 直接映射的问题：
// 1. 应用层直接访问硬件寄存器可能崩溃
// 2. 无法做权限检查
// 3. 无法支持原子操作
```

### 为什么需要自旋锁？

```c
spin_lock(&ep->lock);
// 操作共享寄存器
ep_write32(ep, REG_CTRL, new_val);
spin_unlock(&ep->lock);

// 自旋锁用于多核 CPU 并发访问
// 中断上下文也可以用（sleep 不允许）
```

### DMA 缓冲区为什么需要物理连续？

```
FPGA DMA 引擎访问 PC 内存时：
- 使用物理地址（bus address）
- 无法使用虚拟地址
- 因此需要连续物理页面

普通 kmalloc() 可能产生碎片：
┌────────┬────┬────┬────────┬──┐  ← 不连续
│ Buffer │    │    │ Buffer │  │
└────────┴────┴────┴────────┴──┘

DMA 一致性 buffer：
┌────────────────────────────────┐ ← 物理连续
│         Buffer                 │
└────────────────────────────────┘
```

## 2.5 初始化流程

```
模块加载 (insmod)
    │
    ▼
xilinx_ep_init()
    │
    ├── alloc_chrdev_region()  // 分配主设备号
    │
    ├── class_create()         // 创建 /sys/class/xilinx_ep
    │
    └── pci_register_driver()  // 注册 PCI 驱动
            │
            ▼
    系统启动 / 设备插入
            │
            ▼
    xilinx_ep_probe(pdev)
            │
            ├── pci_enable_device()
            │
            ├── pci_request_regions()
            │
            ├── pci_set_master()      // 使能 Bus Master (DMA 需要)
            │
            ├── pci_iomap(bar0)       // 映射 CSR 空间
            │
            ├── dma_alloc_coherent()  // 分配 DMA 缓冲区
            │
            ├── request_irq()         // 注册中断处理
            │
            ├── cdev_add()           // 添加字符设备
            │
            └── device_create()      // 创建设备节点
                    │
                    ▼
            /dev/xilinx_ep0 出现
```

## 2.6 模块卸载流程

```
rmmod xilinx_pcie_ep
    │
    ▼
xilinx_ep_exit()
    │
    └── pci_unregister_driver()
            │
            ▼
    对每个注册的设备调用 xilinx_ep_remove()
            │
            ├── device_destroy()     // 删除设备节点
            │
            ├── cdev_del()          // 删除字符设备
            │
            ├── free_irq()          // 释放中断
            │
            ├── dma_free_coherent() // 释放 DMA 缓冲区
            │
            ├── pci_iounmap()       // 取消 BAR 映射
            │
            ├── pci_release_regions() // 释放 BAR 区域
            │
            └── pci_disable_device()
```

## 2.7 各组件职责

| 组件 | 职责 | 对应代码 |
|------|------|----------|
| PCI Probe | 发现设备、映射 BAR、分配资源 | `xilinx_ep_probe()` |
| BAR Mapping | 把 PCI 地址空间映射到虚拟地址 | `pci_iomap()` |
| IRQ Handler | 响应硬件中断，清除状态，唤醒进程 | `xilinx_ep_irq_handler()` |
| DMA Engine | 管理 DMA 缓冲区，启动/停止传输 | `ep_ioctl(DMA_START/STOP)` |
| Char Device | 提供用户空间接口（open/ioctl/mmap） | `ep_fops` |
| Spinlock | 保护临界区，防止并发访问冲突 | `ep->lock` |

## 2.8 下一步

下一章：[第三章：PCI probe 和 BAR 映射](./03_pci_probe.md)