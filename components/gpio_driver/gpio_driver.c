#include "gpio_driver.h"
#include "esp_log.h"

static const char *TAG = "ds_gpio";

esp_err_t ds_gpio_init(gpio_num_t gpio_num, bool output)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << gpio_num),
        .mode = output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT,
        .pull_up_en = output ? GPIO_PULLUP_DISABLE : GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_LOGD(TAG, "GPIO initialized as %s on pin %d", output ? "OUTPUT" : "INPUT", gpio_num);
    return gpio_config(&cfg);
}

esp_err_t ds_gpio_write(gpio_num_t gpio_num, uint32_t level)
{
    return gpio_set_level(gpio_num, level);
}

esp_err_t ds_gpio_deinit(gpio_num_t gpio_num)
{
    return gpio_reset_pin(gpio_num);
}

uint32_t ds_gpio_read(gpio_num_t gpio_num)
{
    return (uint32_t)gpio_get_level(gpio_num);
}