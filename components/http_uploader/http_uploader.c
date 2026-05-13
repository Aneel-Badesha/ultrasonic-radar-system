#include "http_uploader.h"
#include "wifi_sta.h"
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_task_wdt.h"
#include "esp_system.h"
#include "esp_random.h"

static const char *TAG = "http_uploader";

static QueueHandle_t s_queue = NULL;
static const char *s_url = NULL;

// HTTP throughput counters
static atomic_uint s_count_2xx;
static atomic_uint s_count_4xx;
static atomic_uint s_count_5xx;
static atomic_uint s_count_transport;
static atomic_uint s_count_enqueued;
static atomic_uint s_count_dropped_overflow;
static atomic_uint s_count_retries_exhausted;

// Exponential backoff with jitter for HTTP retries, capped at HTTP_RETRY_MAX_BACKOFF_MS
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

// Single HTTP POST attempt, returns a classified outcome and bumps the matching counter
static http_send_outcome_t post_radar_data_once(int api_angle, float distance, uint8_t retry_count)
{
    char post_data[64];
    char response_body[HTTP_RESPONSE_BUFFER_SIZE] = {0};

    snprintf(post_data, sizeof(post_data), "{\"angle\":%d,\"distance\":%.1f}", api_angle, distance);

    esp_http_client_config_t config = {
        .url = s_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = HTTP_REQUEST_TIMEOUT_MS,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        atomic_fetch_add(&s_count_transport, 1);
        ESP_LOGW(TAG, "HTTP init failed (retry=%u)", retry_count);
        return HTTP_SEND_TRANSPORT_ERROR;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        atomic_fetch_add(&s_count_transport, 1);
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
        atomic_fetch_add(&s_count_2xx, 1);
        esp_http_client_cleanup(client);
        return HTTP_SEND_OK;
    } else if (status_code >= 400 && status_code < 500) {
        outcome = HTTP_SEND_CLIENT_ERROR;
        counter = &s_count_4xx;
        label = "client error";
    } else if (status_code >= 500) {
        outcome = HTTP_SEND_SERVER_ERROR;
        counter = &s_count_5xx;
        label = "server error";
    } else {
        outcome = HTTP_SEND_TRANSPORT_ERROR;
        counter = &s_count_transport;
        label = "unexpected status";
    }

    atomic_fetch_add(counter, 1);
    ESP_LOGW(TAG, "HTTP %d (%s, retry=%u) body=%s", status_code, label, retry_count, body_str);
    esp_http_client_cleanup(client);
    return outcome;
}

// Handles first attempts and retries through a single queue
static void http_uploader_task(void *pvParameters)
{
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    TickType_t last_stats_log = xTaskGetTickCount();

    while (true)
    {
        esp_task_wdt_reset();

        http_uploader_item_t item;
        if (xQueueReceive(s_queue, &item, pdMS_TO_TICKS(200)) == pdTRUE) {
            uint32_t wait_ms = compute_backoff_ms(item.retry_count);
            if (wait_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
            }

            // Skip the HTTP timeout when WiFi is down
            if (!wifi_sta_is_connected()) {
                if (item.retry_count < HTTP_MAX_RETRIES) {
                    item.retry_count++;
                    http_uploader_enqueue(item);
                } else {
                    atomic_fetch_add(&s_count_retries_exhausted, 1);
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
                    http_uploader_enqueue(item);
                } else {
                    atomic_fetch_add(&s_count_retries_exhausted, 1);
                    ESP_LOGW(TAG, "Dropping sample after retries exhausted: angle=%d distance=%.1f",
                             item.angle, item.distance);
                }
            }
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_stats_log) >= pdMS_TO_TICKS(HTTP_STATS_LOG_INTERVAL_MS)) {
            ESP_LOGI(TAG,
                     "HTTP stats 2xx=%u 4xx=%u 5xx=%u transport=%u enqueued=%u dropped=%u exhausted=%u",
                     atomic_load(&s_count_2xx),
                     atomic_load(&s_count_4xx),
                     atomic_load(&s_count_5xx),
                     atomic_load(&s_count_transport),
                     atomic_load(&s_count_enqueued),
                     atomic_load(&s_count_dropped_overflow),
                     atomic_load(&s_count_retries_exhausted));
            last_stats_log = now;
        }
    }
}

void http_uploader_init(const char *url, UBaseType_t task_priority)
{
    s_url = url;

    s_queue = xQueueCreate(HTTP_SEND_QUEUE_LENGTH, sizeof(http_uploader_item_t));
    if (s_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create HTTP send queue");
        return;
    }

    xTaskCreate(http_uploader_task,
                "http_uploader_task",
                HTTP_TASK_STACK_SIZE,
                NULL,
                task_priority,
                NULL);
}

void http_uploader_enqueue(http_uploader_item_t item)
{
    if (s_queue == NULL) {
        return;
    }

    atomic_fetch_add(&s_count_enqueued, 1);

    if (xQueueSend(s_queue, &item, 0) != pdTRUE) {
        http_uploader_item_t dropped;
        if (xQueueReceive(s_queue, &dropped, 0) == pdTRUE) {
            xQueueSend(s_queue, &item, 0);
            atomic_fetch_add(&s_count_dropped_overflow, 1);
            ESP_LOGW(TAG, "Send queue full. Dropped oldest sample (angle=%d distance=%.1f retries=%u)",
                     dropped.angle, dropped.distance, dropped.retry_count);
        }
    }
}