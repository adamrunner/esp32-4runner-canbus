/*
 * CAN Logger Implementation
 *
 * Uses a ring buffer and background task for efficient SD card writes.
 * The ring buffer allows the CAN RX task to continue without blocking
 * on SD card operations.
 */

#include <stdint.h>
#include <string.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "can_logger.h"
#include "sd_card.h"
#include "rtc_pcf85063a.h"

static const char *TAG = "can_logger";

// Ring buffer item: timestamp + message
typedef struct {
    int64_t timestamp_us;
    can_logger_message_t msg;
} ring_buffer_item_t;

#define CAN_BIN_MAGIC "CANBIN\0"
#define CAN_BIN_VERSION 1
#define CAN_BIN_HEADER_SIZE 64
#define CAN_BIN_RECORD_SIZE 24

typedef struct __attribute__((packed)) {
    char magic[8];
    uint16_t version;
    uint16_t header_size;
    uint64_t log_start_unix_us;
    uint64_t log_start_monotonic_us;
    uint32_t record_size;
    uint32_t flags;
    uint8_t reserved[28];
} can_bin_header_v1_t;

typedef struct __attribute__((packed)) {
    uint64_t timestamp_us;
    uint32_t can_id;
    uint8_t dlc;
    uint8_t flags;
    uint8_t data[8];
    uint16_t reserved;
} can_bin_record_v1_t;

_Static_assert(sizeof(can_bin_header_v1_t) == CAN_BIN_HEADER_SIZE,
               "Binary header size mismatch");
_Static_assert(sizeof(can_bin_record_v1_t) == CAN_BIN_RECORD_SIZE,
               "Binary record size mismatch");

// Write buffer size (bytes) - tuned for binary records
#define WRITE_BUFFER_SIZE 65536
#define WRITER_TASK_STACK_SIZE 4096
#define WRITER_TASK_PRIORITY 5  // Higher priority for keeping up with CAN traffic
#define FLUSH_INTERVAL_MS 1000

// Module state
static struct {
    bool initialized;
    can_logger_state_t state;
    RingbufHandle_t ring_buffer;
    bool ring_buffer_caps_allocated;
    TaskHandle_t writer_task;
    SemaphoreHandle_t stats_mutex;
    void *log_file;
    char current_file[64];
    can_logger_stats_t stats;
    uint64_t log_start_unix_us;
    uint64_t log_start_monotonic_us;
    char *write_buffer;
    size_t write_buffer_size;
    size_t write_buffer_pos;
    int64_t last_flush_time;
} s_logger = {
    .initialized = false,
    .state = CAN_LOGGER_STOPPED,
    .ring_buffer = NULL,
    .ring_buffer_caps_allocated = false,
    .writer_task = NULL,
    .stats_mutex = NULL,
    .log_file = NULL,
    .write_buffer = NULL,
    .write_buffer_size = 0,
    .write_buffer_pos = 0,
    .last_flush_time = 0
};

static bool rtc_datetime_to_unix_us(const pcf_datetime_t *time, uint64_t *unix_us_out)
{
    if (!time || !unix_us_out)
    {
        return false;
    }

    struct tm tm_time = {
        .tm_year = time->year - 1900,
        .tm_mon = time->month - 1,
        .tm_mday = time->day,
        .tm_hour = time->hour,
        .tm_min = time->min,
        .tm_sec = time->sec,
        .tm_isdst = -1
    };

    time_t epoch = mktime(&tm_time);
    if (epoch < 0)
    {
        return false;
    }

    *unix_us_out = ((uint64_t)epoch) * 1000000ULL;
    return true;
}

static void update_stat_atomic(uint32_t *stat, int32_t delta)
{
    if (s_logger.stats_mutex)
    {
        xSemaphoreTake(s_logger.stats_mutex, portMAX_DELAY);
        *stat += delta;
        xSemaphoreGive(s_logger.stats_mutex);
    }
}

static esp_err_t flush_write_buffer(void)
{
    if (s_logger.write_buffer_pos == 0 || !s_logger.log_file || !s_logger.write_buffer)
    {
        return ESP_OK;
    }

    int written = sd_card_write(s_logger.log_file, s_logger.write_buffer,
                                 s_logger.write_buffer_pos);

    if (written < 0 || (size_t)written != s_logger.write_buffer_pos)
    {
        update_stat_atomic(&s_logger.stats.write_errors, 1);
        ESP_LOGE(TAG, "Write error: expected %zu, wrote %d",
                 s_logger.write_buffer_pos, written);
        return ESP_FAIL;
    }

    xSemaphoreTake(s_logger.stats_mutex, portMAX_DELAY);
    s_logger.stats.bytes_written += written;
    xSemaphoreGive(s_logger.stats_mutex);

    s_logger.write_buffer_pos = 0;
    s_logger.last_flush_time = esp_timer_get_time() / 1000;

    return ESP_OK;
}

