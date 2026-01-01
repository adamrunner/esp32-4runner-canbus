/*
 * CAN Logger Implementation
 *
 * Uses a ring buffer and background task for efficient SD card writes.
 * The ring buffer allows the CAN RX task to continue without blocking
 * on SD card operations.
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
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

// Write buffer size (characters) - sized for ~80 messages at ~100 bytes each
#define WRITE_BUFFER_SIZE 8192
#define WRITER_TASK_STACK_SIZE 4096
#define WRITER_TASK_PRIORITY 5  // Higher priority for keeping up with CAN traffic
#define FLUSH_INTERVAL_MS 1000

// Module state
static struct {
    bool initialized;
    can_logger_state_t state;
    RingbufHandle_t ring_buffer;
    TaskHandle_t writer_task;
    SemaphoreHandle_t stats_mutex;
    void *log_file;
    char current_file[64];
    can_logger_stats_t stats;
    char write_buffer[WRITE_BUFFER_SIZE];
    size_t write_buffer_pos;
    int64_t last_flush_time;
} s_logger = {
    .initialized = false,
    .state = CAN_LOGGER_STOPPED,
    .ring_buffer = NULL,
    .writer_task = NULL,
    .stats_mutex = NULL,
    .log_file = NULL,
    .write_buffer_pos = 0,
    .last_flush_time = 0
};

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
    if (s_logger.write_buffer_pos == 0 || !s_logger.log_file)
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

static esp_err_t buffer_write(const char *data, size_t len)
{
    // If data won't fit, flush first
    if (s_logger.write_buffer_pos + len > WRITE_BUFFER_SIZE)
    {
        esp_err_t err = flush_write_buffer();
        if (err != ESP_OK)
        {
            return err;
        }
    }

    // If data is larger than buffer, write directly
    if (len > WRITE_BUFFER_SIZE)
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

static void format_and_write_message(const ring_buffer_item_t *item)
{
    char line[160];
    char datetime[24] = "";

    // Get wall-clock time if available
    pcf_datetime_t now;
    if (pcf_rtc_is_time_valid() && pcf_rtc_get_time(&now) == ESP_OK)
    {
        pcf_rtc_format_display(datetime, sizeof(datetime), &now);
    }

    int len = snprintf(line, sizeof(line),
                       "%s,%lld,%03lX,%d,%02X,%02X,%02X,%02X,%02X,%02X,%02X,%02X\n",
                       datetime,
                       item->timestamp_us,
                       (unsigned long)item->msg.identifier,
                       item->msg.data_length_code,
                       item->msg.data[0], item->msg.data[1],
                       item->msg.data[2], item->msg.data[3],
                       item->msg.data[4], item->msg.data[5],
                       item->msg.data[6], item->msg.data[7]);

    if (len > 0 && (size_t)len < sizeof(line))
    {
        buffer_write(line, len);
        update_stat_atomic(&s_logger.stats.messages_logged, 1);
    }
}

static void writer_task(void *arg)
{
    ESP_LOGI(TAG, "Writer task started");

    // Write CSV header
    const char *header = "datetime,timestamp_us,can_id,dlc,b0,b1,b2,b3,b4,b5,b6,b7\n";
    buffer_write(header, strlen(header));
    flush_write_buffer();

    while (s_logger.state == CAN_LOGGER_RUNNING)
    {
        size_t item_size = 0;
        int messages_processed = 0;

        // Batch process: drain all available messages without waiting
        ring_buffer_item_t *item;
        while ((item = xRingbufferReceive(s_logger.ring_buffer, &item_size, 0)) != NULL)
        {
            format_and_write_message(item);
            vRingbufferReturnItem(s_logger.ring_buffer, item);
            messages_processed++;

            // Flush write buffer when nearly full (160 = max CSV line size)
            if (s_logger.write_buffer_pos > WRITE_BUFFER_SIZE - 160)
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
                format_and_write_message(item);
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
        format_and_write_message(item);
        vRingbufferReturnItem(s_logger.ring_buffer, item);
    }

    // Final flush
    flush_write_buffer();
    sd_card_flush(s_logger.log_file);

    ESP_LOGI(TAG, "Writer task stopped");
    vTaskDelete(NULL);
}

esp_err_t can_logger_init(size_t ring_buffer_size)
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

    // Create ring buffer
    size_t buffer_bytes = ring_buffer_size * sizeof(ring_buffer_item_t);
    s_logger.ring_buffer = xRingbufferCreate(buffer_bytes, RINGBUF_TYPE_NOSPLIT);
    if (!s_logger.ring_buffer)
    {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        vSemaphoreDelete(s_logger.stats_mutex);
        s_logger.stats_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    memset(&s_logger.stats, 0, sizeof(s_logger.stats));
    s_logger.initialized = true;
    s_logger.state = CAN_LOGGER_STOPPED;

    ESP_LOGI(TAG, "Initialized with %zu message buffer", ring_buffer_size);
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
        vRingbufferDelete(s_logger.ring_buffer);
        s_logger.ring_buffer = NULL;
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
    s_logger.log_file = sd_card_create_log_file_with_timestamp("CAN", "CSV",
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
