# 第二章：寄存器映射

## 2.1 寄存器概览

| 地址 | 名称 | 说明 |
|------|------|------|
| 0x00 | DEVICE_ID | 器件 ID (0x24 = TPS6594) |
| 0x01 | INT_STS | 中断状态（写1清零） |
| 0x02 | INT_MSK | 中断屏蔽 |
| 0x03 | INT_MASK_STS | 屏蔽后的中断状态 |
| 0x04 | PWR_STATUS | 电源状态 |
| 0x05 | PWR_CTRL | 电源控制 |
| 0x06 | INT_STS2 | 扩展中断状态 |
| 0x07 | FAULT_STS | 故障状态 |
| 0x08 | I2C_ADDR | I2C 地址配置 |
| 0x09 | GPIO_CONF | GPIO 配置 |
| 0x0A | DEV_CONF | 器件配置 |
| 0x0B | SYNC_CTRL | 同步控制 |
| 0x10-0x13 | BUCK1 | BUCK1 电压/配置/控制 |
| 0x14-0x17 | BUCK2 | BUCK2 ... |
| 0x18-0x1B | BUCK3 | BUCK3 ... |
| 0x1C-0x1F | BUCK4 | BUCK4 ... |
| 0x20-0x23 | BUCK5 | BUCK5 ... |
| 0x28-0x2B | LDO1 | LDO1 ... |
| 0x2C-0x2F | LDO2 | LDO2 ... |
| 0x30-0x33 | LDO3 | LDO3 ... |
| 0x34-0x37 | LDO4 | LDO4 ... |
| 0x40 | SEQ_CONF | 序列配置 |
| 0x41 | SEQ_STATUS | 序列状态 |
| 0x42 | PG_SHOW_INFO | Power Good 显示信息 |
| 0x43 | PG_STATUS | Power Good 状态 |
| 0x50-0x5B | GPIO1-6 | GPIO 输入/配置 |
| 0x60 | WDOG_CONF | 看门狗配置 |
| 0x61 | WDOG_RESPONSE | 看门狗响应 |
| 0x70 | ESM_CONF | ESM 配置 |
| 0x71 | ESM_STATUS | ESM 状态 |
| 0x80 | BACKUP_CONF | 备份配置 |
| 0x81 | PMIC_CONF | PMIC 全局配置 |
| 0x82 | MAIN_STATUS | 主状态 |
| 0x90 | NVM_ADDR | NVM 地址 |
| 0x91 | NVM_DATA | NVM 数据 |
| 0x92 | NVM_LOCK | NVM 锁 |
| 0xA0 | SLEEP_CONF | 睡眠配置 |
| 0xA1 | WAKE_CONF | 唤醒配置 |
| 0xA2 | WAKE_STATUS | 唤醒状态 |

## 2.2 详细寄存器定义

### 0x00 DEVICE_ID

| Bit | 名称 | 说明 |
|-----|------|------|
| 7:0 | DEVICE_ID | 0x24 = TPS6594, 0x30 = TPS6594+FPGA |

### 0x01 INT_STS（中断状态）

| Bit | 名称 | 说明 |
|-----|------|------|
| 0 | POR_FLT | 上电复位故障 |
| 1 | VMON_FLT | 电压监控故障（欠压/过压） |
| 2 | TSD_FLT | 热关断故障 |
| 3 | WD_FLT | 看门狗超时故障 |
| 4 | FSM_FLT | 状态机故障 |
| 5 | CONF_FLT | 配置故障 |
| 6 | REG_FLT | 调节器故障 |
| 7 | GPIO_FLT | GPIO 故障 |

**写 1 清零**

### 0x04 PWR_STATUS（电源状态）

| Bit | 名称 | 说明 |
|-----|------|------|
| 3:0 | PWR_STATE | 0x0=Shutdown, 0x1=Backup, 0x2=Standby, 0x3=Active |
| 4 | SEQ_EN | 序列使能 |
| 5 | RDY | 器件就绪 |
| 6 | OFF_STATE | 关机状态 |
| 7 | BACKUP_STATE | 备份状态 |

### 0x05 PWR_CTRL（电源控制）

| Bit | 名称 | 说明 |
|-----|------|------|
| 1:0 | PWR_CTRL | 00=Active, 01=Standby, 10=Backup, 11=Shutdown |
| 7:2 | Reserved | |

