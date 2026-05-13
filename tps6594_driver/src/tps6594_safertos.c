/**
 * @file tps6594_safertos.c
 * @brief SafeRTOS Integration for TPS6594 PMIC Driver
 *
 * Target: Cortex-R52 with SafeRTOS
 * This file provides SafeRTOS-specific integration:
 * - Interrupt handling with deferred procedure calls (DPC)
 * - Task synchronization
 * - Power management hooks
 * - Watchdog petting from background task
 *
 * Copyright (C) 2026 OpenClaw Agent
 */

#include "tps6594.h"
#include <string.h>

/*
 * Note: This file assumes SafeRTOS API is available.
 * Specific SafeRTOS headers would be included in actual build:
 * #include "SafeRTOS.h"
 * #include "portmacro.h"
 */

/* ========================================================================
 * Configuration
 * ======================================================================== */

#define TPS6594_ISR_PRIORITY       5   /* Interrupt priority (1=highest) */
#define TPS6594_DPC_STACK_SIZE     512 /* DPC task stack size */
#define TPS6594_WATCHDOG_TASK_PRIO 3   /* Watchdog task priority */
#define TPS6594_WATCHDOG_TASK_STACK 384
#define TPS6594_POLL_INTERVAL_MS   10

/* ========================================================================
 * Forward Declarations
 * ======================================================================== */

static void tps6594_isr_handler(void *arg);
static void tps6594_dpc_task(void *arg);
static void tps6594_watchdog_task(void *arg);
static void tps6594_power_state_task(void *arg);

/* ========================================================================
 * Static State
 * ======================================================================== */

static tps6594_dev_t *g_pmic_dev = NULL;
static TaskHandle_t g_dpc_task = NULL;
static TaskHandle_t g_watchdog_task = NULL;
static TaskHandle_t g_power_task = NULL;
static QueueHandle_t g_pmic_event_queue = NULL;
static StaticTask_t g_dpc_task_buf;
static StackType_t g_dpc_stack[TPS6594_DPC_STACK_SIZE];
static StaticTask_t g_wd_task_buf;
static StackType_t g_wd_stack[TPS6594_WATCHDOG_TASK_STACK];

static tps6594_event_callback_t g_event_cb = NULL;
static bool g_watchdog_enabled = false;
static uint32_t g_watchdog_timeout_ms = 30000;

/* Pending events for DPC processing */
#define PMIC_EVT_INT       (1 << 0)
#define PMIC_EVT_WAKE      (1 << 1)
#define PMIC_EVT_FAULT     (1 << 2)
#define PMIC_EVT_STATE_CHG (1 << 3)

typedef struct {
    uint8_t event_type;
    uint32_t event_data;
} pmic_event_t;

/* ========================================================================
 * I2C Bus Integration (Cortex-R52 specific)
 * ======================================================================== */

/* extern declarations - to be implemented in platform specific code */
extern int i2c_bus_init(uint32_t bus_id, uint32_t speed_hz);
extern int i2c_bus_write_read(uint8_t addr, uint8_t wreg, uint8_t wval,
                              uint8_t rreg, uint8_t *rval, uint32_t timeout_ms);
extern int i2c_bus_write(uint8_t addr, uint8_t reg, uint8_t val, uint32_t timeout_ms);
extern int i2c_bus_read(uint8_t addr, uint8_t reg, uint8_t *val, uint32_t timeout_ms);

/* I2C read wrapper (called from tps6594.c) */
int board_i2c_write_read(uint8_t addr, uint8_t wreg, uint8_t wval,
                        uint8_t rreg, uint8_t *rval, uint32_t timeout_ms)
{
    (void)addr;  /* Dev address is passed from driver, use g_pmic_dev->i2c_addr */

    if (rval && rreg != 0) {
        /* Write register address, then read */
        return i2c_bus_write_read(g_pmic_dev->i2c_addr, wreg, wval,
                                  rreg, rval, timeout_ms);
    } else if (wreg != 0) {
        /* Write only */
        return i2c_bus_write(g_pmic_dev->i2c_addr, wreg, wval, timeout_ms);
    } else if (rval && rreg == 0) {
        /* Read without write (shouldn't happen for TPS6594) */
        return i2c_bus_read(g_pmic_dev->i2c_addr, wreg, rval, timeout_ms);
    }

    return -1;
}

/* ========================================================================
 * Interrupt Handler (runs in ISR context)
 * ======================================================================== */

