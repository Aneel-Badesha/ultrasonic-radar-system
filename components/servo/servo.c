#include "servo.h"
#include "esp_log.h"

#define SERVO_FREQ_HZ            50                          // Hobby servos expect a 20 ms period
#define SERVO_LEDC_RESOLUTION    LEDC_TIMER_14_BIT           // 14 bits at 50 Hz gives ~1.2 us per duty step
#define SERVO_LEDC_SPEED_MODE    LEDC_LOW_SPEED_MODE
#define SERVO_MAX_DUTY           ((1 << 14) - 1)             // Full-scale duty value for 14-bit resolution
#define SERVO_PERIOD_US          (1000000 / SERVO_FREQ_HZ)   // 20000 us at 50 Hz
#define SERVO_MAX_ANGLE_DEG      180

static const char *TAG = "servo";

static servo_config_t s_cfg;
static bool s_initialised = false;

// Linear interpolation from angle 0-180 to the configured min/max pulse width
static uint32_t angle_to_duty(uint8_t angle_deg)
{
    if (angle_deg > SERVO_MAX_ANGLE_DEG) {
        angle_deg = SERVO_MAX_ANGLE_DEG;
    }

    uint32_t span_us = s_cfg.max_pulse_us - s_cfg.min_pulse_us;
    uint32_t pulse_us = s_cfg.min_pulse_us + (span_us * angle_deg) / SERVO_MAX_ANGLE_DEG;

    // Convert pulse width to duty value, duty = (pulse_us / period_us) * full_scale
    return (pulse_us * SERVO_MAX_DUTY) / SERVO_PERIOD_US;
}

esp_err_t servo_init(const servo_config_t *cfg)
{
    if (cfg == NULL || cfg->max_pulse_us <= cfg->min_pulse_us) {
        return ESP_ERR_INVALID_ARG;
    }

    s_cfg = *cfg;

    ledc_timer_config_t timer_cfg = {
        .speed_mode      = SERVO_LEDC_SPEED_MODE,
        .timer_num       = cfg->ledc_timer,
        .duty_resolution = SERVO_LEDC_RESOLUTION,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    ledc_channel_config_t channel_cfg = {
        .gpio_num   = cfg->signal_pin,
        .speed_mode = SERVO_LEDC_SPEED_MODE,
        .channel    = cfg->ledc_channel,
        .timer_sel  = cfg->ledc_timer,
        .duty       = angle_to_duty(0),
        .hpoint     = 0,
        .intr_type  = LEDC_INTR_DISABLE,
    };
    err = ledc_channel_config(&channel_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialised = true;
    ESP_LOGI(TAG, "Servo initialised on GPIO %d (LEDC timer %d, channel %d)",
             cfg->signal_pin, cfg->ledc_timer, cfg->ledc_channel);
    return ESP_OK;
}

esp_err_t servo_set_angle(uint8_t angle_deg)
{
    if (!s_initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t duty = angle_to_duty(angle_deg);

    esp_err_t err = ledc_set_duty(SERVO_LEDC_SPEED_MODE, s_cfg.ledc_channel, duty);
    if (err != ESP_OK) {
        return err;
    }
    return ledc_update_duty(SERVO_LEDC_SPEED_MODE, s_cfg.ledc_channel);
}
