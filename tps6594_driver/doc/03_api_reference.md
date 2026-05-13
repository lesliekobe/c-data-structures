# 第三章：API 参考

## 3.1 函数索引

| 函数 | 说明 |
|------|------|
| `tps6594_init()` | 初始化设备句柄 |
| `tps6594_probe()` | 检测并验证器件 |
| `tps6594_safertos_init()` | SafeRTOS 完整初始化 |
| `tps6594_deinit()` | 清理资源 |
| `tps6594_set_config()` | 设置完整配置 |
| `tps6594_regulator_enable()` | 使能稳压器 |
| `tps6594_regulator_disable()` | 关闭稳压器 |
| `tps6594_regulator_set_voltage()` | 设置电压 |
| `tps6594_regulator_get_voltage()` | 读取电压 |
| `tps6594_regulator_get_status()` | 读取 PG 状态 |
| `tps6594_set_power_state()` | 设置电源状态 |
| `tps6594_get_power_state()` | 读取电源状态 |
| `tps6594_configure_sleep()` | 配置睡眠参数 |
| `tps6594_enter_sleep()` | 进入睡眠 |
| `tps6594_exit_sleep()` | 退出睡眠 |
| `tps6594_enable_wake()` | 使能唤醒源 |
| `tps6594_watchdog_enable()` | 使能看门狗 |
| `tps6594_watchdog_disable()` | 关闭看门狗 |
| `tps6594_watchdog_pet()` | 喂狗 |
| `tps6594_esm_enable()` | 使能 ESM |
| `tps6594_esm_disable()` | 关闭 ESM |
| `tps6594_boot_sequence()` | 启动顺序 |
| `tps6594_reg_read()` | 读寄存器 |
| `tps6594_reg_write()` | 写寄存器 |
| `tps6594_err_str()` | 错误码转字符串 |

## 3.2 tps6594_init

```c
int tps6594_init(tps6594_dev_t *dev, uint8_t i2c_addr, void *i2c_ctx);
```

初始化设备句柄。需要在调用其他 API 前调用。

**参数**：
- `dev` - 设备结构体指针
- `i2c_addr` - 7-bit I2C 从地址（默认 0x58）
- `i2c_ctx` - I2C 总线上下文（平台相关）

**返回值**：TPS6594_ERR_OK 或错误码

## 3.3 tps6594_probe

```c
int tps6594_probe(tps6594_dev_t *dev);
```

检测并验证器件。读取 DEVICE_ID (0x00)，确认值为 0x24。

**返回值**：
- TPS6594_ERR_OK - 成功，device_id 和 revid 已填充
- TPS6594_ERR_NOCOMM - I2C 通信失败
- TPS6594_ERR_NOMATCH - 器件 ID 不匹配

## 3.4 tps6594_safertos_init

```c
int tps6594_safertos_init(tps6594_dev_t *dev, uint8_t i2c_addr, uint8_t irq_line);
```

完整的 SafeRTOS 集成初始化。包括：
- I2C 总线初始化
- 设备探测
- 创建 DPC Task
- 创建 Watchdog Task
- 创建 Power Management Task
- 注册硬件中断

**参数**：
- `dev` - 设备结构体指针
- `i2c_addr` - I2C 从地址
- `irq_line` - 硬件中断号

**返回值**：TPS6594_ERR_OK 或错误码

## 3.5 稳压器控制

### tps6594_regulator_enable

```c
int tps6594_regulator_enable(tps6594_dev_t *dev, tps6594_regulator_id_t id);
```

使能指定的稳压器（BUCK1-5 或 LDO1-4）。

```c
// 使能 R52 核心电源
tps6594_regulator_enable(&pmic, TPS6594_REGULATOR_BUCK3);
```

### tps6594_regulator_set_voltage

```c
int tps6594_regulator_set_voltage(tps6594_dev_t *dev,
                                  tps6594_regulator_id_t id,
                                  uint16_t voltage_mv);
```

设置稳压器输出电压。

```c
// 将 R52 核心电压从 1.2V 降到 1.1V (DVS)
tps6594_regulator_set_voltage(&pmic, TPS6594_REGULATOR_BUCK3, 1100);

// 恢复到 1.2V
tps6594_regulator_set_voltage(&pmic, TPS6594_REGULATOR_BUCK3, 1200);
```

**电压范围**：
- BUCK: 0.3V ~ 3.34V (5mV 步进)
- LDO: 0.6V ~ 3.3V (50mV 步进)
- LDO4 (低噪声): 0.6V ~ 3.3V (25mV 步进)

### tps6594_regulator_get_status

