/*
 * RTC Component Implementation
 *
 * PCF85063A driver adapted for shared I2C bus usage.
 * Based on Waveshare example code.
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/i2c.h>
#include <esp_log.h>

#include "rtc_pcf85063a.h"

static const char *TAG = "rtc";

// PCF85063A I2C address
#define PCF85063A_ADDRESS   0x51

// I2C timeout
#define I2C_TIMEOUT_MS      1000

// Year offset - RTC stores 0-99, we add this to get full year
#define YEAR_OFFSET         2000

// Minimum valid year (for detecting unset RTC)
#define MIN_VALID_YEAR      2024

// Register addresses
#define RTC_CTRL_1_ADDR     0x00
#define RTC_CTRL_2_ADDR     0x01
#define RTC_SECOND_ADDR     0x04
#define RTC_MINUTE_ADDR     0x05
#define RTC_HOUR_ADDR       0x06
#define RTC_DAY_ADDR        0x07
#define RTC_WDAY_ADDR       0x08
#define RTC_MONTH_ADDR      0x09
#define RTC_YEAR_ADDR       0x0A

// Control register 1 bits
#define RTC_CTRL_1_CAP_SEL  0x01    // 12.5pF load capacitance

// Day of week names
static const char *k_day_names[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

// Module state
static struct {
    bool initialized;
    int i2c_port;
    SemaphoreHandle_t mutex;
} s_rtc = {
    .initialized = false,
    .i2c_port = -1,
    .mutex = NULL
};

// BCD conversion helpers
static uint8_t dec_to_bcd(uint8_t val)
{
    return (uint8_t)((val / 10 * 16) + (val % 10));
}

static uint8_t bcd_to_dec(uint8_t val)
{
    return (uint8_t)((val / 16 * 10) + (val % 16));
}

// I2C helper functions
static esp_err_t pcf_write_byte(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_write_to_device(s_rtc.i2c_port, PCF85063A_ADDRESS,
                                       buf, 2, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t pcf_write_bytes(uint8_t *data, size_t len)
{
    return i2c_master_write_to_device(s_rtc.i2c_port, PCF85063A_ADDRESS,
                                       data, len, pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t pcf_read_bytes(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_write_read_device(s_rtc.i2c_port, PCF85063A_ADDRESS,
                                         &reg, 1, data, len,
                                         pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

esp_err_t pcf_rtc_init(int i2c_port)
{
    if (s_rtc.initialized)
    {
        ESP_LOGW(TAG, "RTC already initialized");
        return ESP_OK;
    }

    s_rtc.i2c_port = i2c_port;

    // Create mutex for thread safety
    s_rtc.mutex = xSemaphoreCreateMutex();
    if (!s_rtc.mutex)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Configure control register 1 with 12.5pF capacitor selection
    esp_err_t err = pcf_write_byte(RTC_CTRL_1_ADDR, RTC_CTRL_1_CAP_SEL);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure RTC: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_rtc.mutex);
        s_rtc.mutex = NULL;
        return err;
    }

    s_rtc.initialized = true;
    ESP_LOGI(TAG, "RTC initialized (PCF85063A at 0x%02X)", PCF85063A_ADDRESS);

    // Log current time
    pcf_datetime_t now;
    if (pcf_rtc_get_time(&now) == ESP_OK)
    {
        char buf[24];
        pcf_rtc_format_display(buf, sizeof(buf), &now);
        ESP_LOGI(TAG, "Current RTC time: %s", buf);

        if (!pcf_rtc_is_time_valid())
        {
            ESP_LOGW(TAG, "RTC time not set (year < %d)", MIN_VALID_YEAR);
        }
    }

    return ESP_OK;
}

esp_err_t pcf_rtc_deinit(void)
{
    if (!s_rtc.initialized)
    {
        return ESP_OK;
    }

    if (s_rtc.mutex)
    {
        vSemaphoreDelete(s_rtc.mutex);
        s_rtc.mutex = NULL;
    }

    s_rtc.initialized = false;
    s_rtc.i2c_port = -1;

    ESP_LOGI(TAG, "RTC deinitialized");
    return ESP_OK;
}

bool pcf_rtc_is_initialized(void)
{
    return s_rtc.initialized;
}

bool pcf_rtc_is_time_valid(void)
{
    if (!s_rtc.initialized)
    {
        return false;
    }

    pcf_datetime_t time;
    if (pcf_rtc_get_time(&time) != ESP_OK)
    {
        return false;
    }

    return time.year >= MIN_VALID_YEAR;
}

esp_err_t pcf_rtc_get_time(pcf_datetime_t *time)
{
    if (!time)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_rtc.initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_rtc.mutex, portMAX_DELAY);

    uint8_t buf[7] = {0};
    esp_err_t err = pcf_read_bytes(RTC_SECOND_ADDR, buf, 7);

    xSemaphoreGive(s_rtc.mutex);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to read time: %s", esp_err_to_name(err));
        return err;
    }

    // Convert BCD to decimal with appropriate masks
    time->sec = bcd_to_dec(buf[0] & 0x7F);
    time->min = bcd_to_dec(buf[1] & 0x7F);
    time->hour = bcd_to_dec(buf[2] & 0x3F);  // 24-hour format
    time->day = bcd_to_dec(buf[3] & 0x3F);
    time->dotw = bcd_to_dec(buf[4] & 0x07);
    time->month = bcd_to_dec(buf[5] & 0x1F);
    time->year = bcd_to_dec(buf[6]) + YEAR_OFFSET;

    return ESP_OK;
}

esp_err_t pcf_rtc_set_time(const pcf_datetime_t *time)
{
    if (!time)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_rtc.initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    // Validate input ranges
    if (time->month < 1 || time->month > 12 ||
        time->day < 1 || time->day > 31 ||
        time->hour > 23 ||
        time->min > 59 ||
        time->sec > 59 ||
        time->dotw > 6)
    {
        ESP_LOGE(TAG, "Invalid time values");
        return ESP_ERR_INVALID_ARG;
    }

    // Validate year range (RTC stores 0-99)
    if (time->year < YEAR_OFFSET || time->year >= YEAR_OFFSET + 100)
    {
        ESP_LOGE(TAG, "Year out of range (%d - %d)", YEAR_OFFSET, YEAR_OFFSET + 99);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_rtc.mutex, portMAX_DELAY);

    // Build buffer with register address and time data
    uint8_t buf[8] = {
        RTC_SECOND_ADDR,
        dec_to_bcd(time->sec),
        dec_to_bcd(time->min),
        dec_to_bcd(time->hour),
        dec_to_bcd(time->day),
        dec_to_bcd(time->dotw),
        dec_to_bcd(time->month),
        dec_to_bcd((uint8_t)(time->year - YEAR_OFFSET))
    };

    esp_err_t err = pcf_write_bytes(buf, 8);

    xSemaphoreGive(s_rtc.mutex);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set time: %s", esp_err_to_name(err));
        return err;
    }

    char display_buf[24];
    pcf_rtc_format_display(display_buf, sizeof(display_buf), time);
    ESP_LOGI(TAG, "RTC time set to: %s", display_buf);

    return ESP_OK;
}

int pcf_rtc_format_filename(char *buf, size_t buf_size, const pcf_datetime_t *time)
{
    if (!buf || buf_size < 16 || !time)
    {
        return -1;
    }

    return snprintf(buf, buf_size, "%04d%02d%02d_%02d%02d%02d",
                    time->year, time->month, time->day,
                    time->hour, time->min, time->sec);
}

int pcf_rtc_format_display(char *buf, size_t buf_size, const pcf_datetime_t *time)
{
    if (!buf || buf_size < 20 || !time)
    {
        return -1;
    }

    return snprintf(buf, buf_size, "%04d-%02d-%02d %02d:%02d:%02d",
                    time->year, time->month, time->day,
                    time->hour, time->min, time->sec);
}

int pcf_rtc_format_time(char *buf, size_t buf_size, const pcf_datetime_t *time)
{
    if (!buf || buf_size < 9 || !time)
    {
        return -1;
    }

    return snprintf(buf, buf_size, "%02d:%02d:%02d",
                    time->hour, time->min, time->sec);
}

int pcf_rtc_format_date(char *buf, size_t buf_size, const pcf_datetime_t *time)
{
    if (!buf || buf_size < 11 || !time)
    {
        return -1;
    }

    return snprintf(buf, buf_size, "%04d-%02d-%02d",
                    time->year, time->month, time->day);
}

const char *pcf_rtc_get_day_name(uint8_t dotw)
{
    if (dotw > 6)
    {
        return "???";
    }
    return k_day_names[dotw];
}

uint8_t pcf_rtc_calculate_dotw(uint16_t year, uint8_t month, uint8_t day)
{
    // Zeller's formula for Gregorian calendar
    // Returns 0=Sunday, 1=Monday, ..., 6=Saturday

    if (month < 3)
    {
        month += 12;
        year--;
    }

    int k = year % 100;
    int j = year / 100;

    int h = (day + (13 * (month + 1)) / 5 + k + k / 4 + j / 4 - 2 * j) % 7;

    // Convert from Zeller (0=Saturday) to our format (0=Sunday)
    return (uint8_t)((h + 6) % 7);
}
