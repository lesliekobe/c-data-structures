# 第三章：PCI Probe 和 BAR 映射

## 3.1 PCIe 设备发现

Linux PCI 子系统在启动时枚举所有 PCIe 设备，并在 `/sys/bus/pci/devices/` 中表示。

```
/sys/bus/pci/devices/
├── 0000:00:00.0/      # Root Complex
├── 0000:00:01.0/      # PCIe Switch upstream
├── 0000:01:00.0/      # FPGA Device (Endpoint)
└── ...
```

设备路径格式：`Domain:Bus:Device.Function`

## 3.2 PCI 驱动注册

```c
static struct pci_driver xilinx_ep_driver = {
    .name     = DRV_NAME,
    .id_table = xilinx_ep_ids,      // 支持的设备 ID 列表
    .probe    = xilinx_ep_probe,     // 设备匹配后调用
    .remove   = xilinx_ep_remove,    // 设备移除时调用
    .shutdown = xilinx_ep_shutdown,  // 系统关机时调用
};

// 设备 ID 表
static const struct pci_device_id xilinx_ep_ids[] = {
    { PCI_DEVICE(0x10EE, 0x0007) },  // Xilinx Spartan-6 default
    { PCI_DEVICE(0x10EE, 0x7021) },  // Xilinx Artix-7 例
    { 0, }  // 结束标记
};

MODULE_DEVICE_TABLE(pci, xilinx_ep_ids);

// 注册驱动
pci_register_driver(&xilinx_ep_driver);
```

## 3.3 Probe 函数详解

```c
static int xilinx_ep_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
    struct xilinx_ep_dev *ep;
    int ret;

    // 1. 分配私有数据结构
    ep = kzalloc(sizeof(struct xilinx_ep_dev), GFP_KERNEL);
    if (!ep)
        return -ENOMEM;

    ep->pdev = pdev;
    spin_lock_init(&ep->lock);
    atomic_set(&ep->refcount, 0);

    // 2. 启用 PCI 设备
    //    - 使能 Memory Space 和 IO Space
    //    - 设置 Command Register
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable PCI device: %d\n", ret);
        goto err_free;
    }

    // 3. 请求 BAR 区域
    //    - 告诉系统这些 I/O 范围已被占用
    //    - 防止其他驱动或内核代码使用
    ret = pci_request_regions(pdev, DRV_NAME);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request regions: %d\n", ret);
        goto err_disable;
    }

    // 4. 使能 Bus Master
    //    - 这是关键！没有这个，设备无法发起 DMA 事务
    //    - 等同于设置 Command Register 的 MASTER 位
    pci_set_master(pdev);

    // 5. 映射 BAR0 (CSR 空间)
    //    - pci_resource_start() 返回 BAR0 的物理起始地址
    //    - pci_iomap() 创建页表项，返回虚拟地址
    //    - 之后对 ep->bar0 的读写等于访问 PCIe BAR0 空间
    ep->bar0 = pci_iomap(pdev, 0, BAR_0_SIZE);
    if (!ep->bar0) {
        dev_err(&pdev->dev, "Failed to map BAR0\n");
        ret = -ENOMEM;
        goto err_release;
    }

    // 6. 映射 BAR1 (DMA 空间，可选)
    if (pci_resource_len(pdev, 1) > 0) {
        ep->bar1 = pci_iomap(pdev, 1, BAR_1_SIZE);
        if (!ep->bar1)
            dev_warn(&pdev->dev, "BAR1 not available\n");
    }

    // 7. 设置 DMA 掩码
    //    - 告诉系统驱动能访问的物理地址范围
    //    - 32-bit 掩码：设备只能访问 4GB 以下内存
    ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
    if (ret) {
        dev_err(&pdev->dev, "Failed to set DMA mask: %d\n", ret);
        goto err_unmap;
    }

    // 8. 分配 DMA 一致性缓冲区
    ep->dma_size = BAR_1_SIZE;
    ep->dma_virt = dma_alloc_coherent(&pdev->dev, ep->dma_size,
                                      &ep->dma_phys, GFP_KERNEL);
    if (!ep->dma_virt) {
        dev_warn(&pdev->dev, "Failed to allocate DMA buffer\n");
        // fallback 方案
        ep->dma_virt = kzalloc(ep->dma_size, GFP_KERNEL);
        ep->dma_phys = virt_to_phys(ep->dma_virt);
    }

    // 9. 注册中断处理
    ret = request_irq(ep->irq, xilinx_ep_irq_handler,
                     IRQF_SHARED, DRV_NAME, ep);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ: %d\n", ret);
        goto err_dma;
    }

    // 10. 清空中断状态，使能中断
    ep_write32(ep, REG_INT_STATUS, 0xFFFFFFFF);
    ep_write32(ep, REG_INT_ENABLE, 0x07);

    // 11. 添加字符设备
    // ...

    dev_info(&pdev->dev, "Probed: bar0=%p, irq=%d\n", ep->bar0, ep->irq);
    return 0;

// 错误处理路径（按逆序释放资源）
err_dma:
    dma_free_coherent(&pdev->dev, ep->dma_size, ep->dma_virt, ep->dma_phys);
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
```

## 3.4 BAR 映射原理