```c
int tps6594_regulator_get_status(tps6594_dev_t *dev,
                                 tps6594_regulator_id_t id,
                                 bool *is_ok);
```

读取 Power Good 状态。检查电压是否在正常范围内。

```c
bool pg_ok;
tps6594_regulator_get_status(&pmic, TPS6594_REGULATOR_BUCK3, &pg_ok);
if (pg_ok) {
    printf("BUCK3 voltage OK\n");
} else {
    printf("BUCK3 voltage FAULT\n");
}
```

## 3.6 电源状态

### tps6594_set_power_state

```c
int tps6594_set_power_state(tps6594_dev_t *dev, tps6594_power_state_t state);
```

设置系统电源状态。

```c
// 进入待机
tps6594_set_power_state(&pmic, TPS6594_STATE_STANDBY);

// 完全关机
tps6594_set_power_state(&pmic, TPS6594_STATE_SHUTDOWN);
```

**状态值**：
- `TPS6594_STATE_ACTIVE` - 正常运行
- `TPS6594_STATE_STANDBY` - 待机
- `TPS6594_STATE_BACKUP` - 备份
- `TPS6594_STATE_SHUTDOWN` - 关机

## 3.7 睡眠/唤醒

### tps6594_configure_sleep

```c
int tps6594_configure_sleep(tps6594_dev_t *dev, const tps6594_sleep_config_t *cfg);
```

配置睡眠模式的保留电源轨和唤醒源。

```c
tps6594_sleep_config_t cfg = {
    .sleep_enabled = true,
    .buck_mask = 0x04,       // BUCK3 (R52 core) 保持
    .ldo_mask = 0x02,        // LDO2 (RTC) 保持
    .retention_enabled = true,
    .wake_pin = 1,           // GPIO1 唤醒
    .wake_edge = 0           // 上升沿
};
tps6594_configure_sleep(&pmic, &cfg);
```

### tps6594_enter_sleep / tps6594_exit_sleep

```c
// 进入睡眠（由应用调用）
tps6594_enter_sleep(&pmic);

// 退出睡眠（由唤醒中断调用）
tps6594_exit_sleep(&pmic);
```

### tps6594_request_sleep / tps6594_request_wake

```c
// SafeRTOS 集成中的简化 API
void tps6594_request_sleep(void);   // 请求系统睡眠
void tps6594_request_wake(void);    // 请求系统唤醒
```

## 3.8 看门狗

### tps6594_watchdog_enable

```c
int tps6594_watchdog_enable(tps6594_dev_t *dev, uint16_t timeout_ms);
```

使能并配置看门狗。

```c
// 使能 30 秒看门狗
tps6594_watchdog_enable(&pmic, 30000);
```

### tps6594_watchdog_pet

```c
int tps6594_watchdog_pet(tps6594_dev_t *dev);
```

喂狗（重置看门狗计时器）。

```c
// 在 Watchdog Task 中定期调用
while (1) {
    tps6594_watchdog_pet(&pmic);
    vTaskDelay(pdMS_TO_TICKS(15000));  // 每 15 秒
}
```

## 3.9 启动顺序

### tps6594_boot_sequence

```c
int tps6594_boot_sequence(tps6594_dev_t *dev, const tps6594_config_t *cfg);
```

完整的上电启动顺序：

1. 配置所有稳压器（禁用状态）
2. 按顺序使能 BUCK1 → BUCK3 → BUCK4 → BUCK5 → BUCK2
3. 使能 LDOs
4. 验证所有 Power Good
5. 使能看门狗
6. 使能 ESM

```c
// 完整启动
ret = tps6594_boot_sequence(&pmic, &tda4vm_config);
if (ret != TPS6594_ERR_OK) {
    printf("Boot failed: %s\n", tps6594_err_str(ret));
    // 处理错误
}
```

## 3.10 错误码

| 错误码 | 值 | 说明 |
|--------|-----|------|
| TPS6594_ERR_OK | 0 | 成功 |
| TPS6594_ERR_INVAL | -1 | 无效参数 |
| TPS6594_ERR_NOCOMM | -2 | I2C 通信错误 |
| TPS6594_ERR_TIMEOUT | -3 | 操作超时 |
| TPS6594_ERR_NOMATCH | -4 | 器件 ID 不匹配 |
| TPS6594_ERR_FAULT | -5 | 硬件故障 |
| TPS6594_ERR_PROTECTED | -6 | NVM 锁定 |
| TPS6594_ERR_BADCRC | -7 | CRC 错误 |

## 3.11 下一步

下一章：[第四章：SafeRTOS 集成](./04_safertos_integration.md)