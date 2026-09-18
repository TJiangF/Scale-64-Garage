#include "motor_driver.h"

#include "app_config.h"
#include "app_state.h"
#include "motion_core.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MOTOR";

#define STEP_LOG_PERIOD 1000u   /* log one line every N steps */

static void motor_task(void *arg);

static inline void motor_set_dir(motor_dir_t dir)
{
    gpio_set_level(TMC3_PIN_DIR, (dir == MOTOR_DIR_FORWARD) ? 1 : 0);
}

static inline void motor_pulse_step(void)
{
    gpio_set_level(TMC3_PIN_STEP, 1);
    esp_rom_delay_us(MOTOR_STEP_PULSE_US);
    gpio_set_level(TMC3_PIN_STEP, 0);
}

/*
 * Absolute time pacing. Long waits are handed over to the scheduler so the
 * LCD task (higher priority) gets its share of CPU, short waits are busy
 * waited through the ROM routine for a jitter free STEP edge.
 */
static void wait_until_us(int64_t target_us)
{
    for (;;) {
        int64_t remain = target_us - esp_timer_get_time();
        if (remain <= 0) {
            return;
        }
        if (remain >= 15000) {
            vTaskDelay(1);
        } else {
            esp_rom_delay_us((uint32_t)remain);
            return;
        }
    }
}

static void motor_task(void *arg)
{
    (void)arg;

    motor_dir_t dir = MOTOR_START_DIR;
    motor_set_dir(dir);
    esp_rom_delay_us(MOTOR_DIR_SETUP_US);

    int64_t  next_us = esp_timer_get_time() + MOTOR_STEP_INTERVAL_US;
    uint32_t log_div = 0;

    ESP_LOGI(TAG, "task started, %u steps/leg, %d us/step",
             (unsigned)MOTOR_STEPS_PER_LEG, MOTOR_STEP_INTERVAL_US);

    for (;;) {
        motor_pulse_step();

        bool        reversed = false;
        motor_dir_t new_dir  = dir;
        app_state_step(&reversed, &new_dir);

        if (reversed) {
            dir = new_dir;
            motor_set_dir(dir);
            esp_rom_delay_us(MOTOR_DIR_SETUP_US);
        }

        if (++log_div >= STEP_LOG_PERIOD) {
            log_div = 0;
            app_snapshot_t s;
            app_state_snapshot(&s);
            ESP_LOGI(TAG, "steps=%lld dir=%s leg=%u/%u reversals=%u",
                     (long long)s.step_count,
                     (s.dir == MOTOR_DIR_FORWARD) ? "FWD" : "REV",
                     (unsigned)s.steps_in_leg,
                     (unsigned)s.steps_per_leg,
                     (unsigned)s.reversals);
        }

        next_us += MOTOR_STEP_INTERVAL_US;
        if (next_us < esp_timer_get_time()) {
            /* we fell behind (e.g. preempted); resync instead of bursting */
            next_us = esp_timer_get_time() + MOTOR_STEP_INTERVAL_US;
        }
        wait_until_us(next_us);
    }
}

esp_err_t motor_driver_init(void)
{
    uint64_t pin_mask = (1ULL << TMC3_PIN_STEP) | (1ULL << TMC3_PIN_DIR);
#if TMC3_PIN_EN >= 0
    pin_mask |= (1ULL << TMC3_PIN_EN);
#endif

    const gpio_config_t cfg = {
        .pin_bit_mask = pin_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(err));
        return err;
    }

    gpio_set_level(TMC3_PIN_STEP, 0);
    motor_set_dir(MOTOR_START_DIR);

#if TMC3_PIN_EN >= 0
    gpio_set_level(TMC3_PIN_EN, 0);     /* ENN active low -> enable driver */
    ESP_LOGI(TAG, "STEP=GPIO%d DIR=GPIO%d EN=GPIO%d (driven LOW, enabled)",
             TMC3_PIN_STEP, TMC3_PIN_DIR, TMC3_PIN_EN);
#else
    ESP_LOGI(TAG, "STEP=GPIO%d DIR=GPIO%d EN not driven (must be tied to GND)",
             TMC3_PIN_STEP, TMC3_PIN_DIR);
#endif
    return ESP_OK;
}

esp_err_t motor_driver_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(motor_task, MOTOR_TASK_NAME,
                                            MOTOR_TASK_STACK, NULL,
                                            MOTOR_TASK_PRIORITY, NULL,
                                            APP_CORE_MOTOR);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}
