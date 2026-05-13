/**
 * @file tps6594.h
 * @brief TPS6594-Q1 PMIC Driver Public Header
 *
 * Driver for Texas Instruments TPS6594-Q1 Automotive PMIC
 * Target: Cortex-R52 with SafeRTOS
 * Interface: I2C
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#ifndef TPS6594_H_
#define TPS6594_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * Device Identification
 * ======================================================================== */

#define TPS6594_I2C_ADDR_DEFAULT     0x58    /* 7-bit I2C address */
#define TPS6594_I2C_ADDR_ALT1        0x59    /* Alternate address */
#define TPS6594_I2C_ADDR_ALT2        0x5A    /* Alternate address */

/* Device ID register values */
#define TPS6594_DEVICE_ID_PMIC       0x24    /* TPS6594 */
#define TPS6594_DEVICE_ID_FPGA      0x30    /* TPS6594 with FPGA */

/* ========================================================================
 * Register Map (Base Address 0x00)
 * ======================================================================== */

/* Device Info */
#define TPS6594_REG_DEVICE_ID        0x00
#define TPS6594_REG_INT_STS         0x01
#define TPS6594_REG_INT_MSK         0x02
#define TPS6594_REG_INT_MASK_STS    0x03
#define TPS6594_REG_PWR_STATUS       0x04
#define TPS6594_REG_PWR_CTRL         0x05
#define TPS6594_REG_INT_STS2         0x06
#define TPS6594_REG_FAULT_STS        0x07

/* Control Registers */
#define TPS6594_REG_I2C_ADDR         0x08
#define TPS6594_REG_GPIO_CONF         0x09
#define TPS6594_REG_DEV_CONF          0x0A
#define TP6594_REG_SYNC_CTRL         0x0B

/* BUCK Regulators (BUCK1-BUCK5) */
#define TPS6594_REG_BUCK1_VOLT      0x10
#define TPS6594_REG_BUCK1_CONF      0x11
#define TPS6594_REG_BUCK1_CTRL      0x12
#define TPS6594_REG_BUCK1_VMON       0x13

#define TPS6594_REG_BUCK2_VOLT      0x14
#define TPS6594_REG_BUCK2_CONF      0x15
#define TPS6594_REG_BUCK2_CTRL      0x16

#define TPS6594_REG_BUCK3_VOLT      0x18
#define TPS6594_REG_BUCK3_CONF      0x19
#define TPS6594_REG_BUCK3_CTRL      0x1A

#define TPS6594_REG_BUCK4_VOLT      0x1C
#define TPS6594_REG_BUCK4_CONF      0x1D
#define TPS6594_REG_BUCK4_CTRL      0x1E

#define TPS6594_REG_BUCK5_VOLT      0x20
#define TPS6594_REG_BUCK5_CONF      0x21
#define TPS6594_REG_BUCK5_CTRL      0x22

/* LDO Regulators (LDO1-LDO4) */
#define TPS6594_REG_LDO1_VOLT       0x28
#define TPS6594_REG_LDO1_CONF        0x29
#define TPS6594_REG_LDO1_CTRL       0x2A

#define TPS6594_REG_LDO2_VOLT       0x2C
#define TPS6594_REG_LDO2_CONF        0x2D
#define TPS6594_REG_LDO2_CTRL       0x2E

#define TPS6594_REG_LDO3_VOLT       0x30
#define TPS6594_REG_LDO3_CONF        0x31
#define TPS6594_REG_LDO3_CTRL       0x32

#define TPS6594_REG_LDO4_VOLT       0x34
#define TPS6594_REG_LDO4_CONF        0x35
#define TPS6594_REG_LDO4_CTRL       0x36

/* Power Sequence */
#define TPS6594_REG_SEQ_CONF         0x40
#define TPS6594_REG_SEQ_STATUS      0x41
#define TPS6594_REG_PG_SHOW_INFO     0x42
#define TPS6594_REG_PG_STATUS        0x43

/* GPIO Registers */
#define TPS6594_REG_GPIO1_IN         0x50
#define TPS6594_REG_GPIO1_CONF       0x51
#define TPS6594_REG_GPIO2_IN         0x52
#define TPS6594_REG_GPIO2_CONF       0x53
#define TPS6594_REG_GPIO3_IN         0x54
#define TPS6594_REG_GPIO3_CONF       0x55
#define TPS6594_REG_GPIO4_IN         0x56
#define TPS6594_REG_GPIO4_CONF       0x57
#define TPS6594_REG_GPIO5_IN         0x58
#define TPS6594_REG_GPIO5_CONF       0x59
#define TPS6594_REG_GPIO6_IN         0x5A
#define TPS6594_REG_GPIO6_CONF       0x5B

