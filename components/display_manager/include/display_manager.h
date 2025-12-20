/*
 * Display Manager - Abstraction layer for LVGL/ESP-IDF SH8601 driver
 *
 * This component provides a page-based UI system for ESP32 displays.
 */

#pragma once

#include "lvgl.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "driver/spi_master.h"

// Forward declaration
typedef struct dm_page dm_page_t;

// Display orientation options
typedef enum {
    DISPLAY_ORIENTATION_PORTRAIT = 0,
    DISPLAY_ORIENTATION_LANDSCAPE = 1
} display_orientation_t;

#ifdef __cplusplus
extern "C" {
#endif

// Configuration structure for display initialization
typedef struct {
    // SPI configuration
    spi_host_device_t spi_host;
    int dma_chan;
    int sclk_io_num;
    int mosi_io_num;
    int miso_io_num;

    // LCD pin configuration
    int dc_io_num;
    int cs_io_num;
    int rst_io_num;
    int bk_light_io_num;

    // LCD specifications
    int h_res;
    int v_res;
    int pixel_clock_hz;
    int cmd_bits;
    int param_bits;

    // LVGL configuration
    int draw_buf_lines;
    int tick_period_ms;

    // Backlight configuration
    int bk_light_on_level;
    int bk_light_off_level;

    // Panel orientation and offsets
    display_orientation_t orientation;
    int x_offset;
    int y_offset;
} display_config_t;

// Display manager handle
typedef struct display_manager* display_manager_handle_t;

/**
 * @brief Initialize the display manager
 *
 * @param config Display configuration
 * @return display_manager_handle_t Handle to the display manager
 */
display_manager_handle_t display_manager_init(const display_config_t *config);

/**
 * @brief Deinitialize the display manager
 *
 * @param dm_handle Handle to the display manager
 */
void display_manager_deinit(display_manager_handle_t dm_handle);

/**
 * @brief Add a page to the display manager
 *
 * @param dm_handle Handle to the display manager
 * @param page Page to add
 */
void display_manager_add_page(display_manager_handle_t dm_handle, dm_page_t *page);

/**
 * @brief Switch to a specific page
 *
 * @param dm_handle Handle to the display manager
 * @param page_index Index of the page to switch to
 */
void display_manager_switch_to_page(display_manager_handle_t dm_handle, int page_index);

/**
 * @brief Get the active display
 *
 * @param dm_handle Handle to the display manager
 * @return lv_display_t* Pointer to the LVGL display
 */
lv_display_t *display_manager_get_display(display_manager_handle_t dm_handle);

/**
 * @brief Get the number of pages
 *
 * @param dm_handle Handle to the display manager
 * @return int Number of pages
 */
int display_manager_get_page_count(display_manager_handle_t dm_handle);

/**
 * @brief Update all pages (call this periodically)
 *
 * @param dm_handle Handle to the display manager
 */
void display_manager_update(display_manager_handle_t dm_handle);

#ifdef __cplusplus
}
#endif
