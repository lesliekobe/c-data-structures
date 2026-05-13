# 第十章：寄存器映射参考

## 10.1 寄存器概述

驱动通过 BAR0 访问设备控制/状态寄存器（CSR）。以下是基于通用 FPGA 设计的寄存器映射，**需要根据你的实际设计调整**。

## 10.2 寄存器映射表

| 偏移 | 名称 | 访问 | 描述 |
|------|------|------|------|
| 0x00 | CTRL | RW | 控制寄存器 |
| 0x04 | STATUS | RO | 状态寄存器 |
| 0x08 | RING_BASE_LO | RW | DMA 描述符环基地址（低 32 位） |
| 0x0C | RING_BASE_HI | RW | DMA 描述符环基地址（高 32 位） |
| 0x10 | RING_SIZE | RW | DMA 描述符数量 |
| 0x14 | DEV_CMD | RW | 设备命令寄存器 |
| 0x18 | DEV_STATUS | RO | 设备状态寄存器 |
| 0x1C | INT_ENABLE | RW | 中断使能寄存器 |
| 0x20 | INT_STATUS | RW1C | 中断状态寄存器（写 1 清除） |
| 0x24 | FLR | WO | 函数级复位（写 1 触发） |
| 0x28 | VERSION | RO | IP 版本寄存器 |
| 0x2C-0xFF | 保留 | - | 保留给扩展用 |

## 10.3 寄存器详细定义

### CTRL (0x00) - 控制寄存器

```
Bit 0:   START      - DMA 引擎启动（1=启动，0=停止）
Bit 1:   STOP       - DMA 引擎停止（1=停止当前传输）
Bit 2:   RESET      - 软复位（1=复位，脉冲）
Bit 3:   IRQ_EN     - 中断使能（1=使能，0=屏蔽）
Bit 4:   DESC_EN    - 描述符使能（1=循环模式，0=单次）
Bit 5:   WR_EN      - 写使能（1=允许写入，0=禁止）
Bit 6:   RD_EN      - 读使能（1=允许读取，0=禁止）
Bit 7-31: 保留
```

```c
#define CTRL_START    BIT(0)
#define CTRL_STOP     BIT(1)
#define CTRL_RESET    BIT(2)
#define CTRL_IRQ_EN   BIT(3)
#define CTRL_DESC_EN  BIT(4)
#define CTRL_WR_EN    BIT(5)
#define CTRL_RD_EN    BIT(6)

// 示例：启动 DMA
u32 ctrl = ep_read32(ep, REG_CTRL);
ctrl |= CTRL_START | CTRL_IRQ_EN;
ep_write32(ep, REG_CTRL, ctrl);
```

### STATUS (0x04) - 状态寄存器

```
Bit 0:   IDLE       - DMA 引擎空闲（1=空闲）
Bit 1:   RUNNING    - DMA 引擎运行中（1=运行）
Bit 2:   ERROR      - 错误标志（1=有错误）
Bit 3:   IRQ_PEND   - 中断挂起（1=有待处理中断）
Bit 4:   DMA_DONE   - DMA 传输完成（1=完成）
Bit 5:   DMA_ERR    - DMA 错误（1=错误）
Bit 6:   DESC_END   - 描述符链结束（1=到达末尾）
Bit 7-31: 保留
```

```c
#define STATUS_IDLE       BIT(0)
#define STATUS_RUNNING    BIT(1)
#define STATUS_ERROR      BIT(2)
#define STATUS_IRQ_PEND   BIT(3)
#define STATUS_DMA_DONE   BIT(4)
#define STATUS_DMA_ERR    BIT(5)
#define STATUS_DESC_END   BIT(6)

// 示例：轮询 DMA 完成
int wait_dma_done(struct xilinx_ep_dev *ep, int timeout_ms)
{
    int count = timeout_ms;
    while (count--) {
        if (ep_read32(ep, REG_STATUS) & STATUS_DMA_DONE)
            return 0;
        udelay(1000);
    }
    return -ETIMEDOUT;
}
```

### RING_BASE_LO/HI (0x08-0x0C) - DMA 描述符环基地址

```
64-bit 地址，描述 DMA 描述符环的物理起始地址

LO[31:0]: 基地址低 32 位
HI[31:0]: 基地址高 32 位（用于 64-bit 系统）

示例：
dma_addr_t ring_phys = 0x1_0000_0000;  // 4GB 以上的地址
ep_write32(ep, REG_RING_BASE_LO, (u32)ring_phys);
ep_write32(ep, REG_RING_BASE_HI, (u32)(ring_phys >> 32));
```

### RING_SIZE (0x10) - 描述符数量

```
Bit 15-0: 描述符数量（1-65535）
Bit 31-16: 保留

示例：
ep_write32(ep, REG_RING_SIZE, 256);  // 256 个描述符
```

### DEV_CMD (0x14) - 设备命令

```
Bit 0:   WRITE_EN   - 写通道使能
Bit 1:   READ_EN    - 读通道使能
Bit 2:   DMA_EN     - DMA 使能
Bit 3:   MSI_EN     - MSI 中断使能
Bit 4-7: 保留
```

