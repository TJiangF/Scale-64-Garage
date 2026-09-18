/*
 * Scale 64 Garage - TMC2208 #3 continuous back and forth + ST7789 status.
 *
 * Framework : ESP-IDF only (Arduino is not used).
 * Tasks     : motor pulse task + LCD refresh task, shared state behind a mutex.
 */
#include "app_config.h"
#include "app_state.h"
#include "display.h"
#include "motor_driver.h"
#include "self_test.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Scale 64 Garage boot (ESP-IDF, STEP/DIR only)");

    /* shared state + mutex must exist before any task uses it */
    ESP_ERROR_CHECK(app_state_init());

    /* pure logic verification, no hardware required */
    const bool self_test_ok = app_self_test_run();
    ESP_LOGI(TAG, "logic self test: %s", self_test_ok ? "PASS" : "FAIL");

    /* motor: STEP/DIR pins, then the pulse task */
    esp_err_t err = motor_driver_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "motor init failed: %s", esp_err_to_name(err));
    } else if ((err = motor_driver_start()) != ESP_OK) {
        ESP_LOGE(TAG, "motor task start failed: %s", esp_err_to_name(err));
    }

    /* display: optional, falls back to serial logging when absent */
    err = display_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "display init failed (%s), headless mode", esp_err_to_name(err));
    } else if ((err = display_start()) != ESP_OK) {
        ESP_LOGE(TAG, "lcd task start failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "running: motor pulses + lcd refresh");
}
