# 第五章：DMA 引擎

## 5.1 为什么需要 DMA

```
CPU 参与的数据传输（慢）:
CPU ───copy───▶ 内存 ───Pio───▶ PCIe 设备
                  ↑
                  └── CPU 需要等待完成

DMA 传输（快）:
CPU ───启动───▶ DMA 控制器
                    │
                    └── 直接访问内存，无需 CPU 介入
```

**DMA 优势：**
- 释放 CPU（传输期间 CPU 可以做其他事）
- 减少 CPU 参与，降低延迟
- 支持大块数据传输
- 支持分散-聚集（Scatter-Gather）操作

## 5.2 DMA 架构

```
┌─────────────────────────────────────────────────────────┐
│                     PC System                           │
│                                                         │
│     ┌─────────┐         ┌─────────────┐               │
│     │   CPU   │         │     RAM     │               │
│     └────┬────┘         └──────┬──────┘               │
│          │                     │                        │
│          │    ┌────────────────┘                        │
│          │    │                                         │
│          ▼    ▼                                         │
│     ┌─────────────────┐                                 │
│     │  Memory Controller │                             │
│     └────────┬─────────┘                                 │
│              │ PCIe 总线                                 │
└──────────────┼──────────────────────────────────────────┘
               │
┌──────────────┼──────────────────────────────────────────┐
│              ▼        FPGA PCIe Endpoint                 │
│         ┌─────────┐                                     │
│         │ PCIe    │ BAR0: CSR registers                 │
│         │ Block   │ BAR1: DMA buffer space               │
│         └────┬────┘                                     │
│              │                                           │
│    ┌─────────┴─────────┐                                │
│    ▼                   ▼                                │
│ ┌────────┐        ┌────────┐                           │
│ │ DMA    │        │ User   │                           │
│ │ Engine │        │ Logic  │                           │
│ └────────┘        └────────┘                           │
└─────────────────────────────────────────────────────────┘
```

## 5.3 DMA 传输类型

### 存储器写（Memory Write）

```
PC RAM ────────────────▶ FPGA
CPU 启动 DMA，让 FPGA 从 PC 内存读取数据

典型用途：从 PC 发送数据到 FPGA
```

### 存储器读（Memory Read）

```
FPGA ────────────────▶ PC RAM
FPGA 写入的数据直接存到 PC 内存

典型用途：FPGA 采集数据并存储到 PC
```

## 5.4 DMA 描述符

DMA 描述符告诉 DMA 引擎要传输什么数据：

```c
struct dma_desc {
    dma_addr_t src_addr;      // 源地址（物理地址）
    dma_addr_t dst_addr;      // 目标地址
    u32 count;                // 传输字节数
    u32 control;              // 控制标志（启动传输等）
    struct dma_desc *next;   // 链表下一项（可选）
};
```

典型的环形缓冲区布局：

```
BAR1 (DMA Space)
┌─────────────────────────────────────────────────────┐
│  DMA Descriptor Ring                                 │
│                                                     │
│   ┌─────────┐   ┌─────────┐   ┌─────────┐         │
│   │ Desc 0  │──►│ Desc 1  │──►│ Desc 2  │──►...   │
│   └─────────┘   └─────────┘   └─────────┘         │
│                                                     │
│   head pointer (软件跟踪)                           │
│                           tail pointer (硬件更新)   │
│                                                     │
└─────────────────────────────────────────────────────┘
```

## 5.5 DMA 缓冲区分配

```c
// 分配 DMA 一致性缓冲区
// 一致性 = CPU 和 DMA 设备看到同一块内存（缓存在硬件级别同步）

ep->dma_size = 16 * PAGE_SIZE;  // 至少一页
ep->dma_virt = dma_alloc_coherent(
    &pdev->dev,              // 设备指针
    ep->dma_size,            // 缓冲区大小
    &ep->dma_phys,           // 返回物理地址（bus address）
    GFP_KERNEL               // 内存分配标志
);

if (!ep->dma_virt) {
    dev_err(&pdev->dev, "DMA allocation failed\n");
    return -ENOMEM;
}

// 使用完后释放
dma_free_coherent(
    &pdev->dev,
    ep->dma_size,
    ep->dma_virt,
    ep->dma_phys
);
```

## 5.6 启动 DMA 传输

```c
// 步骤 1: 填充 DMA 描述符
struct dma_desc *desc = (struct dma_desc *)ep->dma_virt;
desc->src_addr = ep->dma_phys;  // 源 = PC 内存
desc->dst_addr = FPGA_DEST_ADDR; // 目标 = FPGA 地址
desc->count = transfer_size;
desc->control = CTRL_START | CTRL_INT_ON_COMPLETE;
desc->next = 0;  // 单次传输

// 步骤 2: 把描述符地址写入 FPGA CSR
ep_write64(ep, REG_RING_BASE, REG_RING_BASE_HI, ep->dma_phys);

// 步骤 3: 配置传输大小
ep_write32(ep, REG_RING_SIZE, transfer_size);

// 步骤 4: 启动 DMA
u32 ctrl = ep_read32(ep, REG_CTRL);
ctrl |= CTRL_START;
ctrl |= CTRL_IRQ_EN;  // 完成时中断
ep_write32(ep, REG_CTRL, ctrl);
```

## 5.7 等待 DMA 完成

