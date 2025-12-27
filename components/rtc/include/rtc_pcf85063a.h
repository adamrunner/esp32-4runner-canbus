/*
 * RTC Component for Waveshare ESP32-S3 4.3" Touch LCD
 *
 * This component provides RTC functionality using the PCF85063A chip.
 * The RTC is connected via I2C at address 0x51.
 *
 * Note: I2C must be initialized before calling pcf_rtc_init().
 * The display_manager component handles I2C initialization.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// DateTime structure
typedef struct {
    uint16_t year;      // Full year (e.g., 2025)
    uint8_t month;      // 1-12
    uint8_t day;        // 1-31
    uint8_t dotw;       // Day of week: 0=Sunday, 1=Monday, ..., 6=Saturday
    uint8_t hour;       // 0-23
    uint8_t min;        // 0-59
    uint8_t sec;        // 0-59
} pcf_datetime_t;

/**
 * @brief Initialize the RTC
 *
 * Configures the PCF85063A RTC chip. I2C must already be initialized.
 *
 * @param i2c_port The I2C port number to use
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pcf_rtc_init(int i2c_port);

/**
 * @brief Deinitialize the RTC
 *
 * @return ESP_OK on success
 */
esp_err_t pcf_rtc_deinit(void);

/**
 * @brief Check if the RTC is initialized
 *
 * @return true if initialized, false otherwise
 */
bool pcf_rtc_is_initialized(void);

/**
 * @brief Check if the RTC time appears valid
 *
 * Returns true if the time has been set to a reasonable value
 * (year >= 2024). This can be used to detect if the RTC has
 * never been set or lost power.
 *
 * @return true if time appears valid, false otherwise
 */
bool pcf_rtc_is_time_valid(void);

/**
 * @brief Get the current time from the RTC
 *
 * @param time Pointer to structure to fill with current time
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pcf_rtc_get_time(pcf_datetime_t *time);

/**
 * @brief Set the RTC time
 *
 * @param time Pointer to structure containing time to set
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t pcf_rtc_set_time(const pcf_datetime_t *time);

/**
 * @brief Format datetime as string for log filenames
 *
 * Formats as: YYYYMMDD_HHMMSS (e.g., "20251225_143052")
 *
 * @param buf Buffer to write string (must be at least 16 bytes)
 * @param buf_size Size of buffer
 * @param time Pointer to datetime structure
 * @return Number of characters written, or -1 on error
 */
int pcf_rtc_format_filename(char *buf, size_t buf_size, const pcf_datetime_t *time);

/**
 * @brief Format datetime as human-readable string
 *
 * Formats as: YYYY-MM-DD HH:MM:SS (e.g., "2025-12-25 14:30:52")
 *
 * @param buf Buffer to write string (must be at least 20 bytes)
 * @param buf_size Size of buffer
 * @param time Pointer to datetime structure
 * @return Number of characters written, or -1 on error
 */
int pcf_rtc_format_display(char *buf, size_t buf_size, const pcf_datetime_t *time);

/**
 * @brief Format time only as human-readable string
 *
 * Formats as: HH:MM:SS (e.g., "14:30:52")
 *
 * @param buf Buffer to write string (must be at least 9 bytes)
 * @param buf_size Size of buffer
 * @param time Pointer to datetime structure
 * @return Number of characters written, or -1 on error
 */
int pcf_rtc_format_time(char *buf, size_t buf_size, const pcf_datetime_t *time);

/**
 * @brief Format date only as human-readable string
 *
 * Formats as: YYYY-MM-DD (e.g., "2025-12-25")
 *
 * @param buf Buffer to write string (must be at least 11 bytes)
 * @param buf_size Size of buffer
 * @param time Pointer to datetime structure
 * @return Number of characters written, or -1 on error
 */
int pcf_rtc_format_date(char *buf, size_t buf_size, const pcf_datetime_t *time);

/**
 * @brief Get day of week name
 *
 * @param dotw Day of week (0=Sunday, 6=Saturday)
 * @return Short name string (e.g., "Sun", "Mon") or "???" if invalid
 */
const char *pcf_rtc_get_day_name(uint8_t dotw);

/**
 * @brief Calculate day of week from date
 *
 * Uses Zeller's formula to calculate the day of week.
 *
 * @param year Full year
 * @param month Month (1-12)
 * @param day Day of month (1-31)
 * @return Day of week (0=Sunday, 6=Saturday)
 */
uint8_t pcf_rtc_calculate_dotw(uint16_t year, uint8_t month, uint8_t day);

#ifdef __cplusplus
}
#endif
