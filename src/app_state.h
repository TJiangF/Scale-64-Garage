/*
 * app_state.h - the single shared state between the motor task and the LCD
 * task, protected by a FreeRTOS mutex.
 */
#pragma once

#include "esp_err.h"
#include "motion_core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t     step_count;
    motor_dir_t dir;
    uint32_t    steps_in_leg;
    uint32_t    steps_per_leg;
    uint32_t    reversals;
} app_snapshot_t;

/* Creates the mutex and seeds the motion state. Call once before any task. */
esp_err_t app_state_init(void);

/*
 * Advances exactly one step under the lock.
 * Optionally reports whether the direction flipped and the resulting
 * direction, so the caller can update the DIR pin.
 */
void app_state_step(bool *reversed, motor_dir_t *dir_out);

/* Thread safe copy of the current state for the display task. */
void app_state_snapshot(app_snapshot_t *out);

#ifdef __cplusplus
}
#endif
