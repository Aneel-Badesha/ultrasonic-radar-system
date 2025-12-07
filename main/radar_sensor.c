#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ultrasonic.h>
#include <ssd1351.h>
#include <esp_err.h>
#include "esp_log.h"
#include "driver/uart.h"

#define MAX_DISTANCE_CM 200 // 2m max for display scaling
#define TRIGGER_GPIO 5
#define ECHO_GPIO 18

// OLED Pins (HSPI / SPI2)
#define OLED_HOST    SPI2_HOST
#define OLED_MOSI    13
#define OLED_CLK     14
#define OLED_CS      15
#define OLED_DC      27
#define OLED_RST     26

// UART Pins (UART0 - USB)
#define UART_NUM     UART_NUM_0
#define UART_BAUD    115200

static const char *TAG = "radar_sensor";

// Shared state
volatile float current_distance_cm = -1.0;
volatile int current_angle = 180;

void draw_circle(ssd1351_t *dev, int x0, int y0, int radius, uint16_t color) {
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        ssd1351_draw_pixel(dev, x0 + x, y0 + y, color);
        ssd1351_draw_pixel(dev, x0 + y, y0 + x, color);
        ssd1351_draw_pixel(dev, x0 - y, y0 + x, color);
        ssd1351_draw_pixel(dev, x0 - x, y0 + y, color);
        ssd1351_draw_pixel(dev, x0 - x, y0 - y, color);
        ssd1351_draw_pixel(dev, x0 - y, y0 - x, color);
        ssd1351_draw_pixel(dev, x0 + y, y0 - x, color);
        ssd1351_draw_pixel(dev, x0 + x, y0 - y, color);

        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
}

void sensor_task(void *pvParameters)
{
    ultrasonic_sensor_t sensor = {
        .trigger_pin = TRIGGER_GPIO,
        .echo_pin = ECHO_GPIO
    };

    ultrasonic_init(&sensor);

    while (true)
    {
        float distance;
        esp_err_t res = ultrasonic_measure(&sensor, MAX_DISTANCE_CM, &distance);
        if (res == ESP_OK) {
            current_distance_cm = distance * 100; // Convert to cm
            // printf("Dist: %.1f cm\n", current_distance_cm);
        } else {
            current_distance_cm = -1.0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void display_task(void *pvParameters)
{
    ssd1351_t dev;
    ESP_ERROR_CHECK(ssd1351_init(&dev, OLED_HOST, OLED_MOSI, OLED_CLK, OLED_CS, OLED_DC, OLED_RST));

    ssd1351_fill_screen(&dev, COLOR_BLACK);

    // Radar Center (Bottom Middle)
    int cx = 64;
    int cy = 110;
    int max_radius = 60;

    // Draw static grid (Semi-circles)
    draw_circle(&dev, cx, cy, 20, 0x03E0); // Dark Green
    draw_circle(&dev, cx, cy, 40, 0x03E0);
    draw_circle(&dev, cx, cy, 60, 0x03E0);
    // Horizontal line
    ssd1351_draw_line(&dev, cx-60, cy, cx+60, cy, 0x03E0);
    // Vertical line
    ssd1351_draw_line(&dev, cx, cy, cx, cy-60, 0x03E0);
    // Diagonal lines (45 and 135 degrees)
    // 225 deg (Left-Up diagonal): cos(225)=-0.707, sin(225)=-0.707
    // 315 deg (Right-Up diagonal): cos(315)=0.707, sin(315)=-0.707
    ssd1351_draw_line(&dev, cx, cy, cx - 42, cy - 42, 0x03E0);
    ssd1351_draw_line(&dev, cx, cy, cx + 42, cy - 42, 0x03E0);

    int angle = 180;
    int step = 2;
    int prev_x = cx - 60, prev_y = cy;
    
    while (true)
    {
        // Erase old sweep line
        ssd1351_draw_line(&dev, cx, cy, prev_x, prev_y, COLOR_BLACK);
        
        // Redraw grid parts that might have been erased
        draw_circle(&dev, cx, cy, 20, 0x03E0);
        draw_circle(&dev, cx, cy, 40, 0x03E0);
        draw_circle(&dev, cx, cy, 60, 0x03E0);
        ssd1351_draw_line(&dev, cx-60, cy, cx+60, cy, 0x03E0);
        ssd1351_draw_line(&dev, cx, cy, cx, cy-60, 0x03E0);
        ssd1351_draw_line(&dev, cx, cy, cx - 42, cy - 42, 0x03E0);
        ssd1351_draw_line(&dev, cx, cy, cx + 42, cy - 42, 0x03E0);

        // Calculate new sweep line
        // 180 (Left) -> 270 (Up) -> 360 (Right)
        float rad = angle * M_PI / 180.0;
        int x = cx + (int)(max_radius * cos(rad));
        int y = cy + (int)(max_radius * sin(rad));

        // Draw sweep line
        ssd1351_draw_line(&dev, cx, cy, x, y, COLOR_GREEN);
        prev_x = x;
        prev_y = y;

        // Update shared angle for UART transmission
        current_angle = angle;

        // Draw Blip at current angle if distance is valid
        if (current_distance_cm > 0 && current_distance_cm < MAX_DISTANCE_CM) {
            // Map distance to pixels
            int r = (int)((current_distance_cm / MAX_DISTANCE_CM) * max_radius);
            
            // Calculate blip position along the CURRENT sweep line
            int bx = cx + (int)(r * cos(rad));
            int by = cy + (int)(r * sin(rad));
            
            // Draw a red blip
            ssd1351_fill_rect(&dev, bx-2, by-2, 5, 5, COLOR_RED);
        }

        // Send data to RPi via UART (JSON format)
        char uart_msg[64];
        snprintf(uart_msg, sizeof(uart_msg), "{\"angle\":%d,\"distance\":%.1f}\n", angle, current_distance_cm);
        uart_write_bytes(UART_NUM, uart_msg, strlen(uart_msg));

        // Update angle (Ping-Pong sweep)
        angle += step;
        if (angle >= 360 || angle <= 180) {
            step = -step; // Reverse direction
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main()
{
    ESP_LOGI(TAG, "Radar Sensor Starting...");
    
    // Configure UART
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM, 1024, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    
    ESP_LOGI(TAG, "UART initialized on UART0 @ %d baud", UART_BAUD);
    
    xTaskCreate(sensor_task, "sensor_task", 2048, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 4096, NULL, 5, NULL);
}
