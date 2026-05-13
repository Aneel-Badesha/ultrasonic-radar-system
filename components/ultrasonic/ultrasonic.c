/**
 * @file ultrasonic.c
 *
 * ESP-IDF driver for ultrasonic range sensor HC-SR04
 *
 */
#include "ultrasonic.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp32/rom/ets_sys.h>

#define TRIGGER_LOW_DELAY  5         // Microseconds to hold trigger low before the pulse
#define TRIGGER_HIGH_DELAY 10        // Microseconds to hold trigger high
#define PING_TIMEOUT       6000      // Microseconds to wait for the sensor to start its echo pulse before giving up
#define ROUNDTRIP_M        5800.0f   // Microseconds per meter of round trip travel at the speed of sound

// Spinlock used to make the trigger and echo timing critical section atomic across cores
static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
#define PORT_ENTER_CRITICAL portENTER_CRITICAL(&mux)    // Disable interrupts and acquire the spinlock
#define PORT_EXIT_CRITICAL portEXIT_CRITICAL(&mux)      // Release the spinlock and re-enable interrupts

// True when len microseconds have elapsed since start, used for non-blocking timeouts in busy loops
#define timeout_expired(start, len) ((esp_timer_get_time() - (start)) >= (len))

#define CHECK_ARG(VAL) do { if (!(VAL)) return ESP_ERR_INVALID_ARG; } while (0)         // Bail out with ESP_ERR_INVALID_ARG if VAL is falsy
#define CHECK(x) do { esp_err_t __; if ((__ = x) != ESP_OK) return __; } while (0)      // Propagate any non-OK esp_err_t from x to the caller
#define RETURN_CRITICAL(RES) do { PORT_EXIT_CRITICAL; return RES; } while(0)            // Exit the spinlock-protected section then return RES

esp_err_t ultrasonic_init(const ultrasonic_sensor_t *dev)
{
    CHECK_ARG(dev);
    CHECK_ARG(GPIO_IS_VALID_OUTPUT_GPIO(dev->trigger_pin));
    CHECK_ARG(GPIO_IS_VALID_GPIO(dev->echo_pin));
    CHECK_ARG(dev->trigger_pin != dev->echo_pin);

    CHECK(gpio_set_direction(dev->trigger_pin, GPIO_MODE_OUTPUT));
    CHECK(gpio_set_direction(dev->echo_pin, GPIO_MODE_INPUT));

    return gpio_set_level(dev->trigger_pin, 0);
}

static esp_err_t ultrasonic_measure_raw(const ultrasonic_sensor_t *dev, uint32_t max_time_us, uint32_t *time_us)
{
    CHECK_ARG(dev && time_us);

    PORT_ENTER_CRITICAL;

    // 10us trigger pulse with a 5us low guard before it
    CHECK(gpio_set_level(dev->trigger_pin, 0));
    ets_delay_us(TRIGGER_LOW_DELAY);
    CHECK(gpio_set_level(dev->trigger_pin, 1));
    ets_delay_us(TRIGGER_HIGH_DELAY);
    CHECK(gpio_set_level(dev->trigger_pin, 0));

    if (gpio_get_level(dev->echo_pin))
        RETURN_CRITICAL(ESP_ERR_ULTRASONIC_PING);

    int64_t start = esp_timer_get_time();
    while (!gpio_get_level(dev->echo_pin))
    {
        if (timeout_expired(start, PING_TIMEOUT))
            RETURN_CRITICAL(ESP_ERR_ULTRASONIC_PING_TIMEOUT);
    }

    int64_t echo_start = esp_timer_get_time();
    int64_t time = echo_start;
    while (gpio_get_level(dev->echo_pin))
    {
        time = esp_timer_get_time();
        if (timeout_expired(echo_start, max_time_us))
            RETURN_CRITICAL(ESP_ERR_ULTRASONIC_ECHO_TIMEOUT);
    }
    PORT_EXIT_CRITICAL;

    *time_us = time - echo_start;

    return ESP_OK;
}

esp_err_t ultrasonic_measure(const ultrasonic_sensor_t *dev, float max_distance, float *distance)
{
    CHECK_ARG(dev && distance);

    uint32_t time_us;
    CHECK(ultrasonic_measure_raw(dev, max_distance * ROUNDTRIP_M, &time_us));
    *distance = time_us / ROUNDTRIP_M;

    return ESP_OK;
}

