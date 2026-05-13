# 第一章：PCIe 协议基础

## 1.1 PCIe 是什么

PCI Express（PCIe）是一种高速串行计算机扩展总线标准，用于替代旧的 PCI/PCI-X 并行总线。

```
传统 PCI (并行)              PCIe (串行)
┌─────────┐                 ┌─────────┐
│  数据线  │← 33/66MHz      │  Tx/Rx  │← 2.5/5/8 GT/s
│  32bit   │                │  差分   │
└─────────┘                 └─────────┘
                              ↑
                          串行差分对
```

## 1.2 PCIe 拓扑结构

```
Host Bridge
    │
    ├─── Bus 0 ──┬── Device 0 (Root Complex)
    │            │     │
    │            │     ├── Function 0
    │            │     └── Function 1
    │            │
    │            └── Device 1 ( PCIe Switch)
    │                       │
    │            ┌──────────┴──────────┐
    │            │                     │
    │         Bus 1                  Bus 2
    │      Device 0                Device 0
    │      (Endpoint)              (Endpoint)
    │
    └─── Bus 0 ──┬── Device 1 (Endpoint)
                 │     │
                 │     └── Function 0
                 │
                 └── Device 2 ( PCIe to PCI Bridge)
```

**关键概念：**
- **Root Complex (RC)**：CPU/内存系统与 PCIe 总线的连接点
- **Endpoint (EP)**：终端设备（如网卡、SSD、FPGA）
- **Switch**：扩展端口，连接多个 PCIe 设备

## 1.3 PCIe 传输层

### 1.3.1 TLP (Transaction Layer Packet)

PCIe 所有事务都封装在 TLP 中：

```
┌─────────────────────────────────────────────────────────┐
│  TLP Header    │  Data Payload (可选) │  ECRC (可选)    │
│  3 或 4 DW     │  0 ~ 1024 DW          │  1 DW          │
└─────────────────────────────────────────────────────────┘
```

TLP 类型：

| 类型 | 用途 |
|------|------|
| Memory Read | 读内存空间 |
| Memory Write | 写内存空间 |
| Configuration Read | 读配置空间 |
| Configuration Write | 写配置空间 |
| IO Read | 读 IO 空间（传统兼容） |
| IO Write | 写 IO 空间（传统兼容） |
| Message | 中断/错误/电源管理 |

### 1.3.2 BAR (Base Address Register)

每个 EP 设备有最多 6 个 BAR，用于映射设备寄存器到系统内存/IO 空间：

```
配置空间头部：
┌────────────────────────────────────────┐
│ Vendor ID / Device ID                  │ 0x00
├────────────────────────────────────────┤
│ Command / Status                       │ 0x04
├────────────────────────────────────────┤
│ Class Code / Revision ID               │ 0x08
├────────────────────────────────────────┤
│ BIST / Header Type / Latency Timer     │ 0x0C
├────────────────────────────────────────┤
│ BAR0: [基地址寄存器]                   │ 0x10
├────────────────────────────────────────┤
│ BAR1: [基地址寄存器]                   │ 0x14
├────────────────────────────────────────┤
│ ...                                    │
└────────────────────────────────────────┘

BAR 格式（32-bit）：
┌──┬──────────┬─────────────────────────┬──┐
│0 │  Type    │   Address[31:4]         │RO│
│  │  0=32bit │                         │  │
│  │  1=64bit │                         │  │
└──┴──────────┴─────────────────────────┴──┘

Bit 0: 0 = Memory Space, 1 = IO Space
Bit 1: 0 = 32-bit, 1 = 64-bit (仅 BAR0)
Bit 2-3: Memory 类型（00=非预取，01=预取）
Bit 4-31: 基地址（4KB 对齐）
```

## 1.4 PCIe 速率和带宽

