#ifndef __ULTRASONIC_H__
#define __ULTRASONIC_H__

#include <driver/gpio.h>
#include <esp_err.h>

// Error codes returned by the ultrasonic driver
#define ESP_ERR_ULTRASONIC_PING             0x200   // Echo line was already high before triggering
#define ESP_ERR_ULTRASONIC_PING_TIMEOUT     0x201   // Sensor never started its echo pulse after the trigger
#define ESP_ERR_ULTRASONIC_ECHO_TIMEOUT     0x202   // Echo pulse never ended, target out of range or no reflection

// Pin configuration for an HC-SR04 ultrasonic sensor
typedef struct  {
    gpio_num_t trigger_pin;
    gpio_num_t echo_pin;
} ultrasonic_sensor_t;

// Configure trigger and echo GPIOs
esp_err_t ultrasonic_init(const ultrasonic_sensor_t *dev);

// Measure distance in meters, capped at max_distance
esp_err_t ultrasonic_measure(const ultrasonic_sensor_t *dev, float max_distance, float *distance);


#endif /* __ULTRASONIC_H__ */
