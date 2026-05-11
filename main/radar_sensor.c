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
#include "esp_task_wdt.h"
#include "esp_timer.h"

#define WIFI_SSID      "BadeshaHome"
#define WIFI_PASS      "Canucks@2011"
#define RPI_SERVER_URL "http://192.168.1.90:5000/api/radar"

#define MAX_DISTANCE_CM 200
#define MIN_DISTANCE_CM 2   // HC-SR04 noise floor
#define TRIGGER_GPIO 5
#define ECHO_GPIO 18

#define SENSOR_FRESHNESS_TIMEOUT_MS 500
#define SENSOR_FILTER_WINDOW 3

#define SENSOR_TASK_STACK_SIZE     2048
#define DISPLAY_TASK_STACK_SIZE    3072
#define HTTP_SEND_TASK_STACK_SIZE  4096
#define TASK_PRIORITY              5
#define SENSOR_UPDATE_RATE_MS      100
#define DISPLAY_UPDATE_RATE_MS     10

#define HTTP_REQUEST_TIMEOUT_MS        1200
#define HTTP_RESPONSE_BUFFER_SIZE      192
#define HTTP_SEND_QUEUE_LENGTH         48
#define HTTP_MAX_RETRIES               3
#define HTTP_RETRY_INITIAL_BACKOFF_MS  300
#define HTTP_RETRY_MAX_BACKOFF_MS      3000
#define HTTP_RETRY_JITTER_MS           100
#define HTTP_STATS_LOG_INTERVAL_MS     5000

#define SWEEP_ANGLE_START         180
#define SWEEP_ANGLE_END           360
#define SWEEP_ANGLE_STEP          2

#define RADAR_CENTER_X     64
#define RADAR_CENTER_Y     110
#define RADAR_MAX_RADIUS   60
#define RADAR_GRID_COLOR   0x03E0
#define RADAR_DIAGONAL_OFFSET ((int)(RADAR_MAX_RADIUS * 0.7071f))
static const int RADAR_GRID_RADII[] = {20, 40, 60};
#define RADAR_GRID_RADII_COUNT (sizeof(RADAR_GRID_RADII) / sizeof(RADAR_GRID_RADII[0]))

#define OLED_HOST    SPI2_HOST
#define OLED_MOSI    13
#define OLED_CLK     14
#define OLED_CS      15
#define OLED_DC      27
#define OLED_RST     26

static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "radar_sensor";

typedef enum {
    SENSOR_OK = 0,
    SENSOR_ERR_NO_ECHO,
    SENSOR_ERR_PING_TIMEOUT,
    SENSOR_ERR_PING_BUSY,
    SENSOR_ERR_OUT_OF_RANGE,
    SENSOR_ERR_OTHER
} sensor_error_t;

typedef struct {
    float distance_cm;
    sensor_error_t last_error;
    int64_t timestamp_us;
} sensor_sample_t;

static SemaphoreHandle_t distance_mutex;
static sensor_sample_t current_sample = {
    .distance_cm = -1.0f,
    .last_error = SENSOR_ERR_NO_ECHO,
    .timestamp_us = 0
};
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
} http_send_item_t;

static QueueHandle_t http_send_queue = NULL;
static atomic_uint http_count_2xx;
static atomic_uint http_count_4xx;
static atomic_uint http_count_5xx;
static atomic_uint http_count_transport;
static atomic_uint http_count_enqueued;
static atomic_uint http_count_dropped_overflow;
static atomic_uint http_count_retries_exhausted;