/* Watchdog */
#define TPS6594_REG_WDOG_CONF        0x60
#define TPS6594_REG_WDOG_RESPONSE     0x61

/* ESM (Error Signal Monitor) */
#define TPS6594_REG_ESM_CONF         0x70
#define TPS6594_REG_ESM_STATUS       0x71

/* Memory/Backup */
#define TPS6594_REG_BACKUP_CONF     0x80
#define TPS6594_REG_PMIC_CONF        0x81
#define TPS6594_REG_MAIN_STATUS       0x82

/* NVM Shadow Registers */
#define TPS6594_REG_NVM_ADDR         0x90
#define TPS6594_REG_NVM_DATA          0x91
#define TPS6594_REG_NVM_LOCK         0x92

/* Sleep/Wake Control */
#define TPS6594_REG_SLEEP_CONF       0xA0
#define TPS6594_REG_WAKE_CONF        0xA1
#define TPS6594_REG_WAKE_STATUS      0xA2

/* ========================================================================
 * Bit Masks and Shift Definitions
 * ======================================================================== */

/* INT_STS bits */
#define TPS6594_INT_POR_FLT         (1 << 0)
#define TPS6594_INT_VMON_FLT        (1 << 1)
#define TPS6594_INT_TSD_FLT          (1 << 2)
#define TPS6594_INT_WD_FLT          (1 << 3)
#define TPS6594_INT_FSM_FLT         (1 << 4)
#define TPS6594_INT_CONF_FLT        (1 << 5)
#define TPS6594_INT_REG_FLT         (1 << 6)
#define TPS6594_INT_GPIO_FLT        (1 << 7)

/* PWR_STATUS bits */
#define TPS6594_PWR_STATE_MASK       0x0F
#define TPS6594_PWR_STATE_SHUTDOWN   0x00
#define TPS6594_PWR_STATE_BACKUP    0x01
#define TPS6594_PWR_STATE_STANDBY   0x02
#define TPS6594_PWR_STATE_ACTIVE     0x03

/* BUCK_CONF bits */
#define TPS6594_BUCK_EN             (1 << 7)
#define TPS6594_BUCK_FPWM           (1 << 6)
#define TPS6594_BUCK_FREQ_MASK      (0x07 << 3)
#define TPS6594_BUCK_FREQ_2MHZ      (0x00 << 3)
#define TPS6594_BUCK_FREQ_2_2MHZ    (0x01 << 3)
#define TPS6594_BUCK_FREQ_2_4MHZ    (0x02 << 3)
#define TPS6594_BUCK_FREQ_4MHZ      (0x04 << 3)
#define TPS6594_BUCK_FREQ_4_4MHZ   (0x05 << 3)
#define TPS6594_BUCK_PHASE_MASK     (0x07 << 0)

/* LDO_CONF bits */
#define TPS6594_LDO_EN              (1 << 7)
#define TPS6594_LDO_BYPASS          (1 << 6)
#define TPS6594_LDO_STEPDOWN         (1 << 5)  /* 50mV steps vs 25mV */

/* SLEEP_CONF bits */
#define TPS6594_SLEEP_EN            (1 << 0)
#define TPS6594_SLEEP_BUCK_MASK     0x1F      /* BUCK1-5 enable in sleep */
#define TPS6594_SLEEP_LDO_MASK       0x0F      /* LDO1-4 enable in sleep */
#define TPS6594_SLEEP_RET_EN        (1 << 6)   /* Retention mode */

/* ========================================================================
 * Voltage Settings (BUCK)
 * ======================================================================== */

/* BUCK voltage ranges */
#define TPS6594_BUCK_VOLT_MIN_MV    300     /* 0.3V */
#define TPS6594_BUCK_VOLT_MAX_MV    3340    /* 3.34V */
#define TPS6594_BUCK_STEP_MV         5       /* 5mV step */
#define TPS6594_BUCK_STEP_ALT_MV     10      /* 10mV step */
#define TPS6594_BUCK_STEP_ALT2_MV    20      /* 20mV step */

/* Voltage to register value conversion */
#define TPS6594_BUCK_VOLT_TO_REG(v_mv)  \
    ((v_mv - TPS6594_BUCK_VOLT_MIN_MV) / TPS6594_BUCK_STEP_MV)

#define TPS6594_BUCK_REG_TO_VOLT(reg)  \
    (TPS6594_BUCK_VOLT_MIN_MV + (reg) * TPS6594_BUCK_STEP_MV)

/* ========================================================================
 * Voltage Settings (LDO)
 * ======================================================================== */

