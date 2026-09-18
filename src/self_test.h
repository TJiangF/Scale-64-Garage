/*
 * self_test.h - boot time self verification of the parts that do not need
 * real hardware: the pin map and the motion (step counting) logic.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs every check and logs a PASS / FAIL line per group.
 * Returns true when all checks passed. */
bool app_self_test_run(void);

#ifdef __cplusplus
}
#endif
