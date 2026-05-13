# 第四章：中断处理

## 4.1 PCIe 中断机制

PCIe 有两种中断方式：

```
传统中断 (INTx)          MSI (Message Signaled Interrupt)
───────────────          ───────────────────────────────
设备 ───IRQ pin────► 中断控制器      设备 ──Memory Write TLP──► CPU
  │                              │         (包含中断向量)
  │                              │
  └─> 通过 PCI 桥接器             └─> 直接写入 LAPIC
```

**MSI 优势：**
- 不需要硬件中断引脚（节省引脚）
- 支持多向量（每个功能可独立中断）
- 避免共享中断的竞争
- 延迟更低

## 4.2 MSI Capability 结构

设备在配置空间中通过 Capability 链表宣告 MSI 支持：

```
MSI Capability (0x50 位置):
┌────────────────────────────────────────┐
│ Next Capability Ptr │ Capability ID   │ 0x50
├────────────────────────────────────────┤
│ Message Control        │ Reserved      │ 0x52
├────────────────────────────────────────┤
│ Message Address (Low)                 │ 0x54
├────────────────────────────────────────┤
│ Message Address (High)                │ 0x58
├────────────────────────────────────────┤
│ Message Data / Upper Address           │ 0x5C
└────────────────────────────────────────┘
```

Message Control 格式：
```
Bits 0-3:   MSI Enable (1 = enabled)
Bits 4-6:   Multiple Message Capable (1/2/4/8/16/32)
Bits 7-9:   Multiple Message Enable (软件设置)
Bits 10-15: 64-bit address capable (1 = 支持)
```

## 4.3 Linux 中断处理流程

```
硬件中断触发 (MSI 或 INTx)
    │
    ▼
CPU 执行中断向量 → LAPIC → 中断控制器
    │
    ▼
跳转至 Linux 中断处理入口
    │
    ▼
调用 request_irq() 注册的处理函数
    │
    ▼
中断处理函数读取设备寄存器判断原因
    │
    ▼
处理（清除中断标志、调度工作、唤醒进程等）
    │
    ▼
返回 IRQ_HANDLED
```

## 4.4 中断注册

```c
// 在 probe 中注册
static int xilinx_ep_probe(struct pci_dev *pdev, ...)
{
    // ...

    // 获取中断号（MSI 或 INTx 共享）
    ep->irq = pdev->irq;

    // 注册中断处理函数
    // IRQF_SHARED: 多个设备共享同一中断号
    // IRQF_TRIGGER_HIGH: 上升沿触发（MSI 不需要）
    ret = request_irq(ep->irq,
                      xilinx_ep_irq_handler,
                      IRQF_SHARED,
                      DRV_NAME,
                      ep);  // dev_id 用于区分是哪个设备
    if (ret) {
        dev_err(&pdev->dev, "Failed to request IRQ %d\n", ret);
        goto err;
    }

    // 使能设备中断（在 CSR 中）
    ep_write32(ep, REG_INT_ENABLE, 0x07);  // 使能多种中断源

    return 0;
}
```

## 4.5 中断处理函数

```c
static irqreturn_t xilinx_ep_irq_handler(int irq, void *dev_id)
{
    struct xilinx_ep_dev *ep = (struct xilinx_ep_dev *)dev_id;
    u32 status;
    u32 handled = 0;

    // 读取中断状态寄存器
    // 注意：这里是硬件视角，寄存器是 FPGA 定义的
    status = ep_read32(ep, REG_INT_STATUS);

    if (status == 0) {
        // 我们的设备没有挂起的中断
        // 可能是共享中断，另一个设备触发的
        return IRQ_NONE;  // Linux 会继续调用其他设备的 handler
    }

    // 处理 DMA 完成中断
    if (status & STATUS_DMA_DONE) {
        // 清除中断：写 1 清除（常见设计）
        ep_write32(ep, REG_INT_STATUS, STATUS_DMA_DONE);
        handled = 1;

        // 更新统计或唤醒等待的进程
        dev_info(&ep->pdev->dev, "DMA transfer completed\n");
        // wake_up_interruptible(&ep->wait_queue);
    }

    // 处理 DMA 错误中断
    if (status & STATUS_DMA_ERR) {
        ep_write32(ep, REG_INT_STATUS, STATUS_DMA_ERR);
        handled = 1;
        dev_err(&ep->pdev->dev, "DMA error occurred\n");
    }

    // 处理用户触发中断（可选）
    if (status & STATUS_IRQ_PENDING) {
        ep_write32(ep, REG_INT_STATUS, STATUS_IRQ_PENDING);
        handled = 1;
    }

    return handled ? IRQ_HANDLED : IRQ_NONE;
}
```

## 4.6 共享中断的处理

```c
// INTx 是共享的，MSI 也可以配置为共享

static irqreturn_t xilinx_ep_irq_handler(int irq, void *dev_id)
{
    // 通过 dev_id 确认是我们的设备
    struct xilinx_ep_dev *ep = dev_id;

    // 读取设备状态
    u32 status = ep_read32(ep, REG_INT_STATUS);

    if (status == 0) {
        // 确认不是我们的设备触发的
        return IRQ_NONE;  // 让 Linux 继续检查其他共享者
    }

    // 处理我们的中断...
    return IRQ_HANDLED;
}
```

## 4.7 中断上下文注意事项

中断处理函数运行在中断上下文（IRQ context），**不能睡眠**：

