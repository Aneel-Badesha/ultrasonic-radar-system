#ifndef SSD1351_H
#define SSD1351_H

#include <stdint.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

// SSD1351 commands
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

// SSD1351 Max width & height in pixels
#define SSD1351_WIDTH   128
#define SSD1351_HEIGHT  128

// RGB565 color codes
#define COLOR_BLACK     0x0000
#define COLOR_WHITE     0xFFFF
#define COLOR_RED       0xF800
#define COLOR_GREEN     0x07E0
#define COLOR_BLUE      0x001F
#define COLOR_CYAN      0x07FF
#define COLOR_MAGENTA   0xF81F
#define COLOR_YELLOW    0xFFE0
#define COLOR_ORANGE    0xFC00

// Per-display handle, populated by ssd1351_init and passed to every other driver call
typedef struct {
    spi_device_handle_t spi;    // SPI device handle returned by spi_bus_add_device
    gpio_num_t dc_pin;          // Data/Command pin, low for commands and high for data
    gpio_num_t rst_pin;         // Hardware reset pin, set low once during init
    uint16_t width;             // Display width in pixels
    uint16_t height;            // Display height in pixels
} ssd1351_t;

// Initialise the SPI bus and the display, call once before any draw call
esp_err_t ssd1351_init(ssd1351_t *dev, spi_host_device_t host,
                       gpio_num_t mosi_pin, gpio_num_t sclk_pin,
                       gpio_num_t cs_pin, gpio_num_t dc_pin, gpio_num_t rst_pin);

// Paint the entire display with a single RGB565 colour
esp_err_t ssd1351_fill_screen(ssd1351_t *dev, uint16_t color);

// Set a single pixel at (x, y) to the given RGB565 colour
esp_err_t ssd1351_draw_pixel(ssd1351_t *dev, uint16_t x, uint16_t y, uint16_t color);

// Fill a w by h rectangle anchored at (x, y) with the given RGB565 colour
esp_err_t ssd1351_fill_rect(ssd1351_t *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

// Draw a line from (x0, y0) to (x1, y1) using Bresenham's algorithm
esp_err_t ssd1351_draw_line(ssd1351_t *dev, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);

// Draw an outlined circle of the given radius centred at (x0, y0), pixels outside the panel are clipped
esp_err_t ssd1351_draw_circle(ssd1351_t *dev, int16_t x0, int16_t y0, int16_t radius, uint16_t color);

#endif
