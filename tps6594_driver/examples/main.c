/**
 * @file main.c
 * @brief TPS6594 Driver Usage Example
 *
 * Example demonstrating full initialization, runtime control,
 * and sleep/wake sequence on Cortex-R52 + SafeRTOS.
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#include "tps6594.h"
#include <stdio.h>

/* Configuration for Jacinto TDA4VM (typical automotive SoC) */
static const tps6594_config_t tda4vm_config = {
    .i2c_address = TPS6594_I2C_ADDR_DEFAULT,

    .buck = {
        /* BUCK1: 3.3V @ 4A - General I/O, always on */
        { .voltage_mv = 3300, .enable = true, .fpwm_mode = false,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 0, .current_limit_ma = 4000,
          .discharge_enable = true, .dvs_enable = false },

        /* BUCK2: 1.8V @ 3.5A - DDR4 VDDQ */
        { .voltage_mv = 1800, .enable = true, .fpwm_mode = true,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 1, .current_limit_ma = 3500,
          .discharge_enable = true, .dvs_enable = true },

        /* BUCK3: 1.2V @ 3.5A - Cortex-R52 core */
        { .voltage_mv = 1200, .enable = true, .fpwm_mode = true,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 2, .current_limit_ma = 3500,
          .discharge_enable = true, .dvs_enable = true },

        /* BUCK4: 1.1V @ 3.5A - MCU island */
        { .voltage_mv = 1100, .enable = true, .fpwm_mode = false,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 3, .current_limit_ma = 3500,
          .discharge_enable = true, .dvs_enable = true },

        /* BUCK5: 0.9V @ 2A - PLL/SRAM */
        { .voltage_mv = 900, .enable = true, .fpwm_mode = false,
          .freq = TPS6594_BUCK_FREQ_2_2MHZ, .phase = 4, .current_limit_ma = 2000,
          .discharge_enable = true, .dvs_enable = false }
    },

    .ldo = {
        /* LDO1: 1.8V @ 500mA - Analog rails */
        { .voltage_mv = 1800, .enable = true, .bypass_mode = false,
          .low_noise_mode = false, .current_limit_ma = 500 },
        /* LDO2: 1.2V @ 500mA - IO 1.2V domain */
        { .voltage_mv = 1200, .enable = true, .bypass_mode = false,
          .low_noise_mode = false, .current_limit_ma = 500 },
        /* LDO3: 3.3V @ 500mA - General I/O */
        { .voltage_mv = 3300, .enable = true, .bypass_mode = false,
          .low_noise_mode = false, .current_limit_ma = 500 },
        /* LDO4: 1.8V @ 300mA - Audio PLL (low noise) */
        { .voltage_mv = 1800, .enable = true, .bypass_mode = false,
          .low_noise_mode = true, .current_limit_ma = 300 }
    },

    .sleep = {
        .sleep_enabled = true,
        .buck_mask = 0x04,       /* Only BUCK3 (R52 core) stays on */
        .ldo_mask = 0x02,       /* LDO2 for RTC/wake logic */
        .retention_enabled = true,
        .wake_pin = 1,
        .wake_edge = 0          /* Rising edge wake */
    },

    .watchdog = {
        .enabled = true,
        .timeout_ms = 30000,
        .qa_mode = false
    },

    .esm_mode = 1,             /* PWM mode */
    .enable_gpio_wake = true,
    .gpio_wake_pin = 1
};

/* ========================================================================
 * Application Code
 * ======================================================================== */

static tps6594_dev_t g_pmic;

void pmic_event_handler(uint8_t event_type, uint32_t data)
{
    switch (event_type) {
    case 0xFF:
        printf("[PMIC] Wake event: 0x%08X\n", data);
        break;
    default:
        printf("[PMIC] Event: type=0x%02X, data=0x%08X\n", event_type, data);
        break;
    }
}

void fault_handler(uint8_t fault)
{
    printf("[PMIC FAULT] 0x%02X\n", fault);

    if (fault & TPS6594_INT_TSD_FLT) {
        printf("  -> Thermal shutdown!\n");
        /* Emergency: signal safe state */
    }
    if (fault & TPS6594_INT_VMON_FLT) {
        printf("  -> Voltage monitor fault!\n");
    }
    if (fault & TPS6594_INT_WD_FLT) {
        printf("  -> Watchdog timeout!\n");
    }
}

/* ========================================================================
 * Main Entry Point
 * ======================================================================== */

int main(void)
{
    int ret;
    uint16_t voltage;
    bool pg_ok;

    printf("TPS6594 PMIC Driver Example\n");
    printf("============================\n\n");

    /* Initialize driver with SafeRTOS integration */
    printf("Initializing PMIC (I2C addr=0x%02X, IRQ=42)...\n",
           TPS6594_I2C_ADDR_DEFAULT);

    ret = tps6594_safertos_init(&g_pmic, TPS6594_I2C_ADDR_DEFAULT, 42);
    if (ret != TPS6594_ERR_OK) {
        printf("ERROR: tps6594_safertos_init failed: %s\n",
               tps6594_err_str(ret));
        return -1;
    }
    printf("PMIC initialized, device_id=0x%02X, rev=0x%X\n",
           g_pmic.device_id, g_pmic.revid);

    /* Register callbacks */
    tps6594_register_event_callback(&g_pmic, pmic_event_handler);

    /* Run boot sequence */
    printf("\nRunning boot sequence...\n");
    ret = tps6594_boot_sequence(&g_pmic, &tda4vm_config);
    if (ret != TPS6594_ERR_OK) {
        printf("ERROR: boot sequence failed: %s\n", tps6594_err_str(ret));
        return -1;
    }
    printf("Boot sequence complete.\n");

    /* Verify all rails */
    printf("\nPower rail status:\n");
    for (int i = 0; i < TPS6594_REGULATOR_COUNT; i++) {
        tps6594_regulator_get_voltage(&g_pmic, (tps6594_regulator_id_t)i, &voltage);
        tps6594_regulator_get_status(&g_pmic, (tps6594_regulator_id_t)i, &pg_ok);
        const char *names[] = { "BUCK1", "BUCK2", "BUCK3", "BUCK4", "BUCK5",
                               "LDO1", "LDO2", "LDO3", "LDO4" };
        printf("  %s: %dmV, PG=%s\n", names[i], voltage, pg_ok ? "OK" : "FAIL");
    }

    /* Example: DVS - adjust R52 core voltage based on load */
    printf("\nDemonstrating DVS...\n");
    ret = tps6594_regulator_set_voltage(&g_pmic, TPS6594_REGULATOR_BUCK3, 1100);
    printf("  BUCK3 scaled to 1.1V (ret=%d)\n", ret);

    ret = tps6594_regulator_set_voltage(&g_pmic, TPS6594_REGULATOR_BUCK3, 1200);
    printf("  BUCK3 scaled to 1.2V (ret=%d)\n", ret);

    /* Get statistics */
    mbox_stats_t stats;
    printf("\nDriver initialized and running.\n");

    /* Main loop - application would run here */
    printf("\nEntering main loop...\n");
    for (int i = 0; i < 5; i++) {
        /* Simulate work with watchdog pet */
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("  [Tick %d]\n", i + 1);
    }

    /* Request sleep */
    printf("\nRequesting system sleep...\n");
    tps6594_request_sleep();

    /* Wake would come from GPIO interrupt */

    /* Cleanup */
    tps6594_safertos_deinit(&g_pmic);
    printf("\nDone.\n");
    return 0;
}