#include "app_state.h"
#include "app_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static motion_state_t    s_motion;
static SemaphoreHandle_t s_lock;

esp_err_t app_state_init(void)
{
    motion_init(&s_motion, MOTOR_STEPS_PER_LEG, MOTOR_START_DIR);

    s_lock = xSemaphoreCreateMutex();
    return (s_lock != NULL) ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_state_step(bool *reversed, motor_dir_t *dir_out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    bool did_reverse = motion_advance(&s_motion);

    if (reversed != NULL) {
        *reversed = did_reverse;
    }
    if (dir_out != NULL) {
        *dir_out = s_motion.dir;
    }

    xSemaphoreGive(s_lock);
}

void app_state_snapshot(app_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    out->step_count    = s_motion.step_count;
    out->dir           = s_motion.dir;
    out->steps_in_leg  = s_motion.steps_in_leg;
    out->steps_per_leg = s_motion.steps_per_leg;
    out->reversals     = s_motion.reversals;

    xSemaphoreGive(s_lock);
}
