# 第十一章：适配到你的 FPGA 设计

## 11.1 适配流程概览

```
适配步骤：
1. 确认 PCIe EP 核配置（Vendor ID, Device ID, BAR 配置）
2. 获取寄存器偏移和功能定义（从你的 FPGA 设计文档）
3. 修改驱动中的 ID 表和寄存器偏移
4. 适配 DMA 描述符格式
5. 测试验证
```

## 11.2 第一步：收集 FPGA 设计信息

你需要从 FPGA 设计中获取以下信息：

| 信息 | 来源 | 用途 |
|------|------|------|
| Vendor ID | PCIe EP 核配置 | 修改 ID 表 |
| Device ID | PCIe EP 核配置 | 修改 ID 表 |
| BAR 大小 | Address Editor | 设置 BAR_SIZE |
| BAR 属性 | Address Editor | Memory/IO, 32/64-bit |
| 寄存器偏移 | 逻辑设计文档 | 修改 REG_* 宏 |
| 中断类型 | 核配置 | 使能 MSI/INTx |
| DMA 描述符格式 | 用户逻辑 | 修改 struct dma_desc |

## 11.3 第二步：修改 Vendor/Device ID

```c
// 找到你的设备 ID
// 方法 1: lspci
lspci -nn | grep -i xilinx

// 输出示例：
// 01:00.0 Memory controller [0580]: Xilinx Corporation Device [10ee:7021]

// 方法 2: 读取配置空间
setpci -s 01:00.0 vendor_id.w
setpci -s 01:00.0 device_id.w

// 修改驱动
static const struct pci_device_id xilinx_ep_ids[] = {
    // 示例：Xilinx Artix-7 PCIe EP
    { PCI_DEVICE(0x10EE, 0x7021) },  // 你的 Device ID
    // 示例：Spartan-6 默认 ID
    { PCI_DEVICE(0x10EE, 0x0007) },
    // 添加更多 ID...
    { 0, }
};
```

## 11.4 第三步：确认 BAR 配置

```bash
# 查看系统分配的 BAR
lspci -v -s 01:00.0

# 输出示例：
# Memory at e0000000 (64-bit, non-prefetchable) [size=64K]
# Memory at e0010000 (64-bit, non-prefetchable) [size=1M]

# 对应：
# BAR0: 64KB (CSR)
# BAR1: 1MB (DMA)
```

```c
// 修改 BAR 大小
#define BAR_0_SIZE     0x10000  // 64KB - 根据你的设计修改
#define BAR_1_SIZE     0x100000 // 1MB  - 根据你的设计修改
#define BAR_2_SIZE     0x1000   // 4KB  - 根据你的设计修改

// 如果只用 BAR0
#ifdef USE_BAR0_ONLY
#undef BAR_1_SIZE
#define BAR_1_SIZE     0
#endif
```

## 11.5 第四步：适配寄存器偏移

```c
// 原始定义
#define REG_CTRL       0x00
#define REG_STATUS     0x04

// 适配示例：你的 FPGA 设计偏移可能不同
#define REG_CTRL       0x100  // 你的 CSR 基地址是 0x100
#define REG_STATUS     0x104
#define REG_RING_BASE  0x200  // DMA 描述符环基地址在 0x200
#define REG_RING_SIZE  0x208

// 更好的方式：用结构体
struct ep_registers {
    volatile u32 ctrl;         // 0x00
    volatile u32 status;        // 0x04
    volatile u32 reserved1[2]; // 0x08-0x0C
    volatile u32 ring_base_lo; // 0x10
    volatile u32 ring_base_hi; // 0x14
    volatile u32 ring_size;    // 0x18
    volatile u32 reserved2[2]; // 0x1C-0x23
    volatile u32 int_enable;   // 0x24
    volatile u32 int_status;   // 0x28
};

// 使用
struct ep_registers __iomem *regs = ep->bar0;
u32 status = ioread32(&regs->status);
```

## 11.6 第五步：适配 DMA 描述符

```c
// 原始描述符
struct dma_descriptor {
    u32 src_addr_lo;
    u32 src_addr_hi;
    u32 dst_addr_lo;
    u32 dst_addr_hi;
    u32 count;
    u32 control;
    u32 status;
    u32 next_lo;
    u32 next_hi;
};

// 适配示例：你的设计可能不同
struct my_dma_desc {
    u64 src_addr;      // 连续的 64-bit 地址
    u64 dst_addr;
    u32 count;
    u16 flags;         // 更小的控制字段
    u16 status;
    u64 next;          // 连续的 64-bit 下一描述符指针
};

// 初始化描述符环
int ep_init_ring(struct xilinx_ep_dev *ep)
{
    int i;
    struct my_dma_desc *desc = ep->dma_virt;

    for (i = 0; i < ep->ring_size; i++) {
        desc[i].src_addr = 0;
        desc[i].dst_addr = 0;
        desc[i].count = 0;
        desc[i].flags = 0;
        desc[i].status = 0;
        desc[i].next = ep->dma_phys + (i + 1) * sizeof(struct my_dma_desc);
    }
    // 最后一个指向第一个（循环）
    desc[ep->ring_size - 1].next = ep->dma_phys;

    return 0;
}
```

