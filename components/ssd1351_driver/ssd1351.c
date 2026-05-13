#include "ssd1351.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SSD1351";

// DC low tells the chip this byte is a command, not data
static esp_err_t ssd1351_write_command(ssd1351_t *dev, uint8_t cmd) {
    gpio_set_level(dev->dc_pin, 0);

    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .flags = 0
    };

    return spi_device_polling_transmit(dev->spi, &t);
}

// DC high tells the chip these bytes are data, not commands
static esp_err_t ssd1351_write_data(ssd1351_t *dev, const uint8_t *data, size_t len) {
    gpio_set_level(dev->dc_pin, 1);

    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0
    };

    return spi_device_polling_transmit(dev->spi, &t);
}

// Hardware reset pulse, forces the chip into a known state at boot
static void ssd1351_reset(ssd1351_t *dev) {
    gpio_set_level(dev->rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(dev->rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

// Set the GDDRAM window and arm WRITERAM so the next data write fills this rectangle
static esp_err_t ssd1351_set_addr_window(ssd1351_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
    ssd1351_write_command(dev, SSD1351_CMD_SETCOLUMN);
    uint8_t col_data[] = {x0, x1};
    ssd1351_write_data(dev, col_data, 2);

    ssd1351_write_command(dev, SSD1351_CMD_SETROW);
    uint8_t row_data[] = {y0, y1};
    ssd1351_write_data(dev, row_data, 2);

    ssd1351_write_command(dev, SSD1351_CMD_WRITERAM);
    return ESP_OK;
}

// One-shot setup, configures GPIO and SPI then runs the chip's power-on command sequence
esp_err_t ssd1351_init(ssd1351_t *dev, spi_host_device_t host,
                       gpio_num_t mosi_pin, gpio_num_t sclk_pin,
                       gpio_num_t cs_pin, gpio_num_t dc_pin, gpio_num_t rst_pin) {

    dev->dc_pin = dc_pin;
    dev->rst_pin = rst_pin;
    dev->width = SSD1351_WIDTH;
    dev->height = SSD1351_HEIGHT;

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dc_pin) | (1ULL << rst_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi_pin,
        .miso_io_num = -1,
        .sclk_io_num = sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SSD1351_WIDTH * SSD1351_HEIGHT * 2
    };

    // ESP_ERR_INVALID_STATE means another component already initialised the bus, which is fine
    esp_err_t ret = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 1 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = cs_pin,
        .queue_size = 7,
        .flags = 0
    };

    ret = spi_bus_add_device(host, &dev_cfg, &dev->spi);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ssd1351_reset(dev);

    // Two-stage command-set unlock, chip ships with most commands locked out
    ssd1351_write_command(dev, SSD1351_CMD_COMMANDLOCK);
    uint8_t unlock[] = {0x12};
    ssd1351_write_data(dev, unlock, 1);

    ssd1351_write_command(dev, SSD1351_CMD_COMMANDLOCK);
    uint8_t unlock2[] = {0xB1};
    ssd1351_write_data(dev, unlock2, 1);

    ssd1351_write_command(dev, SSD1351_CMD_DISPLAYOFF);

    ssd1351_write_command(dev, SSD1351_CMD_CLOCKDIV);
    uint8_t clk[] = {0xF1};
    ssd1351_write_data(dev, clk, 1);

    ssd1351_write_command(dev, SSD1351_CMD_MUXRATIO);
    uint8_t mux[] = {127};
    ssd1351_write_data(dev, mux, 1);

    // 0x74 selects 65k RGB565 with the orientation this panel expects
    ssd1351_write_command(dev, SSD1351_CMD_SETREMAP);
    uint8_t remap[] = {0x74};
    ssd1351_write_data(dev, remap, 1);

    ssd1351_write_command(dev, SSD1351_CMD_SETCOLUMN);
    uint8_t col[] = {0x00, 0x7F};
    ssd1351_write_data(dev, col, 2);

    ssd1351_write_command(dev, SSD1351_CMD_SETROW);
    uint8_t row[] = {0x00, 0x7F};
    ssd1351_write_data(dev, row, 2);

    ssd1351_write_command(dev, SSD1351_CMD_STARTLINE);
    uint8_t start[] = {0x00};
    ssd1351_write_data(dev, start, 1);

    ssd1351_write_command(dev, SSD1351_CMD_DISPLAYOFFSET);
    uint8_t offset[] = {0x00};
    ssd1351_write_data(dev, offset, 1);

    ssd1351_write_command(dev, SSD1351_CMD_SETGPIO);
    uint8_t gpio[] = {0x00};
    ssd1351_write_data(dev, gpio, 1);

    ssd1351_write_command(dev, SSD1351_CMD_FUNCTIONSELECT);
    uint8_t func[] = {0x01};
    ssd1351_write_data(dev, func, 1);

    ssd1351_write_command(dev, SSD1351_CMD_PRECHARGE);
    uint8_t pre[] = {0x32};
    ssd1351_write_data(dev, pre, 1);

    ssd1351_write_command(dev, SSD1351_CMD_VCOMH);
    uint8_t vcom[] = {0x05};
    ssd1351_write_data(dev, vcom, 1);

    ssd1351_write_command(dev, SSD1351_CMD_NORMALDISPLAY);

    // Per-channel contrast for A (blue), B (green), C (red)
    ssd1351_write_command(dev, SSD1351_CMD_CONTRASTABC);
    uint8_t contrast[] = {0xC8, 0x80, 0xC8};
    ssd1351_write_data(dev, contrast, 3);

    ssd1351_write_command(dev, SSD1351_CMD_CONTRASTMASTER);
    uint8_t master[] = {0x0F};
    ssd1351_write_data(dev, master, 1);

    ssd1351_write_command(dev, SSD1351_CMD_SETVSL);
    uint8_t vsl[] = {0xA0, 0xB5, 0x55};
    ssd1351_write_data(dev, vsl, 3);

    ssd1351_write_command(dev, SSD1351_CMD_PRECHARGE2);
    uint8_t pre2[] = {0x01};
    ssd1351_write_data(dev, pre2, 1);

    ssd1351_write_command(dev, SSD1351_CMD_DISPLAYON);

    ssd1351_fill_screen(dev, COLOR_BLACK);

    ESP_LOGI(TAG, "SSD1351 initialized (128x128 RGB OLED)");
    return ESP_OK;
}

esp_err_t ssd1351_fill_screen(ssd1351_t *dev, uint16_t color) {
    return ssd1351_fill_rect(dev, 0, 0, dev->width, dev->height, color);
}

esp_err_t ssd1351_draw_pixel(ssd1351_t *dev, uint16_t x, uint16_t y, uint16_t color) {
    if (x >= dev->width || y >= dev->height) return ESP_ERR_INVALID_ARG;

    ssd1351_set_addr_window(dev, x, y, x, y);

    uint8_t data[2] = {color >> 8, color & 0xFF};
    return ssd1351_write_data(dev, data, 2);
}

// Fills via one heap-allocated buffer and one DMA burst, faster than per-pixel writes
esp_err_t ssd1351_fill_rect(ssd1351_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
    if (x >= dev->width || y >= dev->height) return ESP_ERR_INVALID_ARG;
    if (x + w > dev->width) w = dev->width - x;
    if (y + h > dev->height) h = dev->height - y;

    ssd1351_set_addr_window(dev, x, y, x + w - 1, y + h - 1);

    uint8_t hi = color >> 8;
    uint8_t lo = color & 0xFF;

    uint32_t pixels = w * h;
    uint8_t *buffer = malloc(pixels * 2);
    if (!buffer) return ESP_ERR_NO_MEM;

    for (uint32_t i = 0; i < pixels * 2; i += 2) {
        buffer[i] = hi;
        buffer[i + 1] = lo;
    }

    esp_err_t ret = ssd1351_write_data(dev, buffer, pixels * 2);
    free(buffer);
    return ret;
}

// Bresenham line algorithm
esp_err_t ssd1351_draw_line(ssd1351_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    // Signed locals so sx/sy = -1 doesn't wrap
    int16_t cx0 = (int16_t)x0, cy0 = (int16_t)y0;
    int16_t cx1 = (int16_t)x1, cy1 = (int16_t)y1;
    int16_t dx = abs(cx1 - cx0);
    int16_t dy = abs(cy1 - cy0);
    int16_t sx = cx0 < cx1 ? 1 : -1;
    int16_t sy = cy0 < cy1 ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        ssd1351_draw_pixel(dev, (uint16_t)cx0, (uint16_t)cy0, color);

        if (cx0 == cx1 && cy0 == cy1) break;

        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            cx0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            cy0 += sy;
        }
    }

    return ESP_OK;
}

