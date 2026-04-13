#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <math.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <ultrasonic.h>
#include <ssd1351.h>
#include <esp_err.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_http_client.h"

// WiFi Configuration - CHANGE THESE!
#define WIFI_SSID      "BadeshaHome"
#define WIFI_PASS      "Canucks@2011"
#define RPI_SERVER_URL "http://192.168.1.90:5000/api/radar"

#define MAX_DISTANCE_CM 200 // 2m max for display scaling
#define TRIGGER_GPIO 5
#define ECHO_GPIO 18

// Task Configuration
#define SENSOR_TASK_STACK_SIZE    2048
#define DISPLAY_TASK_STACK_SIZE   8192
#define HTTP_RETRY_TASK_STACK_SIZE 4096
#define TASK_PRIORITY             5
#define SENSOR_UPDATE_RATE_MS     100
#define DISPLAY_UPDATE_RATE_MS    10

// HTTP Retry/Telemetry Configuration
#define HTTP_REQUEST_TIMEOUT_MS        1200
#define HTTP_RESPONSE_BUFFER_SIZE      192
#define HTTP_RETRY_QUEUE_LENGTH        24
#define HTTP_MAX_RETRIES               3
#define HTTP_RETRY_INITIAL_BACKOFF_MS  300
#define HTTP_RETRY_MAX_BACKOFF_MS      3000
#define HTTP_RETRY_JITTER_MS           100
#define HTTP_STATS_LOG_INTERVAL_MS     5000

// Radar Sweep Configuration
#define SWEEP_ANGLE_START         180
#define SWEEP_ANGLE_END           360
#define SWEEP_ANGLE_STEP          2

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

// Shared state (protected by mutex or atomics)
static SemaphoreHandle_t distance_mutex;
volatile float current_distance_cm = -1.0;
atomic_int current_angle = 180;
atomic_bool wifi_connected = false;

typedef enum {
    HTTP_SEND_OK = 0,
    HTTP_SEND_CLIENT_ERROR,
    HTTP_SEND_SERVER_ERROR,
    HTTP_SEND_TRANSPORT_ERROR
} http_send_outcome_t;

typedef struct {
    int angle;
    float distance;
    uint8_t retry_count;
} http_retry_item_t;

static QueueHandle_t http_retry_queue = NULL;
static atomic_uint http_count_2xx;
static atomic_uint http_count_4xx;
static atomic_uint http_count_5xx;
static atomic_uint http_count_transport;
static atomic_uint http_count_retries_enqueued;
static atomic_uint http_count_retries_exhausted;

static uint32_t compute_backoff_ms(uint8_t retry_count)
{
    uint32_t backoff = HTTP_RETRY_INITIAL_BACKOFF_MS;
    for (uint8_t i = 0; i < retry_count && backoff < HTTP_RETRY_MAX_BACKOFF_MS; i++) {
        backoff *= 2;
        if (backoff > HTTP_RETRY_MAX_BACKOFF_MS) {
            backoff = HTTP_RETRY_MAX_BACKOFF_MS;
            break;
        }
    }

    int jitter_window = (HTTP_RETRY_JITTER_MS * 2) + 1;
    int jitter = ((int)(esp_random() % jitter_window)) - HTTP_RETRY_JITTER_MS;
    int32_t jittered = (int32_t)backoff + jitter;
    if (jittered < 0) {
        jittered = 0;
    }

    return (uint32_t)jittered;
}

static void enqueue_retry_item(http_retry_item_t item)
{
    if (http_retry_queue == NULL) {
        return;
    }

    if (xQueueSend(http_retry_queue, &item, 0) != pdTRUE) {
        http_retry_item_t dropped;
        if (xQueueReceive(http_retry_queue, &dropped, 0) == pdTRUE) {
            xQueueSend(http_retry_queue, &item, 0);
            ESP_LOGW(TAG, "Retry queue full. Dropped oldest sample (angle=%d distance=%.1f)", dropped.angle, dropped.distance);
        }
    }
}