```c
// ❌ 错误：在中断处理中睡眠
static irqreturn_t bad_handler(int irq, void *dev_id)
{
    // 会崩溃！
    msleep(100);           // 不允许
    down(&semaphore);      // 不允许（可能睡眠）
    copy_to_user(buf, ...); // 不允许（可能睡眠）
    return IRQ_HANDLED;
}

// ✅ 正确：使用自旋锁或顶半部/底半部
static irqreturn_t good_handler(int irq, void *dev_id)
{
    unsigned long flags;

    // 可以使用自旋锁（中断上下文允许）
    spin_lock_irqsave(&ep->lock, flags);
    // ... 操作共享数据 ...
    spin_unlock_irqrestore(&ep->lock, flags);

    // 如果需要更多处理时间，使用 tasklet 或 workqueue
    // （从中断上下文调度，在进程上下文执行）
    tasklet_schedule(&ep->tasklet);

    return IRQ_HANDLED;
}
```

## 4.8 顶半部 vs 底半部

```
中断到达
    │
    ▼
顶半部 (Top Half) - 快速执行
    │
    ├── 读取状态寄存器
    ├── 清除中断标志
    ├── 调度底半部
    └── 返回 IRQ_HANDLED
            │
            ▼
底半部 (Bottom Half) - 延迟处理
    │
    ├── tasklet_schedule() 或 workqueue
    │
    ▼
实际处理（可睡眠、访问用户空间等）
```

实现底半部：

```c
// 定义 tasklet
static void dma_done_tasklet(unsigned long data)
{
    struct xilinx_ep_dev *ep = (struct xilinx_ep_dev *)data;
    // 在进程上下文执行，可以睡眠
    dev_info(&ep->pdev->dev, "DMA tasklet running\n");
}

static DECLARE_TASKLET(ep->tasklet, dma_done_tasklet, (unsigned long)ep);

// 在中断处理中调度
static irqreturn_t xilinx_ep_irq_handler(int irq, void *dev_id)
{
    struct xilinx_ep_dev *ep = dev_id;
    u32 status = ep_read32(ep, REG_INT_STATUS);

    if (status & STATUS_DMA_DONE) {
        ep_write32(ep, REG_INT_STATUS, STATUS_DMA_DONE);
        // 调度底半部处理耗时工作
        tasklet_schedule(&ep->tasklet);
    }

    return IRQ_HANDLED;
}

// 模块清理时销毁 tasklet
void xilinx_ep_remove(struct pci_dev *pdev)
{
    // ...
    tasklet_kill(&ep->tasklet);  // 等待 tasklet 完成
    free_irq(ep->irq, ep);
    // ...
}
```

## 4.9 禁用和同步中断

```c
// 禁用中断（临时禁止设备发中断）
disable_irq(ep->irq);  // 等待当前中断处理完成
// ... 临界区 ...
enable_irq(ep->irq);

// 同步等待中断处理完成
synchronize_irq(ep->irq);  // 确保没有中断处理在运行

// 在自旋锁内自动禁用中断
spin_lock_irq(&ep->lock);   // 等于 spin_lock + local_irq_disable
// ... 临界区 ...
spin_unlock_irq(&ep->lock); // 等于 spin_unlock + local_irq_enable
```

## 4.10 MSI 中断配置（进阶）

```c
// 如果需要自定义 MSI 向量而不是用系统分配的

static int enable_msi(struct pci_dev *pdev, struct xilinx_ep_dev *ep)
{
    int ret;

    // 请求 MSI 中断（最多 1 个向量）
    ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSI);
    if (ret < 0) {
        dev_err(&pdev->dev, "Failed to allocate MSI vectors\n");
        return ret;
    }

    // 获取分配的 IRQ 号
    ep->irq = pci_irq_vector(pdev, 0);

    // 配置设备端的 MSI 地址和数据
    // 这些信息在 pdev 中，内核已自动配置设备
    // 但如果你需要手动设置：
    u64 msi_addr = pci_resource_start(pdev, 0) + 0x100;  // 假设 MSI 窗口在 BAR0
    u16 msi_data = 0x1234;

    // 写 MSI Capability 寄存器
    // （某些自定义设备需要这样做）
    // ...

    return 0;
}
```

## 4.11 调试中断问题

```bash
# 查看中断分配
cat /proc/interrupts | grep -i xilinx

# 查看 MSI 状态
cat /sys/bus/pci/devices/0000:01:00.0/msi_bus

# 查看irq信息
cat /proc/irq/$(cat /proc/interrupts | grep xilinx | awk '{print $1}' | tr -d ':')/spurious

# 使用 get_irq_info() 调试
dmesg | grep -i "xilinx\|pcie\|irq"
```

## 4.12 中断设计检查清单

| 检查项 | 说明 |
|--------|------|
| ✅ request_irq() 使用 IRQF_SHARED | 共享中断必须 |
| ✅ 读取 REG_INT_STATUS 判断来源 | 避免误处理 |
| ✅ 写 1 清除中断标志 | 大多数 FPGA 设计用此模式 |
| ✅ 中断上下文不睡眠 | 不使用 msleep, down, copy_to_user |
| ✅ synchronize_irq() 在 remove 时 | 确保没有 handler 在运行 |
| ✅ 使用底半部处理耗时操作 | 中断处理要快 |

## 4.13 下一步

下一章：[第五章：DMA 引擎](./05_dma_engine.md)