static uint32_t compute_backoff_ms(uint8_t retry_count)
{
    // First attempt has no delay
    if (retry_count == 0) {
        return 0;
    }

    uint32_t backoff = HTTP_RETRY_INITIAL_BACKOFF_MS;
    for (uint8_t i = 1; i < retry_count && backoff < HTTP_RETRY_MAX_BACKOFF_MS; i++) {
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

// Non-blocking, drops oldest on overflow so fresh samples beat stale ones
static void enqueue_send_item(http_send_item_t item)
{
    if (http_send_queue == NULL) {
        return;
    }

    atomic_fetch_add(&http_count_enqueued, 1);

    if (xQueueSend(http_send_queue, &item, 0) != pdTRUE) {
        http_send_item_t dropped;
        if (xQueueReceive(http_send_queue, &dropped, 0) == pdTRUE) {
            xQueueSend(http_send_queue, &item, 0);
            atomic_fetch_add(&http_count_dropped_overflow, 1);
            ESP_LOGW(TAG, "Send queue full. Dropped oldest sample (angle=%d distance=%.1f retries=%u)",
                     dropped.angle, dropped.distance, dropped.retry_count);
        }
    }
}

static http_send_outcome_t post_radar_data_once(int api_angle, float distance, uint8_t retry_count)
{
    char post_data[64];
    char response_body[HTTP_RESPONSE_BUFFER_SIZE] = {0};

    snprintf(post_data, sizeof(post_data), "{\"angle\":%d,\"distance\":%.1f}", api_angle, distance);

    esp_http_client_config_t config = {
        .url = RPI_SERVER_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_REQUEST_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        atomic_fetch_add(&http_count_transport, 1);
        ESP_LOGW(TAG, "HTTP init failed (retry=%u)", retry_count);
        return HTTP_SEND_TRANSPORT_ERROR;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        atomic_fetch_add(&http_count_transport, 1);
        ESP_LOGW(TAG, "HTTP transport failure (retry=%u): %s", retry_count, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return HTTP_SEND_TRANSPORT_ERROR;
    }

    int status_code = esp_http_client_get_status_code(client);
    int response_len = esp_http_client_read_response(client, response_body, sizeof(response_body) - 1);
    if (response_len < 0) {
        response_len = 0;
    }
    response_body[response_len] = '\0';
    const char *body_str = response_len > 0 ? response_body : "<empty>";

    http_send_outcome_t outcome;
    atomic_uint *counter;
    const char *label;

    if (status_code >= 200 && status_code < 300) {
        atomic_fetch_add(&http_count_2xx, 1);
        esp_http_client_cleanup(client);
        return HTTP_SEND_OK;
    } else if (status_code >= 400 && status_code < 500) {
        outcome = HTTP_SEND_CLIENT_ERROR;
        counter = &http_count_4xx;
        label = "client error";
    } else if (status_code >= 500) {
        outcome = HTTP_SEND_SERVER_ERROR;
        counter = &http_count_5xx;
        label = "server error";
    } else {
        outcome = HTTP_SEND_TRANSPORT_ERROR;
        counter = &http_count_transport;
        label = "unexpected status";
    }

    atomic_fetch_add(counter, 1);
    ESP_LOGW(TAG, "HTTP %d (%s, retry=%u) body=%s", status_code, label, retry_count, body_str);
    esp_http_client_cleanup(client);
    return outcome;
}

// Handles first attempts and retries through a single queue
void http_send_task(void *pvParameters)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    TickType_t last_stats_log = xTaskGetTickCount();

    while (true)
    {
        esp_task_wdt_reset();

        http_send_item_t item;
        if (xQueueReceive(http_send_queue, &item, pdMS_TO_TICKS(200)) == pdTRUE) {
            uint32_t wait_ms = compute_backoff_ms(item.retry_count);
            if (wait_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
            }

            // Skip the HTTP timeout when WiFi is down
            if (!atomic_load(&wifi_connected)) {
                if (item.retry_count < HTTP_MAX_RETRIES) {
                    item.retry_count++;
                    enqueue_send_item(item);
                } else {
                    atomic_fetch_add(&http_count_retries_exhausted, 1);
                    ESP_LOGW(TAG, "Dropping sample after retries exhausted (wifi down): angle=%d distance=%.1f",
                             item.angle, item.distance);
                }
                continue;
            }

            http_send_outcome_t result = post_radar_data_once(item.angle, item.distance, item.retry_count);

            // 4xx is not retried since a bad payload won't get better
            if (result == HTTP_SEND_SERVER_ERROR || result == HTTP_SEND_TRANSPORT_ERROR) {
                if (item.retry_count < HTTP_MAX_RETRIES) {
                    item.retry_count++;
                    enqueue_send_item(item);
                } else {
                    atomic_fetch_add(&http_count_retries_exhausted, 1);
                    ESP_LOGW(TAG, "Dropping sample after retries exhausted: angle=%d distance=%.1f",
                             item.angle, item.distance);
                }
            }
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_stats_log) >= pdMS_TO_TICKS(HTTP_STATS_LOG_INTERVAL_MS)) {
            ESP_LOGI(TAG,
                     "HTTP stats 2xx=%u 4xx=%u 5xx=%u transport=%u enqueued=%u dropped=%u exhausted=%u",
                     atomic_load(&http_count_2xx),
                     atomic_load(&http_count_4xx),
                     atomic_load(&http_count_5xx),
                     atomic_load(&http_count_transport),
                     atomic_load(&http_count_enqueued),
                     atomic_load(&http_count_dropped_overflow),
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

static void draw_radar_grid(ssd1351_t *dev)
{
    const int cx = RADAR_CENTER_X;
    const int cy = RADAR_CENTER_Y;
    const int r  = RADAR_MAX_RADIUS;
    const int d  = RADAR_DIAGONAL_OFFSET;

    for (size_t i = 0; i < RADAR_GRID_RADII_COUNT; i++) {
        draw_circle(dev, cx, cy, RADAR_GRID_RADII[i], RADAR_GRID_COLOR);
    }
    ssd1351_draw_line(dev, cx - r, cy, cx + r, cy, RADAR_GRID_COLOR);
    ssd1351_draw_line(dev, cx, cy, cx, cy - r, RADAR_GRID_COLOR);
    ssd1351_draw_line(dev, cx, cy, cx - d, cy - d, RADAR_GRID_COLOR);
    ssd1351_draw_line(dev, cx, cy, cx + d, cy - d, RADAR_GRID_COLOR);
}

static sensor_error_t classify_sensor_error(esp_err_t res)
{
    switch (res) {
        case ESP_OK:                          return SENSOR_OK;
        case ESP_ERR_ULTRASONIC_ECHO_TIMEOUT: return SENSOR_ERR_NO_ECHO;
        case ESP_ERR_ULTRASONIC_PING_TIMEOUT: return SENSOR_ERR_PING_TIMEOUT;
        case ESP_ERR_ULTRASONIC_PING:         return SENSOR_ERR_PING_BUSY;
        default:                              return SENSOR_ERR_OTHER;
    }
}

static float median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    return b;
}

void sensor_task(void *pvParameters)
{
    ultrasonic_sensor_t sensor = {
        .trigger_pin = TRIGGER_GPIO,
        .echo_pin = ECHO_GPIO
    };

    ESP_ERROR_CHECK(ultrasonic_init(&sensor));
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    float window[SENSOR_FILTER_WINDOW];
    for (int i = 0; i < SENSOR_FILTER_WINDOW; i++) {
        window[i] = -1.0f;
    }
    int window_idx = 0;

    while (true)
    {
        esp_task_wdt_reset();

        float distance_m;
        esp_err_t res = ultrasonic_measure(&sensor, MAX_DISTANCE_CM / 100.0f, &distance_m);
        sensor_error_t err = classify_sensor_error(res);
        float distance_cm = (res == ESP_OK) ? (distance_m * 100.0f) : -1.0f;

        if (err == SENSOR_OK &&
            (distance_cm < MIN_DISTANCE_CM || distance_cm > MAX_DISTANCE_CM)) {
            err = SENSOR_ERR_OUT_OF_RANGE;
            distance_cm = -1.0f;
        }

        window[window_idx] = distance_cm;
        window_idx = (window_idx + 1) % SENSOR_FILTER_WINDOW;
        float filtered = median3(window[0], window[1], window[2]);

        if (xSemaphoreTake(distance_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            current_sample.distance_cm  = filtered;
            current_sample.last_error   = err;
            current_sample.timestamp_us = esp_timer_get_time();
            xSemaphoreGive(distance_mutex);
        }

        // Angle is captured at the measurement instant, not at send time
        if (atomic_load(&wifi_connected)) {
            int sweep_angle = atomic_load(&current_angle);
            int api_angle = sweep_angle - SWEEP_ANGLE_START;
            http_send_item_t item = {
                .angle = api_angle,
                .distance = filtered,
                .retry_count = 0
            };
            enqueue_send_item(item);
        }

        if (err == SENSOR_ERR_PING_TIMEOUT || err == SENSOR_ERR_PING_BUSY) {
            ESP_LOGW(TAG, "Ultrasonic sensor error class=%d (esp_err=%s)",
                     (int)err, esp_err_to_name(res));
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_UPDATE_RATE_MS));
    }
}

void display_task(void *pvParameters)
{
    ssd1351_t dev;
    ESP_ERROR_CHECK(ssd1351_init(&dev, OLED_HOST, OLED_MOSI, OLED_CLK, OLED_CS, OLED_DC, OLED_RST));

    ssd1351_fill_screen(&dev, COLOR_BLACK);

    int angle = SWEEP_ANGLE_START;
    int step = SWEEP_ANGLE_STEP;
    int prev_x = RADAR_CENTER_X - RADAR_MAX_RADIUS;
    int prev_y = RADAR_CENTER_Y;

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    while (true)
    {
        esp_task_wdt_reset();

        ssd1351_draw_line(&dev, RADAR_CENTER_X, RADAR_CENTER_Y, prev_x, prev_y, COLOR_BLACK);
        draw_radar_grid(&dev);

        // Sweep range [180, 360] maps cos/sin into the upper half on a Y-down screen
        float rad = angle * M_PI / 180.0;
        int x = RADAR_CENTER_X + (int)(RADAR_MAX_RADIUS * cos(rad));
        int y = RADAR_CENTER_Y + (int)(RADAR_MAX_RADIUS * sin(rad));

        ssd1351_draw_line(&dev, RADAR_CENTER_X, RADAR_CENTER_Y, x, y, COLOR_GREEN);
        prev_x = x;
        prev_y = y;

        atomic_store(&current_angle, angle);

        sensor_sample_t snapshot = { .distance_cm = -1.0f,
                                     .last_error = SENSOR_ERR_OTHER,
                                     .timestamp_us = 0 };
        if (xSemaphoreTake(distance_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            snapshot = current_sample;
            xSemaphoreGive(distance_mutex);
        }

        // Reject readings older than SENSOR_FRESHNESS_TIMEOUT_MS
        int64_t age_us = esp_timer_get_time() - snapshot.timestamp_us;
        bool fresh = snapshot.timestamp_us != 0 &&
                     age_us < (SENSOR_FRESHNESS_TIMEOUT_MS * 1000);
        float local_distance = (fresh && snapshot.last_error == SENSOR_OK)
                                 ? snapshot.distance_cm
                                 : -1.0f;

        if (local_distance >= MIN_DISTANCE_CM && local_distance < MAX_DISTANCE_CM) {
            int r = (int)((local_distance / MAX_DISTANCE_CM) * RADAR_MAX_RADIUS);
            int bx = RADAR_CENTER_X + (int)(r * cos(rad));
            int by = RADAR_CENTER_Y + (int)(r * sin(rad));
            ssd1351_fill_rect(&dev, bx-2, by-2, 5, 5, COLOR_RED);
        }

        angle += step;
        if (angle > SWEEP_ANGLE_END || angle < SWEEP_ANGLE_START) {
            step = -step;
        }

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_UPDATE_RATE_MS));
    }
}

void app_main()
{
    ESP_LOGI(TAG, "Radar Sensor Starting...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    distance_mutex = xSemaphoreCreateMutex();
    if (distance_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create distance mutex");
        return;
    }

    wifi_init();

    http_send_queue = xQueueCreate(HTTP_SEND_QUEUE_LENGTH, sizeof(http_send_item_t));
    if (http_send_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP send queue");
        return;
    }

    // Spawn consumer before producers so the queue has a drain ready
    xTaskCreate(http_send_task,
                "http_send_task",
                HTTP_SEND_TASK_STACK_SIZE,
                NULL,
                TASK_PRIORITY - 1,
                NULL);

    ESP_LOGI(TAG, "WiFi connected! Starting tasks...");

    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL);
    xTaskCreate(display_task, "display_task", DISPLAY_TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL);
}
