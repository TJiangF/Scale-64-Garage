/*
 * motion_core.h - platform independent motion bookkeeping.
 *
 * This module has zero dependencies on FreeRTOS / ESP-IDF so that it can be
 * unit tested on the host and, more importantly here, verified at boot on the
 * target by self_test.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MOTOR_DIR_FORWARD = 0,
    MOTOR_DIR_REVERSE = 1,
} motor_dir_t;

typedef struct {
    int64_t     step_count;     /* cumulative step counter, never reset      */
    motor_dir_t dir;            /* current direction                         */
    uint32_t    steps_in_leg;   /* steps already issued in the current leg   */
    uint32_t    steps_per_leg;  /* leg length before reversing               */
    uint32_t    reversals;      /* how many times we reversed                */
} motion_state_t;

/* Initialise the state. A steps_per_leg of 0 is coerced to 1. */
void motion_init(motion_state_t *m, uint32_t steps_per_leg, motor_dir_t start_dir);

/*
 * Register exactly one STEP pulse.
 * Returns true when this step completed a leg and the direction was flipped.
 * step_count only ever grows, reversing must not reset it.
 */
bool motion_advance(motion_state_t *m);

#ifdef __cplusplus
}
#endif
