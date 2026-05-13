# 第一章：驱动架构

## 1.1 系统架构

```
┌──────────────────────────────────────────────────────────────────┐
│                      Application Layer                           │
│                                                                  │
│   SOC Bootloader / OS     tps6594_boot_sequence()                │
│   Power Management Task    tps6594_request_sleep()               │
│   User Callback            pmic_event_handler()                │
│                              │                                    │
└──────────────────────────────┼──────────────────────────────────┘
                               │
┌──────────────────────────────┼──────────────────────────────────┐
│                      SafeRTOS Layer                              │
│                                                                  │
│   ┌─────────────────┐    ┌─────────────────┐    ┌───────────┐ │
│   │   DPC Task       │    │  Watchdog Task  │    │ Power Task │ │
│   │  (ISR defer)    │    │   (pet WD)      │    │(state mgr) │ │
│   └────────┬─────────┘    └────────┬─────────┘    └─────┬─────┘ │
│            │                       │                    │        │
│            ▼                       ▼                    ▼        │
│   ┌────────────────────────────────────────────────────────┐  │
│   │              tps6594_safertos.c                         │  │
│   │   - ISR handler (hardware interrupt)                    │  │
│   │   - DPC task (deferred procedure call)                │  │
│   │   - Watchdog petting task                             │  │
│   │   - Sleep/Wake state machine                           │  │
│   └────────┬───────────────────────────────────────┬────────┘  │
└────────────┼───────────────────────────────────────┼───────────┘
             │                                       │
┌────────────┼───────────────────────────────────────┼───────────┐
│            ▼                                       ▼           │
│   ┌────────────────────────────────────────────────────────┐  │
│   │              tps6594.c (Platform-agnostic driver)      │  │
│   │                                                        │  │
│   │  - tps6594_init/probe()     - Regulator control        │  │
│   │  - tps6594_regulator_*()    - Power state control      │  │
│   │  - tps6594_sleep/wake_*()   - Interrupt handling       │  │
│   │  - tps6594_watchdog_*()     - ESM/WD control          │  │
│   └────────┬───────────────────────────────────────┬────────┘  │
└────────────┼───────────────────────────────────────┼───────────┘
             │
┌────────────┼───────────────────────────────────────┼───────────┐
│            ▼                                       ▼           │
│   ┌─────────────────┐              ┌──────────────────────┐   │
│   │  I2C Bus Driver  │              │   TPS6594 PMIC      │   │
│   │  (Cortex-R52)    │◄────────────►│   (Hardware)         │   │
│   │  - 400kHz I2C    │    I2C       │   - 5 BUCK, 4 LDO   │   │
│   │  - GIC interrupt │              │   - Watchdog/ESM    │   │
│   └─────────────────┘              └──────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## 1.2 核心模块

### tps6594.c（平台无关）

| 函数组 | 说明 |
|--------|------|
| `tps6594_init/probe` | 设备初始化和 ID 检测 |
| `tps6594_regulator_*` | BUCK/LDO 电压控制 |
| `tps6594_set_power_state` | 系统电源状态切换 |
| `tps6594_sleep/wake_*` | 休眠唤醒配置 |
| `tps6594_watchdog_*` | 看门狗控制 |
| `tps6594_esm_*` | 错误信号监控 |
| `tps6594_reg_read/write` | 寄存器读写 |

### tps6594_safertos.c（SafeRTOS 集成）

| 模块 | 说明 |
|------|------|
| ISR Handler | 硬件中断处理（Context: ISR） |
| DPC Task | 延迟处理（Context: Task） |
| Watchdog Task | 定期 pet 看门狗 |
| Power Task | 电源状态监控 |
| Event Queue | ISR → Task 事件传递 |

## 1.3 数据流

### 启动顺序

```
1. SoC boot → tps6594_safertos_init()
2. I2C init → tps6594_init()
3. tps6594_probe() → 读取 DEVICE_ID (0x00)
4. 创建 DPC Task, Watchdog Task, Power Task
5. 注册 GPIO 中断
6. tps6594_boot_sequence() → 按顺序使能各路 rail
7. tps6594_regulator_get_status() → 确认 Power Good
8. 系统就绪
```

### 中断处理流程

```
硬件中断 (GPIO wake / ESM fault / WD timeout)
    │
    ▼
tps6594_isr_handler() [ISR Context]
    │
    ├── 读取 INT_STS (0x01)
    ├── 清除中断标志 (写回)
    ├── 发送事件到 g_pmic_event_queue
    └── portYIELD_FROM_ISR()
            │
            ▼
tps6594_dpc_task() [Task Context]
    │
    ├── xQueueReceive()
    │
    ├── PMIC_EVT_WAKE → tps6594_request_wake()
    ├── PMIC_EVT_FAULT → fault_handler()
    └── PMIC_EVT_INT → g_event_cb()
```

## 1.4 安全机制

```
Watchdog Task (30s interval)
    │
    ├── tps6594_watchdog_pet()
    │       └── 写入 WDOG_CONF (0x60), toggle RST bit
    │
    └── 超时 → WDOG_FLT 中断 → ISR → fault_handler()
            │
            └── 可选：系统复位或安全关闭
```

### ESM (Error Signal Monitor)

```
ESM 模块监控外部信号（如 R52 故障输出）
    │
    ├── 配置: ESM_CONF (0x70)
    │       ├── mode 1 = PWM (默认)
    │       └── mode 2 = Level
    │
    └── 故障 → ESM_FLT 中断 → fault_handler()
```

## 1.5 休眠/唤醒状态机

```
                    ┌─────────────────┐
                    │     ACTIVE      │
                    │  (正常运行)      │
                    └────────┬────────┘
                             │
              tps6594_request_sleep()
                             │
                             ▼
                    ┌─────────────────┐
                    │     STANDBY     │
                    │ (配置已加载)     │
                    └────────┬────────┘
                             │
                   (sleep_enabled && PWR_CTRL=0x01)
                             │
                             ▼
                    ┌─────────────────┐
                    │     SLEEP       │
                    │  只保留 RTC 域   │
                    │  BUCK3 + LDO2   │
                    └────────┬────────┘
                             │
                  Wake Interrupt (GPIO1)
                             │
                             ▼
                    ┌─────────────────┐
                    │     WAKING      │
                    │ (恢复配置)       │
                    └────────┬────────┘
                             │
                             ▼
                    ┌─────────────────┐
                    │     ACTIVE      │
                    └─────────────────┘
```

## 1.6 电压域配置

```
典型 Jacinto TDA4VM 电源树：

        TPS6594
          │
    ┌─────┼─────┬─────┬─────┐
    │     │     │     │     │
  BUCK1  BUCK2 BUCK3 BUCK4 BUCK5
  3.3V   1.8V  1.2V  1.1V  0.9V
   │     │     │     │     │
  IO    DDR   R52   MCU   PLL/
  3.3V  VDDQ  Core Island SRAM
         │     │
        LDO1  LDO2
        1.8V  1.2V
         │     │
       Analog RTC
```

## 1.7 下一步

下一章：[第二章：寄存器映射](./02_register_map.md)