#define TPS6594_LDO_VOLT_MIN_MV      600     /* 0.6V */
#define TPS6594_LDO_VOLT_MAX_MV     3300    /* 3.3V */
#define TPS6594_LDO_STEP_MV          50     /* 50mV step (linear mode) */
#define TPS6594_LDO_STEP_ALT_MV      25     /* 25mV step (low noise LDO4) */

#define TPS6594_LDO_VOLT_TO_REG(v_mv)  \
    ((v_mv - TPS6594_LDO_VOLT_MIN_MV) / TPS6594_LDO_STEP_MV)

#define TPS6594_LDO_REG_TO_VOLT(reg)  \
    (TPS6594_LDO_VOLT_MIN_MV + (reg) * TPS6594_LDO_STEP_MV)

/* ========================================================================
 * Regulator IDs
 * ======================================================================== */

typedef enum {
    TPS6594_REGULATOR_BUCK1 = 0,
    TPS6594_REGULATOR_BUCK2,
    TPS6594_REGULATOR_BUCK3,
    TPS6594_REGULATOR_BUCK4,
    TPS6594_REGULATOR_BUCK5,
    TPS6594_REGULATOR_LDO1,
    TPS6594_REGULATOR_LDO2,
    TPS6594_REGULATOR_LDO3,
    TPS6594_REGULATOR_LDO4,
    TPS6594_REGULATOR_COUNT
} tps6594_regulator_id_t;

/* ========================================================================
 * Power State Definitions
 * ======================================================================== */

typedef enum {
    TPS6594_STATE_ACTIVE = 0,
    TPS6594_STATE_STANDBY,
    TPS6594_STATE_BACKUP,
    TPS6594_STATE_SHUTDOWN,
    TPS6594_STATE_SLEEP,
    TPS6594_STATE_WAKE,
    TPS6594_STATE_UNKNOWN
} tps6594_power_state_t;

/* ========================================================================
 * Configuration Structures
 * ======================================================================== */

/* BUCK regulator configuration */
typedef struct {
    uint16_t voltage_mv;           /* Target voltage in mV */
    bool enable;                    /* Enable on init */
    bool fpwm_mode;                 /* Force PWM mode */
    uint8_t freq;                   /* Switching frequency */
    uint8_t phase;                  /* Phase for multi-phase */
    uint8_t current_limit_ma;       /* Current limit in mA */
    bool discharge_enable;           /* Discharge when disabled */
    bool dvs_enable;                 /* Dynamic voltage scaling */
} tps6594_buck_config_t;

/* LDO regulator configuration */
typedef struct {
    uint16_t voltage_mv;            /* Target voltage in mV */
    bool enable;                   /* Enable on init */
    bool bypass_mode;               /* LDO bypass (if supported) */
    bool low_noise_mode;            /* Low noise (LDO4 only) */
    uint8_t current_limit_ma;      /* Current limit in mA */
} tps6594_ldo_config_t;

/* Sleep configuration */
typedef struct {
    bool sleep_enabled;
    uint8_t buck_mask;              /* Which BUCKs active in sleep */
    uint8_t ldo_mask;              /* Which LDOs active in sleep */
    bool retention_enabled;
    uint8_t wake_pin;              /* GPIO pin for wake */
    uint8_t wake_edge;             /* 0=rising, 1=falling, 2=both */
} tps6594_sleep_config_t;

/* Watchdog configuration */
typedef struct {
    bool enabled;
    uint16_t timeout_ms;           /* Timeout in ms */
    bool qa_mode;                  /* Q&A watchdog mode */
} tps6594_wdog_config_t;

/* Full device configuration */
typedef struct {
    uint8_t i2c_address;            /* I2C slave address */

    /* Regulator configurations */
    tps6594_buck_config_t buck[5];
    tps6594_ldo_config_t ldo[4];

    /* Sleep/Wake */
    tps6594_sleep_config_t sleep;

    /* Watchdog */
    tps6594_wdog_config_t watchdog;

    /* ESM */
    uint8_t esm_mode;               /* 0=disabled, 1=PWM, 2=level */

    /* General */
    bool enable_gpio_wake;         /* Use GPIO for wake */
    uint8_t gpio_wake_pin;          /* GPIO pin number */
} tps6594_config_t;

/* Runtime regulator state */
typedef struct {
    bool is_enabled;
    uint16_t voltage_mv;
    bool voltage_ok;                 /* Power good status */
    bool in_sleep_mode;
} tps6594_regulator_state_t;

/* Driver instance */
typedef struct {
    uint8_t i2c_addr;
    bool initialized;
    tps6594_power_state_t current_state;
    tps6594_regulator_state_t regulators[TPS6594_REGULATOR_COUNT];
    void *i2c_context;              /* I2C bus adapter context */
    uint16_t device_id;            /* Read from chip */
    uint8_t revid;                  /* Revision ID */
} tps6594_dev_t;

