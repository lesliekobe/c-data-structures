/**
 * @file tps6594.c
 * @brief TPS6594-Q1 PMIC Driver Implementation
 *
 * Target: Cortex-R52 with SafeRTOS
 * Interface: I2C
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#include "tps6594.h"
#include <string.h>
#include <stddef.h>

/* ========================================================================
 * Configuration Defaults
 * ======================================================================== */

#define TPS6594_I2C_RETRY_MAX      3
#define TPS6594_I2C_TIMEOUT_MS     100
#define TPS6594_POLL_INTERVAL_MS    5
#define TPS6594_WAKE_TIMEOUT_MS    50

/* Default regulator configurations for Jacinto DRA8x/TDA4x */
static const tps6594_config_t tps6594_default_config = {
    .i2c_address = TPS6594_I2C_ADDR_DEFAULT,

    /* BUCK regulators - typical for automotive SoC */
    .buck = {
        /* BUCK1: 3.3V, 4A - general I/O */
        { .voltage_mv = 3300, .enable = true, .fpwm_mode = false,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 0, .current_limit_ma = 4000,
          .discharge_enable = true, .dvs_enable = false },
        /* BUCK2: 1.8V, 3.5A - DDR or analog */
        { .voltage_mv = 1800, .enable = true, .fpwm_mode = false,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 1, .current_limit_ma = 3500,
          .discharge_enable = true, .dvs_enable = true },
        /* BUCK3: 1.2V, 3.5A - ARM/R52 core */
        { .voltage_mv = 1200, .enable = true, .fpwm_mode = true,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 2, .current_limit_ma = 3500,
          .discharge_enable = true, .dvs_enable = true },
        /* BUCK4: 1.1V, 3.5A - MCU/Sub-system */
        { .voltage_mv = 1100, .enable = true, .fpwm_mode = false,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 3, .current_limit_ma = 3500,
          .discharge_enable = true, .dvs_enable = true },
        /* BUCK5: 0.9V, 2A - SRAM/PLL */
        { .voltage_mv = 900, .enable = true, .fpwm_mode = false,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 4, .current_limit_ma = 2000,
          .discharge_enable = true, .dvs_enable = false }
    },

    /* LDO regulators */
    .ldo = {
        /* LDO1: 1.8V, 500mA - analog */
        { .voltage_mv = 1800, .enable = true, .bypass_mode = false, .low_noise_mode = false, .current_limit_ma = 500 },
        /* LDO2: 1.2V, 500mA - digital */
        { .voltage_mv = 1200, .enable = true, .bypass_mode = false, .low_noise_mode = false, .current_limit_ma = 500 },
        /* LDO3: 3.3V, 500mA - general I/O */
        { .voltage_mv = 3300, .enable = true, .bypass_mode = false, .low_noise_mode = false, .current_limit_ma = 500 },
        /* LDO4: 1.8V, 300mA - low noise (audio/pll) */
        { .voltage_mv = 1800, .enable = true, .bypass_mode = false, .low_noise_mode = true, .current_limit_ma = 300 }
    },

    /* Sleep config - keep critical rails */
    .sleep = {
        .sleep_enabled = true,
        .buck_mask = 0x04,       /* Only BUCK3 (core) in sleep */
        .ldo_mask = 0x02,       /* LDO2 on for RTC domain */
        .retention_enabled = true,
        .wake_pin = 1,          /* GPIO1 as wake source */
        .wake_edge = 0          /* Rising edge */
    },

    /* Watchdog - 30 second timeout */
    .watchdog = {
        .enabled = true,
        .timeout_ms = 30000,
        .qa_mode = false
    },

    /* ESM - error signal monitor */
    .esm_mode = 1,             /* PWM mode */

    /* Wake config */
    .enable_gpio_wake = true,
    .gpio_wake_pin = 1
};

/* ========================================================================
 * Internal I2C Bus Operations
 * ======================================================================== */

