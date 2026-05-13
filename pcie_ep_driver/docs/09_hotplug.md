# 第九章：热插拔与电源管理

## 9.1 PCIe 热插拔概述

```
热拔除：
用户拔出设备 → 系统收到 Surprise Removal 中断 →
驱动收到 remove() 调用 → 清理资源 → 总线重新扫描

热插入：
用户插入设备 → 系统检测到 Presence Detect →
枚举设备 → 调用 probe → 设备可用
```

## 9.2 热插拔检测机制

```
PCIe 热插拔信号：
- Presence Detect (CPEDET#)
- Data Link Layer Active (DLLLA)
- Hot Plug Surprise (HPINT#)

这些信号触发系统中断或状态变化
```

驱动通过 `pci_error_handlers` 接收错误/热插拔事件：

```c
// PCIe AER 错误处理（高级错误报告）
static const struct pci_error_handlers ep_err_handler = {
    .error_detected = ep_error_detected,
    .slot_reset = ep_slot_reset,
    .resume = ep_resume,
    .resume_early = ep_resume_early,
};

static pci_ers_result_t ep_error_detected(struct pci_dev *pdev,
                                          enum pci_channel_state state)
{
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);

    switch (state) {
    case pci_channel_io_normal:
        return PCI_ERS_RESULT_CAN_RECOVER;
    case pci_channel_io_frozen:
        dev_err(&pdev->dev, "PCIe frozen, trying to recover\n");
        return PCI_ERS_RESULT_NEED_RESET;
    case pci_channel_io_perm_failure:
        dev_err(&pdev->dev, "PCIe perm failure, device dead\n");
        return PCI_ERS_RESULT_DISCONNECT;
    }
    return PCI_ERS_RESULT_NONE;
}

static pci_ers_result_t ep_slot_reset(struct pci_dev *pdev)
{
    dev_info(&pdev->dev, "Slot reset, reinitializing\n");
    // 重新配置设备
    return PCI_ERS_RESULT_RECOVERED;
}
```

## 9.3 Surprise Removal 处理

```c
// 驱动移除时清理资源
static void xilinx_ep_remove(struct pci_dev *pdev)
{
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);
    unsigned long flags;

    // 停止所有 DMA 操作
    spin_lock_irqsave(&ep->lock, flags);
    ep_write32(ep, REG_CTRL, 0);  // 停止引擎
    ep_write32(ep, REG_INT_ENABLE, 0);  // 禁用中断
    spin_unlock_irqrestore(&ep->lock, flags);

    // 等待中断处理完成
    synchronize_irq(ep->irq);

    // 释放 IRQ
    free_irq(ep->irq, ep);

    // 释放 DMA 缓冲区
    if (ep->dma_virt) {
        dma_free_coherent(&pdev->dev, ep->dma_size,
                          ep->dma_virt, ep->dma_phys);
    }

    // 释放 BAR 映射
    pci_iounmap(pdev, ep->bar0);
    if (ep->bar1)
        pci_iounmap(pdev, ep->bar1);

    // 释放 BAR 区域
    pci_release_regions(pdev);

    // 禁用设备
    pci_disable_device(pdev);

    // 销毁字符设备
    device_destroy(ep_class, ep->devno);
    cdev_del(&ep->cdev);
    clear_bit(MINOR(ep->devno), dev_minor);

    kfree(ep);
}
```

## 9.4 电源管理概述

```
D0 (正常工作):  full power, D0
D1 (节能):      轻度节能，保留配置
D2 (深度节能):  进一步降低，可以保留或丢失上下文
D3hot (待机关): 最低功耗，保留配置到 RAM
D3cold (断电):  完全关闭，需要重新枚举

系统休眠 → 进入 D3hot → 保存 PCI 配置到内存
系统唤醒 → 恢复 PCI 配置 → 回到 D0
```

## 9.5 PCI PM 状态机

