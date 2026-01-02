/*
 * Page Utilities Implementation
 */

#include "page_utils.h"
#include "app_state.h"

#include <stdio.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "page_utils";

static void log_lvgl_mem(const char *context)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "%s: lvgl mem free=%lu/%lu, biggest=%lu, used=%u%%, frag=%u%%",
             context,
             (unsigned long)mon.free_size,
             (unsigned long)mon.total_size,
             (unsigned long)mon.free_biggest_size,
             (unsigned int)mon.used_pct,
             (unsigned int)mon.frag_pct);
}

// Color palette - using macros to create colors at point of use
// to avoid static initialization order issues with LVGL
#define K_BG_COLOR_HEX          0x111417
#define K_CARD_COLOR_HEX        0x151f2b
#define K_CARD_BORDER_HEX       0x253142
#define K_NAV_BUTTON_COLOR_HEX  0x1b2635
#define K_TEXT_COLOR_HEX        0xe6e6e6
#define K_MUTED_TEXT_COLOR_HEX  0xa1afbf
#define K_ACCENT_COLOR_HEX      0x43c6b6
#define K_WARNING_COLOR_HEX     0xf2b94b

// Functions to get colors safely after LVGL init
lv_color_t get_bg_color(void) { return lv_color_hex(K_BG_COLOR_HEX); }
lv_color_t get_card_color(void) { return lv_color_hex(K_CARD_COLOR_HEX); }
lv_color_t get_card_border(void) { return lv_color_hex(K_CARD_BORDER_HEX); }
lv_color_t get_nav_button_color(void) { return lv_color_hex(K_NAV_BUTTON_COLOR_HEX); }
lv_color_t get_text_color(void) { return lv_color_hex(K_TEXT_COLOR_HEX); }
lv_color_t get_muted_text_color(void) { return lv_color_hex(K_MUTED_TEXT_COLOR_HEX); }
lv_color_t get_accent_color(void) { return lv_color_hex(K_ACCENT_COLOR_HEX); }
lv_color_t get_warning_color(void) { return lv_color_hex(K_WARNING_COLOR_HEX); }

// Fonts
const lv_font_t *k_title_font = &lv_font_montserrat_20;
const lv_font_t *k_value_font = &lv_font_montserrat_20;
const lv_font_t *k_label_font = &lv_font_montserrat_14;

