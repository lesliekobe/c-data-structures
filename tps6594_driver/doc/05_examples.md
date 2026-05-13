# 第五章：使用示例

## 5.1 最小初始化

```c
#include "tps6594.h"

tps6594_dev_t pmic;

int main(void)
{
    int ret;

    // 初始化 PMIC
    ret = tps6594_safertos_init(&pmic, TPS6594_I2C_ADDR_DEFAULT, 42);
    if (ret != TPS6594_ERR_OK) {
        printf("Init failed: %s\n", tps6594_err_str(ret));
        return -1;
    }

    printf("PMIC ready, Device ID: 0x%02X\n", pmic.device_id);

    // 使用默认配置启动
    ret = tps6594_boot_sequence(&pmic, &tps6594_default_config);
    if (ret != TPS6594_ERR_OK) {
        printf("Boot failed: %s\n", tps6594_err_str(ret));
    }

    // 主循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        tps6594_watchdog_pet(&pmic);
    }

    return 0;
}
```

## 5.2 自定义电压配置

```c
// 为特定 SoC 配置电压
static const tps6594_config_t my_config = {
    .i2c_address = 0x58,

    .buck = {
        { .voltage_mv = 3300, .enable = true },  // 3.3V for IO
        { .voltage_mv = 1850, .enable = true },  // 1.85V for DDR (slightly high for stability)
        { .voltage_mv = 1150, .enable = true },  // 1.15V for R52 core (underclocked)
        { .voltage_mv = 1100, .enable = true },  // 1.1V for MCU
        { .voltage_mv = 950,  .enable = true }   // 0.95V for PLL
    },

    .ldo = {
        { .voltage_mv = 1800, .enable = true },  // 1.8V analog
        { .voltage_mv = 1200, .enable = true },  // 1.2V digital
        { .voltage_mv = 3300, .enable = true },  // 3.3V auxiliary
        { .voltage_mv = 1800, .enable = true, .low_noise_mode = true }  // 1.8V audio PLL
    },

    .watchdog = {
        .enabled = true,
        .timeout_ms = 60000,  // 60 seconds
        .qa_mode = false
    }
};
```

## 5.3 DVS（动态电压调整）

```c
void adjust_cpu_voltage(uint16_t freq_mhz)
{
    uint16_t voltage_mv;

    // 根据频率选择电压
    if (freq_mhz >= 1000) {
        voltage_mv = 1200;  // 1.2V @ 1GHz+
    } else if (freq_mhz >= 800) {
        voltage_mv = 1100;  // 1.1V @ 800MHz
    } else if (freq_mhz >= 600) {
        voltage_mv = 1000;  // 1.0V @ 600MHz
    } else {
        voltage_mv = 900;   // 0.9V @ low power
    }

    int ret = tps6594_regulator_set_voltage(&pmic,
                                           TPS6594_REGULATOR_BUCK3,
                                           voltage_mv);
    if (ret == TPS6594_ERR_OK) {
        printf("CPU voltage scaled to %dmV for %dMHz\n",
               voltage_mv, freq_mhz);
    }
}
```

## 5.4 睡眠/唤醒配置

```c
void configure_sleep_wake(void)
{
    tps6594_sleep_config_t sleep_cfg = {
        .sleep_enabled = true,
        .buck_mask = 0x04,       // Keep R52 core (BUCK3)
        .ldo_mask = 0x02,       // Keep RTC domain (LDO2)
        .retention_enabled = true,
        .wake_pin = 1,          // GPIO1 wake
        .wake_edge = 0          // Rising edge
    };

    // 配置睡眠
    tps6594_configure_sleep(&pmic, &sleep_cfg);

    // 使能 GPIO1 唤醒
    tps6594_enable_wake(&pmic, 1, 0);
}

void enter_low_power(void)
{
    printf("Entering sleep mode...\n");
    tps6594_request_sleep();
    // 此时 SoC 可以进入 WFI
}

void wake_handler(void)
{
    printf("Wake event detected!\n");
    tps6594_request_wake();
}
```