static void tps6594_isr_handler(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;
    pmic_event_t evt;

    (void)arg;

    if (!g_pmic_dev || !g_pmic_dev->initialized)
        return;

    /* Read interrupt status register */
    uint8_t int_sts;
    if (tps6594_i2c_read(g_pmic_dev, TPS6594_REG_INT_STS, &int_sts) != TPS6594_ERR_OK)
        return;

    if (int_sts == 0)
        return;  /* No interrupt pending */

    /* Clear interrupts by writing back status */
    tps6594_i2c_write(g_pmic_dev, TPS6594_REG_INT_STS, int_sts);

    /* Check for wake event */
    if (int_sts & (1 << 4)) {  /* FSM fault - may indicate wake */
        evt.event_type = PMIC_EVT_WAKE;
        evt.event_data = int_sts;
        xQueueSendFromISR(g_pmic_event_queue, &evt, &higher_priority_task_woken);
    }

    /* Check for fault conditions */
    if (int_sts & (TPS6594_INT_POR_FLT | TPS6594_INT_TSD_FLT | TPS6594_INT_VMON_FLT)) {
        evt.event_type = PMIC_EVT_FAULT;
        evt.event_data = int_sts;
        xQueueSendFromISR(g_pmic_event_queue, &evt, &higher_priority_task_woken);
    }

    /* General interrupt for DPC processing */
    if (int_sts) {
        evt.event_type = PMIC_EVT_INT;
        evt.event_data = int_sts;
        xQueueSendFromISR(g_pmic_event_queue, &evt, &higher_priority_task_woken);
    }

    /* Request context switch if higher priority task woke */
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/* Register ISR callback with platform (called from tps6594.c) */
void tps6594_register_isr_cb(tps6594_event_callback_t cb)
{
    g_event_cb = cb;
}

/* Initialize interrupt controller for Cortex-R52 */
static int tps6594_setup_interrupt(tps6594_dev_t *dev, uint8_t irq_line)
{
    extern int arm_gic_register_handler(uint8_t irq_num, void (*handler)(void*),
                                        void *arg, uint8_t priority);
    extern int arm_gic_enable_irq(uint8_t irq_num);

    /* Register and enable the interrupt */
    return arm_gic_register_handler(irq_line, tps6594_isr_handler, dev,
                                     TPS6594_ISR_PRIORITY);
}

/* ========================================================================
 * Deferred Procedure Call (DPC) Task
 * ======================================================================== */

static void tps6594_dpc_task(void *arg)
{
    pmic_event_t evt;

    (void)arg;

    for (;;) {
        /* Wait for events from ISR */
        if (xQueueReceive(g_pmic_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            switch (evt.event_type) {
            case PMIC_EVT_INT:
                /* Process pending interrupts */
                if (g_event_cb) {
                    /* Call user callback */
                    g_event_cb(evt.event_data, 0);
                }
                break;

            case PMIC_EVT_WAKE:
                /* Wake from sleep mode */
                if (g_event_cb) {
                    g_event_cb(0xFF, evt.event_data);  /* Wake event */
                }
                break;

            case PMIC_EVT_FAULT:
                /* Handle fault - may need to shut down or reset */
                extern void tps6594_handle_fault(uint8_t fault_data);
                tps6594_handle_fault((uint8_t)evt.event_data);
                break;

            case PMIC_EVT_STATE_CHG:
                /* Power state changed */
                break;
            }
        }
    }
}

/* ========================================================================
 * Watchdog Petting Task
 * ======================================================================== */

static void tps6594_watchdog_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();

    for (;;) {
        if (g_watchdog_enabled && g_pmic_dev && g_pmic_dev->initialized) {
            /* Pet the watchdog */
            tps6594_watchdog_pet(g_pmic_dev);
        }

        /* Sleep until next pet time */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(g_watchdog_timeout_ms / 2));
    }
}

/* ========================================================================
 * Power Management Task
 * ======================================================================== */