void apply_page_theme(lv_obj_t *container)
{
    if (!container) {
        return;
    }

    lv_obj_set_style_bg_color(container, k_bg_color, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(container, k_text_color, 0);
    lv_obj_set_style_text_font(container, k_value_font, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_outline_width(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
}

void nav_prev_event_cb(lv_event_t *e)
{
    (void)e;
    switch_page_by_offset(-1);
}

void nav_next_event_cb(lv_event_t *e)
{
    (void)e;
    switch_page_by_offset(1);
}

void page_swipe_event_cb(lv_event_t *e)
{
    (void)e;
    lv_indev_t *indev = (lv_indev_t *)lv_event_get_param(e);
    if (!indev) {
        indev = lv_indev_get_act();
    }
    if (!indev) {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        switch_page_by_offset(1);
    } else if (dir == LV_DIR_RIGHT) {
        switch_page_by_offset(-1);
    }
}

void can_toggle_event_cb(lv_event_t *e)
{
    (void)e;
    set_can_paused(!can_state_is_paused());
}

lv_obj_t *create_header_block(lv_obj_t *parent, const char *title, const char *subtitle,
                              lv_obj_t **counter_out, lv_obj_t **error_out)
{
    lv_obj_t *header = lv_obj_create(parent);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_PCT(10));
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_style_pad_row(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *left = lv_obj_create(header);
    lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left, 0, 0);
    lv_obj_set_style_pad_all(left, 0, 0);
    lv_obj_set_style_pad_row(left, 0, 0);
    lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_width(left, LV_PCT(80));
    lv_obj_set_height(left, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_right(left, 8, 0);
    lv_obj_set_flex_grow(left, 1);
    lv_obj_clear_flag(left, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(left, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *title_label = lv_label_create(left);
    lv_label_set_text(title_label, title);
    lv_obj_set_width(title_label, LV_PCT(100));
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(title_label, k_title_font, 0);
    lv_obj_set_style_text_color(title_label, k_text_color, 0);
    lv_obj_add_flag(title_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    if (subtitle) {
        lv_obj_t *subtitle_label = lv_label_create(left);
        lv_label_set_text(subtitle_label, subtitle);
        lv_obj_set_style_text_font(subtitle_label, k_label_font, 0);
        lv_obj_set_style_text_color(subtitle_label, k_muted_text_color, 0);
        lv_obj_add_flag(subtitle_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    }

    lv_obj_t *counter = lv_label_create(header);
    lv_label_set_text(counter, "1/1");
    lv_obj_set_style_text_font(counter, k_label_font, 0);
    lv_obj_set_style_text_color(counter, k_muted_text_color, 0);
    lv_obj_set_style_text_align(counter, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(counter, LV_OBJ_FLAG_GESTURE_BUBBLE);

    if (counter_out) {
        *counter_out = counter;
    }

    lv_obj_t *error_label = lv_label_create(header);
    lv_label_set_text(error_label, "");
    lv_obj_set_style_text_font(error_label, k_label_font, 0);
    lv_obj_set_style_text_color(error_label, k_warning_color, 0);
    lv_obj_add_flag(error_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(error_label, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_align(error_label, LV_ALIGN_TOP_MID, 0, 0);

    if (error_out) {
        *error_out = error_label;
    }

    return header;
}

lv_obj_t *create_metrics_grid(lv_obj_t *parent)
{
    lv_obj_t *grid = lv_obj_create(parent);
    lv_obj_set_width(grid, LV_PCT(100));
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, 12, 0);
    lv_obj_set_style_pad_column(grid, 12, 0);
    lv_obj_set_flex_grow(grid, 1);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_GESTURE_BUBBLE);

    return grid;
}

lv_obj_t *create_nav_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 56, 44);
    lv_obj_set_style_bg_color(btn, k_nav_button_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 14, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, k_card_border, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, k_value_font, 0);
    lv_obj_set_style_text_color(label, k_text_color, 0);
    lv_obj_center(label);

    return btn;
}

void create_nav_bar(lv_obj_t *parent, lv_obj_t **toggle_label_out)
{
    if (toggle_label_out) {
        *toggle_label_out = NULL;
    }
    if (!parent) {
        ESP_LOGE(TAG, "create_nav_bar: parent is NULL");
        log_lvgl_mem("create_nav_bar: parent NULL");
        return;
    }

    lv_obj_t *bar = lv_obj_create(parent);
    if (!bar) {
        ESP_LOGE(TAG, "create_nav_bar: bar create failed");
        log_lvgl_mem("create_nav_bar: bar create failed");
        return;
    }
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 56);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_left(bar, 6, 0);
    lv_obj_set_style_pad_right(bar, 6, 0);
    lv_obj_set_style_pad_top(bar, 0, 0);
    lv_obj_set_style_pad_bottom(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);

    create_nav_button(bar, "<", nav_prev_event_cb);

    lv_obj_t *can_btn = lv_btn_create(bar);
    if (!can_btn) {
        ESP_LOGE(TAG, "create_nav_bar: can toggle button create failed");
        log_lvgl_mem("create_nav_bar: can toggle button create failed");
        return;
    }
    lv_obj_set_size(can_btn, 160, 44);
    lv_obj_set_style_bg_color(can_btn, k_nav_button_color, 0);
    lv_obj_set_style_bg_opa(can_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(can_btn, 14, 0);
    lv_obj_set_style_border_width(can_btn, 1, 0);
    lv_obj_set_style_border_color(can_btn, k_card_border, 0);
    lv_obj_set_style_shadow_width(can_btn, 0, 0);
    lv_obj_add_event_cb(can_btn, can_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *toggle_label = lv_label_create(can_btn);
    if (!toggle_label) {
        ESP_LOGE(TAG, "create_nav_bar: toggle label create failed");
        log_lvgl_mem("create_nav_bar: toggle label create failed");
        return;
    }
    bool paused = can_state_is_paused();
    const char *text = paused ? "Resume CAN" : "Pause CAN";
    lv_label_set_text(toggle_label, text);
    lv_obj_set_style_text_font(toggle_label, k_label_font, 0);
    lv_obj_set_style_text_color(toggle_label, k_text_color, 0);
    lv_obj_center(toggle_label);

    if (toggle_label_out) {
        *toggle_label_out = toggle_label;
    }

    create_nav_button(bar, ">", nav_next_event_cb);
}

void create_nav_bar_simple(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_width(bar, LV_PCT(100));
    lv_obj_set_height(bar, 56);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_left(bar, 6, 0);
    lv_obj_set_style_pad_right(bar, 6, 0);
    lv_obj_set_style_pad_top(bar, 0, 0);
    lv_obj_set_style_pad_bottom(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);

    create_nav_button(bar, "<", nav_prev_event_cb);

    // Spacer
    lv_obj_t *spacer = lv_obj_create(bar);
    lv_obj_set_size(spacer, 160, 44);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(spacer, 0, 0);

    create_nav_button(bar, ">", nav_next_event_cb);
}

lv_obj_t *create_metric_card(lv_obj_t *parent, const char *label_text, lv_obj_t **value_label_out)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, k_card_color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, k_card_border, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_shadow_color(card, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(card, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(card, 18, 0);
    lv_obj_set_style_shadow_offset_y(card, 6, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 6, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, k_label_font, 0);
    lv_obj_set_style_text_color(label, k_muted_text_color, 0);
    lv_obj_add_flag(label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *value = lv_label_create(card);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_font(value, k_value_font, 0);
    lv_obj_set_style_text_color(value, k_accent_color, 0);
    lv_obj_add_flag(value, LV_OBJ_FLAG_GESTURE_BUBBLE);

    if (value_label_out) {
        *value_label_out = value;
    }

    return card;
}

void update_page_counter(lv_obj_t *label, int page_index)
{
    int page_count = app_state_get_page_count();
    if (!label || page_count <= 0) {
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d", page_index + 1, page_count);
    lv_label_set_text(label, buf);
}

lv_obj_t *create_adj_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 36, 32);
    lv_obj_set_style_bg_color(btn, k_nav_button_color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, k_card_border, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, k_label_font, 0);
    lv_obj_set_style_text_color(label, k_text_color, 0);
    lv_obj_center(label);

    return btn;
}

lv_obj_t *create_time_field(lv_obj_t *parent, const char *label_text,
                            lv_obj_t **value_out,
                            lv_event_cb_t up_cb, lv_event_cb_t down_cb)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_style_bg_color(card, k_card_color, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, k_card_border, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_pad_all(card, 6, 0);
    lv_obj_set_style_pad_row(card, 4, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *label = lv_label_create(card);
    lv_label_set_text(label, label_text);
    lv_obj_set_style_text_font(label, k_label_font, 0);
    lv_obj_set_style_text_color(label, k_muted_text_color, 0);

    create_adj_button(card, "+", up_cb);

    lv_obj_t *value = lv_label_create(card);
    lv_label_set_text(value, "--");
    lv_obj_set_style_text_font(value, k_value_font, 0);
    lv_obj_set_style_text_color(value, k_accent_color, 0);

    create_adj_button(card, "-", down_cb);

    if (value_out) {
        *value_out = value;
    }

    return card;
}
