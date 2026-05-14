#ifndef SERVO_H
#define SERVO_H

#include <stdint.h>
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_err.h"

// Configuration for a single hobby servo driven by an LEDC channel
typedef struct {
    gpio_num_t signal_pin;          // GPIO driving the servo's PWM signal
    ledc_timer_t ledc_timer;        // LEDC timer to use, e.g. LEDC_TIMER_0
    ledc_channel_t ledc_channel;    // LEDC channel to use, e.g. LEDC_CHANNEL_0
    uint32_t min_pulse_us;          // Pulse width for 0 degrees, typically 500 us
    uint32_t max_pulse_us;          // Pulse width for 180 degrees, typically 2500 us
} servo_config_t;

// Configure the LEDC timer and channel at 50 Hz, start the servo at 0 degrees
esp_err_t servo_init(const servo_config_t *cfg);

// Set the commanded angle, clamped to the 0 to 180 range
esp_err_t servo_set_angle(uint8_t angle_deg);

#endif