### DEV_STATUS (0x18) - 设备状态

```
Bit 0:   LINK_UP    - PCIe 链路 UP（1=链路正常）
Bit 1:   CONFIG_OK  - 配置成功（1=配置完成）
Bit 2:   BUS_MASTER - Bus Master 使能（1=已使能）
Bit 3-7: 保留
```

### INT_ENABLE (0x1C) - 中断使能

```
Bit 0:   DMA_DONE_EN   - DMA 完成中断使能
Bit 1:   DMA_ERR_EN    - DMA 错误中断使能
Bit 2:   LINK_EN       - 链路状态改变中断使能
Bit 3:   USER_EN       - 用户中断使能
Bit 4-7: 保留

示例：使能所有中断
ep_write32(ep, REG_INT_ENABLE, 0x0F);
```

### INT_STATUS (0x20) - 中断状态（写 1 清除）

```
Bit 0:   DMA_DONE   - DMA 完成中断标志
Bit 1:   DMA_ERR    - DMA 错误中断标志
Bit 2:   LINK_CHG   - 链路状态改变中断标志
Bit 3:   USER_IRQ   - 用户触发中断标志
Bit 4-7: 保留

示例：清除 DMA 完成标志
ep_write32(ep, REG_INT_STATUS, STATUS_DMA_DONE);
```

### FLR (0x24) - 函数级复位（只写）

```
Bit 0:   FLR_TRIGGER  - 触发 FLR（写 1）
Bit 1-31: 保留

写入后设备会执行复位，需要等待一段时间后重新配置。
```

### VERSION (0x28) - IP 版本（只读）

```
Bit 7-0:   修订版本号
Bit 15-8:  补丁版本号
Bit 23-16: 副版本号
Bit 31-24: 主版本号

示例：读取版本
u32 ver = ep_read32(ep, REG_VERSION);
printk("IP Version: %d.%d.%d.%d\n",
       (ver >> 24) & 0xFF,
       (ver >> 16) & 0xFF,
       (ver >> 8) & 0xFF,
       ver & 0xFF);
```

## 10.4 DMA 描述符格式

```c
struct dma_descriptor {
    volatile u32 src_addr_lo;    // 源地址低 32 位
    volatile u32 src_addr_hi;    // 源地址高 32 位（64-bit）
    volatile u32 dst_addr_lo;    // 目标地址低 32 位
    volatile u32 dst_addr_hi;    // 目标地址高 32 位（64-bit）
    volatile u32 count;          // 传输字节数
    volatile u32 control;        // 控制标志
    volatile u32 status;          // 状态标志（硬件更新）
    volatile u32 next_desc_lo;   // 下一个描述符地址低 32 位
    volatile u32 next_desc_hi;   // 下一个描述符地址高 32 位
};

#define DESC_CTRL_OWN    BIT(31)  // 描述符所有权（1=硬件，0=软件）
#define DESC_CTRL_INT    BIT(30)  // 完成中断
#define DESC_CTRL_EOP    BIT(29)  // 传输结束
#define DESC_CTRL_SOP    BIT(28)  // 传输开始

#define DESC_STAT_DONE   BIT(0)   // 描述符完成
#define DESC_STAT_ERROR  BIT(1)   // 描述符错误
```

## 10.5 初始化序列

```c
int ep_init(struct xilinx_ep_dev *ep)
{
    u32 tmp;

    // 1. 确认链路 UP
    tmp = ep_read32(ep, REG_STATUS);
    if (!(tmp & STATUS_LINK_UP)) {
        dev_err(&ep->pdev->dev, "PCIe link not up\n");
        return -ENODEV;
    }

    // 2. 复位 DMA 引擎
    ep_write32(ep, REG_CTRL, CTRL_RESET);
    udelay(10);
    ep_write32(ep, REG_CTRL, 0);
    udelay(10);

    // 3. 等待 IDLE
    int retry = 100;
    while (retry-- && !(ep_read32(ep, REG_STATUS) & STATUS_IDLE))
        udelay(100);
    if (retry <= 0) {
        dev_err(&ep->pdev->dev, "DMA engine not idle\n");
        return -EBUSY;
    }

    // 4. 配置 DMA 描述符环
    ep_write64(ep, REG_RING_BASE_LO, REG_RING_BASE_HI, ep->dma_phys);
    ep_write32(ep, REG_RING_SIZE, ep->ring_size);

    // 5. 使能所有中断
    ep_write32(ep, REG_INT_ENABLE, 0x0F);
    ep_write32(ep, REG_INT_STATUS, 0xFFFFFFFF);  // 清所有标志

    // 6. 使能设备命令
    ep_write32(ep, REG_DEV_CMD, DEV_CMD_WRITE_EN | DEV_CMD_READ_EN);

    // 7. 使能 DMA（不启动传输）
    tmp = ep_read32(ep, REG_CTRL);
    tmp |= CTRL_WR_EN | CTRL_RD_EN;
    ep_write32(ep, REG_CTRL, tmp);

    return 0;
}
```

## 10.6 下一步

下一章：[第十一章：适配到你的 FPGA 设计](./11_porting_guide.md)