// Midpoint circle algorithm, uses 8-way symmetry to draw only 1/8 of the points
esp_err_t ssd1351_draw_circle(ssd1351_t *dev, int16_t x0, int16_t y0, int16_t radius, uint16_t color) {
    int16_t x = radius;
    int16_t y = 0;
    int16_t err = 0;

    while (x >= y) {
        ssd1351_draw_pixel(dev, (uint16_t)(x0 + x), (uint16_t)(y0 + y), color);
        ssd1351_draw_pixel(dev, (uint16_t)(x0 + y), (uint16_t)(y0 + x), color);
        ssd1351_draw_pixel(dev, (uint16_t)(x0 - y), (uint16_t)(y0 + x), color);
        ssd1351_draw_pixel(dev, (uint16_t)(x0 - x), (uint16_t)(y0 + y), color);
        ssd1351_draw_pixel(dev, (uint16_t)(x0 - x), (uint16_t)(y0 - y), color);
        ssd1351_draw_pixel(dev, (uint16_t)(x0 - y), (uint16_t)(y0 - x), color);
        ssd1351_draw_pixel(dev, (uint16_t)(x0 + y), (uint16_t)(y0 - x), color);
        ssd1351_draw_pixel(dev, (uint16_t)(x0 + x), (uint16_t)(y0 - y), color);

        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }

    return ESP_OK;
}

