#include <stdio.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <ultrasonic.h>
#include <ssd1351.h>
#include <esp_err.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_http_client.h"

// WiFi Configuration - CHANGE THESE!
#define WIFI_SSID      "BadeshaHome"
#define WIFI_PASS      "Canucks@2011"
#define RPI_SERVER_URL "http://192.168.1.90:5000/api/radar"

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

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "radar_sensor";

// Shared state
volatile float current_distance_cm = -1.0;
volatile int current_angle = 180;
volatile bool wifi_connected = false;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        wifi_connected = false;
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialized. Connecting to %s...", WIFI_SSID);
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

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

        // Send data to RPi via WiFi HTTP POST
        if (wifi_connected) {
            char post_data[64];
            snprintf(post_data, sizeof(post_data), "{\"angle\":%d,\"distance\":%.1f}", angle, current_distance_cm);
            
            esp_http_client_config_t config = {
                .url = RPI_SERVER_URL,
                .method = HTTP_METHOD_POST,
            };
            
            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "Content-Type", "application/json");
            esp_http_client_set_post_field(client, post_data, strlen(post_data));
            
            esp_err_t err = esp_http_client_perform(client);
            if (err != ESP_OK) {
                ESP_LOGD(TAG, "HTTP POST failed: %s", esp_err_to_name(err));
            }
            
            esp_http_client_cleanup(client);
        }

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
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize WiFi
    wifi_init();
    
    ESP_LOGI(TAG, "WiFi connected! Starting tasks...");
    
    xTaskCreate(sensor_task, "sensor_task", 2048, NULL, 5, NULL);
    xTaskCreate(display_task, "display_task", 8192, NULL, 5, NULL);
}