static esp_err_t buffer_write(const void *data, size_t len)
{
    // If data won't fit, flush first
    if (!s_logger.write_buffer || s_logger.write_buffer_size == 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_logger.write_buffer_pos + len > s_logger.write_buffer_size)
    {
        esp_err_t err = flush_write_buffer();
        if (err != ESP_OK)
        {
            return err;
        }
    }

    // If data is larger than buffer, write directly
    if (len > s_logger.write_buffer_size)
    {
        int written = sd_card_write(s_logger.log_file, data, len);
        if (written < 0 || (size_t)written != len)
        {
            update_stat_atomic(&s_logger.stats.write_errors, 1);
            return ESP_FAIL;
        }
        xSemaphoreTake(s_logger.stats_mutex, portMAX_DELAY);
        s_logger.stats.bytes_written += written;
        xSemaphoreGive(s_logger.stats_mutex);
        return ESP_OK;
    }

    // Copy to buffer
    memcpy(s_logger.write_buffer + s_logger.write_buffer_pos, data, len);
    s_logger.write_buffer_pos += len;

    return ESP_OK;
}

static esp_err_t write_bin_header(void)
{
    can_bin_header_v1_t header = {0};
    memcpy(header.magic, CAN_BIN_MAGIC, sizeof(header.magic));
    header.version = CAN_BIN_VERSION;
    header.header_size = CAN_BIN_HEADER_SIZE;
    header.log_start_unix_us = s_logger.log_start_unix_us;
    header.log_start_monotonic_us = s_logger.log_start_monotonic_us;
    header.record_size = CAN_BIN_RECORD_SIZE;
    header.flags = 0;

    esp_err_t err = buffer_write(&header, sizeof(header));
    if (err != ESP_OK)
    {
        update_stat_atomic(&s_logger.stats.write_errors, 1);
        return err;
    }

    return ESP_OK;
}

static esp_err_t write_bin_record(const ring_buffer_item_t *item)
{
    can_bin_record_v1_t record = {0};
    record.timestamp_us = (uint64_t)item->timestamp_us;
    record.can_id = item->msg.identifier;
    record.dlc = item->msg.data_length_code;
    record.flags = 0;
    memcpy(record.data, item->msg.data, sizeof(record.data));
    record.reserved = 0;

    esp_err_t err = buffer_write(&record, sizeof(record));
    if (err == ESP_OK)
    {
        update_stat_atomic(&s_logger.stats.messages_logged, 1);
    }
    else
    {
        update_stat_atomic(&s_logger.stats.write_errors, 1);
    }

    return err;
}

static void writer_task(void *arg)
{
    ESP_LOGI(TAG, "Writer task started");

    if (write_bin_header() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to write binary header");
        s_logger.state = CAN_LOGGER_ERROR;
        flush_write_buffer();
        sd_card_flush(s_logger.log_file);
        ESP_LOGI(TAG, "Writer task stopped");
        vTaskDelete(NULL);
        return;
    }

    flush_write_buffer();

    while (s_logger.state == CAN_LOGGER_RUNNING)
    {
        size_t item_size = 0;
        int messages_processed = 0;

        // Batch process: drain all available messages without waiting
        ring_buffer_item_t *item;
        while ((item = xRingbufferReceive(s_logger.ring_buffer, &item_size, 0)) != NULL)
        {
            write_bin_record(item);
            vRingbufferReturnItem(s_logger.ring_buffer, item);
            messages_processed++;

            // Flush write buffer when nearly full (record size = 24 bytes)
            if (s_logger.write_buffer_pos > s_logger.write_buffer_size - CAN_BIN_RECORD_SIZE)
            {
                flush_write_buffer();
            }
        }

        // If no messages were available, wait briefly for new ones
        if (messages_processed == 0)
        {
            item = xRingbufferReceive(s_logger.ring_buffer, &item_size, pdMS_TO_TICKS(20));
            if (item)
            {
                write_bin_record(item);
                vRingbufferReturnItem(s_logger.ring_buffer, item);
            }
        }

        // Periodic flush to ensure data reaches SD card
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - s_logger.last_flush_time > FLUSH_INTERVAL_MS)
        {
            flush_write_buffer();
            sd_card_flush(s_logger.log_file);
        }
    }

    // Drain remaining items
    size_t item_size = 0;
    ring_buffer_item_t *item;
    while ((item = xRingbufferReceive(s_logger.ring_buffer, &item_size, 0)) != NULL)
    {
        write_bin_record(item);
        vRingbufferReturnItem(s_logger.ring_buffer, item);
    }

    // Final flush
    flush_write_buffer();
    sd_card_flush(s_logger.log_file);

    ESP_LOGI(TAG, "Writer task stopped");
    vTaskDelete(NULL);
}

