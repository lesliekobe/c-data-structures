# 第四章：SafeRTOS 集成

## 4.1 集成架构

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Task                        │
│                    (用户代码)                              │
└───────────────────────────┬────────────────────────────────┘
                            │ tps6594_request_sleep()
                            │ tps6594_regulator_set_voltage()
┌───────────────────────────┼────────────────────────────────┐
│                    Driver API Layer                        │
│                            │                                │
│              tps6594_safertos_init()                       │
│              tps6594_boot_sequence()                       │
│              tps6594_watchdog_pet()                       │
│              tps6594_request_sleep()                       │
└───────────────────────────┬────────────────────────────────┘
                            │
┌───────────────────────────┼────────────────────────────────┐
│                 SafeRTOS Primitives                        │
│                                                              │
│   ┌──────────┐   ┌──────────┐   ┌──────────┐               │
│   │ DPC Task│   │ WD Task  │   │PWR Task  │               │
│   │         │   │          │   │          │               │
│   │Queue    │   │Pet WD    │   │Monitor   │               │
│   │Recv     │   │30s cycle │   │State     │               │
│   └────┬────┘   └────┬────┘   └────┬────┘               │
│        │             │             │                        │
│        ▼             ▼             ▼                        │
│   ISR → Queue → Task processing                           │
│                                                              │
│   ┌─────────────────────────────────────────────────┐       │
│   │              tps6594_safertos.c                 │       │
│   │   - ISR handler (portYIELD_FROM_ISR)          │       │
│   │   - Task creation (xTaskCreateStatic)          │       │
│   │   - Event queue (xQueueCreate)                │       │
│   └─────────────────────────────────────────────────┘       │
└──────────────────────────────────────────────────────────────┘
                            │
┌───────────────────────────┼────────────────────────────────┐
│                    Hardware Layer                          │
│                                                              │
│   I2C Bus Driver          GIC (Generic Interrupt Ctrl)     │
│   (Cortex-R52)            ARM v7-R GIC-400                   │
│                            │                                │
│                  ┌─────────┴─────────┐                     │
│                  │   tps6594_isr     │                     │
│                  └─────────┬─────────┘                     │
│                            │                                │
│                  ┌─────────┴─────────┐                     │
│                  │   TPS6594 PMIC    │                     │
│                  │   (I2C @ 400kHz)  │                     │
│                  └───────────────────┘                     │
└─────────────────────────────────────────────────────────────┘
```

## 4.2 任务配置

| 任务 | 优先级 | 栈大小 | 说明 |
|------|--------|--------|------|
| DPC Task | 5 | 512 words | 延迟中断处理 |
| Watchdog Task | 3 | 384 words | 30 秒周期喂狗 |
| Power Task | 2 | 512 words | 电源状态监控 |

## 4.3 中断处理

### ISR Handler

```c
static void tps6594_isr_handler(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    // 读取并清除中断状态
    uint8_t int_sts;
    tps6594_i2c_read(g_pmic_dev, TPS6594_REG_INT_STS, &int_sts);
    tps6594_i2c_write(g_pmic_dev, TPS6594_REG_INT_STS, int_sts);

    // 发送事件到 DPC Task
    pmic_event_t evt = { .event_type = PMIC_EVT_INT, .event_data = int_sts };
    xQueueSendFromISR(g_pmic_event_queue, &evt, &higher_priority_task_woken);

    // 请求调度（如果有更高优先级任务唤醒）
    portYIELD_FROM_ISR(higher_priority_task_woken);
}
```

### 注册中断

```c
// 连接到 ARM GIC
extern int arm_gic_register_handler(uint8_t irq_num,
                                   void (*handler)(void*),
                                   void *arg,
                                   uint8_t priority);

arm_gic_register_handler(IRQ_NUM_PMIC, tps6594_isr_handler, dev, 5);
arm_gic_enable_irq(IRQ_NUM_PMIC);
```

## 4.4 事件类型

```c
#define PMIC_EVT_INT       (1 << 0)  // 普通中断
#define PMIC_EVT_WAKE      (1 << 1)  // 唤醒事件
#define PMIC_EVT_FAULT     (1 << 2)  // 故障事件
#define PMIC_EVT_STATE_CHG  (1 << 3)  // 状态变化

typedef struct {
    uint8_t event_type;
    uint32_t event_data;
} pmic_event_t;
```

## 4.5 电源管理状态机

```c
static void tps6594_power_state_task(void *arg)
{
    tps6594_power_state_t current_state = TPS6594_STATE_ACTIVE;
    bool sleep_requested = false;

    for (;;) {
        // 检查是否有睡眠请求
        sleep_requested = xApplicationSleepRequest();

        if (sleep_requested && current_state == TPS6594_STATE_ACTIVE) {
            tps6594_enter_sleep(g_pmic_dev);
            current_state = TPS6594_STATE_SLEEP;
        }

        // 检查唤醒状态
        uint8_t wake_status;
        if (tps6594_i2c_read(g_pmic_dev, TPS6594_REG_WAKE_STATUS, &wake_status) == 0) {
            if (wake_status & 0x80) {  // Wake detected
                tps6594_exit_sleep(g_pmic_dev);
                current_state = TPS6594_STATE_ACTIVE;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

## 4.6 休眠流程

```
Application calls tps6594_request_sleep()
    │
    ▼
tps6594_enter_sleep()
    │
    ├── 读取 SLEEP_CONF
    ├── 设置 SLEEP_EN 位
    └── 设置 PWR_CTRL = STANDBY
            │
            ▼
    PMIC 进入低功耗模式
    (只保留 RTC 域、BUCK3、LDO2)
            │
            ▼
    Cortex-R52 进入 WFI (Wait For Interrupt)
            │
    Wake Interrupt (GPIO1 上升沿)
            │
            ▼
    PMIC 退出睡眠 → WAKE_STATUS |= 0x80
            │
            ▼
    ISR handler → PMIC_EVT_WAKE
            │
            ▼
    DPC Task → tps6594_exit_sleep()
            │
            ▼
    恢复正常运行
```

## 4.7 喂狗任务

```c
static void tps6594_watchdog_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        if (g_watchdog_enabled) {
            tps6594_watchdog_pet(g_pmic_dev);
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(g_watchdog_timeout_ms / 2));
    }
}
```

## 4.8 回调机制

```c
// 注册事件回调
tps6594_register_event_callback(&pmic, my_callback);

// 回调函数签名
void my_callback(uint8_t event_type, uint32_t data) {
    if (event_type == 0xFF) {  // Wake event
        printf("PMIC Wake: 0x%08X\n", data);
    }
}
```

## 4.9 平台适配

需要实现的平台函数：

```c
// I2C 总线初始化
extern int i2c_bus_init(uint32_t bus_id, uint32_t speed_hz);

// I2C 读写（带超时）
extern int i2c_bus_write_read(uint8_t addr, uint8_t wreg, uint8_t wval,
                              uint8_t rreg, uint8_t *rval, uint32_t timeout_ms);

// ARM GIC 中断注册
extern int arm_gic_register_handler(uint8_t irq_num,
                                    void (*handler)(void*),
                                    void *arg,
                                    uint8_t priority);
extern int arm_gic_enable_irq(uint8_t irq_num);
```

## 4.10 下一步

下一章：[第五章：使用示例](./05_examples.md)