/*
 * app_config.h - single place for every pin / motion / task parameter.
 *
 * Scale 64 Garage : 1:64 multi-storey (stack) parking garage model.
 * Target : ESP32-S3 DevKit + home made PCB.
 * Stack  : ESP-IDF only (no Arduino).
 *
 * NOTE: this build implements TMC2208 #3 (STEP/DIR) + ST7789 LCD only.
 *       The remaining peripherals are declared here for later use but are
 *       deliberately NOT instantiated yet.
 */
#pragma once

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "esp_lcd_types.h"
#include "motion_core.h"

/* =========================================================================
 *  ST7789 SPI LCD  (240x320, no CS-less bit-bang, hardware SPI)
 * ========================================================================= */
#define LCD_PIN_RST             9
#define LCD_PIN_MOSI            11
#define LCD_PIN_SCLK            12
#define LCD_PIN_DC              13
#define LCD_PIN_CS              14
#define LCD_PIN_BL              (-1)    /* backlight not driven by the MCU   */

#define LCD_SPI_HOST            SPI2_HOST
#define LCD_PIXEL_CLOCK_HZ      (40 * 1000 * 1000)
#define LCD_CMD_BITS            8
#define LCD_PARAM_BITS          8
#define LCD_TRANS_QUEUE_DEPTH   10

#define LCD_H_RES               240
#define LCD_V_RES               320
#define LCD_X_GAP               0       /* adjust if your glass has an offset */
#define LCD_Y_GAP               0
#define LCD_INVERT_COLOR        true
#define LCD_RGB_ELEMENT_ORDER   LCD_RGB_ELEMENT_ORDER_BGR

/*
 * The esp_lcd SPI driver clocks the framebuffer out byte by byte. ST7789
 * expects RGB565 with the MSB first, the ESP32-S3 stores uint16_t little
 * endian, so the 16-bit value must be byte swapped before it is sent.
 * Flip this to 0 if your panel ends up showing swapped colours.
 */
#define LCD_COLOR_SWAP          1

#define LCD_TEXT_SCALE          2
#define LCD_LINE_PX             (7 * LCD_TEXT_SCALE + LCD_TEXT_SCALE)  /* 16 */
#define LCD_LINE_TOP            16
#define LCD_LINE_SPACING        40
#define LCD_REFRESH_PERIOD_MS   100

/* =========================================================================
 *  TMC2208 #3 - STEP / DIR only (standalone mode, no UART)
 * ========================================================================= */
#define TMC3_PIN_STEP           47
#define TMC3_PIN_DIR            21
/*
 * ENN is active LOW. Set this to the MCU GPIO if your PCB routes EN there,
 * or leave -1 when EN is hard wired to GND on the board.
 * NOTE: the module pulls EN up, so a floating EN means DISABLED.
 */
#define TMC3_PIN_EN             (-1)

/* =========================================================================
 *  Reserved peripherals - defined for the final build, unused for now.
 *  A negative pin means "not assigned yet" and is ignored by the pin check.
 * ========================================================================= */
#define TMC1_PIN_STEP           (-1)
#define TMC1_PIN_DIR            (-1)
#define TMC2_PIN_STEP           (-1)
#define TMC2_PIN_DIR            (-1)
#define SERVO_PIN_PWM           (-1)    /* 180 degree, 3-wire servo          */

/* =========================================================================
 *  Motion parameters
 * ========================================================================= */
#define MOTOR_STEP_INTERVAL_US  1000    /* 1 kHz nominal microstep rate      */
#define MOTOR_STEP_PULSE_US     10      /* STEP high time                    */
#define MOTOR_DIR_SETUP_US      5       /* DIR setup time before a STEP edge */

/*
 * Leg length is derived from the mechanics instead of a magic number.
 * 1.8 deg motor  -> 200 full steps / revolution
 * TMC2208 MS1/MS2 -> 1/16 microstepping
 * 200 * 16 = 3200 microsteps per motor revolution.
 */
#define MOTOR_FULL_STEPS_PER_REV    200
#define MOTOR_MICROSTEPS            16
#define MOTOR_MICROSTEPS_PER_REV    (MOTOR_FULL_STEPS_PER_REV * MOTOR_MICROSTEPS)
#define MOTOR_REVS_PER_LEG          2
#define MOTOR_STEPS_PER_LEG \
    (MOTOR_MICROSTEPS_PER_REV * MOTOR_REVS_PER_LEG)  /* 6400 = 2 turns */

#define MOTOR_START_DIR         MOTOR_DIR_FORWARD

/* =========================================================================
 *  Bring-up diagnostic (compile time)
 *  Runs a slow, 50% duty STEP train before normal operation so the STEP
 *  waveform is visible on a multimeter (~1.65 V average) and single steps
 *  can be seen on the shaft. Set MOTOR_BRINGUP_TEST to 0 for normal run.
 * ========================================================================= */
#define MOTOR_BRINGUP_TEST          0
#define MOTOR_BRINGUP_STEPS         400     /* 25 full steps at 1/16         */
#define MOTOR_BRINGUP_INTERVAL_US   50000   /* 20 Hz, 50% duty               */

/* =========================================================================
 *  FreeRTOS tasks
 * ========================================================================= */
#if CONFIG_FREERTOS_UNICORE
#define APP_CORE_MOTOR          0
#define APP_CORE_LCD            0
#else
#define APP_CORE_MOTOR          1
#define APP_CORE_LCD            0
#endif

#define MOTOR_TASK_NAME         "motor"
#define MOTOR_TASK_STACK        4096
#define MOTOR_TASK_PRIORITY     5

#define LCD_TASK_NAME           "lcd"
#define LCD_TASK_STACK          4096
#define LCD_TASK_PRIORITY       6       /* higher, so it preempts the motor  */
