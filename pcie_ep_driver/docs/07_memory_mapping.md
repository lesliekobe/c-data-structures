# 第七章：内存映射与 DMA 缓冲区管理

## 7.1 虚拟地址空间布局

```
64-bit Linux 虚拟地址空间:

0x0000_0000_0000_0000  ──── 用户空间 (48-bit)
                        │
0x0000_7FFF_FFFF_FFFF  ────┤
                        │
0xFFFF_8000_0000_0000  ──── kernel space (canonical)
                        │
0xFFFF_8800_0000_0000  ──── 直接映射区 (物理内存直接映射)
                        │
0xFFFF_C000_0000_0000  ──── vmalloc/ioremap 区域
                        │
0xFFFF_FFFF_FFFF_FFFF  ────┘
```

**关键区域：**
- `0xFFFF_8800_0000_0000` 以下：直接映射区，线性映射物理内存 `phys + 0xFFFF_8800_0000_0000`
- `vmalloc` 区域：非连续物理内存映射（用于 large buffer）
- `ioremap` 区域：设备寄存器映射

## 7.2 BAR 映射到用户空间

用户程序可以通过 mmap 直接访问 PCI BAR：

```c
// mmap 实现
static int ep_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    unsigned long pfn;
    size_t size = vma->vm_end - vma->vm_start;

    if (vma->vm_pgoff == 0) {
        // BAR0: CSR 空间
        if (size > BAR_0_SIZE)
            return -EINVAL;
        pfn = pci_resource_start(ep->pdev, 0) >> PAGE_SHIFT;
    } else if (vma->vm_pgoff == 1) {
        // BAR1: DMA 空间
        if (size > BAR_1_SIZE)
            return -EINVAL;
        pfn = pci_resource_start(ep->pdev, 1) >> PAGE_SHIFT;
    } else {
        return -EINVAL;
    }

    // 设置非缓存访问（设备寄存器必须）
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    // 物理地址 → 用户虚拟地址映射
    // io_remap_pfn_range 不修改页表，只是创建映射
    if (io_remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot))
        return -EAGAIN;

    return 0;
}
```

## 7.3 mmap 使用示例

```c
// 用户空间代码
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>

#define BAR0_SIZE  0x10000   // 64KB
#define BAR1_SIZE   0x100000  // 1MB

int main()
{
    int fd = open("/dev/xilinx_ep0", O_RDWR);

    // 映射 BAR0（CSR 空间）
    volatile uint32_t *bar0 = mmap(NULL, BAR0_SIZE,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED, fd, 0);
    if (bar0 == MAP_FAILED) {
        perror("mmap bar0");
        return 1;
    }

    // 映射 BAR1（DMA 空间）
    void *bar1 = mmap(NULL, BAR1_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, PAGE_SIZE);  // 页偏移 1 = BAR1
    if (bar1 == MAP_FAILED) {
        perror("mmap bar1");
        return 1;
    }

    // 直接访问硬件寄存器
    uint32_t ctrl = bar0[0];      // 读控制寄存器
    bar0[1] = ctrl | 0x01;        // 写控制寄存器

    // 填充 DMA 缓冲区
    uint32_t *dma_buf = (uint32_t *)bar1;
    for (int i = 0; i < 1024; i++) {
        dma_buf[i] = i;
    }

    munmap(bar0, BAR0_SIZE);
    munmap(bar1, BAR1_SIZE);
    close(fd);

    return 0;
}
```

## 7.4 DMA 缓冲区分配

```c
// 分配 DMA 一致性缓冲区（物理连续）
ep->dma_size = 16 * PAGE_SIZE;  // 至少 16 页

ep->dma_virt = dma_alloc_coherent(
    &pdev->dev,              // 设备（用于管理物理地址）
    ep->dma_size,            // 缓冲区大小
    &ep->dma_phys,           // 返回物理地址（bus address）
    GFP_KERNEL               // 分配标志
);

if (!ep->dma_virt) {
    dev_err(&pdev->dev, "DMA allocation failed\n");
    return -ENOMEM;
}

// 释放
void xilinx_ep_remove(struct pci_dev *pdev)
{
    // ...
    dma_free_coherent(&pdev->dev, ep->dma_size,
                      ep->dma_virt, ep->dma_phys);
    // ...
}
```

## 7.5 一致性 vs 流式映射

### 一致性缓冲区（Coherent）

```c
// 分配时指定一致标志，CPU 和 DMA 看到同样的数据
void *buf = dma_alloc_coherent(dev, size, &phys, GFP_KERNEL);

// CPU 写入后，DMA 直接可以读取（硬件保证一致性）
memcpy(buf, src, size);

// DMA 完成后，CPU 直接可以读取（无需额外同步）
memcpy(dst, buf, size);
```

### 流式映射（Streaming）

```c
// 用于临时缓冲区，避免长时间占用 DMA 引擎
void *tmp = kmalloc(size, GFP_KERNEL);

// 映射：建立 DMA 可访问的地址翻译
dma_addr_t phys = dma_map_single(dev, tmp, size, DMA_TO_DEVICE);
// 此时 CPU 应该不要再访问 tmp（除非先 unmap）

// DMA 传输（使用 phys）
// ...

// 解除映射：确保 cache 同步
dma_unmap_single(dev, phys, size, DMA_TO_DEVICE);

kfree(tmp);
```

## 7.6 cache 一致性问题

