/*
 * display.h - ST7789 status screen (official ESP-IDF esp_lcd component).
 *
 * Hardware is optional: if the panel can not be initialised the code logs a
 * warning and keeps running headless, so the motion logic can still be
 * verified over the serial console.
 */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Brings up SPI + esp_lcd ST7789 and the line buffer. */
esp_err_t display_init(void);

/* Starts the FreeRTOS task that refreshes the screen. */
esp_err_t display_start(void);

/* true when a panel was successfully initialised. */
bool display_ready(void);

#ifdef __cplusplus
}
#endif