| 世代 | 编码 | 单通道速率 | x1 | x4 | x8 |
|------|------|-----------|-----|-----|-----|
| PCIe Gen1 | 8b/10b | 2.5 GT/s | 250 MB/s | 1 GB/s | 2 GB/s |
| PCIe Gen2 | 8b/10b | 5 GT/s | 500 MB/s | 2 GB/s | 4 GB/s |
| PCIe Gen3 | 128b/130b | 8 GT/s | ~1 GB/s | ~4 GB/s | ~8 GB/s |
| PCIe Gen4 | 128b/130b | 16 GT/s | ~2 GB/s | ~8 GB/s | ~16 GB/s |

## 1.5 配置空间

PCIe 设备的配置空间（256字节）：

```
┌─────────────────────────────────────────┐
│ 0x00   Vendor ID                        │
│ 0x02   Device ID                        │
├─────────────────────────────────────────┤
│ 0x04   Command Register                 │
│ 0x06   Status Register                  │
├─────────────────────────────────────────┤
│ 0x08   Revision ID / Class Code          │
│ 0x0C   Cache Line Size / Latency Timer   │
├─────────────────────────────────────────┤
│ 0x10   BAR0 (Base Address 0)            │
│ 0x14   BAR1 (Base Address 1)            │
│ 0x18   BAR2 (Base Address 2)            │
│ 0x1C   BAR3 (Base Address 3)           │
│ 0x20   BAR4 (Base Address 4)            │
│ 0x24   BAR5 (Base Address 5)            │
├─────────────────────────────────────────┤
│ 0x28   Cardbus CIS Pointer              │
├─────────────────────────────────────────┤
│ 0x2C   Subsystem Vendor ID / Subsystem ID│
├─────────────────────────────────────────┤
│ 0x30   Expansion ROM Base Address       │
├─────────────────────────────────────────┤
│ 0x34   Capabilities Pointer             │
│ 0x36   Reserved                         │
├─────────────────────────────────────────┤
│ 0x38   Reserved                         │
├─────────────────────────────────────────┤
│ 0x3C   Max_Lat / Min_Gnt / IRQ / Pin    │
└─────────────────────────────────────────┘
```

**重要寄存器：**

```c
// Command Register (0x04)
#define PCI_COMMAND_IO         0x01  // Enable IO Space
#define PCI_COMMAND_MEMORY     0x02  // Enable Memory Space
#define PCI_COMMAND_MASTER     0x04  // Enable Bus Master (DMA 必须)

// Status Register (0x06)
#define PCI_STATUS_CAP_LIST    0x10  // Capability List present
#define PCI_STATUS_66MHZ       0x20  // 66MHz capable
#define PCI_STATUS_FAST_BACK   0x80  // Fast Back-to-Back capable
```

## 1.6 枚举过程

系统启动时，BIOS/操作系统枚举 PCIe 设备：

```
1. Host RC 扫描 Bus 0
2. 对每个 Device 发送 Configuration Read
3. 如果返回有效 Vendor ID，说明设备存在
4. 读取 BAR，确定需要多少地址空间
5. 分配系统地址范围
6. 继续深入，直到发现所有设备
```

## 1.7 MSI (Message Signaled Interrupt)

MSI 是 PCIe 推荐的中断方式，不需要物理中断引脚：

```
传统中断:          MSI 中断:
Device ---IRQ---> CPU    Device --TLP--> CPU
                        (Memory Write 带有中断向量)
```

MSI 优势：
- 不需要独立中断引脚（节省引脚）
- 支持多个中断向量
- 避免共享中断的竞争

## 1.8 关键概念总结

| 概念 | 说明 |
|------|------|
| TLP | Transaction Layer Packet，PCIe 所有事务的封装格式 |
| BAR | Base Address Register，映射设备寄存器到系统地址空间 |
| RC | Root Complex，CPU 与 PCIe 总线的桥接 |
| EP | Endpoint，PCIe 终端设备 |
| CRS | Configuration Request Retry Status，枚举过程中常见状态 |
| MSI | Message Signaled Interrupt，PCIe 推荐的中断方式 |
| P2P | Peer-to-Peer，设备间直接通信（不需要经过主存） |

## 1.9 下一步

下一章：[第二章：驱动架构](./02_driver_architecture.md)