esp_err_t can_logger_init(size_t ring_buffer_bytes)
{
    if (s_logger.initialized)
    {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (!sd_card_is_mounted())
    {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    // Create stats mutex
    s_logger.stats_mutex = xSemaphoreCreateMutex();
    if (!s_logger.stats_mutex)
    {
        ESP_LOGE(TAG, "Failed to create stats mutex");
        return ESP_ERR_NO_MEM;
    }

    if (ring_buffer_bytes == 0)
    {
        ESP_LOGE(TAG, "Ring buffer size must be > 0");
        vSemaphoreDelete(s_logger.stats_mutex);
        s_logger.stats_mutex = NULL;
        return ESP_ERR_INVALID_ARG;
    }

    size_t buffer_bytes = (ring_buffer_bytes + 3) & ~((size_t)3);
    s_logger.ring_buffer = xRingbufferCreateWithCaps(
        buffer_bytes,
        RINGBUF_TYPE_NOSPLIT,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (s_logger.ring_buffer)
    {
        s_logger.ring_buffer_caps_allocated = true;
    }
    else
    {
        s_logger.ring_buffer = xRingbufferCreate(buffer_bytes, RINGBUF_TYPE_NOSPLIT);
        s_logger.ring_buffer_caps_allocated = false;
    }

    if (!s_logger.ring_buffer)
    {
        ESP_LOGE(TAG, "Failed to create ring buffer (%zu bytes)", buffer_bytes);
        vSemaphoreDelete(s_logger.stats_mutex);
        s_logger.stats_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_logger.write_buffer_size = WRITE_BUFFER_SIZE;
    s_logger.write_buffer = heap_caps_malloc(s_logger.write_buffer_size,
                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_logger.write_buffer) {
        s_logger.write_buffer = heap_caps_malloc(s_logger.write_buffer_size, MALLOC_CAP_8BIT);
    }
    if (!s_logger.write_buffer) {
        ESP_LOGE(TAG, "Failed to allocate write buffer");
        vRingbufferDelete(s_logger.ring_buffer);
        s_logger.ring_buffer = NULL;
        vSemaphoreDelete(s_logger.stats_mutex);
        s_logger.stats_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    memset(&s_logger.stats, 0, sizeof(s_logger.stats));
    s_logger.initialized = true;
    s_logger.state = CAN_LOGGER_STOPPED;

    size_t item_bytes = sizeof(ring_buffer_item_t);
    size_t item_stride = ((item_bytes + 3) & ~((size_t)3)) + 8;
    size_t approx_items = item_stride ? (buffer_bytes / item_stride) : 0;
    ESP_LOGI(TAG, "Initialized ring buffer: %zu bytes (~%zu msgs)",
             buffer_bytes, approx_items);
    return ESP_OK;
}

esp_err_t can_logger_deinit(void)
{
    if (!s_logger.initialized)
    {
        return ESP_OK;
    }

    // Stop if running
    if (s_logger.state == CAN_LOGGER_RUNNING)
    {
        can_logger_stop();
    }

    if (s_logger.ring_buffer)
    {
        if (s_logger.ring_buffer_caps_allocated)
        {
            vRingbufferDeleteWithCaps(s_logger.ring_buffer);
        }
        else
        {
            vRingbufferDelete(s_logger.ring_buffer);
        }
        s_logger.ring_buffer = NULL;
        s_logger.ring_buffer_caps_allocated = false;
    }

    if (s_logger.write_buffer)
    {
        heap_caps_free(s_logger.write_buffer);
        s_logger.write_buffer = NULL;
        s_logger.write_buffer_size = 0;
    }

    if (s_logger.stats_mutex)
    {
        vSemaphoreDelete(s_logger.stats_mutex);
        s_logger.stats_mutex = NULL;
    }

    s_logger.initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

esp_err_t can_logger_start(void)
{
    if (!s_logger.initialized)
    {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_logger.state == CAN_LOGGER_RUNNING)
    {
        ESP_LOGW(TAG, "Already running");
        return ESP_OK;
    }

    // Create new log file with RTC timestamp in name
    s_logger.log_file = sd_card_create_log_file_with_timestamp("CAN", "bin",
                                                                s_logger.current_file,
                                                                sizeof(s_logger.current_file));
    if (!s_logger.log_file)
    {
        ESP_LOGE(TAG, "Failed to create log file");
        s_logger.state = CAN_LOGGER_ERROR;
        return ESP_FAIL;
    }

    // Reset counters for new session
    can_logger_reset_stats();
    strncpy(s_logger.stats.current_file, s_logger.current_file,
            sizeof(s_logger.stats.current_file) - 1);

    s_logger.write_buffer_pos = 0;
    s_logger.last_flush_time = esp_timer_get_time() / 1000;

    s_logger.log_start_unix_us = 0;
    s_logger.log_start_monotonic_us = (uint64_t)esp_timer_get_time();
    pcf_datetime_t rtc_now;
    if (pcf_rtc_is_time_valid() && pcf_rtc_get_time(&rtc_now) == ESP_OK)
    {
        uint64_t unix_us = 0;
        if (rtc_datetime_to_unix_us(&rtc_now, &unix_us))
        {
            s_logger.log_start_unix_us = unix_us;
            s_logger.log_start_monotonic_us = (uint64_t)esp_timer_get_time();
        }
    }
    s_logger.state = CAN_LOGGER_RUNNING;

    // Start writer task
    BaseType_t result = xTaskCreate(writer_task, "can_log_wr",
                                     WRITER_TASK_STACK_SIZE, NULL,
                                     WRITER_TASK_PRIORITY, &s_logger.writer_task);

    if (result != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to create writer task");
        sd_card_close_log_file(s_logger.log_file);
        s_logger.log_file = NULL;
        s_logger.state = CAN_LOGGER_ERROR;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Logging started: %s", s_logger.current_file);
    return ESP_OK;
}

esp_err_t can_logger_stop(void)
{
    if (s_logger.state != CAN_LOGGER_RUNNING)
    {
        return ESP_OK;
    }

    s_logger.state = CAN_LOGGER_STOPPED;

    // Wait for writer task to finish (it checks state)
    if (s_logger.writer_task)
    {
        // Give task time to drain and exit
        vTaskDelay(pdMS_TO_TICKS(500));
        s_logger.writer_task = NULL;
    }

    // Close file
    if (s_logger.log_file)
    {
        sd_card_close_log_file(s_logger.log_file);
        s_logger.log_file = NULL;
    }

    ESP_LOGI(TAG, "Logging stopped. Messages: %lu, Bytes: %lu",
             (unsigned long)s_logger.stats.messages_logged,
             (unsigned long)s_logger.stats.bytes_written);

    return ESP_OK;
}

bool can_logger_is_running(void)
{
    return s_logger.state == CAN_LOGGER_RUNNING;
}

esp_err_t can_logger_log_message(int64_t timestamp_us, const can_logger_message_t *msg)
{
    if (!s_logger.initialized || s_logger.state != CAN_LOGGER_RUNNING)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (!msg)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ring_buffer_item_t item = {
        .timestamp_us = timestamp_us,
        .msg = *msg
    };

    BaseType_t result = xRingbufferSend(s_logger.ring_buffer, &item,
                                         sizeof(item), 0);

    if (result != pdTRUE)
    {
        update_stat_atomic(&s_logger.stats.messages_dropped, 1);
        update_stat_atomic(&s_logger.stats.buffer_overruns, 1);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t can_logger_get_stats(can_logger_stats_t *stats)
{
    if (!stats)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_logger.initialized || !s_logger.stats_mutex)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_logger.stats_mutex, portMAX_DELAY);
    memcpy(stats, &s_logger.stats, sizeof(*stats));
    stats->state = s_logger.state;
    xSemaphoreGive(s_logger.stats_mutex);

    return ESP_OK;
}

void can_logger_reset_stats(void)
{
    xSemaphoreTake(s_logger.stats_mutex, portMAX_DELAY);
    s_logger.stats.messages_logged = 0;
    s_logger.stats.messages_dropped = 0;
    s_logger.stats.buffer_overruns = 0;
    s_logger.stats.write_errors = 0;
    s_logger.stats.bytes_written = 0;
    xSemaphoreGive(s_logger.stats_mutex);
}
