#include <stdio.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <math.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <ultrasonic.h>
#include <ssd1351.h>
#include <wifi_sta.h>
#include <http_uploader.h>
#include <esp_err.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

// WiFi credentials and dashboard endpoint
#define WIFI_SSID      "BadeshaHome"
#define WIFI_PASS      "Canucks@2011"
#define RPI_SERVER_URL "http://192.168.1.90:5000/api/radar"

// Ultrasonic ranging limits and pinout
#define MAX_DISTANCE_CM 200
#define MIN_DISTANCE_CM 2   // HC-SR04 noise floor
#define TRIGGER_GPIO 5
#define ECHO_GPIO 18

// Sensor-sample post-processing
#define SENSOR_FRESHNESS_TIMEOUT_MS 500    // Drop readings older than this from the display
#define SENSOR_FILTER_WINDOW 3             // Size of the median filter window

// FreeRTOS task tuning
#define SENSOR_TASK_STACK_SIZE     2048
#define DISPLAY_TASK_STACK_SIZE    3072
#define TASK_PRIORITY              5
#define SENSOR_UPDATE_RATE_MS      100
#define DISPLAY_UPDATE_RATE_MS     10

// Servo sweep, angles are in the display coordinate frame
#define SWEEP_ANGLE_START         180
#define SWEEP_ANGLE_END           360
#define SWEEP_ANGLE_STEP          2

// Radar overlay geometry on the 128x128 OLED
#define RADAR_CENTER_X     64
#define RADAR_CENTER_Y     110
#define RADAR_MAX_RADIUS   60
#define RADAR_GRID_COLOR   0x03E0    // Dim green RGB565
#define RADAR_DIAGONAL_OFFSET ((int)(RADAR_MAX_RADIUS * 0.7071f))    // cos(45) for the diagonal grid lines
static const int radar_grid_radii[] = {20, 40, 60};
#define RADAR_GRID_RADII_COUNT (sizeof(radar_grid_radii) / sizeof(radar_grid_radii[0]))

// OLED SPI bus and pinout
#define OLED_HOST    SPI2_HOST
#define OLED_MOSI    13
#define OLED_CLK     14
#define OLED_CS      15
#define OLED_DC      27
#define OLED_RST     26

static const char *TAG = "radar_sensor";

// Classified ultrasonic sensor outcome
typedef enum {
    SENSOR_OK = 0,
    SENSOR_ERR_NO_ECHO,
    SENSOR_ERR_PING_TIMEOUT,
    SENSOR_ERR_PING_BUSY,
    SENSOR_ERR_OUT_OF_RANGE,
    SENSOR_ERR_OTHER
} sensor_error_t;

// Latest filtered sensor reading, shared between sensor_task and display_task under distance_mutex
typedef struct {
    float distance_cm;
    sensor_error_t last_error;
    int64_t timestamp_us;    // esp_timer_get_time() snapshot, used for the freshness check
} sensor_sample_t;

static SemaphoreHandle_t distance_mutex;
static sensor_sample_t current_sample = {
    .distance_cm = -1.0f,
    .last_error = SENSOR_ERR_NO_ECHO,
    .timestamp_us = 0
};
static atomic_int current_angle = 180;       // Latest sweep angle, written by display_task and read by sensor_task

// Static overlay drawn each frame, range rings plus the four axis lines
static void draw_radar_grid(ssd1351_t *dev)
{
    const int cx = RADAR_CENTER_X;
    const int cy = RADAR_CENTER_Y;
    const int r  = RADAR_MAX_RADIUS;
    const int d  = RADAR_DIAGONAL_OFFSET;

    for (size_t i = 0; i < RADAR_GRID_RADII_COUNT; i++) {
        ssd1351_draw_circle(dev, cx, cy, radar_grid_radii[i], RADAR_GRID_COLOR);
    }
    ssd1351_draw_line(dev, cx - r, cy, cx + r, cy, RADAR_GRID_COLOR);
    ssd1351_draw_line(dev, cx, cy, cx, cy - r, RADAR_GRID_COLOR);
    ssd1351_draw_line(dev, cx, cy, cx - d, cy - d, RADAR_GRID_COLOR);
    ssd1351_draw_line(dev, cx, cy, cx + d, cy - d, RADAR_GRID_COLOR);
}

// Translate ultrasonic driver error codes into the local sensor_error_t enum
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

// Three-element median, sorts in place via three compares then returns the middle
static float median3(float a, float b, float c)
{
    if (a > b) { float t = a; a = b; b = t; }
    if (b > c) { float t = b; b = c; c = t; }
    if (a > b) { float t = a; a = b; b = t; }
    return b;
}

// FreeRTOS task, polls the ultrasonic sensor at SENSOR_UPDATE_RATE_MS and publishes filtered samples
static void sensor_task(void *pvParameters)
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
        if (wifi_sta_is_connected()) {
            int sweep_angle = atomic_load(&current_angle);
            int api_angle = sweep_angle - SWEEP_ANGLE_START;
            http_uploader_item_t item = {
                .angle = api_angle,
                .distance = filtered,
                .retry_count = 0
            };
            http_uploader_enqueue(item);
        }

        if (err == SENSOR_ERR_PING_TIMEOUT || err == SENSOR_ERR_PING_BUSY) {
            ESP_LOGW(TAG, "Ultrasonic sensor error class=%d (esp_err=%s)",
                     (int)err, esp_err_to_name(res));
        }

        vTaskDelay(pdMS_TO_TICKS(SENSOR_UPDATE_RATE_MS));
    }
}

// FreeRTOS task, sweeps the radar line and renders detected targets at DISPLAY_UPDATE_RATE_MS
static void display_task(void *pvParameters)
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

        sensor_sample_t snapshot;
        bool have_sample = false;
        if (xSemaphoreTake(distance_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            snapshot = current_sample;
            xSemaphoreGive(distance_mutex);
            have_sample = true;
        }

        // Reject readings older than SENSOR_FRESHNESS_TIMEOUT_MS
        float local_distance = -1.0f;
        if (have_sample) {
            int64_t age_us = esp_timer_get_time() - snapshot.timestamp_us;
            bool fresh = snapshot.timestamp_us != 0 &&
                         age_us < (SENSOR_FRESHNESS_TIMEOUT_MS * 1000);
            if (fresh && snapshot.last_error == SENSOR_OK) {
                local_distance = snapshot.distance_cm;
            }
        }

        if (local_distance >= MIN_DISTANCE_CM && local_distance <= MAX_DISTANCE_CM) {
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

// Entry point, initialises NVS and WiFi then spawns the three worker tasks
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

    wifi_sta_init(WIFI_SSID, WIFI_PASS);

    // Spawn the HTTP uploader before producers so the queue has a drain ready
    http_uploader_init(RPI_SERVER_URL, TASK_PRIORITY - 1);

    ESP_LOGI(TAG, "WiFi connected! Starting tasks...");

    xTaskCreate(sensor_task, "sensor_task", SENSOR_TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL);
    xTaskCreate(display_task, "display_task", DISPLAY_TASK_STACK_SIZE, NULL, TASK_PRIORITY, NULL);
}
