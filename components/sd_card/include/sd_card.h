/*
 * SD Card Component for Waveshare ESP32-S3 4.3" Touch LCD
 *
 * This component provides SD card functionality using SPI interface.
 * The SD card CS pin is controlled via CH422G I2C IO expander.
 *
 * Note: I2C must be initialized before calling sd_card_init().
 * The display_manager component handles I2C initialization.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// SD card status
typedef enum {
    SD_CARD_STATUS_NOT_INITIALIZED = 0,
    SD_CARD_STATUS_MOUNTED,
    SD_CARD_STATUS_MOUNT_FAILED,
    SD_CARD_STATUS_NO_CARD,
    SD_CARD_STATUS_ERROR
} sd_card_status_t;

// SD card info structure
typedef struct {
    sd_card_status_t status;
    uint64_t total_bytes;
    uint64_t free_bytes;
    char card_name[16];
} sd_card_info_t;

/**
 * @brief Initialize the SD card
 *
 * This function initializes the SPI bus and mounts the SD card filesystem.
 * I2C must already be initialized (by display_manager).
 *
 * @param i2c_port The I2C port number to use for CH422G control
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_init(int i2c_port);

/**
 * @brief Deinitialize the SD card
 *
 * Unmounts the filesystem and releases SPI resources.
 *
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_deinit(void);

/**
 * @brief Check if the SD card is mounted
 *
 * @return true if mounted, false otherwise
 */
bool sd_card_is_mounted(void);

/**
 * @brief Get SD card status and info
 *
 * @param info Pointer to structure to fill with card info
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_get_info(sd_card_info_t *info);

/**
 * @brief Get the mount point path
 *
 * @return Mount point string (e.g., "/sdcard")
 */
const char *sd_card_get_mount_point(void);

/**
 * @brief Create a new file for logging
 *
 * Creates a new file with auto-incrementing name (LOG_0001.CSV, etc.)
 *
 * @param prefix File name prefix (e.g., "LOG")
 * @param extension File extension (e.g., "CSV")
 * @param out_path Buffer to receive the full path (must be at least 64 bytes)
 * @param out_path_size Size of out_path buffer
 * @return FILE pointer on success, NULL on failure
 */
void *sd_card_create_log_file(const char *prefix, const char *extension,
                               char *out_path, size_t out_path_size);

/**
 * @brief Create a new file for logging with RTC timestamp
 *
 * Creates a new file with RTC timestamp in the name (LOG_20251225_143052.CSV)
 * Falls back to auto-incrementing name if RTC time is not valid.
 *
 * @param prefix File name prefix (e.g., "LOG")
 * @param extension File extension (e.g., "CSV")
 * @param out_path Buffer to receive the full path (must be at least 64 bytes)
 * @param out_path_size Size of out_path buffer
 * @return FILE pointer on success, NULL on failure
 */
void *sd_card_create_log_file_with_timestamp(const char *prefix, const char *extension,
                                              char *out_path, size_t out_path_size);

/**
 * @brief Close a log file
 *
 * @param file FILE pointer from sd_card_create_log_file
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_close_log_file(void *file);

/**
 * @brief Write data to a log file
 *
 * @param file FILE pointer
 * @param data Data to write
 * @param len Length of data
 * @return Number of bytes written, or -1 on error
 */
int sd_card_write(void *file, const void *data, size_t len);

/**
 * @brief Flush file buffer to SD card
 *
 * @param file FILE pointer
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sd_card_flush(void *file);

#ifdef __cplusplus
}
#endif