static http_send_outcome_t post_radar_data_once(int api_angle, float distance, bool from_retry)
{
    char post_data[64];
    char response_body[HTTP_RESPONSE_BUFFER_SIZE] = {0};
    int response_len = 0;
    int status_code = -1;

    snprintf(post_data, sizeof(post_data), "{\"angle\":%d,\"distance\":%.1f}", api_angle, distance);

    esp_http_client_config_t config = {
        .url = RPI_SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_REQUEST_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        atomic_fetch_add(&http_count_transport, 1);
        ESP_LOGW(TAG, "HTTP init failed");
        return HTTP_SEND_TRANSPORT_ERROR;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        atomic_fetch_add(&http_count_transport, 1);
        ESP_LOGW(TAG, "HTTP transport failure%s: %s", from_retry ? " (retry)" : "", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return HTTP_SEND_TRANSPORT_ERROR;
    }

    status_code = esp_http_client_get_status_code(client);
    response_len = esp_http_client_read_response(client, response_body, sizeof(response_body) - 1);
    if (response_len < 0) {
        response_len = 0;
    }
    response_body[response_len] = '\0';

    if (status_code >= 200 && status_code < 300) {
        atomic_fetch_add(&http_count_2xx, 1);
        esp_http_client_cleanup(client);
        return HTTP_SEND_OK;
    }

    if (status_code >= 400 && status_code < 500) {
        atomic_fetch_add(&http_count_4xx, 1);
        ESP_LOGW(TAG, "HTTP %d (client error)%s body=%s", status_code, from_retry ? " (retry)" : "", response_len > 0 ? response_body : "<empty>");
        esp_http_client_cleanup(client);
        return HTTP_SEND_CLIENT_ERROR;
    }

    if (status_code >= 500) {
        atomic_fetch_add(&http_count_5xx, 1);
        ESP_LOGW(TAG, "HTTP %d (server error)%s body=%s", status_code, from_retry ? " (retry)" : "", response_len > 0 ? response_body : "<empty>");
        esp_http_client_cleanup(client);
        return HTTP_SEND_SERVER_ERROR;
    }

    atomic_fetch_add(&http_count_transport, 1);
    ESP_LOGW(TAG, "HTTP unexpected status=%d%s body=%s", status_code, from_retry ? " (retry)" : "", response_len > 0 ? response_body : "<empty>");
    esp_http_client_cleanup(client);
    return HTTP_SEND_TRANSPORT_ERROR;
}

void http_retry_task(void *pvParameters)
{
    TickType_t last_stats_log = xTaskGetTickCount();

    while (true)
    {
        http_retry_item_t item;
        if (xQueueReceive(http_retry_queue, &item, pdMS_TO_TICKS(200)) == pdTRUE) {
            uint32_t wait_ms = compute_backoff_ms(item.retry_count);
            if (wait_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
            }

            if (!atomic_load(&wifi_connected)) {
                if (item.retry_count < HTTP_MAX_RETRIES) {
                    item.retry_count++;
                    atomic_fetch_add(&http_count_retries_enqueued, 1);
                    enqueue_retry_item(item);
                } else {
                    atomic_fetch_add(&http_count_retries_exhausted, 1);
                    ESP_LOGW(TAG, "Dropping sample after retries exhausted (wifi down): angle=%d distance=%.1f", item.angle, item.distance);
                }
                continue;
            }

            http_send_outcome_t retry_result = post_radar_data_once(item.angle, item.distance, true);
            if (retry_result == HTTP_SEND_SERVER_ERROR || retry_result == HTTP_SEND_TRANSPORT_ERROR) {
                if (item.retry_count < HTTP_MAX_RETRIES) {
                    item.retry_count++;
                    atomic_fetch_add(&http_count_retries_enqueued, 1);
                    enqueue_retry_item(item);
                } else {
                    atomic_fetch_add(&http_count_retries_exhausted, 1);
                    ESP_LOGW(TAG, "Dropping sample after retries exhausted: angle=%d distance=%.1f", item.angle, item.distance);
                }
            }
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_stats_log) >= pdMS_TO_TICKS(HTTP_STATS_LOG_INTERVAL_MS)) {
            ESP_LOGI(TAG,
                     "HTTP stats 2xx=%u 4xx=%u 5xx=%u transport=%u queued_retries=%u exhausted=%u",
                     atomic_load(&http_count_2xx),
                     atomic_load(&http_count_4xx),
                     atomic_load(&http_count_5xx),
                     atomic_load(&http_count_transport),
                     atomic_load(&http_count_retries_enqueued),
                     atomic_load(&http_count_retries_exhausted));
            last_stats_log = now;
        }
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        atomic_store(&wifi_connected, false);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        atomic_store(&wifi_connected, true);
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
        
        // Update shared variable with mutex protection
        if (xSemaphoreTake(distance_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            if (res == ESP_OK) {
                current_distance_cm = distance * 100; // Convert to cm
                // printf("Dist: %.1f cm\n", current_distance_cm);
            } else {
                current_distance_cm = -1.0;
            }
            xSemaphoreGive(distance_mutex);
        }
        
        vTaskDelay(pdMS_TO_TICKS(SENSOR_UPDATE_RATE_MS));
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

    int angle = SWEEP_ANGLE_START;
    int step = SWEEP_ANGLE_STEP;
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

        // Update shared angle
        atomic_store(&current_angle, angle);

        // Read distance with mutex protection
        float local_distance = -1.0;
        if (xSemaphoreTake(distance_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            local_distance = current_distance_cm;
            xSemaphoreGive(distance_mutex);
        }

        // Draw Blip at current angle if distance is valid
        if (local_distance > 0 && local_distance < MAX_DISTANCE_CM) {
            // Map distance to pixels
            int r = (int)((local_distance / MAX_DISTANCE_CM) * max_radius);
            
            // Calculate blip position along the CURRENT sweep line
            int bx = cx + (int)(r * cos(rad));
            int by = cy + (int)(r * sin(rad));
            
            // Draw a red blip
            ssd1351_fill_rect(&dev, bx-2, by-2, 5, 5, COLOR_RED);
        }

        // Send data to RPi via WiFi HTTP POST
        if (atomic_load(&wifi_connected)) {
            // Keep display sweep in [180,360], but report [0,180] to match server validation.
            int api_angle = angle - SWEEP_ANGLE_START;
            http_send_outcome_t send_result = post_radar_data_once(api_angle, local_distance, false);
            if (send_result == HTTP_SEND_SERVER_ERROR || send_result == HTTP_SEND_TRANSPORT_ERROR) {
                http_retry_item_t retry_item = {
                    .angle = api_angle,
                    .distance = local_distance,
                    .retry_count = 0
                };
                atomic_fetch_add(&http_count_retries_enqueued, 1);
                enqueue_retry_item(retry_item);
            }
        }

        // Update angle
        angle += step;
        if (angle > SWEEP_ANGLE_END || angle < SWEEP_ANGLE_START) {
            step = -step; // Reverse direction
        }

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_RATE_MS));
    }
}

void app_main()
{
    ESP_LOGI(TAG, "Radar Sensor Starting...");
    
    // Initialize NVS for WiFi
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create mutex for shared data protection
    distance_mutex = xSemaphoreCreateMutex();
    if (distance_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create distance mutex");
        return;
    }
    
    // Initialize WiFi
    wifi_init();

    http_retry_queue = xQueueCreate(HTTP_RETRY_QUEUE_LENGTH, sizeof(http_retry_item_t));
    if (http_retry_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP retry queue");
        return;
    }

    xTaskCreate(http_retry_task,
                "http_retry_task",
                HTTP_RETRY_TASK_STACK_SIZE,
                NULL,
                TASK_PRIORITY - 1,
                NULL);
    
    ESP_LOGI(TAG, "WiFi connected! Starting tasks...");
    
    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL);
    xTaskCreate(display_task, "display_task", DISPLAY_TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL);
}
