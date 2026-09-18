#include "display.h"

#include "app_config.h"
#include "app_state.h"
#include "lcd_font.h"
#include "motion_core.h"

#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "LCD";

/* ---------------------------------------------------------------- colours */
#if LCD_COLOR_SWAP
#define RGB565(r, g, b) \
    ((uint16_t)__builtin_bswap16((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))))
#else
#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#endif

#define COL_BG      RGB565(0, 0, 0)
#define COL_TITLE   RGB565(0, 255, 255)
#define COL_WHITE   RGB565(255, 255, 255)
#define COL_GRAY    RGB565(160, 160, 160)
#define COL_FWD     RGB565(0, 255, 0)
#define COL_REV     RGB565(255, 0, 0)

static esp_lcd_panel_handle_t    s_panel;
static uint16_t                 *s_line;       /* one text line, DMA capable */
static SemaphoreHandle_t         s_done;       /* signalled by colour ISR    */
static bool                      s_ready;

static void lcd_task(void *arg);

/* ------------------------------------------------------------- esp_lcd io */
static bool IRAM_ATTR on_color_trans_done(esp_lcd_panel_io_handle_t io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
    (void)io;
    (void)edata;

    BaseType_t higher_prio_woken = pdFALSE;
    SemaphoreHandle_t sem = (SemaphoreHandle_t)user_ctx;
    if (sem != NULL) {
        xSemaphoreGiveFromISR(sem, &higher_prio_woken);
    }
    return higher_prio_woken == pdTRUE;
}

/* Push the line buffer to the panel and wait for the transfer to finish so
 * the buffer can safely be reused for the next line. */
static void line_blit(int y)
{
    if (!s_ready) {
        return;
    }
    esp_lcd_panel_draw_bitmap(s_panel, 0, y, LCD_H_RES, y + LCD_LINE_PX, s_line);
    xSemaphoreTake(s_done, pdMS_TO_TICKS(200));
}

static void line_text(int index, const char *text, uint16_t fg)
{
    const int y = LCD_LINE_TOP + index * LCD_LINE_SPACING;

    lcd_font_fill_rect(s_line, LCD_H_RES, LCD_LINE_PX, 0, 0,
                       LCD_H_RES, LCD_LINE_PX, COL_BG);
    lcd_font_draw_text(s_line, LCD_H_RES, LCD_LINE_PX, 6, 0,
                       text, fg, COL_BG, LCD_TEXT_SCALE);
    line_blit(y);
}

/* --------------------------------------------------------------- public API */
esp_err_t display_init(void)
{
    s_done = xSemaphoreCreateBinary();
    s_line = heap_caps_malloc(LCD_H_RES * LCD_LINE_PX * sizeof(uint16_t),
                              MALLOC_CAP_DMA);
    if (s_done == NULL || s_line == NULL) {
        ESP_LOGE(TAG, "out of DMA memory");
        return ESP_ERR_NO_MEM;
    }

    const spi_bus_config_t buscfg = {
        .sclk_io_num     = LCD_PIN_SCLK,
        .mosi_io_num     = LCD_PIN_MOSI,
        .miso_io_num     = -1,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = LCD_H_RES * LCD_LINE_PX * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_io_handle_t io = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = {
        .cs_gpio_num          = LCD_PIN_CS,
        .dc_gpio_num          = LCD_PIN_DC,
        .spi_mode             = 0,
        .pclk_hz              = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth    = LCD_TRANS_QUEUE_DEPTH,
        .on_color_trans_done  = on_color_trans_done,
        .user_ctx             = s_done,
        .lcd_cmd_bits         = LCD_CMD_BITS,
        .lcd_param_bits       = LCD_PARAM_BITS,
    };
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST,
                                   &io_config, &io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
        return err;
    }

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7789 failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_panel_reset(s_panel);
    esp_lcd_panel_init(s_panel);
    esp_lcd_panel_invert_color(s_panel, LCD_INVERT_COLOR);
    esp_lcd_panel_set_gap(s_panel, LCD_X_GAP, LCD_Y_GAP);
    esp_lcd_panel_disp_on_off(s_panel, true);

    s_ready = true;

    /* wipe the screen */
    for (int y = 0; y + LCD_LINE_PX <= LCD_V_RES; y += LCD_LINE_PX) {
        lcd_font_fill_rect(s_line, LCD_H_RES, LCD_LINE_PX, 0, 0,
                           LCD_H_RES, LCD_LINE_PX, COL_BG);
        line_blit(y);
    }
    line_text(0, "SCALE 64 GARAGE", COL_TITLE);

    ESP_LOGI(TAG, "ST7789 %dx%d up (SPI2, MOSI=%d SCLK=%d DC=%d CS=%d RST=%d)",
             LCD_H_RES, LCD_V_RES, LCD_PIN_MOSI, LCD_PIN_SCLK,
             LCD_PIN_DC, LCD_PIN_CS, LCD_PIN_RST);
    return ESP_OK;
}

bool display_ready(void)
{
    return s_ready;
}

esp_err_t display_start(void)
{
    BaseType_t ok = xTaskCreatePinnedToCore(lcd_task, LCD_TASK_NAME,
                                            LCD_TASK_STACK, NULL,
                                            LCD_TASK_PRIORITY, NULL,
                                            APP_CORE_LCD);
    return (ok == pdPASS) ? ESP_OK : ESP_ERR_NO_MEM;
}

/* ------------------------------------------------------------------- task */
static void lcd_task(void *arg)
{
    (void)arg;

    if (!s_ready) {
        ESP_LOGW(TAG, "no panel, status will only be printed on the console");
    }

    char buf[48];

    for (;;) {
        app_snapshot_t s;
        app_state_snapshot(&s);

        const bool fwd = (s.dir == MOTOR_DIR_FORWARD);

        snprintf(buf, sizeof(buf), "DIR  : %s", fwd ? "FWD" : "REV");
        line_text(1, buf, fwd ? COL_FWD : COL_REV);

        snprintf(buf, sizeof(buf), "STEPS: %lld", (long long)s.step_count);
        line_text(2, buf, COL_WHITE);

        snprintf(buf, sizeof(buf), "LEG  : %u/%u",
                 (unsigned)s.steps_in_leg, (unsigned)s.steps_per_leg);
        line_text(3, buf, COL_GRAY);

        snprintf(buf, sizeof(buf), "REVS : %u", (unsigned)s.reversals);
        line_text(4, buf, COL_GRAY);

        ESP_LOGI(TAG, "steps=%lld dir=%s leg=%u/%u revs=%u",
                 (long long)s.step_count, fwd ? "FWD" : "REV",
                 (unsigned)s.steps_in_leg, (unsigned)s.steps_per_leg,
                 (unsigned)s.reversals);

        vTaskDelay(pdMS_TO_TICKS(LCD_REFRESH_PERIOD_MS));
    }
}
