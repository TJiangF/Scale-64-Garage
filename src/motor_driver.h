/*
 * motor_driver.h - TMC2208 #3 driven through STEP / DIR only.
 * The driver runs in standalone mode, no UART configuration is used.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configures the STEP / DIR GPIOs and drives them to a safe idle level. */
esp_err_t motor_driver_init(void);

/* Starts the FreeRTOS task that continuously emits STEP pulses. */
esp_err_t motor_driver_start(void);

#ifdef __cplusplus
}
#endif
