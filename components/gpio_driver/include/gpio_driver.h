#ifndef DS_GPIO_H
#define DS_GPIO_H

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t ds_gpio_init(gpio_num_t gpio_num, bool output);
esp_err_t ds_gpio_write(gpio_num_t gpio_num, uint32_t level);
esp_err_t ds_gpio_deinit(gpio_num_t gpio_num);
uint32_t ds_gpio_read(gpio_num_t gpio_num);

#endif // DS_GPIO_H