/* ========================================================================
 * Callback Types
 * ======================================================================== */

typedef void (*tps6594_event_callback_t)(uint8_t event_type, uint32_t data);
typedef void (*tps6594_wake_callback_t)(void);
typedef void (*tps6594_fault_callback_t)(uint8_t fault_type);

/* ========================================================================
 * Error Codes
 * ======================================================================== */

#define TPS6594_ERR_OK           0
#define TPS6594_ERR_INVAL        -1    /* Invalid argument */
#define TPS6594_ERR_NOCOMM       -2    /* I2C communication error */
#define TPS6594_ERR_TIMEOUT      -3    /* Operation timeout */
#define TPS6594_ERR_NOMATCH      -4    /* Register not match */
#define TPS6594_ERR_FAULT        -5    /* Hardware fault detected */
#define TPS6594_ERR_PROTECTED     -6    /* NVM locked */
#define TPS6594_ERR_BADCRC       -7    /* CRC error */
#define TPS6594_ERR_TIMEOUTWAIT  -8    /* Timed out waiting */

/* ========================================================================
 * API Functions
 * ======================================================================== */

/* Initialization and probe */
int tps6594_init(tps6594_dev_t *dev, uint8_t i2c_addr, void *i2c_ctx);
int tps6594_deinit(tps6594_dev_t *dev);
int tps6594_probe(tps6594_dev_t *dev);
int tps6594_set_config(tps6594_dev_t *dev, const tps6594_config_t *cfg);
int tps6594_get_config(tps6594_dev_t *dev, tps6594_config_t *cfg);

/* Regulator control */
int tps6594_regulator_enable(tps6594_dev_t *dev, tps6594_regulator_id_t id);
int tps6594_regulator_disable(tps6594_dev_t *dev, tps6594_regulator_id_t id);
int tps6594_regulator_set_voltage(tps6594_dev_t *dev, tps6594_regulator_id_t id,
                                   uint16_t voltage_mv);
int tps6594_regulator_get_voltage(tps6594_dev_t *dev, tps6594_regulator_id_t id,
                                  uint16_t *voltage_mv);
int tps6594_regulator_get_status(tps6594_dev_t *dev, tps6594_regulator_id_t id,
                                bool *is_ok);

/* Power state control */
int tps6594_set_power_state(tps6594_dev_t *dev, tps6594_power_state_t state);
int tps6594_get_power_state(tps6594_dev_t *dev, tps6594_power_state_t *state);

/* Sleep/Wake control */
int tps6594_enter_sleep(tps6594_dev_t *dev);
int tps6594_exit_sleep(tps6594_dev_t *dev);
int tps6594_configure_sleep(tps6594_dev_t *dev, const tps6594_sleep_config_t *cfg);
int tps6594_enable_wake(tps6594_dev_t *dev, uint8_t gpio_pin, uint8_t edge);

/* Watchdog */
int tps6594_watchdog_enable(tps6594_dev_t *dev, uint16_t timeout_ms);
int tps6594_watchdog_disable(tps6594_dev_t *dev);
int tps6594_watchdog_pet(tps6594_dev_t *dev);

/* ESM (Error Signal Monitor) */
int tps6594_esm_enable(tps6594_dev_t *dev, uint8_t mode);
int tps6594_esm_disable(tps6594_dev_t *dev);
int tps6594_esm_reset(tps6594_dev_t *dev);

/* Interrupt handling */
int tps6594_enable_interrupts(tps6594_dev_t *dev, uint8_t mask);
int tps6594_disable_interrupts(tps6594_dev_t *dev, uint8_t mask);
int tps6594_clear_interrupts(tps6594_dev_t *dev, uint8_t mask);
int tps6594_get_pending_interrupts(tps6594_dev_t *dev, uint8_t *pending);
int tps6594_register_event_callback(tps6594_dev_t *dev, tps6594_event_callback_t cb);

/* Read/Write raw registers (for debugging) */
int tps6594_reg_read(tps6594_dev_t *dev, uint8_t reg, uint8_t *val);
int tps6594_reg_write(tps6594_dev_t *dev, uint8_t reg, uint8_t val);
int tps6594_reg_modify(tps6594_dev_t *dev, uint8_t reg, uint8_t mask, uint8_t val);

/* NVM operations */
int tps6594_nvm_lock(tps6594_dev_t *dev, bool lock);
int tps6594_nvm_crc_check(tps6594_dev_t *dev, bool *crc_ok);

/* Utility */
const char *tps6594_err_str(int err);
void tps6594_dump_registers(tps6594_dev_t *dev);

#ifdef __cplusplus
}
#endif

#endif /* TPS6594_H_ */