```
场景：CPU 写入数据，然后 DMA 传输到 FPGA

问题：数据可能还在 CPU cache 里，没写入 RAM

        CPU                              DMA 引擎
         │                                  │
         ▼                                  ▼
    ┌─────────┐                        ┌─────────┐
    │  L1/L2  │  writeback?            │  RAM    │
    │  Cache  │ ────────► ──────────► │         │
    └─────────┘                        └─────────┘
         │                                  ▲
         │                                  │
         └───────── copy_to_user ────────────┘
                     (同步点)
```

```c
// 流式映射时，DMA_TO_DEVICE 意味着"从 CPU 角度是输出"
// 内核会自动调用 flush_cpu_dcache() 确保数据到 RAM

// 但如果你自己管理 cache，需要：
dma_sync_single_for_device(dev, phys, size, DMA_TO_DEVICE);
// 之后 CPU 不能访问，直到：
dma_sync_single_for_cpu(dev, phys, size, DMA_FROM_DEVICE);
```

## 7.7 用户空间 DMA 缓冲区

用户程序需要 DMA 地址时，需要特殊处理：

```c
// 方法 1: mmap DMA 缓冲区
static int ep_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    unsigned long pfn;

    // 只允许映射 DMA 缓冲区
    if (vma->vm_pgoff == 2) {  // 假设偏移 2 = DMA 缓冲区
        size_t npages = ep->dma_size >> PAGE_SHIFT;
        pfn = ep->dma_phys >> PAGE_SHIFT;
        return io_remap_pfn_range(vma, vma->vm_start, pfn,
                                  ep->dma_size,
                                  pgprot_noncached(vma->vm_page_prot));
    }

    return -EINVAL;
}

// 方法 2: ioctl 返回物理地址（谨慎使用）
case XILINX_EP_IOC_DMA_GET_PHYS: {
    struct dma_phys_info info;
    info.phys_addr = ep->dma_phys;
    info.size = ep->dma_size;
    if (copy_to_user((void __user *)arg, &info, sizeof(info)))
        return -EFAULT;
    break;
}
```

## 7.8 物理地址 vs Bus 地址

```
CPU 视角的物理地址：
  - 在内核态，直接用于页表映射
  - 用户空间通常看不到

Bus 地址（DMA 地址）：
  - PCIe 设备看到的地址
  - 可能与 CPU 物理地址不同（如果有 IOMMU）

dma_alloc_coherent 返回的就是 bus 地址：

  CPU 物理地址 ──────► 0x1_0000_0000
                         │
  Bus 地址 (EP sees) ──► 0x0_0000_0000（通过 BAR 窗口）
```

## 7.9 64-bit DMA 地址

```c
// 检查设备是否支持 64-bit 寻址
if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64)) == 0) {
    dev_info(&pdev->dev, "Using 64-bit DMA addresses\n");
} else if (dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)) == 0) {
    dev_info(&pdev->dev, "Using 32-bit DMA addresses\n");
} else {
    dev_err(&pdev->dev, "No suitable DMA addressing mode\n");
    return -ENODEV;
}

// 分配大缓冲区（可能跨多个 4GB 边界）
size_t big_size = 16 * 1024 * 1024;  // 16MB
void *big_buf = dma_alloc_coherent(&pdev->dev, big_size,
                                   &big_phys, GFP_KERNEL);
```

## 7.10 mmap 完整实现

```c
static int ep_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    unsigned long pfn;
    size_t size = vma->vm_end - vma->vm_start;

    switch (vma->vm_pgoff) {
    case 0:
        // BAR0: CSR 空间
        if (size > BAR_0_SIZE) {
            dev_warn(&ep->pdev->dev, "BAR0 mmap size exceeded\n");
            return -EINVAL;
        }
        pfn = pci_resource_start(ep->pdev, 0) >> PAGE_SHIFT;
        break;

    case 1:
        // BAR1: 外部设备内存（如果有）
        if (!ep->bar1 || size > BAR_1_SIZE)
            return -EINVAL;
        pfn = pci_resource_start(ep->pdev, 1) >> PAGE_SHIFT;
        break;

    case 2:
        // DMA 缓冲区
        if (size > ep->dma_size)
            return -EINVAL;
        pfn = ep->dma_phys >> PAGE_SHIFT;
        break;

    default:
        return -EINVAL;
    }

    // 非缓存映射（设备寄存器必须）
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    // IO 映射：创建页表项，允许用户直接访问设备内存
    if (io_remap_pfn_range(vma, vma->vm_start, pfn, size, vma->vm_page_prot)) {
        dev_err(&ep->pdev->dev, "io_remap_pfn_range failed\n");
        return -EAGAIN;
    }

    vma->vm_ops = &ep_vm_ops;  // 可选：实现 fault handler
    return 0;
}

// vm_operations_struct 可选，用于延迟映射（按需分配）
static const struct vm_operations_struct ep_vm_ops = {
    .fault = ep_vm_fault,
};

static vm_fault_t ep_vm_fault(struct vm_fault *vmf)
{
    // 按需从设备读取数据填充页面
    // 大多数驱动不需要实现这个
    return VM_FAULT_SIGBUS;
}
```

## 7.11 地址转换总结

| 地址类型 | 来源 | 用途 |
|---------|------|------|
| 虚拟地址 | kmalloc/vmalloc/mmap | CPU 访问 |
| 物理地址 | virt_to_phys() | 调试/诊断 |
| Bus 地址 | dma_alloc_coherent() | DMA 引擎 |
| 用户虚拟地址 | mmap() | 用户空间访问设备 |

## 7.12 下一步

下一章：[第八章：调试技巧](./08_debugging.md)