```c
// PCI PM 能力结构
// 设备需要在配置空间中声明 PME (Power Management Event)

// 或者使用标准 PCI 驱动接口
static int __maybe_unused ep_suspend(struct device *dev)
{
    struct pci_dev *pdev = to_pci_dev(dev);
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);

    dev_info(&pdev->dev, "Suspending...\n");

    // 停止 DMA
    ep_write32(ep, REG_CTRL, 0);

    // 禁用中断
    ep_write32(ep, REG_INT_ENABLE, 0);

    // 等待中断处理完成
    synchronize_irq(ep->irq);

    // 进入低功耗状态
    pci_save_state(pdev);
    pci_set_power_state(pdev, PCI_D3hot);

    return 0;
}

static int __maybe_unused ep_resume(struct device *dev)
{
    struct pci_dev *pdev = to_pci_dev(dev);
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);
    int ret;

    dev_info(&pdev->dev, "Resuming...\n");

    // 恢复 PCIe 链路
    ret = pci_set_power_state(pdev, PCI_D0);
    if (ret) {
        dev_err(&pdev->dev, "Failed to set D0 state: %d\n", ret);
        return ret;
    }

    pci_restore_state(pdev);

    // 重新使能设备
    ret = pci_enable_device(pdev);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable device: %d\n", ret);
        return ret;
    }

    // 重新配置
    ep_write32(ep, REG_INT_STATUS, 0xFFFFFFFF);
    ep_write32(ep, REG_INT_ENABLE, 0x07);

    dev_info(&pdev->dev, "Resumed\n");
    return 0;
}

// 关联到 struct pci_driver
static struct pci_driver xilinx_ep_driver = {
    // ...
#ifdef CONFIG_PM
    .driver.pm = &(struct pm_ops) {
        .suspend = ep_suspend,
        .resume = ep_resume,
        .freeze = ep_suspend,
        .thaw = ep_resume,
        .poweroff = ep_suspend,
        .restore = ep_resume,
    },
#endif
};
```

## 9.6 Runtime PM (动态电源管理)

```c
// 允许设备在不使用时进入低功耗状态

static int ep_runtime_suspend(struct device *dev)
{
    struct pci_dev *pdev = to_pci_dev(dev);
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);

    dev_info(&pdev->dev, "Runtime suspend\n");

    // 停止 DMA，关闭时钟
    // ...

    return 0;
}

static int ep_runtime_resume(struct device *dev)
{
    struct pci_dev *pdev = to_pci_dev(dev);
    struct xilinx_ep_dev *ep = pci_get_drvdata(pdev);

    dev_info(&pdev->dev, "Runtime resume\n");

    // 恢复时钟，重新配置
    // ...

    return 0;
}

static int ep_runtime_idle(struct device *dev)
{
    // 没有活跃的传输，可以进入低功耗
    // 返回 -EAGAIN 表示"不要立即挂起"
    return -EAGAIN;
}

static const struct dev_pm_ops ep_pm_ops = {
    RUNTIME_PM_OPS(ep_runtime_suspend, ep_runtime_resume, ep_runtime_idle)
};
```

## 9.7 FLR (Function Level Reset)

```c
// 有些 PCIe 设备支持函数级复位，不需要热插拔

static void ep_flr(struct xilinx_ep_dev *ep)
{
    int pos;
    u32 cap;

    // 检查是否支持 FLR
    pos = pci_find_capability(ep->pdev, PCI_CAP_ID_EXP);
    if (!pos)
        return;

    pci_read_config_dword(ep->pdev, pos + PCI_EXP_DEVCAP, &cap);
    if (!(cap & PCI_EXP_DEVCAP_FLR))
        return;

    dev_info(&ep->pdev->dev, "Performing FLR\n");

    // 发起 FLR（设置 bit[15] in control register）
    pci_read_config_dword(ep->pdev, pos + PCI_EXP_DEVCTL, &cap);
    cap |= PCI_EXP_DEVCTL_BCR_FLR;
    pci_write_config_dword(ep->pdev, pos + PCI_EXP_DEVCTL, cap);

    // 等待设备重新出现（最多 100ms）
    msleep(100);
}
```

## 9.8 完整性检查清单

| 项目 | 检查 |
|------|------|
| ✅ remove 中停止 DMA | 没有 DMA 在运行时释放资源 |
| ✅ synchronize_irq() | 等待所有中断处理完成 |
| ✅ 禁用中断 | 不再有新中断产生 |
| ✅ 释放 DMA 缓冲区 | 防止内存泄漏 |
| ✅ 释放 IRQ | 防止 zombie IRQ handlers |
| ✅ 释放 BAR 映射 | 防止页表泄漏 |
| ✅ 释放字符设备 | 防止设备节点泄漏 |
| ✅ 恢复电源状态 | 休眠后能正确恢复 |

## 9.9 下一步

下一章：[第十章：寄存器映射参考](./10_register_map.md)