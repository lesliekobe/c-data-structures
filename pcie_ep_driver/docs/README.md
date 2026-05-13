# PCIe Endpoint Driver 开发文档

本目录包含 Xilinx Spartan-6 PCIe Endpoint Linux 驱动的完整开发文档。

## 文档列表

| 章节 | 文件 | 内容 |
|------|------|------|
| 概述 | [README.md](README.md) | 文档概览和索引 |
| 基础 | [01_pcie_basics.md](01_pcie_basics.md) | PCIe 总线协议基础 |
| 架构 | [02_driver_architecture.md](02_driver_architecture.md) | 驱动整体架构 |
| 探测 | [03_pci_probe.md](03_pci_probe.md) | PCI probe 和 BAR 映射 |
| 中断 | [04_interrupt_handling.md](04_interrupt_handling.md) | MSI/MSI-X 中断机制 |
| DMA | [05_dma_engine.md](05_dma_engine.md) | DMA 引擎和描述符链表 |
| 字符设备 | [06_character_device.md](06_character_device.md) | 字符设备与 ioctl API |
| 内存 | [07_memory_mapping.md](07_memory_mapping.md) | mmap 和 DMA 缓冲区管理 |
| 调试 | [08_debugging.md](08_debugging.md) | 调试技巧和方法 |
| 热插拔 | [09_hotplug.md](09_hotplug.md) | 热插拔和电源管理 |
| 寄存器 | [10_register_map.md](10_register_map.md) | 寄存器映射参考 |
| 适配 | [11_porting_guide.md](11_porting_guide.md) | 如何适配你的 FPGA 设计 |

## 阅读顺序

建议按顺序阅读：

1. 先看 [01_pcie_basics.md](01_pcie_basics.md) 理解 PCIe 协议
2. 再看 [02_driver_architecture.md](02_driver_architecture.md) 建立整体概念
3. 然后按章节深入：探测 → 中断 → DMA → 字符设备
4. 调试和热插拔可以随时查阅

## 代码对照

源代码位于 `../xilinx_pcie_ep.c`，文档中的代码示例都对应这个源文件。

## 反馈

有问题欢迎提交 Issue。