## 5.5 看门狗配置

```c
// 使能并喂狗
void init_watchdog(void)
{
    // 30 秒超时
    tps6594_watchdog_enable(&pmic, 30000);
    printf("Watchdog enabled (30s timeout)\n");
}

// 定期喂狗（在任务中）
void watchdog_task(void *arg)
{
    (void)arg;

    for (;;) {
        tps6594_watchdog_pet(&pmic);
        vTaskDelay(pdMS_TO_TICKS(15000));  // 每 15 秒
    }
}

// Q&A 看门狗模式（更安全）
void init_qa_watchdog(void)
{
    tps6594_wdog_config_t wd_cfg = {
        .enabled = true,
        .timeout_ms = 10000,
        .qa_mode = true  // 启用 Q&A 模式
    };
    // 需要额外的 Q&A 响应序列
}
```

## 5.6 故障处理

```c
void pmic_fault_callback(uint8_t fault)
{
    printf("[PMIC FAULT] 0x%02X\n", fault);

    if (fault & TPS6594_INT_TSD_FLT) {
        printf("  Thermal shutdown triggered!\n");
        // 紧急：通知系统进入安全状态
        enter_safe_state();
    }

    if (fault & TPS6594_INT_VMON_FLT) {
        printf("  Voltage fault (over/under voltage)\n");
        // 检查哪路电压故障
    }

    if (fault & TPS6594_INT_WD_FLT) {
        printf("  Watchdog timeout - system hang!\n");
        // 看门狗超时，系统需要复位
    }

    if (fault & TPS6594_INT_POR_FLT) {
        printf("  Power-on reset fault\n");
        // 上电复位异常
    }
}
```

## 5.7 读取所有轨状态

```c
void dump_all_rails(void)
{
    const char *names[] = {
        "BUCK1", "BUCK2", "BUCK3", "BUCK4", "BUCK5",
        "LDO1", "LDO2", "LDO3", "LDO4"
    };

    printf("\n=== Power Rail Status ===\n");
    printf("%-8s %-6s %-4s\n", "Rail", "Voltage", "PG");

    for (int i = 0; i < TPS6594_REGULATOR_COUNT; i++) {
        uint16_t voltage;
        bool pg_ok;

        tps6594_regulator_get_voltage(&pmic, i, &voltage);
        tps6594_regulator_get_status(&pmic, i, &pg_ok);

        printf("%-8s %-5dmV %s\n", names[i], voltage,
               pg_ok ? "OK" : "FAIL");
    }
    printf("========================\n");
}
```

## 5.8 完整应用模板

```c
#include "tps6594.h"

static tps6594_dev_t g_pmic;

void app_init(void)
{
    // 1. 初始化
    int ret = tps6594_safertos_init(&g_pmic, TPS6594_I2C_ADDR_DEFAULT, 42);
    if (ret != TPS6594_ERR_OK) {
        printf("PMIC init failed: %s\n", tps6594_err_str(ret));
        return;
    }

    // 2. 注册故障回调
    tps6594_register_event_callback(&g_pmic, pmic_fault_callback);

    // 3. 配置睡眠/唤醒
    configure_sleep_wake();

    // 4. 启动电源轨
    ret = tps6594_boot_sequence(&g_pmic, &my_config);
    if (ret != TPS6594_ERR_OK) {
        printf("Boot failed: %s\n", tps6594_err_str(ret));
        return;
    }

    // 5. 使能看门狗
    init_watchdog();

    printf("System initialized successfully\n");
}

void app_main(void)
{
    while (1) {
        // 正常工作
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 检查是否需要进入低功耗
        if (should_sleep()) {
            enter_low_power();
        }
    }
}
```

---

文档完成。如需更多内容请告诉我。