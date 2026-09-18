#include "motion_core.h"

void motion_init(motion_state_t *m, uint32_t steps_per_leg, motor_dir_t start_dir)
{
    if (m == NULL) {
        return;
    }
    m->step_count    = 0;
    m->dir           = start_dir;
    m->steps_in_leg  = 0;
    m->steps_per_leg = (steps_per_leg == 0) ? 1 : steps_per_leg;
    m->reversals     = 0;
}

bool motion_advance(motion_state_t *m)
{
    if (m == NULL) {
        return false;
    }

    m->step_count++;
    m->steps_in_leg++;

    if (m->steps_in_leg >= m->steps_per_leg) {
        m->steps_in_leg = 0;
        m->dir = (m->dir == MOTOR_DIR_FORWARD) ? MOTOR_DIR_REVERSE : MOTOR_DIR_FORWARD;
        m->reversals++;
        return true;
    }
    return false;
}