## 11.7 第六步：适配中断处理

```c
// 检查是 MSI 还是 INTx
static int ep_setup_irq(struct xilinx_ep_dev *ep)
{
    struct pci_dev *pdev = ep->pdev;
    int ret;

    ep->irq = pdev->irq;

    // 检查 MSI 是否可用
    if (pci_msi_enabled()) {
        ret = pci_enable_msi(pdev);
        if (ret == 0) {
            dev_info(&pdev->dev, "Using MSI interrupts\n");
        } else {
            dev_warn(&pdev->dev, "MSI not available, using INTx\n");
        }
    } else {
        dev_info(&pdev->dev, "Using INTx interrupts\n");
    }

    // 注册中断
    ret = request_irq(ep->irq, xilinx_ep_irq_handler,
                     IRQF_SHARED, DRV_NAME, ep);
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ: %d\n", ret);
        return ret;
    }

    return 0;
}
```

## 11.8 第七步：编译测试

```bash
# 1. 安装内核头文件
sudo apt install linux-headers-$(uname -r)

# 2. 编译驱动
make clean
make

# 3. 加载驱动
sudo insmod xilinx_pcie_ep.ko

# 4. 检查日志
dmesg | tail -50

# 5. 检查设备节点
ls -la /dev/xilinx_ep*
```

## 11.9 常见适配问题

### 问题 1：probe 打印了但寄存器读写没反应

**原因**：BAR 偏移不对或 BAR 大小不匹配

**解决**：
```bash
# 确认 BAR 地址
cat /sys/bus/pci/devices/0000:01:00.0/resource0
hexdump -C /sys/bus/pci/devices/0000:01:00.0/resource0 | head
```

### 问题 2：中断不触发

**原因**：MSI 未使能或中断类型配置错误

**解决**：
```c
// 在 probe 中确保设置
pci_set_master(pdev);  // 启用 Bus Master（才能发 MSI）

// 检查配置空间
setpci -s 01:00.0 04.w  # 应该是 0x07（Memory + Master）
```

### 问题 3：DMA 传输失败

**原因**：描述符格式不匹配或地址错误

**解决**：
```c
// 检查 DMA 物理地址是否正确
dev_info(&ep->pdev->dev, "DMA phys: %pad\n", &ep->dma_phys);

// 检查描述符环地址是否正确写入
ep_write64(ep, REG_RING_BASE_LO, REG_RING_BASE_HI, ep->dma_phys);
u64 verify = ep_read64(ep, REG_RING_BASE_LO, REG_RING_BASE_HI);
dev_info(&ep->pdev->dev, "Verified ring base: %llx\n", verify);
```

## 11.10 调试适配问题清单

| 检查项 | 方法 |
|--------|------|
| Device ID 匹配？ | `lspci -nn \| grep <ID>` |
| BAR 地址映射正确？ | `cat /sys/bus/pci/devices/*/resource*` |
| 寄存器可读？ | `hexdump` BAR0 前几个字节 |
| 中断使能？ | `cat /proc/interrupts` 观察计数 |
| DMA 地址有效？ | 检查 `dma_alloc_coherent()` 返回值 |
| 描述符格式匹配？ | 对比驱动结构和 FPGA 定义 |

## 11.11 适配后验证清单

- [ ] `lspci` 能看到设备
- [ ] 驱动 `insmod` 成功，无错误日志
- [ ] `/dev/xilinx_ep0` 设备节点出现
- [ ] `ioctl` 能控制设备（START/STOP）
- [ ] 中断处理函数被调用（`dmesg` 可见）
- [ ] DMA 传输完成无错误
- [ ] `rmmod` 正常，无资源泄漏

## 11.12 获取帮助

如果遇到问题，提供以下信息：

```bash
# 1. lspci 输出
lspci -nnv

# 2. dmesg 相关日志
dmesg | grep -i xilinx

# 3. 驱动版本
modinfo xilinx_pcie_ep

# 4. 系统信息
uname -a
cat /proc/interrupts | grep -i xilinx
```

## 11.13 下一步

完成适配后，可以进一步优化：

- [PCIe 性能优化](./05_dma_engine.md) - 提高 DMA 带宽
- [电源管理](./09_hotplug.md) - 实现 Runtime PM
- [调试](./08_debugging.md) - 验证稳定性

---

文档撰写完毕。祝开发顺利！