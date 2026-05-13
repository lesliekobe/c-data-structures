# TPS6594-Q1 PMIC Driver

Texas Instruments TPS6594-Q1 Automotive PMIC Driver for Cortex-R52 + SafeRTOS.

## Target

- **SoC**: Cortex-R52 (e.g., TI Jacinto TDA4x, DRA8x)
- **RTOS**: SafeRTOS
- **Interface**: I2C (400kHz)
- **Package**: VQFNP 56-pin

## Directory Structure

```
tps6594_driver/
├── include/
│   └── tps6594.h          # Public API header
├── src/
│   ├── tps6594.c           # Core driver implementation
│   └── tps6594_safertos.c  # SafeRTOS integration
├── doc/
│   └── README.md          # This file
├── examples/
│   └── main.c             # Usage example
└── Makefile
```

## Features

| Feature | Status |
|---------|--------|
| I2C communication | ✅ Complete |
| BUCK1-5 control | ✅ Complete |
| LDO1-4 control | ✅ Complete |
| Voltage set/get | ✅ Complete |
| Power state (active/standby/backup/shutdown) | ✅ Complete |
| Sleep/Wake configuration | ✅ Complete |
| Watchdog (enable/disable/pet) | ✅ Complete |
| ESM (Error Signal Monitor) | ✅ Complete |
| Interrupt handling (ISR + DPC) | ✅ Complete |
| NVM lock/CRC | ✅ Complete |
| DVS (Dynamic Voltage Scaling) | ✅ Complete |

## Default Voltage Settings

| Regulator | Voltage | Current | Purpose |
|----------|---------|---------|---------|
| BUCK1 | 3.3V | 4A | General I/O |
| BUCK2 | 1.8V | 3.5A | DDR / Analog |
| BUCK3 | 1.2V | 3.5A | ARM/R52 Core |
| BUCK4 | 1.1V | 3.5A | MCU Subsystem |
| BUCK5 | 0.9V | 2A | SRAM / PLL |
| LDO1 | 1.8V | 500mA | Analog |
| LDO2 | 1.2V | 500mA | Digital |
| LDO3 | 3.3V | 500mA | General I/O |
| LDO4 | 1.8V | 300mA | Low noise (audio) |

## Quick Start

```c
#include "tps6594.h"

tps6594_dev_t pmic;
int ret;

// Initialize with SafeRTOS integration
ret = tps6594_safertos_init(&pmic, TPS6594_I2C_ADDR_DEFAULT, 42);  // IRQ 42
if (ret != TPS6594_ERR_OK) {
    // Handle error
}

// Boot sequence - power up all rails in order
ret = tps6594_boot_sequence(&pmic, &default_config);

// Set BUCK3 voltage (dynamic)
ret = tps6594_regulator_set_voltage(&pmic, TPS6594_REGULATOR_BUCK3, 1100);

// Request sleep
tps6594_request_sleep();

// Handle wake via interrupt
void wake_handler(void) {
    tps6594_request_wake();
}
```

## Sleep/Wake Configuration

```c
tps6594_sleep_config_t sleep_cfg = {
    .sleep_enabled = true,
    .buck_mask = 0x04,        // Keep BUCK3 (core) on
    .ldo_mask = 0x02,         // Keep LDO2 (RTC) on
    .retention_enabled = true,
    .wake_pin = 1,           // GPIO1 wake
    .wake_edge = 0            // Rising edge
};

tps6594_configure_sleep(&pmic, &sleep_cfg);
tps6594_enable_wake(&pmic, 1, 0);  // Wake on GPIO1 rising edge
```

## Register Map Reference

| Register | Address | Description |
|----------|---------|-------------|
| DEVICE_ID | 0x00 | Device identification |
| INT_STS | 0x01 | Interrupt status |
| INT_MSK | 0x02 | Interrupt mask |
| PWR_STATUS | 0x04 | Power state status |
| PWR_CTRL | 0x05 | Power state control |
| BUCK1_VOLT | 0x10 | BUCK1 voltage setting |
| BUCK1_CONF | 0x11 | BUCK1 configuration |
| LDO1_VOLT | 0x28 | LDO1 voltage setting |
| SLEEP_CONF | 0xA0 | Sleep mode configuration |
| WAKE_CONF | 0xA1 | Wake configuration |
| WDOG_CONF | 0x60 | Watchdog configuration |
| ESM_CONF | 0x70 | ESM configuration |

## Safety Features

- **ASIL-D** capable (hardware integrity)
- **AEC-Q100** qualified (-40°C to +125°C)
- Watchdog with selectable Q&A mode
- Two ESM channels with PWM/Level modes
- Over/under voltage monitoring
- Over-current protection on all rails
- Thermal monitoring and shutdown

## License

GPL-2.0