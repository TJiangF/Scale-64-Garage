#include "self_test.h"

#include "app_config.h"
#include "motion_core.h"

#include "esp_log.h"

#include <stddef.h>

static const char *TAG = "SELFTEST";

/* --------------------------------------------------------------- pin check */
typedef struct {
    const char *name;
    int         pin;
} pin_desc_t;

static bool pin_is_reserved(int pin)
{
    if (pin < 0) {
        return false;
    }
    if (pin >= 26 && pin <= 37) {
        return true;                    /* SPI0/1 flash + octal PSRAM */
    }
    switch (pin) {
        case 0:
        case 3:
        case 45:
        case 46:
            return true;                /* strapping pins */
        case 43:
        case 44:
            return true;                /* UART0 console */
        default:
            return false;
    }
}

static bool self_test_pins(void)
{
    static const pin_desc_t pins[] = {
        { "LCD_RST",   LCD_PIN_RST },
        { "LCD_MOSI",  LCD_PIN_MOSI },
        { "LCD_SCLK",  LCD_PIN_SCLK },
        { "LCD_DC",    LCD_PIN_DC },
        { "LCD_CS",    LCD_PIN_CS },
        { "TMC3_STEP", TMC3_PIN_STEP },
        { "TMC3_DIR",  TMC3_PIN_DIR },
        { "TMC1_STEP", TMC1_PIN_STEP },
        { "TMC1_DIR",  TMC1_PIN_DIR },
        { "TMC2_STEP", TMC2_PIN_STEP },
        { "TMC2_DIR",  TMC2_PIN_DIR },
        { "SERVO_PWM", SERVO_PIN_PWM },
    };
    const size_t n = sizeof(pins) / sizeof(pins[0]);
    bool ok = true;

    for (size_t i = 0; i < n; i++) {
        const int p = pins[i].pin;
        if (p < 0) {
            continue;                   /* unassigned / reserved for later */
        }
        if (p > 48) {
            ESP_LOGE(TAG, "pin: %s uses out-of-range GPIO%d", pins[i].name, p);
            ok = false;
        }
        if (pin_is_reserved(p)) {
            ESP_LOGE(TAG, "pin: %s uses reserved GPIO%d", pins[i].name, p);
            ok = false;
        }
        for (size_t j = i + 1; j < n; j++) {
            if (pins[j].pin == p) {
                ESP_LOGE(TAG, "pin: %s and %s both use GPIO%d",
                         pins[i].name, pins[j].name, p);
                ok = false;
            }
        }
    }
    return ok;
}

/* ------------------------------------------------------------ motion check */
static bool self_test_motion(void)
{
    enum { LEG = 7, STEPS = 50 };

    motion_state_t m;
    motion_init(&m, LEG, MOTOR_DIR_FORWARD);

    bool     ok       = true;
    int64_t  prev     = 0;
    uint32_t last_rev = 0;

    for (int i = 0; i < STEPS; i++) {
        (void)motion_advance(&m);

        if (m.step_count != prev + 1) {
            ESP_LOGE(TAG, "motion: step counter jumped %lld -> %lld",
                     (long long)prev, (long long)m.step_count);
            ok = false;
        }
        if (m.step_count <= prev) {
            ESP_LOGE(TAG, "motion: step counter did not grow (reset on reverse?)");
            ok = false;
        }
        if (m.steps_in_leg >= LEG) {
            ESP_LOGE(TAG, "motion: leg overflow %u", (unsigned)m.steps_in_leg);
            ok = false;
        }
        if (m.reversals < last_rev) {
            ESP_LOGE(TAG, "motion: reversal counter decreased");
            ok = false;
        }
        prev     = m.step_count;
        last_rev = m.reversals;
    }

    if (m.step_count != STEPS) {
        ESP_LOGE(TAG, "motion: expected %d steps, got %lld",
                 STEPS, (long long)m.step_count);
        ok = false;
    }
    if (m.reversals != STEPS / LEG) {
        ESP_LOGE(TAG, "motion: expected %d reversals, got %u",
                 STEPS / LEG, (unsigned)m.reversals);
        ok = false;
    }
    if (m.steps_in_leg != (uint32_t)(STEPS % LEG)) {
        ESP_LOGE(TAG, "motion: leg position %u, expected %d",
                 (unsigned)m.steps_in_leg, STEPS % LEG);
        ok = false;
    }
    if (m.dir != MOTOR_DIR_REVERSE) {   /* 7 flips starting from FORWARD */
        ESP_LOGE(TAG, "motion: direction %d, expected REVERSE",
                 (int)m.dir);
        ok = false;
    }

    /* a zero leg length must not cause a divide by zero / endless leg */
    motion_state_t zero;
    motion_init(&zero, 0, MOTOR_DIR_FORWARD);
    if (zero.steps_per_leg != 1) {
        ESP_LOGE(TAG, "motion: zero leg not coerced to 1");
        ok = false;
    }
    motion_advance(&zero);
    if (zero.reversals != 1 || zero.dir != MOTOR_DIR_REVERSE) {
        ESP_LOGE(TAG, "motion: zero leg reverse handling broken");
        ok = false;
    }

    return ok;
}

bool app_self_test_run(void)
{
    const bool pins_ok   = self_test_pins();
    const bool motion_ok = self_test_motion();

    ESP_LOGI(TAG, "pin map      : %s", pins_ok ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "motion logic : %s", motion_ok ? "PASS" : "FAIL");

    return pins_ok && motion_ok;
}
