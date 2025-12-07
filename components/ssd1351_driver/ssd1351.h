#ifndef SSD1351_H
#define SSD1351_H

#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

// SSD1351 Commands
#define SSD1351_CMD_SETCOLUMN       0x15
#define SSD1351_CMD_SETROW          0x75
#define SSD1351_CMD_WRITERAM        0x5C
#define SSD1351_CMD_READRAM         0x5D
#define SSD1351_CMD_SETREMAP        0xA0
#define SSD1351_CMD_STARTLINE       0xA1
#define SSD1351_CMD_DISPLAYOFFSET   0xA2
#define SSD1351_CMD_DISPLAYALLOFF   0xA4
#define SSD1351_CMD_DISPLAYALLON    0xA5
#define SSD1351_CMD_NORMALDISPLAY   0xA6
#define SSD1351_CMD_INVERTDISPLAY   0xA7
#define SSD1351_CMD_FUNCTIONSELECT  0xAB
#define SSD1351_CMD_DISPLAYOFF      0xAE
#define SSD1351_CMD_DISPLAYON       0xAF
#define SSD1351_CMD_PRECHARGE       0xB1
#define SSD1351_CMD_DISPLAYENHANCE  0xB2
#define SSD1351_CMD_CLOCKDIV        0xB3
#define SSD1351_CMD_SETVSL          0xB4
#define SSD1351_CMD_SETGPIO         0xB5
#define SSD1351_CMD_PRECHARGE2      0xB6
#define SSD1351_CMD_SETGRAY         0xB8
#define SSD1351_CMD_USELUT          0xB9
#define SSD1351_CMD_PRECHARGELEVEL  0xBB
#define SSD1351_CMD_VCOMH           0xBE
#define SSD1351_CMD_CONTRASTABC     0xC1
#define SSD1351_CMD_CONTRASTMASTER  0xC7
#define SSD1351_CMD_MUXRATIO        0xCA
#define SSD1351_CMD_COMMANDLOCK     0xFD
#define SSD1351_CMD_HORIZSCROLL     0x96
#define SSD1351_CMD_STOPSCROLL      0x9E
#define SSD1351_CMD_STARTSCROLL     0x9F

// Display dimensions
#define SSD1351_WIDTH   128
#define SSD1351_HEIGHT  128

// Color definitions (16-bit RGB565 format)
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_YELLOW    0xFFE0
#define COLOR_ORANGE    0xFC00

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t dc_pin;
    gpio_num_t rst_pin;
    uint16_t width;
    uint16_t height;
} ssd1351_t;

/**
 * @brief Initialize SSD1351 RGB OLED display
 * 
 * @param dev Pointer to SSD1351 device structure
 * @param host SPI host (SPI2_HOST or SPI3_HOST)
 * @param mosi_pin MOSI GPIO pin
 * @param sclk_pin SCLK GPIO pin
 * @param cs_pin CS GPIO pin
 * @param dc_pin DC (Data/Command) GPIO pin
 * @param rst_pin RST (Reset) GPIO pin
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ssd1351_init(ssd1351_t *dev, spi_host_device_t host, 
                       gpio_num_t mosi_pin, gpio_num_t sclk_pin, 
                       gpio_num_t cs_pin, gpio_num_t dc_pin, gpio_num_t rst_pin);

/**
 * @brief Fill entire screen with one color
 * 
 * @param dev Pointer to SSD1351 device structure
 * @param color 16-bit RGB565 color
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ssd1351_fill_screen(ssd1351_t *dev, uint16_t color);

/**
 * @brief Draw a single pixel
 * 
 * @param dev Pointer to SSD1351 device structure
 * @param x X coordinate
 * @param y Y coordinate
 * @param color 16-bit RGB565 color
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ssd1351_draw_pixel(ssd1351_t *dev, uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief Draw a filled rectangle
 * 
 * @param dev Pointer to SSD1351 device structure
 * @param x X coordinate
 * @param y Y coordinate
 * @param w Width
 * @param h Height
 * @param color 16-bit RGB565 color
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ssd1351_fill_rect(ssd1351_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

/**
 * @brief Draw a line
 * 
 * @param dev Pointer to SSD1351 device structure
 * @param x0 Start X coordinate
 * @param y0 Start Y coordinate
 * @param x1 End X coordinate
 * @param y1 End Y coordinate
 * @param color 16-bit RGB565 color
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ssd1351_draw_line(ssd1351_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

/**
 * @brief Draw a character
 * 
 * @param dev Pointer to SSD1351 device structure
 * @param x X coordinate
 * @param y Y coordinate
 * @param c Character to draw
 * @param color Foreground color
 * @param bg Background color
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ssd1351_draw_char(ssd1351_t *dev, uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg);

/**
 * @brief Draw a string
 * 
 * @param dev Pointer to SSD1351 device structure
 * @param x X coordinate
 * @param y Y coordinate
 * @param str String to draw
 * @param color Foreground color
 * @param bg Background color
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ssd1351_draw_string(ssd1351_t *dev, uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg);

/**
 * @brief Convert RGB888 to RGB565
 * 
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @return uint16_t RGB565 color
 */
uint16_t ssd1351_color565(uint8_t r, uint8_t g, uint8_t b);

#endif // SSD1351_H
