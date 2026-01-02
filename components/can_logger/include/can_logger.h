/*
 * CAN Logger Component
 *
 * Provides CAN message logging to SD card with ring buffer for efficient
 * writes. Messages are stored in CSV format compatible with existing logs.
 *
 * Format: timestamp_us,can_id,dlc,byte0,byte1,...,byte7
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Logging state
typedef enum {
    CAN_LOGGER_STOPPED = 0,
    CAN_LOGGER_RUNNING,
    CAN_LOGGER_ERROR
} can_logger_state_t;

// Statistics structure
typedef struct {
    can_logger_state_t state;
    uint32_t messages_logged;
    uint32_t messages_dropped;
    uint32_t buffer_overruns;
    uint32_t write_errors;
    uint32_t bytes_written;
    char current_file[64];
} can_logger_stats_t;

// CAN message structure (matches TWAI driver format)
typedef struct {
    uint32_t identifier;
    uint8_t data_length_code;
    uint8_t data[8];
} can_logger_message_t;

/**
 * @brief Initialize the CAN logger
 *
 * Must be called after sd_card_init().
 *
 * @param ring_buffer_size Number of messages in the ring buffer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t can_logger_init(size_t ring_buffer_size);

/**
 * @brief Deinitialize the CAN logger
 *
 * Stops logging if active and frees resources.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t can_logger_deinit(void);

/**
 * @brief Start logging to a new file
 *
 * Creates a new log file and starts the writer task.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t can_logger_start(void);

/**
 * @brief Stop logging
 *
 * Flushes remaining buffer contents and closes the file.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t can_logger_stop(void);

/**
 * @brief Check if logging is active
 *
 * @return true if logging, false otherwise
 */
bool can_logger_is_running(void);

/**
 * @brief Log a CAN message
 *
 * This function is designed to be called from ISR context or
 * high-priority tasks. It copies the message to a ring buffer
 * for later writing to SD card.
 *
 * @param timestamp_us Timestamp in microseconds
 * @param msg Pointer to CAN message
 * @return ESP_OK on success, ESP_ERR_NO_MEM if buffer full
 */
esp_err_t can_logger_log_message(int64_t timestamp_us, const can_logger_message_t *msg);

/**
 * @brief Get logging statistics
 *
 * @param stats Pointer to statistics structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t can_logger_get_stats(can_logger_stats_t *stats);

/**
 * @brief Reset statistics counters
 */
void can_logger_reset_stats(void);

#ifdef __cplusplus
}
#endif
