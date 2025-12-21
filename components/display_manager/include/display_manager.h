/*
 * Display Manager - Abstraction layer for LVGL/ESP-IDF RGB panel driver
 *
 * This component provides a page-based UI system for ESP32 displays.
 */

#pragma once

#include <stdbool.h>

#include "lvgl.h"

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
    // LCD specifications
    int h_res;
    int v_res;
    int pixel_clock_hz;
    int hsync_pulse_width;
    int hsync_back_porch;
    int hsync_front_porch;
    int vsync_pulse_width;
    int vsync_back_porch;
    int vsync_front_porch;
    int data_width;
    int bits_per_pixel;
    int num_fbs;
    int bounce_buffer_size_px;
    bool fb_in_psram;

    // LCD pin configuration
    int hsync_io_num;
    int vsync_io_num;
    int de_io_num;
    int pclk_io_num;
    int disp_io_num;
    int data_io_nums[16];

    // Touch/I2C configuration
    int i2c_port;
    int i2c_sda_io_num;
    int i2c_scl_io_num;
    int i2c_freq_hz;
    int touch_reset_io_num;
    int touch_int_io_num;
    bool touch_enabled;

    // LVGL configuration
    int draw_buf_lines;
    int tick_period_ms;

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