/* Forward declarations */
static int tps6594_i2c_write(tps6594_dev_t *dev, uint8_t reg, uint8_t val);
static int tps6594_i2c_read(tps6594_dev_t *dev, uint8_t reg, uint8_t *val);
static int tps6594_i2c_write_read(tps6594_dev_t *dev, uint8_t wreg, uint8_t wval,
                                   uint8_t rreg, uint8_t *rval);
static void tps6594_delay_ms(uint32_t ms);

/* I2C write with retry */
static int tps6594_i2c_write(tps6594_dev_t *dev, uint8_t reg, uint8_t val)
{
    int ret;
    uint8_t retry;

    for (retry = 0; retry < TPS6594_I2C_RETRY_MAX; retry++) {
        ret = tps6594_i2c_write_read(dev, reg, val, 0, NULL);
        if (ret == TPS6594_ERR_OK)
            return TPS6594_ERR_OK;
    }
    return TPS6594_ERR_NOCOMM;
}

/* I2C read with retry */
static int tps6594_i2c_read(tps6594_dev_t *dev, uint8_t reg, uint8_t *val)
{
    return tps6594_i2c_write_read(dev, 0, 0, reg, val);
}

/* Combined write-read (write register address, then read or write data) */
static int tps6594_i2c_write_read(tps6594_dev_t *dev, uint8_t wreg, uint8_t wval,
                                   uint8_t rreg, uint8_t *rval)
{
    /* Note: This is a stub. Actual implementation needs platform-specific
     * I2C driver integration. The following assumes a standard
     * polled I2C transaction on Cortex-R52 (e.g., using I2C controller
     * like kHz I2C on J721E/J7200)
     */
    extern int board_i2c_write_read(uint8_t addr, uint8_t wreg, uint8_t wval,
                                     uint8_t rreg, uint8_t *rval, uint32_t timeout_ms);

    int ret = board_i2c_write_read(dev->i2c_addr, wreg, wval,
                                    rreg, rval, TPS6594_I2C_TIMEOUT_MS);
    return (ret == 0) ? TPS6594_ERR_OK : TPS6594_ERR_NOCOMM;
}

