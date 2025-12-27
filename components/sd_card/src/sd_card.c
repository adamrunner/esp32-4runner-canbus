/*
 * SD Card Component Implementation
 *
 * Uses SPI interface with CH422G I2C IO expander for CS control.
 * SPI pins: MOSI=11, MISO=13, CLK=12
 * CS is controlled via CH422G at I2C addresses 0x24/0x38
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/unistd.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <driver/i2c.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

#include "sd_card.h"
#include "rtc_pcf85063a.h"

static const char *TAG = "sd_card";

// Mount point for the SD card
#define MOUNT_POINT "/sdcard"

// SPI pins for SD card
#define PIN_NUM_MOSI GPIO_NUM_11
#define PIN_NUM_MISO GPIO_NUM_13
#define PIN_NUM_CLK  GPIO_NUM_12
#define PIN_NUM_CS   GPIO_NUM_NC  // CS controlled via CH422G

// CH422G I2C addresses
#define CH422G_IO_ADDR      0x24
#define CH422G_OUTPUT_ADDR  0x38

// CH422G configuration and output values
// The IO expander controls multiple peripherals:
// - Backlight
// - Touch reset
// - SD card CS
// We need to preserve backlight state while enabling SD CS
#define CH422G_IO_OUTPUT_CFG     0x01
#define CH422G_SD_CS_ENABLE      0x0E  // SD CS low + backlight on
#define CH422G_SD_CS_DISABLE     0x1E  // SD CS high + backlight on (normal display state)

#define I2C_TIMEOUT_MS 1000

// Module state
static struct {
    bool initialized;
    bool mounted;
    int i2c_port;
    sdmmc_card_t *card;
    sdmmc_host_t host;
    SemaphoreHandle_t mutex;
} s_sd_state = {
    .initialized = false,
    .mounted = false,
    .i2c_port = -1,
    .card = NULL,
    .host = SDSPI_HOST_DEFAULT(),
    .mutex = NULL
};

static esp_err_t ch422g_write(uint8_t addr, uint8_t value)
{
    if (s_sd_state.i2c_port < 0)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_write_to_device(s_sd_state.i2c_port, addr, &value, 1,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t sd_card_enable_cs(void)
{
    esp_err_t err = ch422g_write(CH422G_IO_ADDR, CH422G_IO_OUTPUT_CFG);
    if (err != ESP_OK)
    {
        return err;
    }

    return ch422g_write(CH422G_OUTPUT_ADDR, CH422G_SD_CS_ENABLE);
}

esp_err_t sd_card_init(int i2c_port)
{
    if (s_sd_state.initialized)
    {
        ESP_LOGW(TAG, "SD card already initialized");
        return ESP_OK;
    }

    s_sd_state.i2c_port = i2c_port;

    // Create mutex for thread safety
    s_sd_state.mutex = xSemaphoreCreateMutex();
    if (!s_sd_state.mutex)
    {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Enable SD card CS via CH422G
    esp_err_t err = sd_card_enable_cs();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to enable SD CS via CH422G: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_sd_state.mutex);
        s_sd_state.mutex = NULL;
        return err;
    }

    ESP_LOGI(TAG, "SD card CS enabled via CH422G");

    // Configure SPI bus
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .sclk_io_num = PIN_NUM_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    err = spi_bus_initialize(s_sd_state.host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_sd_state.mutex);
        s_sd_state.mutex = NULL;
        return err;
    }

    // Configure SD card slot
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = PIN_NUM_CS;  // -1 means CS is externally controlled
    slot_config.host_id = s_sd_state.host.slot;

    // Mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ESP_LOGI(TAG, "Mounting filesystem...");
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &s_sd_state.host, &slot_config,
                                   &mount_config, &s_sd_state.card);

    if (err != ESP_OK)
    {
        if (err == ESP_FAIL)
        {
            ESP_LOGE(TAG, "Failed to mount filesystem");
        }
        else
        {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(err));
        }

        spi_bus_free(s_sd_state.host.slot);
        vSemaphoreDelete(s_sd_state.mutex);
        s_sd_state.mutex = NULL;
        return err;
    }

    s_sd_state.initialized = true;
    s_sd_state.mounted = true;

    ESP_LOGI(TAG, "SD card mounted successfully");
    sdmmc_card_print_info(stdout, s_sd_state.card);

    return ESP_OK;
}

esp_err_t sd_card_deinit(void)
{
    if (!s_sd_state.initialized)
    {
        return ESP_OK;
    }

    if (s_sd_state.mounted)
    {
        esp_vfs_fat_sdcard_unmount(MOUNT_POINT, s_sd_state.card);
        s_sd_state.mounted = false;
    }

    spi_bus_free(s_sd_state.host.slot);

    if (s_sd_state.mutex)
    {
        vSemaphoreDelete(s_sd_state.mutex);
        s_sd_state.mutex = NULL;
    }

    s_sd_state.initialized = false;
    s_sd_state.card = NULL;

    ESP_LOGI(TAG, "SD card deinitialized");
    return ESP_OK;
}

bool sd_card_is_mounted(void)
{
    return s_sd_state.mounted;
}

esp_err_t sd_card_get_info(sd_card_info_t *info)
{
    if (!info)
    {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));

    if (!s_sd_state.initialized)
    {
        info->status = SD_CARD_STATUS_NOT_INITIALIZED;
        return ESP_OK;
    }

    if (!s_sd_state.mounted)
    {
        info->status = SD_CARD_STATUS_MOUNT_FAILED;
        return ESP_OK;
    }

    info->status = SD_CARD_STATUS_MOUNTED;

    // Copy card name
    if (s_sd_state.card)
    {
        strncpy(info->card_name, s_sd_state.card->cid.name, sizeof(info->card_name) - 1);
        info->card_name[sizeof(info->card_name) - 1] = '\0';

        // Calculate card size
        info->total_bytes = (uint64_t)s_sd_state.card->csd.capacity *
                            s_sd_state.card->csd.sector_size;
    }

    // Get free space using FATFS
    FATFS *fs;
    DWORD fre_clust;
    FRESULT res = f_getfree("0:", &fre_clust, &fs);
    if (res == FR_OK)
    {
        uint64_t fre_sect = fre_clust * fs->csize;
        info->free_bytes = fre_sect * 512;  // Assuming 512 byte sectors
    }

    return ESP_OK;
}

const char *sd_card_get_mount_point(void)
{
    return MOUNT_POINT;
}

void *sd_card_create_log_file(const char *prefix, const char *extension,
                               char *out_path, size_t out_path_size)
{
    if (!s_sd_state.mounted || !prefix || !extension || !out_path)
    {
        return NULL;
    }

    xSemaphoreTake(s_sd_state.mutex, portMAX_DELAY);

    // Find the next available file number
    int next_num = 1;
    DIR *dir = opendir(MOUNT_POINT);
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL)
        {
            // Check if filename matches pattern PREFIX_NNNN.EXT
            int num = 0;
            char test_prefix[32];
            char test_ext[16];

            if (sscanf(entry->d_name, "%31[^_]_%d.%15s", test_prefix, &num, test_ext) == 3)
            {
                if (strcasecmp(test_prefix, prefix) == 0 &&
                    strcasecmp(test_ext, extension) == 0)
                {
                    if (num >= next_num)
                    {
                        next_num = num + 1;
                    }
                }
            }
        }
        closedir(dir);
    }

    // Create the file path
    snprintf(out_path, out_path_size, "%s/%s_%04d.%s",
             MOUNT_POINT, prefix, next_num, extension);

    FILE *f = fopen(out_path, "w");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to create log file: %s", out_path);
        xSemaphoreGive(s_sd_state.mutex);
        return NULL;
    }

    ESP_LOGI(TAG, "Created log file: %s", out_path);
    xSemaphoreGive(s_sd_state.mutex);
    return f;
}

void *sd_card_create_log_file_with_timestamp(const char *prefix, const char *extension,
                                              char *out_path, size_t out_path_size)
{
    if (!s_sd_state.mounted || !prefix || !extension || !out_path)
    {
        return NULL;
    }

    // Try to get RTC time
    pcf_datetime_t time;
    if (!pcf_rtc_is_time_valid() || pcf_rtc_get_time(&time) != ESP_OK)
    {
        // Fall back to auto-incrementing names
        ESP_LOGW(TAG, "RTC time not valid, using incrementing filename");
        return sd_card_create_log_file(prefix, extension, out_path, out_path_size);
    }

    xSemaphoreTake(s_sd_state.mutex, portMAX_DELAY);

    // Format timestamp: YYYYMMDD_HHMMSS
    char timestamp[20];
    pcf_rtc_format_filename(timestamp, sizeof(timestamp), &time);

    // Create the file path with timestamp
    snprintf(out_path, out_path_size, "%s/%s_%s.%s",
             MOUNT_POINT, prefix, timestamp, extension);

    // Check if file already exists (unlikely but possible if creating
    // multiple files in the same second)
    struct stat st;
    if (stat(out_path, &st) == 0)
    {
        // File exists, append a counter
        for (int i = 1; i <= 99; i++)
        {
            snprintf(out_path, out_path_size, "%s/%s_%s_%02d.%s",
                     MOUNT_POINT, prefix, timestamp, i, extension);
            if (stat(out_path, &st) != 0)
            {
                break;
            }
        }
    }

    FILE *f = fopen(out_path, "w");
    if (!f)
    {
        ESP_LOGE(TAG, "Failed to create log file: %s", out_path);
        xSemaphoreGive(s_sd_state.mutex);
        return NULL;
    }

    ESP_LOGI(TAG, "Created log file: %s", out_path);
    xSemaphoreGive(s_sd_state.mutex);
    return f;
}

esp_err_t sd_card_close_log_file(void *file)
{
    if (!file)
    {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_sd_state.mutex, portMAX_DELAY);
    int result = fclose((FILE *)file);
    xSemaphoreGive(s_sd_state.mutex);

    return (result == 0) ? ESP_OK : ESP_FAIL;
}

int sd_card_write(void *file, const void *data, size_t len)
{
    if (!file || !data || len == 0)
    {
        return -1;
    }

    return (int)fwrite(data, 1, len, (FILE *)file);
}

esp_err_t sd_card_flush(void *file)
{
    if (!file)
    {
        return ESP_ERR_INVALID_ARG;
    }

    int result = fflush((FILE *)file);
    return (result == 0) ? ESP_OK : ESP_FAIL;
}
