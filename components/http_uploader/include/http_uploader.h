#ifndef HTTP_UPLOADER_H
#define HTTP_UPLOADER_H

#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// HTTP client tuning and retry policy
#define HTTP_REQUEST_TIMEOUT_MS        1200    // Max time for one POST attempt before giving up
#define HTTP_RESPONSE_BUFFER_SIZE      192     // Bytes reserved for the response body, used for error logging
#define HTTP_SEND_QUEUE_LENGTH         48      // Slots in the producer-consumer queue, oldest is dropped on overflow
#define HTTP_MAX_RETRIES               3       // Retry limit per sample
#define HTTP_RETRY_INITIAL_BACKOFF_MS  300     // Delay before the first retry, doubles each subsequent attempt
#define HTTP_RETRY_MAX_BACKOFF_MS      3000    // Ceiling for exponential backoff
#define HTTP_RETRY_JITTER_MS           100     // Random plus or minus offset added to backoff to de-sync clients
#define HTTP_STATS_LOG_INTERVAL_MS     5000    // How often the worker logs throughput counters
#define HTTP_TASK_STACK_SIZE           4096    // Worker task stack in bytes, sized for the lwIP and TLS call chain

// Classification of a single HTTP attempt
typedef enum {
    HTTP_SEND_OK = 0,
    HTTP_SEND_CLIENT_ERROR,      // 4xx, payload is bad and retrying won't help
    HTTP_SEND_SERVER_ERROR,      // 5xx, retryable
    HTTP_SEND_TRANSPORT_ERROR    // No response, retryable
} http_send_outcome_t;

// One pending POST, queued by the producer and consumed by the uploader task
typedef struct {
    int angle;
    float distance;
    uint8_t retry_count;    // Bumped on each requeue so backoff grows
} http_uploader_item_t;

// Initialise the queue and spawn the worker task, must be called after wifi_sta_init
void http_uploader_init(const char *url, UBaseType_t task_priority);

// Non-blocking enqueue, drops oldest on overflow so fresh samples beat stale ones
void http_uploader_enqueue(http_uploader_item_t item);

#endif
