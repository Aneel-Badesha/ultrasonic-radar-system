#include "ssd1351.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "SSD1351";

// Simple 5x7 font (ASCII 32-122)
static const uint8_t font_5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space (32)
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A (65)
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x01, 0x01}, // F
    {0x3E, 0x41, 0x41, 0x51, 0x32}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x7F, 0x20, 0x18, 0x20, 0x7F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x00, 0x7F, 0x41, 0x41}, // [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // backslash
    {0x41, 0x41, 0x7F, 0x00, 0x00}, // ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // ^
    {0x40, 0x40, 0x40, 0x40, 0x40}, // _
    {0x00, 0x01, 0x02, 0x04, 0x00}, // `
    {0x20, 0x54, 0x54, 0x54, 0x78}, // a (97)
    {0x7F, 0x48, 0x44, 0x44, 0x38}, // b
    {0x38, 0x44, 0x44, 0x44, 0x20}, // c
    {0x38, 0x44, 0x44, 0x48, 0x7F}, // d
    {0x38, 0x54, 0x54, 0x54, 0x18}, // e
    {0x08, 0x7E, 0x09, 0x01, 0x02}, // f
    {0x0C, 0x52, 0x52, 0x52, 0x3E}, // g
    {0x7F, 0x08, 0x04, 0x04, 0x78}, // h
    {0x00, 0x44, 0x7D, 0x40, 0x00}, // i
    {0x20, 0x40, 0x44, 0x3D, 0x00}, // j
    {0x00, 0x7F, 0x10, 0x28, 0x44}, // k
    {0x00, 0x41, 0x7F, 0x40, 0x00}, // l
    {0x7C, 0x04, 0x18, 0x04, 0x78}, // m
    {0x7C, 0x08, 0x04, 0x04, 0x78}, // n
    {0x38, 0x44, 0x44, 0x44, 0x38}, // o
    {0x7C, 0x14, 0x14, 0x14, 0x08}, // p
    {0x08, 0x14, 0x14, 0x18, 0x7C}, // q
    {0x7C, 0x08, 0x04, 0x04, 0x08}, // r
    {0x48, 0x54, 0x54, 0x54, 0x20}, // s
    {0x04, 0x3F, 0x44, 0x40, 0x20}, // t
    {0x3C, 0x40, 0x40, 0x20, 0x7C}, // u
    {0x1C, 0x20, 0x40, 0x20, 0x1C}, // v
    {0x3C, 0x40, 0x30, 0x40, 0x3C}, // w
    {0x44, 0x28, 0x10, 0x28, 0x44}, // x
    {0x0C, 0x50, 0x50, 0x50, 0x3C}, // y
    {0x44, 0x64, 0x54, 0x4C, 0x44}, // z (122)
};

// Send command to SSD1351
static esp_err_t ssd1351_write_command(ssd1351_t *dev, uint8_t cmd) {
    gpio_set_level(dev->dc_pin, 0); // Command mode
    
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
        .flags = 0
    };
    
    return spi_device_polling_transmit(dev->spi, &t);
}

// Send data to SSD1351
static esp_err_t ssd1351_write_data(ssd1351_t *dev, const uint8_t *data, size_t len) {
    gpio_set_level(dev->dc_pin, 1); // Data mode
    
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .flags = 0
    };
    
    return spi_device_polling_transmit(dev->spi, &t);
}

// Hardware reset
static void ssd1351_reset(ssd1351_t *dev) {
    gpio_set_level(dev->rst_pin, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(dev->rst_pin, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
}

// Set address window
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

esp_err_t ssd1351_init(ssd1351_t *dev, spi_host_device_t host,
                       gpio_num_t mosi_pin, gpio_num_t sclk_pin,
                       gpio_num_t cs_pin, gpio_num_t dc_pin, gpio_num_t rst_pin) {
    
    dev->dc_pin = dc_pin;
    dev->rst_pin = rst_pin;
    dev->width = SSD1351_WIDTH;
    dev->height = SSD1351_HEIGHT;
    
    // Configure DC and RST pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dc_pin) | (1ULL << rst_pin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    
    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi_pin,
        .miso_io_num = -1,
        .sclk_io_num = sclk_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SSD1351_WIDTH * SSD1351_HEIGHT * 2
    };
    
    esp_err_t ret = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure SPI device
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 1 * 1000 * 1000, // 1 MHz (Lowered for stability)
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
    
    // Hardware reset
    ssd1351_reset(dev);
    
    // Initialization sequence
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
    
    // Clear screen to black
    ssd1351_fill_screen(dev, 0x0000);

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

esp_err_t ssd1351_draw_line(ssd1351_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx - dy;
    
    while (1) {
        ssd1351_draw_pixel(dev, x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    
    return ESP_OK;
}

esp_err_t ssd1351_draw_char(ssd1351_t *dev, uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg) {
    if (c < 32 || c > 122) c = 32; // Space for non-printable
    
    const uint8_t *glyph = font_5x7[c - 32];
    
    for (uint8_t i = 0; i < 5; i++) {
        uint8_t line = glyph[i];
        for (uint8_t j = 0; j < 7; j++) {
            if (line & 0x01) {
                ssd1351_draw_pixel(dev, x + i, y + j, color);
            } else {
                ssd1351_draw_pixel(dev, x + i, y + j, bg);
            }
            line >>= 1;
        }
    }
    
    return ESP_OK;
}

esp_err_t ssd1351_draw_string(ssd1351_t *dev, uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg) {
    uint16_t cur_x = x;
    
    while (*str) {
        if (*str == '\n') {
            cur_x = x;
            y += 8;
        } else {
            ssd1351_draw_char(dev, cur_x, y, *str, color, bg);
            cur_x += 6;
        }
        str++;
    }
    
    return ESP_OK;
}

uint16_t ssd1351_color565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