```c
// 方法 1: 中断驱动（推荐）
// 在中断处理中唤醒

static wait_queue_head_t ep->wait_queue;

init_waitqueue_head(&ep->wait_queue);

// 应用层
wait_event_interruptible(ep->wait_queue, ep->dma_done);

// 中断处理
if (status & STATUS_DMA_DONE) {
    ep->dma_done = 1;
    wake_up_interruptible(&ep->wait_queue);
}

// 方法 2: 轮询（不推荐，但调试时有用）
while (ep_read32(ep, REG_STATUS) & STATUS_RUNNING) {
    cpu_relax();  // 忙等待
    udelay(1);
}
```

## 5.8 DMA 错误处理

```c
// 中断处理中检测错误
if (status & STATUS_DMA_ERR) {
    dev_err(&ep->pdev->dev, "DMA error: ");

    // 读取详细错误信息（如果设计支持）
    u32 err_code = ep_read32(ep, REG_ERR_CODE);
    dev_err(&ep->pdev->dev, "Error code: 0x%08x\n", err_code);

    // 停止 DMA
    u32 ctrl = ep_read32(ep, REG_CTRL);
    ep_write32(ep, REG_CTRL, ctrl & ~CTRL_START);

    // 标记传输失败
    ep->transfer_error = 1;
    wake_up_interruptible(&ep->wait_queue);
}
```

## 5.9 Scatter-Gather DMA

Scatter-Gather 允许一次 DMA 操作使用多个不连续的缓冲区：

```c
// 描述符链表
struct sg_entry {
    dma_addr_t addr;
    u32 size;
    u32 reserved;
};

struct sg_entry *sg_list = (struct sg_entry *)ep->dma_virt;
int num_entries = 0;

// 添加第一个缓冲区
sg_list[num_entries].addr = buffer1_phys;
sg_list[num_entries].size = buffer1_size;
num_entries++;

// 添加第二个缓冲区（可能不连续）
sg_list[num_entries].addr = buffer2_phys;
sg_list[num_entries].size = buffer2_size;
num_entries++;

// 写入 FPGA
ep_write64(ep, REG_RING_BASE, REG_RING_BASE_HI,
           ep->dma_phys + offsetof(struct sg_entry, addr));
ep_write32(ep, REG_SG_COUNT, num_entries);
ep_write32(ep, REG_CTRL, ctrl | CTRL_START);
```

## 5.10 DMA 与 Cache 一致性

```
问题：
CPU 写入数据到 DMA 缓冲区 → cache 更新，但 RAM 可能还没更新
DMA 引擎从 RAM 读取数据 → 可能读到旧数据

解决：

1. 一致性 DMA 缓冲区（dma_alloc_coherent）
   - 硬件级别保证一致
   - 可能有性能损失

2. 流式映射（dma_map_single / dma_unmap_single）
   - 用于临时缓冲区
   - 需要手动刷 cache

3. 非一致性缓冲区 + IOMMU
   - 高性能场景
   - 需要管理软件 cache
```

```c
// 流式映射示例（临时 DMA 缓冲区）
char *temp_buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
dma_addr_t temp_phys = dma_map_single(&pdev->dev,
                                       temp_buffer,
                                       BUFFER_SIZE,
                                       DMA_TO_DEVICE);

// 现在可以 DMA 传输
// ...

// 传输完成后解除映射
dma_unmap_single(&pdev->dev, temp_phys, BUFFER_SIZE, DMA_TO_DEVICE);
kfree(temp_buffer);
```

## 5.11 DMA 地址转换

```c
// 用户空间虚拟地址 → 物理地址（用于 DMA）
// 需要在用户空间和内核空间之间进行地址转换

static int get_user_phys_addr(struct vm_area_struct *vma,
                               unsigned long user_addr,
                               dma_addr_t *phys_addr)
{
    unsigned long pfn;
    int ret;

    // 获取用户地址对应的物理页面
    ret = get_user_pages_fast(user_addr, 1, FOLL_WRITE, NULL);
    if (ret < 0)
        return ret;

    // 转换虚拟地址到物理地址
    *phys_addr = ((dma_addr_t)page_to_pfn(pages[0])) << PAGE_SHIFT;
    put_page(pages[0]);

    return 0;
}

// mmap 中完成映射
static int ep_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct xilinx_ep_dev *ep = filp->private_data;
    unsigned long pfn;

    // 映射 BAR0 到用户空间
    if (vma->vm_pgoff == 0) {
        pfn = pci_resource_start(ep->pdev, 0) >> PAGE_SHIFT;
        return io_remap_pfn_range(vma, vma->vm_start, pfn,
                                  vma->vm_end - vma->vm_start,
                                  pgprot_noncached(vma->vm_page_prot));
    }

    return -EINVAL;
}
```

## 5.12 DMA 性能优化

| 优化项 | 方法 |
|--------|------|
| 增大缓冲区 | 减少每次传输的启动开销 |
| 对齐到 cache line | 避免 cache line split |
| 使用预取 | 让 CPU 提前准备好下一批数据 |
| 环形缓冲区 | 允许 DMA 持续运行，无需每次重新配置 |
| IOMMU | 支持超过 4GB 的缓冲区（需要 64-bit 系统）|

## 5.13 常见 DMA 问题

| 问题 | 原因 | 解决 |
|------|------|------|
| DMA 不启动 | 没有设置 Bus Master | 调用 `pci_set_master()` |
| 传输数据错误 | Cache 未同步 | 使用 `dma_sync_*()` 函数 |
| 只传输部分数据 | 缓冲区太小 | 增大 DMA 缓冲区 |
| 地址错误 | 用了虚拟地址而非物理地址 | 使用 `dma_map_*()` |
| 性能差 | 缓冲区不对齐 | 确保 cache line 对齐 |

## 5.14 下一步

下一章：[第六章：字符设备](./06_character_device.md)