### 0x10-0x12 BUCK1

| 寄存器 | Bit | 说明 |
|--------|-----|------|
| 0x10 VOLT | 7:0 | 电压设置值 (5mV step, 0x00=0.3V, 0xFF=3.345V) |
| 0x11 CONF | 7 | EN: 使能位 |
| | 6 | FPWM: 强制 PWM 模式 |
| | 5:3 | FREQ: 2/2.2/2.4/4/4.4 MHz |
| | 2:0 | PHASE: 相位 (0-4) |
| 0x12 CTRL | 7 | DISCHG: 放电使能 |
| | 6 | VSEL: 电压选择源 (0=NVM, 1=I2C) |
| | 5 | ILIM: 电流限制 (读寄存器) |
| | 4 | VOK: 电压 OK 状态 |
| 0x13 VMON | 7 | VMON_FLT 状态 |
| | 6:0 | 电压监测值 |

### 0x28-0x2A LDO1

| 寄存器 | Bit | 说明 |
|--------|-----|------|
| 0x28 VOLT | 7:0 | 电压设置值 (50mV step, 0x00=0.6V, 0x36=3.3V) |
| 0x29 CONF | 7 | EN: 使能位 |
| | 6 | BYPASS: 旁路模式 |
| | 5 | STPDWN: 25mV 步进 (LDO4 专用) |
| | 4 | ILIM: 电流限制状态 |
| 0x2A CTRL | 7 | DISCHG: 放电使能 |
| | 4 | VOK: 电压 OK |

### 0x60 WDOG_CONF（看门狗配置）

| Bit | 名称 | 说明 |
|-----|------|------|
| 0 | WD_EN | 看门狗使能 |
| 1 | RST | 软件复位（toggle） |
| 2 | QA_MODE | Q&A 看门狗模式 |
| 6:3 | WD_TIME | 超时时间 (ms = WD_TIME * 0.5) |
| 7 | Reserved | |

### 0x70 ESM_CONF（错误信号监控配置）

| Bit | 名称 | 说明 |
|-----|------|------|
| 1:0 | MODE | 0=禁用, 1=PWM, 2=Level |
| 7 | EN | ESM 使能 |

### 0xA0 SLEEP_CONF（睡眠配置）

| Bit | 名称 | 说明 |
|-----|------|------|
| 0 | SLEEP_EN | 睡眠使能 |
| 5:1 | BUCK_SLP_MASK | 睡眠中保持的 BUCK (bit0=BUCK1...) |
| 6 | RET_EN | 保持模式使能 |
| 7 | LDO_SLP_MASK | 睡眠中保持的 LDO (bit0=LDO1...) |

### 0xA1 WAKE_CONF（唤醒配置）

| Bit | 名称 | 说明 |
|-----|------|------|
| 2:0 | WAKE_PIN | GPIO 引脚用于唤醒 |
| 4:3 | WAKE_EDGE | 00=Rising, 01=Falling, 1x=Both |
| 7 | WAKE_EN | 唤醒使能 |

## 2.3 电压设置计算

```c
// BUCK 电压 (5mV 步进)
reg = (voltage_mv - 300) / 5;
voltage_mv = 300 + reg * 5;

// LDO 电压 (50mV 步进)
reg = (voltage_mv - 600) / 50;
voltage_mv = 600 + reg * 50;

// LDO4 低噪声模式 (25mV 步进)
reg = (voltage_mv - 600) / 25;
voltage_mv = 600 + reg * 25;
```

## 2.4 NVM Shadow 寄存器

器件在上电时从内部 NVM 加载默认配置到 shadow 寄存器。如果需要修改并保存：

```c
// 修改寄存器后写入 NVM
tps6594_i2c_write(dev, 0x90, addr);  // NVM_ADDR
tps6594_i2c_write(dev, 0x91, val);  // NVM_DATA
tps6594_i2c_write(dev, 0x92, 0x80); // NVM_LOCK (开始写入)

// 检查写入是否完成（CRC 通过）
bool crc_ok;
tps6594_nvm_crc_check(dev, &crc_ok);
```

## 2.5 下一步

下一章：[第三章：API 参考](./03_api_reference.md)