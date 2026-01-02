/*
 * Page Utilities - Shared UI helpers for all pages
 *
 * This module provides common UI creation functions, color palette,
 * fonts, and navigation callbacks used across all pages.
 */

#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Color palette - use functions to avoid static init order issues
lv_color_t get_bg_color(void);
lv_color_t get_card_color(void);
lv_color_t get_card_border(void);
lv_color_t get_nav_button_color(void);
lv_color_t get_text_color(void);
lv_color_t get_muted_text_color(void);
lv_color_t get_accent_color(void);
lv_color_t get_warning_color(void);

// Convenience macros for cleaner code
#define k_bg_color          get_bg_color()
#define k_card_color        get_card_color()
#define k_card_border       get_card_border()
#define k_nav_button_color  get_nav_button_color()
#define k_text_color        get_text_color()
#define k_muted_text_color  get_muted_text_color()
#define k_accent_color      get_accent_color()
#define k_warning_color     get_warning_color()

// Fonts
extern const lv_font_t *k_title_font;
extern const lv_font_t *k_value_font;
extern const lv_font_t *k_label_font;

/**
 * @brief Apply standard page theme to a container
 * @param container LVGL container object
 */
void apply_page_theme(lv_obj_t *container);

/**
 * @brief Create page header with title, subtitle, counter, and error label
 * @param parent Parent LVGL object
 * @param title Page title text
 * @param subtitle Subtitle text (can be NULL)
 * @param counter_out Output pointer for page counter label
 * @param error_out Output pointer for error label
 * @return Header container object
 */
lv_obj_t *create_header_block(lv_obj_t *parent, const char *title, const char *subtitle,
                              lv_obj_t **counter_out, lv_obj_t **error_out);

/**
 * @brief Create a flex grid for metric cards
 * @param parent Parent LVGL object
 * @return Grid container object
 */
lv_obj_t *create_metrics_grid(lv_obj_t *parent);

/**
 * @brief Create a styled metric card with label and value
 * @param parent Parent LVGL object
 * @param label_text Label text for the metric
 * @param value_label_out Output pointer for value label
 * @return Card container object
 */
lv_obj_t *create_metric_card(lv_obj_t *parent, const char *label_text, lv_obj_t **value_label_out);

/**
 * @brief Create a navigation button
 * @param parent Parent LVGL object
 * @param text Button text
 * @param cb Click event callback
 * @return Button object
 */
lv_obj_t *create_nav_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb);

/**
 * @brief Create navigation bar with prev/next buttons and CAN toggle
 * @param parent Parent LVGL object
 * @param toggle_label_out Output pointer for CAN toggle label
 */
void create_nav_bar(lv_obj_t *parent, lv_obj_t **toggle_label_out);

/**
 * @brief Create a simple navigation bar without CAN toggle
 * @param parent Parent LVGL object
 */
void create_nav_bar_simple(lv_obj_t *parent);

/**
 * @brief Create a small adjustment button (+/-)
 * @param parent Parent LVGL object
 * @param text Button text ("+" or "-")
 * @param cb Click event callback
 * @return Button object
 */
lv_obj_t *create_adj_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb);

/**
 * @brief Create a time field with label, value, and +/- buttons
 * @param parent Parent LVGL object
 * @param label_text Label text
 * @param value_out Output pointer for value label
 * @param up_cb Callback for + button
 * @param down_cb Callback for - button
 * @return Card container object
 */
lv_obj_t *create_time_field(lv_obj_t *parent, const char *label_text,
                            lv_obj_t **value_out,
                            lv_event_cb_t up_cb, lv_event_cb_t down_cb);

/**
 * @brief Update page counter label
 * @param label Label object to update
 * @param page_index Current page index (0-based)
 */
void update_page_counter(lv_obj_t *label, int page_index);

/**
 * @brief Navigation callback for previous page button
 */
void nav_prev_event_cb(lv_event_t *e);

/**
 * @brief Navigation callback for next page button
 */
void nav_next_event_cb(lv_event_t *e);

/**
 * @brief Navigation callback for swipe gestures
 */
void page_swipe_event_cb(lv_event_t *e);

/**
 * @brief CAN toggle button callback
 */
void can_toggle_event_cb(lv_event_t *e);

#ifdef __cplusplus
}
#endif