/* Software delay */
static void tps6594_delay_ms(uint32_t ms)
{
    /* SafeRTOS vTaskDelay equivalent */
    extern void vTaskDelay(uint32_t ticks);
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

/* ========================================================================
 * Register Access Helpers
 * ======================================================================== */

int tps6594_reg_read(tps6594_dev_t *dev, uint8_t reg, uint8_t *val)
{
    return tps6594_i2c_read(dev, reg, val);
}

int tps6594_reg_write(tps6594_dev_t *dev, uint8_t reg, uint8_t val)
{
    return tps6594_i2c_write(dev, reg, val);
}

int tps6594_reg_modify(tps6594_dev_t *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
    int ret;
    uint8_t cur;

    ret = tps6594_i2c_read(dev, reg, &cur);
    if (ret != TPS6594_ERR_OK)
        return ret;

    cur = (cur & ~mask) | (val & mask);
    return tps6594_i2c_write(dev, reg, cur);
}

/* ========================================================================
 * Device Initialization
 * ======================================================================== */

int tps6594_init(tps6594_dev_t *dev, uint8_t i2c_addr, void *i2c_ctx)
{
    if (!dev || !i2c_ctx)
        return TPS6594_ERR_INVAL;

    memset(dev, 0, sizeof(*dev));

    dev->i2c_addr = i2c_addr;
    dev->i2c_context = i2c_ctx;
    dev->initialized = false;
    dev->current_state = TPS6594_STATE_UNKNOWN;

    return TPS6594_ERR_OK;
}

int tps6594_deinit(tps6594_dev_t *dev)
{
    if (!dev)
        return TPS6594_ERR_INVAL;

    /* Disable watchdog before shutdown */
    tps6594_watchdog_disable(dev);

    /* Disable all regulators */
    for (int i = 0; i < TPS6594_REGULATOR_COUNT; i++) {
        tps6594_regulator_disable(dev, (tps6594_regulator_id_t)i);
    }

    dev->initialized = false;
    return TPS6594_ERR_OK;
}

int tps6594_probe(tps6594_dev_t *dev)
{
    int ret;
    uint8_t val;

    /* Read DEVICE_ID register (0x00) */
    ret = tps6594_i2c_read(dev, TPS6594_REG_DEVICE_ID, &val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    dev->device_id = val;

    /* Verify device ID */
    if (val != TPS6594_DEVICE_ID_PMIC && val != TPS6594_DEVICE_ID_FPGA) {
        return TPS6594_ERR_NOMATCH;
    }

    /* Read revision ID (0x01) */
    ret = tps6594_i2c_read(dev, TPS6594_REG_INT_STS, &val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    dev->revid = val & 0x0F;

    /* Read current power state */
    ret = tps6594_i2c_read(dev, TPS6594_REG_PWR_STATUS, &val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    dev->current_state = (tps6594_power_state_t)(val & TPS6594_PWR_STATE_MASK);

    dev->initialized = true;
    return TPS6594_ERR_OK;
}

/* ========================================================================
 * Regulator Control
 * ======================================================================== */

int tps6594_regulator_enable(tps6594_dev_t *dev, tps6594_regulator_id_t id)
{
    uint8_t reg_offset;
    uint8_t reg_addr;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    if (id >= TPS6594_REGULATOR_COUNT)
        return TPS6594_ERR_INVAL;

    /* Get register address for this regulator */
    if (id <= TPS6594_REGULATOR_BUCK5) {
        reg_addr = TPS6594_REG_BUCK1_CONF + (id * 4);
    } else {
        reg_addr = TPS6594_REG_LDO1_CONF + ((id - 5) * 4);
    }

    /* Set enable bit */
    return tps6594_reg_modify(dev, reg_addr, 0x80, 0x80);
}

int tps6594_regulator_disable(tps6594_dev_t *dev, tps6594_regulator_id_t id)
{
    uint8_t reg_addr;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    if (id >= TPS6594_REGULATOR_COUNT)
        return TPS6594_ERR_INVAL;

    if (id <= TPS6594_REGULATOR_BUCK5) {
        reg_addr = TPS6594_REG_BUCK1_CONF + (id * 4);
    } else {
        reg_addr = TPS6594_REG_LDO1_CONF + ((id - 5) * 4);
    }

    /* Clear enable bit */
    return tps6594_reg_modify(dev, reg_addr, 0x80, 0x00);
}

int tps6594_regulator_set_voltage(tps6594_dev_t *dev, tps6594_regulator_id_t id,
                                   uint16_t voltage_mv)
{
    int ret;
    uint8_t reg_addr;
    uint8_t reg_val;
    uint16_t min_v, max_v, step;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    if (id >= TPS6594_REGULATOR_COUNT)
        return TPS6594_ERR_INVAL;

    /* Determine voltage range based on regulator type */
    if (id <= TPS6594_REGULATOR_BUCK5) {
        min_v = TPS6594_BUCK_VOLT_MIN_MV;
        max_v = TPS6594_BUCK_VOLT_MAX_MV;
        step = TPS6594_BUCK_STEP_MV;
        reg_addr = TPS6594_REG_BUCK1_VOLT + (id * 4);
    } else {
        min_v = TPS6594_LDO_VOLT_MIN_MV;
        max_v = TPS6594_LDO_VOLT_MAX_MV;
        step = TPS6594_LDO_STEP_MV;
        reg_addr = TPS6594_REG_LDO1_VOLT + ((id - 5) * 4);
    }

    /* Validate voltage */
    if (voltage_mv < min_v || voltage_mv > max_v)
        return TPS6594_ERR_INVAL;

    /* Convert to register value */
    reg_val = (voltage_mv - min_v) / step;

    /* Write voltage setting */
    ret = tps6594_i2c_write(dev, reg_addr, reg_val);
    if (ret == TPS6594_ERR_OK) {
        dev->regulators[id].voltage_mv = voltage_mv;
    }

    return ret;
}

int tps6594_regulator_get_voltage(tps6594_dev_t *dev, tps6594_regulator_id_t id,
                                  uint16_t *voltage_mv)
{
    int ret;
    uint8_t reg_addr;
    uint8_t reg_val;
    uint16_t min_v, step;

    if (!dev || !dev->initialized || !voltage_mv)
        return TPS6594_ERR_INVAL;

    if (id >= TPS6594_REGULATOR_COUNT)
        return TPS6594_ERR_INVAL;

    if (id <= TPS6594_REGULATOR_BUCK5) {
        min_v = TPS6594_BUCK_VOLT_MIN_MV;
        step = TPS6594_BUCK_STEP_MV;
        reg_addr = TPS6594_REG_BUCK1_VOLT + (id * 4);
    } else {
        min_v = TPS6594_LDO_VOLT_MIN_MV;
        step = TPS6594_LDO_STEP_MV;
        reg_addr = TPS6594_REG_LDO1_VOLT + ((id - 5) * 4);
    }

    ret = tps6594_i2c_read(dev, reg_addr, &reg_val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    *voltage_mv = min_v + (reg_val * step);
    return TPS6594_ERR_OK;
}

int tps6594_regulator_get_status(tps6594_dev_t *dev, tps6594_regulator_id_t id,
                                bool *is_ok)
{
    int ret;
    uint8_t pg_status;

    if (!dev || !dev->initialized || !is_ok)
        return TPS6594_ERR_INVAL;

    /* Read power good status register */
    ret = tps6594_i2c_read(dev, TPS6594_REG_PG_STATUS, &pg_status);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Check corresponding bit (BUCK1=bit0, BUCK2=bit1, etc.) */
    if (id <= TPS6594_REGULATOR_BUCK5) {
        *is_ok = (pg_status & (1 << id)) != 0;
    } else {
        /* LDOs start at bit 8 */
        *is_ok = (pg_status & (1 << (id - 3))) != 0;
    }

    dev->regulators[id].voltage_ok = *is_ok;
    return TPS6594_ERR_OK;
}

/* ========================================================================
 * Power State Control
 * ======================================================================== */

int tps6594_set_power_state(tps6594_dev_t *dev, tps6594_power_state_t state)
{
    uint8_t ctrl_val = 0;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    switch (state) {
    case TPS6594_STATE_ACTIVE:
        ctrl_val = 0x00;
        break;
    case TPS6594_STATE_STANDBY:
        ctrl_val = 0x01;
        break;
    case TPS6594_STATE_BACKUP:
        ctrl_val = 0x02;
        break;
    case TPS6594_STATE_SHUTDOWN:
        ctrl_val = 0x03;
        break;
    default:
        return TPS6594_ERR_INVAL;
    }

    /* Write to PWR_CTRL register (0x05) */
    int ret = tps6594_i2c_write(dev, TPS6594_REG_PWR_CTRL, ctrl_val);
    if (ret == TPS6594_ERR_OK) {
        dev->current_state = state;
    }

    return ret;
}

int tps6594_get_power_state(tps6594_dev_t *dev, tps6594_power_state_t *state)
{
    int ret;
    uint8_t val;

    if (!dev || !dev->initialized || !state)
        return TPS6594_ERR_INVAL;

    ret = tps6594_i2c_read(dev, TPS6594_REG_PWR_STATUS, &val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    *state = (tps6594_power_state_t)(val & TPS6594_PWR_STATE_MASK);
    dev->current_state = *state;
    return TPS6594_ERR_OK;
}

/* ========================================================================
 * Sleep/Wake Control
 * ======================================================================== */

int tps6594_enter_sleep(tps6594_dev_t *dev)
{
    int ret;
    uint8_t sleep_cfg = 0;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    /* Configure sleep state first */
    ret = tps6594_i2c_read(dev, TPS6594_REG_SLEEP_CONF, &sleep_cfg);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Enable sleep mode */
    sleep_cfg |= TPS6594_SLEEP_EN;

    ret = tps6594_i2c_write(dev, TPS6594_REG_SLEEP_CONF, sleep_cfg);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Set power state to standby (will transition to sleep) */
    return tps6594_set_power_state(dev, TPS6594_STATE_STANDBY);
}

int tps6594_exit_sleep(tps6594_dev_t *dev)
{
    int ret;
    uint8_t val;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    /* Clear sleep enable */
    ret = tps6594_i2c_read(dev, TPS6594_REG_SLEEP_CONF, &val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    val &= ~TPS6594_SLEEP_EN;
    ret = tps6594_i2c_write(dev, TPS6594_REG_SLEEP_CONF, val);

    /* Set back to active state */
    if (ret == TPS6594_ERR_OK) {
        dev->current_state = TPS6594_STATE_ACTIVE;
    }

    return ret;
}

int tps6594_configure_sleep(tps6594_dev_t *dev, const tps6594_sleep_config_t *cfg)
{
    uint8_t sleep_val = 0;
    uint8_t wake_val = 0;

    if (!dev || !dev->initialized || !cfg)
        return TPS6594_ERR_INVAL;

    /* Build sleep configuration register */
    if (cfg->sleep_enabled)
        sleep_val |= TPS6594_SLEEP_EN;
    if (cfg->retention_enabled)
        sleep_val |= TPS6594_SLEEP_RET_EN;

    sleep_val |= (cfg->buck_mask & TPS6594_SLEEP_BUCK_MASK);
    sleep_val |= ((cfg->ldo_mask & TPS6594_SLEEP_LDO_MASK) << 6);

    /* Write sleep configuration */
    int ret = tps6594_i2c_write(dev, TPS6594_REG_SLEEP_CONF, sleep_val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Configure wake detection */
    wake_val = cfg->wake_pin & 0x07;
    wake_val |= (cfg->wake_edge & 0x03) << 4;

    ret = tps6594_i2c_write(dev, TPS6594_REG_WAKE_CONF, wake_val);

    return ret;
}

int tps6594_enable_wake(tps6594_dev_t *dev, uint8_t gpio_pin, uint8_t edge)
{
    uint8_t val = 0;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    if (gpio_pin > 6)
        return TPS6594_ERR_INVAL;

    /* Configure GPIO as wake source */
    val = (gpio_pin & 0x07);
    val |= (edge & 0x03) << 4;
    val |= (1 << 7);  /* Enable wake */

    return tps6594_i2c_write(dev, TPS6594_REG_WAKE_CONF, val);
}

/* ========================================================================
 * Watchdog Control
 * ======================================================================== */

int tps6594_watchdog_enable(tps6594_dev_t *dev, uint16_t timeout_ms)
{
    uint8_t wd_val;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    /* Watchdog timeout in 1ms steps, max ~32s */
    if (timeout_ms > 32000)
        timeout_ms = 32000;

    /* Calculate WDOG response register value */
    wd_val = (timeout_ms >> 8) & 0x7F;  /* Upper 7 bits */

    return tps6594_i2c_write(dev, TPS6594_REG_WDOG_RESPONSE, wd_val);
}

int tps6594_watchdog_disable(tps6594_dev_t *dev)
{
    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    return tps6594_i2c_write(dev, TPS6594_REG_WDOG_RESPONSE, 0x00);
}

int tps6594_watchdog_pet(tps6594_dev_t *dev)
{
    uint8_t val;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    /* Read current watchdog config */
    int ret = tps6594_i2c_read(dev, TPS6594_REG_WDOG_CONF, &val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Toggle reset bit to pet watchdog */
    val ^= 0x02;  /* Toggle RST bit */
    return tps6594_i2c_write(dev, TPS6594_REG_WDOG_CONF, val);
}

/* ========================================================================
 * ESM (Error Signal Monitor)
 * ======================================================================== */

int tps6594_esm_enable(tps6594_dev_t *dev, uint8_t mode)
{
    uint8_t val;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    if (mode > 2)
        return TPS6594_ERR_INVAL;

    val = mode & 0x03;
    val |= (1 << 7);  /* Enable ESM */

    return tps6594_i2c_write(dev, TPS6594_REG_ESM_CONF, val);
}

int tps6594_esm_disable(tps6594_dev_t *dev)
{
    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    return tps6594_i2c_write(dev, TPS6594_REG_ESM_CONF, 0x00);
}

int tps6594_esm_reset(tps6594_dev_t *dev)
{
    int ret;
    uint8_t val;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    /* Read ESM status and clear any faults */
    ret = tps6594_i2c_read(dev, TPS6594_REG_ESM_STATUS, &val);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Write to clear error */
    return tps6594_i2c_write(dev, TPS6594_REG_ESM_STATUS, val);
}

/* ========================================================================
 * Interrupt Handling
 * ======================================================================== */

int tps6594_enable_interrupts(tps6594_dev_t *dev, uint8_t mask)
{
    uint8_t cur;
    int ret;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    ret = tps6594_i2c_read(dev, TPS6594_REG_INT_MSK, &cur);
    if (ret != TPS6594_ERR_OK)
        return ret;

    cur |= mask;
    return tps6594_i2c_write(dev, TPS6594_REG_INT_MSK, cur);
}

int tps6594_disable_interrupts(tps6594_dev_t *dev, uint8_t mask)
{
    uint8_t cur;
    int ret;

    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    ret = tps6594_i2c_read(dev, TPS6594_REG_INT_MSK, &cur);
    if (ret != TPS6594_ERR_OK)
        return ret;

    cur &= ~mask;
    return tps6594_i2c_write(dev, TPS6594_REG_INT_MSK, cur);
}

int tps6594_clear_interrupts(tps6594_dev_t *dev, uint8_t mask)
{
    if (!dev || !dev->initialized)
        return TPS6594_ERR_INVAL;

    /* Write 1 to clear status bits */
    return tps6594_i2c_write(dev, TPS6594_REG_INT_STS, mask);
}

int tps6594_get_pending_interrupts(tps6594_dev_t *dev, uint8_t *pending)
{
    if (!dev || !dev->initialized || !pending)
        return TPS6594_ERR_INVAL;

    return tps6594_i2c_read(dev, TPS6594_REG_INT_STS, pending);
}

int tps6594_register_event_callback(tps6594_dev_t *dev, tps6594_event_callback_t cb)
{
    if (!dev)
        return TPS6594_ERR_INVAL;

    /* Note: In SafeRTOS, callbacks should be called from interrupt context
     * or via deferred interrupt handler (DPC). Store callback for
     * integration with SafeRTOS interrupt handling.
     */
    extern void tps6594_register_isr_cb(tps6594_event_callback_t cb);
    tps6594_register_isr_cb(cb);
    (void)dev;  /* Dev may not be needed if using global callback */
    return TPS6594_ERR_OK;
}

/* ========================================================================
 * NVM Operations
 * ======================================================================== */

int tps6594_nvm_lock(tps6594_dev_t *dev, bool lock)
{
    uint8_t val = lock ? 0x80 : 0x00;
    return tps6594_i2c_write(dev, TPS6594_REG_NVM_LOCK, val);
}

int tps6594_nvm_crc_check(tps6594_dev_t *dev, bool *crc_ok)
{
    int ret;
    uint8_t status;

    if (!dev || !dev->initialized || !crc_ok)
        return TPS6594_ERR_INVAL;

    ret = tps6594_i2c_read(dev, TPS6594_REG_FAULT_STS, &status);
    if (ret != TPS6594_ERR_OK)
        return ret;

    *crc_ok = !(status & 0x01);  /* CRC_FLT bit */
    return TPS6594_ERR_OK;
}

/* ========================================================================
 * Full Configuration
 * ======================================================================== */

int tps6594_set_config(tps6594_dev_t *dev, const tps6594_config_t *cfg)
{
    int ret;
    uint8_t i;

    if (!dev || !cfg)
        return TPS6594_ERR_INVAL;

    /* Configure BUCK regulators */
    for (i = 0; i < 5; i++) {
        const tps6594_buck_config_t *bc = &cfg->buck[i];

        /* Set voltage */
        ret = tps6594_regulator_set_voltage(dev, (tps6594_regulator_id_t)i,
                                            bc->voltage_mv);
        if (ret != TPS6594_ERR_OK)
            return ret;

        /* Configure BUCK settings */
        uint8_t conf = 0;
        if (bc->enable)
            conf |= TPS6594_BUCK_EN;
        if (bc->fpwm_mode)
            conf |= TPS6594_BUCK_FPWM;
        conf |= (bc->freq & 0x38);
        conf |= (bc->phase & 0x07);

        ret = tps6594_i2c_write(dev, TPS6594_REG_BUCK1_CONF + (i * 4), conf);
        if (ret != TPS6594_ERR_OK)
            return ret;
    }

    /* Configure LDO regulators */
    for (i = 0; i < 4; i++) {
        const tps6594_ldo_config_t *lc = &cfg->ldo[i];

        ret = tps6594_regulator_set_voltage(dev,
                                           (tps6594_regulator_id_t)(TPS6594_REGULATOR_LDO1 + i),
                                           lc->voltage_mv);
        if (ret != TPS6594_ERR_OK)
            return ret;

        uint8_t conf = 0;
        if (lc->enable)
            conf |= TPS6594_LDO_EN;
        if (lc->bypass_mode)
            conf |= TPS6594_LDO_BYPASS;
        if (i == 3 && lc->low_noise_mode)  /* LDO4 only */
            conf |= TPS6594_LDO_STEPDOWN;

        ret = tps6594_i2c_write(dev, TPS6594_REG_LDO1_CONF + (i * 4), conf);
        if (ret != TPS6594_ERR_OK)
            return ret;
    }

    /* Configure sleep */
    ret = tps6594_configure_sleep(dev, &cfg->sleep);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Configure watchdog */
    if (cfg->watchdog.enabled) {
        tps6594_watchdog_enable(dev, cfg->watchdog.timeout_ms);
    } else {
        tps6594_watchdog_disable(dev);
    }

    /* Configure ESM */
    if (cfg->esm_mode > 0) {
        tps6594_esm_enable(dev, cfg->esm_mode);
    }

    return TPS6594_ERR_OK;
}

int tps6594_get_config(tps6594_dev_t *dev, tps6594_config_t *cfg)
{
    /* Read back current configuration from registers */
    /* Implementation would mirror set_config reading back */
    (void)dev;
    (void)cfg;
    return TPS6594_ERR_INVAL;  /* Not yet implemented */
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

const char *tps6594_err_str(int err)
{
    switch (err) {
    case TPS6594_ERR_OK:       return "OK";
    case TPS6594_ERR_INVAL:    return "Invalid argument";
    case TPS6594_ERR_NOCOMM:   return "I2C communication error";
    case TPS6594_ERR_TIMEOUT:  return "Operation timeout";
    case TPS6594_ERR_NOMATCH:  return "Device ID mismatch";
    case TPS6594_ERR_FAULT:    return "Hardware fault";
    case TPS6594_ERR_PROTECTED: return "NVM locked";
    case TPS6594_ERR_BADCRC:   return "CRC error";
    case TPS6594_ERR_TIMEOUTWAIT: return "Timed out waiting";
    default:                   return "Unknown error";
    }
}

void tps6594_dump_registers(tps6594_dev_t *dev)
{
    uint8_t regs[] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x10, 0x11, 0x14, 0x15, 0x18, 0x19, 0x1C, 0x1D,
        0x28, 0x29, 0x2C, 0x2D, 0x30, 0x31, 0x34, 0x35,
        0x60, 0x70, 0x80, 0x81, 0xA0, 0xA1
    };

    /* Debug output via SafeRTOS debug print */
    extern void debug_print(const char *fmt, ...);

    for (size_t i = 0; i < sizeof(regs); i++) {
        uint8_t val;
        if (tps6594_i2c_read(dev, regs[i], &val) == TPS6594_ERR_OK) {
            /* debug_print("[%02X] = 0x%02X\n", regs[i], val); */
        }
    }
    (void)debug_print;
}