static void tps6594_power_state_task(void *arg)
{
    tps6594_power_state_t current_state = TPS6594_STATE_ACTIVE;
    bool sleep_requested = false;

    (void)arg;

    for (;;) {
        /* Check for sleep request (from application) */
        extern BaseType_t xApplicationSleepRequest(void);
        sleep_requested = xApplicationSleepRequest();

        if (sleep_requested && current_state == TPS6594_STATE_ACTIVE) {
            /* Request sleep transition */
            tps6594_enter_sleep(g_pmic_dev);
            current_state = TPS6594_STATE_SLEEP;
        }

        /* Monitor wake-up events */
        uint8_t wake_status;
        if (tps6594_i2c_read(g_pmic_dev, TPS6594_REG_WAKE_STATUS, &wake_status) == TPS6594_ERR_OK) {
            if (wake_status & 0x80) {  /* Wake detected */
                tps6594_exit_sleep(g_pmic_dev);
                current_state = TPS6594_STATE_ACTIVE;
            }
        }

        /* Periodic power status check */
        uint8_t pwr_status;
        if (tps6594_i2c_read(g_pmic_dev, TPS6594_REG_PWR_STATUS, &pwr_status) == TPS6594_ERR_OK) {
            tps6594_power_state_t new_state = (pwr_status & TPS6594_PWR_STATE_MASK);
            if (new_state != current_state) {
                current_state = new_state;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(TPS6594_POLL_INTERVAL_MS));
    }
}

/* ========================================================================
 * Public Initialization API
 * ======================================================================== */

/**
 * @brief Initialize TPS6594 driver with SafeRTOS integration
 *
 * @param dev Device instance
 * @param i2c_addr I2C slave address
 * @param irq_line Hardware IRQ line number
 * @return TPS6594_ERR_OK on success
 */
int tps6594_safertos_init(tps6594_dev_t *dev, uint8_t i2c_addr,
                         uint8_t irq_line)
{
    int ret;
    BaseType_t xResult;

    if (!dev)
        return TPS6594_ERR_INVAL;

    /* Initialize I2C bus */
    ret = i2c_bus_init(0, 400000);  /* 400kHz Fast Mode */
    if (ret != 0)
        return TPS6594_ERR_NOCOMM;

    /* Initialize driver */
    ret = tps6594_init(dev, i2c_addr, NULL);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Probe device */
    ret = tps6594_probe(dev);
    if (ret != TPS6594_ERR_OK)
        return ret;

    g_pmic_dev = dev;

    /* Create event queue */
    g_pmic_event_queue = xQueueCreate(10, sizeof(pmic_event_t));
    if (!g_pmic_event_queue)
        return TPS6594_ERR_NOMEM;

    /* Create DPC task */
    g_dpc_task = xTaskCreateStatic(
        tps6594_dpc_task,
        "PMIC_DPC",
        TPS6594_DPC_STACK_SIZE,
        NULL,
        TPS6594_ISR_PRIORITY,
        g_dpc_stack,
        &g_dpc_task_buf
    );

    /* Create watchdog task */
    g_watchdog_enabled = true;
    g_watchdog_timeout_ms = 30000;  /* 30 second default */

    g_watchdog_task = xTaskCreateStatic(
        tps6594_watchdog_task,
        "PMIC_WD",
        TPS6594_WATCHDOG_TASK_STACK,
        NULL,
        TPS6594_WATCHDOG_TASK_PRIO,
        g_wd_stack,
        &g_wd_task_buf
    );

    /* Create power management task */
    g_power_task = xTaskCreateStatic(
        tps6594_power_state_task,
        "PMIC_PWR",
        512,
        NULL,
        2,  /* Higher priority for power management */
        NULL,  /* Use default stack (not static) */
        NULL   /* Task structure not pre-allocated */
    );

    /* Setup hardware interrupt */
    ret = tps6594_setup_interrupt(dev, irq_line);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Enable all interrupts */
    tps6594_enable_interrupts(dev, 0xFF);

    return TPS6594_ERR_OK;
}

/**
 * @brief Deinitialize and cleanup
 */
int tps6594_safertos_deinit(tps6594_dev_t *dev)
{
    if (!dev)
        return TPS6594_ERR_INVAL;

    /* Stop tasks */
    if (g_watchdog_task) {
        vTaskDelete(g_watchdog_task);
        g_watchdog_task = NULL;
    }

    if (g_dpc_task) {
        vTaskDelete(g_dpc_task);
        g_dpc_task = NULL;
    }

    if (g_pmic_event_queue) {
        vQueueDelete(g_pmic_event_queue);
        g_pmic_event_queue = NULL;
    }

    /* Disable watchdog and regulators */
    tps6594_deinit(dev);

    g_pmic_dev = NULL;
    return TPS6594_ERR_OK;
}

/**
 * @brief Request system sleep (called by application)
 */
void tps6594_request_sleep(void)
{
    if (g_pmic_dev && g_pmic_dev->initialized) {
        tps6594_enter_sleep(g_pmic_dev);
    }
}

/**
 * @brief Request system wake (called by wake interrupt)
 */
void tps6594_request_wake(void)
{
    if (g_pmic_dev && g_pmic_dev->initialized) {
        tps6594_exit_sleep(g_pmic_dev);
    }
}

/**
 * @brief Handle fault condition
 */
void tps6594_handle_fault(uint8_t fault_data)
{
    /* Log fault */
    extern void debug_printf(const char *fmt, ...);
    debug_printf("[PMIC FAULT] 0x%02X\n", fault_data);

    /* Depending on fault type, take appropriate action */
    if (fault_data & TPS6594_INT_TSD_FLT) {
        /* Thermal shutdown - critical */
        debug_printf("[PMIC] Thermal shutdown triggered!\n");
        /* Could trigger system reset or safe shutdown */
    }

    if (fault_data & TPS6594_INT_POR_FLT) {
        /* Power-on reset fault */
        debug_printf("[PMIC] POR fault detected\n");
    }

    if (fault_data & TPS6594_INT_VMON_FLT) {
        /* Voltage monitor fault - under/over voltage */
        debug_printf("[PMIC] Voltage monitor fault\n");
    }

    if (fault_data & TPS6594_INT_WD_FLT) {
        /* Watchdog timeout */
        debug_printf("[PMIC] Watchdog timeout!\n");
    }
}

/* ========================================================================
 * Power State Hooks (called from idle task)
 * ======================================================================== */

/**
 * @brief SafeRTOS idle hook - place CPU in low power mode
 */
void vApplicationIdleHook(void)
{
    /* Enable deep sleep (if SafeRTOS supports) */
    /* __asm volatile ("wfi"); */  /* Wait for interrupt */
}

/**
 * @brief SafeRTOS tick hook - can be used to pet watchdog
 */
void vApplicationTickHook(void)
{
    /* Optional: pet watchdog on tick if not using dedicated task */
}

/* ========================================================================
 * Boot Sequence Helper
 * ======================================================================== */

/**
 * @brief Boot sequence - power up all rails in correct order
 *
 * @param dev Device handle
 * @param cfg Configuration
 * @return error code
 */
int tps6594_boot_sequence(tps6594_dev_t *dev, const tps6594_config_t *cfg)
{
    int ret;

    /* Step 1: Configure all regulators (disabled) */
    ret = tps6594_set_config(dev, cfg);
    if (ret != TPS6594_ERR_OK)
        return ret;

    /* Step 2: Enable BUCK regulators in sequence */
    tps6594_regulator_enable(dev, TPS6594_REGULATOR_BUCK1);  /* I/O first */
    vTaskDelay(pdMS_TO_TICKS(5));
    tps6594_regulator_enable(dev, TPS6594_REGULATOR_BUCK3);  /* Core next */
    vTaskDelay(pdMS_TO_TICKS(5));
    tps6594_regulator_enable(dev, TPS6594_REGULATOR_BUCK4);
    vTaskDelay(pdMS_TO_TICKS(5));
    tps6594_regulator_enable(dev, TPS6594_REGULATOR_BUCK5);
    vTaskDelay(pdMS_TO_TICKS(5));
    tps6594_regulator_enable(dev, TPS6594_REGULATOR_BUCK2);  /* DDR last */

    /* Step 3: Enable LDOs */
    for (int i = TPS6594_REGULATOR_LDO1; i <= TPS6594_REGULATOR_LDO4; i++) {
        vTaskDelay(pdMS_TO_TICKS(2));
        tps6594_regulator_enable(dev, (tps6594_regulator_id_t)i);
    }

    /* Step 4: Verify power good */
    vTaskDelay(pdMS_TO_TICKS(10));
    for (int i = 0; i < TPS6594_REGULATOR_COUNT; i++) {
        bool ok;
        tps6594_regulator_get_status(dev, (tps6594_regulator_id_t)i, &ok);
        if (!ok) {
            /* Power rail not good */
            return TPS6594_ERR_FAULT;
        }
    }

    /* Step 5: Enable watchdog */
    tps6594_watchdog_enable(dev, cfg->watchdog.timeout_ms);

    /* Step 6: Enable ESM */
    if (cfg->esm_mode > 0) {
        tps6594_esm_enable(dev, cfg->esm_mode);
    }

    return TPS6594_ERR_OK;
}