```
物理地址空间（CPU 视角）：
┌─────────────────────────────────────────────┐
│ 0x0000_0000        │        4GB              │
│───────────────────────────── PCI Memory ────│
│ 0xC000_0000        │                       │ │
│                    │  PCIe BAR0: 0xC000_0000│ │  64KB
│                    │  PCIe BAR1: 0xC001_0000│ │  1MB
│                    │                       │ │
└─────────────────────────────────────────────┘

内核创建页表映射：
  ep->bar0 (虚拟地址) ────映射───▶  物理 BAR0 地址 (0xC000_0000)
  0xFFFF_8800_1000_0000        0xC000_0000

iowrite32(val, ep->bar0 + 0x04)
     │
     ▼
1. 计算虚拟地址: 0xFFFF_8800_1000_0000 + 0x04 = 0xFFFF_8800_1000_0004
2. 读取页表项，找到物理地址
3. 通过 PCIe 总线发送 Memory Write TLP:
   └─ TLP Header + Data → PCIe 设备 BAR0 + 0x04
```

## 3.5 读取 BAR 信息

```bash
# 查看设备 BAR
cat /sys/bus/pci/devices/0000:01:00.0/resource
# 输出: start end flags (每行一个 BAR)

# 查看 BAR 映射的虚拟地址
cat /sys/bus/pci/devices/0000:01:00.0/resource0  # BAR0
cat /sys/bus/pci/devices/0000:01:00.0/resource1  # BAR1

# 查看设备信息
lspci -v -s 01:00.0
```

## 3.6 PCI 地址空间 vs CPU 虚拟空间

```
┌─────────────────────────────────────────────────────────────┐
│                      CPU Virtual Address Space              │
│                                                             │
│  0xFFFF_8000_0000_0000  ──── kernel space (canonical)       │
│                           ──── vmalloc/ioremap              │
│  0xFFFF_8800_0000_0000  ──── direct mapping of RAM           │
│                                                             │
│  0x0000_0000_0000_0000  ──── user space (canonical)         │
└─────────────────────────────────────────────────────────────┘

pci_iomap() 返回的地址是内核空间的虚拟地址。
这些地址通过内核页表映射到实际的 PCI 地址空间。

应用层看到的 /dev/mem 访问的地址是物理地址。
直接用 /dev/mem 访问 PCI BAR 是危险的操作！
```

## 3.7 I/O 读写函数

```c
// 读取 32-bit 寄存器
static inline u32 ep_read32(struct xilinx_ep_dev *ep, int offset)
{
    // ioread32 会处理内存屏障和 endianness 转换
    return ioread32(ep->bar0 + offset);
}

// 写入 32-bit 寄存器
static inline void ep_write32(struct xilinx_ep_dev *ep, int offset, u32 val)
{
    iowrite32(val, ep->bar0 + offset);
}

// 读取 64-bit 寄存器 (分高低 32-bit)
static inline u64 ep_read64(struct xilinx_ep_dev *ep, int lo, int hi)
{
    u32 low = ioread32(ep->bar0 + lo);
    u32 high = ioread32(ep->bar0 + hi);
    return ((u64)high << 32) | low;
}

// 写入 64-bit 寄存器
static inline void ep_write64(struct xilinx_ep_dev *ep, int lo, int hi, u64 val)
{
    iowrite32((u32)val, ep->bar0 + lo);
    iowrite32((u32)(val >> 32), ep->bar0 + hi);
}

// 读取 16-bit 寄存器 (配置空间)
u16 pci_read_config(struct pci_dev *pdev, int reg)
{
    u16 val;
    pci_read_config_word(pdev, reg, &val);
    return val;
}

// 写入配置空间
pci_write_config_word(pdev, reg, val);
```

## 3.8 电源管理和 FLR

```c
// Function Level Reset (FLR)
// PCIe EP 支持不需要驱动干预的函数级复位

static void xilinx_ep_shutdown(struct pci_dev *pdev)
{
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);
    u32 tmp;

    spin_lock(&ep->lock);

    // 停止 DMA 引擎
    tmp = ep_read32(ep, REG_CTRL);
    ep_write32(ep, REG_CTRL, tmp & ~CTRL_START);

    // 禁用中断
    ep_write32(ep, REG_INT_ENABLE, 0);

    spin_unlock(&ep->lock);

    // 等待中断处理完成
    synchronize_irq(ep->irq);

    // 让系统知道设备正在关闭
    pci_disable_device(pdev);
}

// 可选：实现 FLR
// 有些 EP 设计支持通过配置空间触发 FLR
```

## 3.9 常见错误排查

| 问题 | 原因 | 解决方法 |
|------|------|----------|
| BAR 映射返回 NULL | BAR 长度为 0 或映射失败 | 检查 `pci_resource_len()` |
| 读写寄存器无效 | 访问偏移超过 BAR 大小 | 确认 BAR_SIZE 和偏移 |
| DMA 不工作 | 没有设置 Bus Master | 调用 `pci_set_master()` |
| 中断不触发 | MSI 未使能或 BAR 设置错误 | 检查配置空间 Cap 指针 |
| probe 失败 | Vendor/Device ID 不匹配 | 检查设备 ID 表 |

## 3.10 下一步

下一章：[第四章：中断处理](./04_interrupt_handling.md)