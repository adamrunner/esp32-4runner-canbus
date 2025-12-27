/*
 * 4Runner CAN Bus Display
 *
 * Polls OBD-II and Toyota-specific PIDs over CAN and displays metrics
 * on the Waveshare ESP32-S3 4.3-inch Touch LCD using LVGL.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "display_manager.h"
#include "display_manager/page.h"
#include "lvgl.h"
#include "sd_card.h"
#include "can_logger.h"
#include "rtc_pcf85063a.h"

static const char *TAG = "4RUNNER_CAN";

#define TX_GPIO_NUM GPIO_NUM_15
#define RX_GPIO_NUM GPIO_NUM_16

#define OBD_REQUEST_ID 0x7E0
#define OBD_RESPONSE_ID_MIN 0x7E8
#define OBD_RESPONSE_ID_MAX 0x7EF

#define ABS_REQUEST_ID 0x7B0
#define METER_REQUEST_ID 0x7C0
#define WHEEL_SPEED_BROADCAST_ID 0x0AA

#define OBD_POLL_INTERVAL_MS 150

static const int lcd_h_res = 800;
static const int lcd_v_res = 480;
static const int lcd_pixel_clock_hz = 16 * 1000 * 1000;
static const int lcd_hsync_pulse_width = 4;
static const int lcd_hsync_back_porch = 8;
static const int lcd_hsync_front_porch = 8;
static const int lcd_vsync_pulse_width = 4;
static const int lcd_vsync_back_porch = 8;
static const int lcd_vsync_front_porch = 8;
static const int lcd_data_width = 16;
static const int lcd_bits_per_pixel = 16;
static const int lcd_num_fbs = 2;
static const int lcd_bounce_buffer_size_px = 0;
static const bool lcd_fb_in_psram = true;
static const int lcd_hsync_io_num = GPIO_NUM_46;
static const int lcd_vsync_io_num = GPIO_NUM_3;
static const int lcd_de_io_num = GPIO_NUM_5;
static const int lcd_pclk_io_num = GPIO_NUM_7;
static const int lcd_disp_io_num = -1;
static const int lcd_data_io_nums[16] = {
    GPIO_NUM_14,
    GPIO_NUM_38,
    GPIO_NUM_18,
    GPIO_NUM_17,
    GPIO_NUM_10,
    GPIO_NUM_39,
    GPIO_NUM_0,
    GPIO_NUM_45,
    GPIO_NUM_48,
    GPIO_NUM_47,
    GPIO_NUM_21,
    GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_42,
    GPIO_NUM_41,
    GPIO_NUM_40,
};
static const int lcd_i2c_port = 0;
static const int lcd_i2c_sda_io_num = 8;
static const int lcd_i2c_scl_io_num = 9;
static const int lcd_i2c_freq_hz = 400000;
static const int lcd_touch_reset_io_num = 4;
static const int lcd_touch_int_io_num = -1;

typedef struct {
    float rpm;
    float vbatt_v;
    float iat_c;
    float baro_kpa;
    float atf_pan_c;
    float atf_tqc_c;
    float fli_vol_gal;
    uint32_t odo_km;
    int gear;
    bool tqc_lockup;
    // Diagnostic wheel speeds (from 0x7B0 PID 0x03)
    float diag_wheel_fl_kph;
    float diag_wheel_fr_kph;
    float diag_wheel_rl_kph;
    float diag_wheel_rr_kph;
    // Broadcast wheel speeds (from 0x0AA)
    float bcast_wheel_fl_kph;
    float bcast_wheel_fr_kph;
    float bcast_wheel_rl_kph;
    float bcast_wheel_rr_kph;
    bool rpm_valid;
    bool vbatt_valid;
    bool iat_valid;
    bool baro_valid;
    bool atf_valid;
    bool fuel_valid;
    bool odo_valid;
    bool gear_valid;
    bool diag_wheel_speed_valid;
    bool bcast_wheel_speed_valid;
} can_metrics_t;

static SemaphoreHandle_t s_metrics_mutex = NULL;
static can_metrics_t s_metrics = {};
static SemaphoreHandle_t s_can_state_mutex = NULL;

typedef struct {
    bool paused;
    bool error_active;
    int fail_count;
    int64_t last_rx_ms;
} can_state_t;

static can_state_t s_can_state = {};

static lv_obj_t *s_diag_error_label = NULL;
static lv_obj_t *s_fourrunner_error_label = NULL;
static lv_obj_t *s_tire_error_label = NULL;
static lv_obj_t *s_diag_can_toggle_label = NULL;
static lv_obj_t *s_fourrunner_can_toggle_label = NULL;
static lv_obj_t *s_tire_can_toggle_label = NULL;

typedef struct {
    uint16_t header;
    uint8_t service;
    uint8_t pid;
    uint8_t ext_addr;  // 0 = no extended addressing, otherwise the extended address byte
} obd_request_t;

static const obd_request_t k_request_sequence[] = {
    {OBD_REQUEST_ID, 0x01, 0x0C, 0},
    {OBD_REQUEST_ID, 0x01, 0x42, 0},
    {OBD_REQUEST_ID, 0x01, 0x0F, 0},
    {OBD_REQUEST_ID, 0x01, 0x33, 0},
    {OBD_REQUEST_ID, 0x21, 0x82, 0},
    {OBD_REQUEST_ID, 0x21, 0x85, 0},
    {OBD_REQUEST_ID, 0x21, 0x28, 0},
    {METER_REQUEST_ID, 0x21, 0x29, 0},  // Fuel level
    {ABS_REQUEST_ID, 0x21, 0x03, 0},    // Wheel speeds (diagnostic)
    // Note: 0x0AA broadcasts wheel speeds passively - no request needed
};

static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
static const twai_general_config_t g_config = {
    .controller_id = 0,
    .mode = TWAI_MODE_NORMAL,
    .tx_io = TX_GPIO_NUM,
    .rx_io = RX_GPIO_NUM,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 5,
    .rx_queue_len = 20,
    .alerts_enabled = TWAI_ALERT_NONE,
    .clkout_divider = 0,
    .intr_flags = 0,
    .general_flags = {
        .sleep_allow_pd = 0,
    },
};


static display_manager_handle_t s_display = NULL;
static int s_page_count = 0;
static int s_active_page = 0;

static const lv_color_t k_bg_color = lv_color_hex(0x111417);
static const lv_color_t k_card_color = lv_color_hex(0x151f2b);
static const lv_color_t k_card_border = lv_color_hex(0x253142);
static const lv_color_t k_nav_button_color = lv_color_hex(0x1b2635);
static const lv_color_t k_text_color = lv_color_hex(0xe6e6e6);
static const lv_color_t k_muted_text_color = lv_color_hex(0xa1afbf);
static const lv_color_t k_accent_color = lv_color_hex(0x43c6b6);
static const lv_color_t k_warning_color = lv_color_hex(0xf2b94b);
static const lv_font_t *k_title_font = &lv_font_montserrat_20;
static const lv_font_t *k_value_font = &lv_font_montserrat_20;
static const lv_font_t *k_label_font = &lv_font_montserrat_14;

static const int k_can_error_fail_threshold = 5;
static const int64_t k_can_error_stale_ms = 2000;

static void apply_page_theme(lv_obj_t *container)
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

static void switch_page_by_offset(int offset)
{
    if (!s_display || s_page_count <= 1) {
        return;
    }

    s_active_page += offset;
    if (s_active_page < 0) {
        s_active_page = s_page_count - 1;
    } else if (s_active_page >= s_page_count) {
        s_active_page = 0;
    }

    display_manager_switch_to_page(s_display, s_active_page);
}

static void nav_prev_event_cb(lv_event_t *e)
{
    (void)e;
    switch_page_by_offset(-1);
}

static void nav_next_event_cb(lv_event_t *e)
{
    (void)e;
    switch_page_by_offset(1);
}

static void page_swipe_event_cb(lv_event_t *e)
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

static int64_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static bool can_state_is_paused(void)
{
    bool paused = false;
    if (s_can_state_mutex &&
        xSemaphoreTake(s_can_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        paused = s_can_state.paused;
        xSemaphoreGive(s_can_state_mutex);
    }
    return paused;
}

static void can_ui_update_cb(void *arg)
{
    (void)arg;
    can_state_t snapshot = {};
    if (s_can_state_mutex &&
        xSemaphoreTake(s_can_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        snapshot = s_can_state;
        xSemaphoreGive(s_can_state_mutex);
    }

    const char *indicator = "";
    if (snapshot.paused) {
        indicator = "CAN PAUSED";
    } else if (snapshot.error_active) {
        indicator = "CAN ERROR";
    }
    if (s_diag_error_label) {
        lv_label_set_text(s_diag_error_label, indicator);
    }
    if (s_fourrunner_error_label) {
        lv_label_set_text(s_fourrunner_error_label, indicator);
    }
    if (s_tire_error_label) {
        lv_label_set_text(s_tire_error_label, indicator);
    }

    const char *toggle_text = snapshot.paused ? "Resume CAN" : "Pause CAN";
    if (s_diag_can_toggle_label) {
        lv_label_set_text(s_diag_can_toggle_label, toggle_text);
    }
    if (s_fourrunner_can_toggle_label) {
        lv_label_set_text(s_fourrunner_can_toggle_label, toggle_text);
    }
    if (s_tire_can_toggle_label) {
        lv_label_set_text(s_tire_can_toggle_label, toggle_text);
    }
}

static void schedule_can_ui_update(void)
{
    lv_async_call(can_ui_update_cb, NULL);
}

static void update_can_error_state(bool rx_ok, bool tx_failed)
{
    if (!s_can_state_mutex) {
        return;
    }

    bool changed = false;
    if (xSemaphoreTake(s_can_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        bool prev_error = s_can_state.error_active;
        int64_t now_ms = get_time_ms();

        if (rx_ok) {
            s_can_state.last_rx_ms = now_ms;
            s_can_state.fail_count = 0;
            s_can_state.error_active = false;
        }

        if (!s_can_state.paused && tx_failed) {
            s_can_state.fail_count++;
        }

        if (!s_can_state.paused && !s_can_state.error_active &&
            s_can_state.fail_count >= k_can_error_fail_threshold &&
            (now_ms - s_can_state.last_rx_ms) > k_can_error_stale_ms) {
            s_can_state.error_active = true;
        }

        if (prev_error != s_can_state.error_active) {
            changed = true;
        }

        xSemaphoreGive(s_can_state_mutex);
    }

    if (changed) {
        schedule_can_ui_update();
    }
}

static void set_can_paused(bool paused)
{
    if (!s_can_state_mutex) {
        return;
    }

    if (paused) {
        esp_err_t err = twai_stop();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to stop TWAI: %s", esp_err_to_name(err));
        }
    } else {
        esp_err_t err = twai_start();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "Failed to start TWAI: %s", esp_err_to_name(err));
        }
    }

    if (xSemaphoreTake(s_can_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_can_state.paused = paused;
        s_can_state.error_active = false;
        s_can_state.fail_count = 0;
        s_can_state.last_rx_ms = get_time_ms();
        xSemaphoreGive(s_can_state_mutex);
    }

    schedule_can_ui_update();
}

static void can_toggle_event_cb(lv_event_t *e)
{
    (void)e;
    set_can_paused(!can_state_is_paused());
}

static lv_obj_t *create_header_block(lv_obj_t *parent, const char *title, const char *subtitle,
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

static lv_obj_t *create_metrics_grid(lv_obj_t *parent)
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

static lv_obj_t *create_nav_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
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

static void create_nav_bar(lv_obj_t *parent, lv_obj_t **toggle_label_out)
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
    lv_obj_t *can_btn = lv_btn_create(bar);
    lv_obj_set_size(can_btn, 160, 44);
    lv_obj_set_style_bg_color(can_btn, k_nav_button_color, 0);
    lv_obj_set_style_bg_opa(can_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(can_btn, 14, 0);
    lv_obj_set_style_border_width(can_btn, 1, 0);
    lv_obj_set_style_border_color(can_btn, k_card_border, 0);
    lv_obj_set_style_shadow_width(can_btn, 0, 0);
    lv_obj_add_event_cb(can_btn, can_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *toggle_label = lv_label_create(can_btn);
    lv_label_set_text(toggle_label, can_state_is_paused() ? "Resume CAN" : "Pause CAN");
    lv_obj_set_style_text_font(toggle_label, k_label_font, 0);
    lv_obj_set_style_text_color(toggle_label, k_text_color, 0);
    lv_obj_center(toggle_label);

    if (toggle_label_out) {
        *toggle_label_out = toggle_label;
    }

    create_nav_button(bar, ">", nav_next_event_cb);
}

static lv_obj_t *create_metric_card(lv_obj_t *parent, const char *label_text, lv_obj_t **value_label_out)
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

static void update_page_counter(lv_obj_t *label, int page_index)
{
    if (!label || s_page_count <= 0) {
        return;
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%d/%d", page_index + 1, s_page_count);
    lv_label_set_text(label, buf);
}

static void metrics_get_snapshot(can_metrics_t *out)
{
    if (!out) {
        return;
    }

    xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);
    memcpy(out, &s_metrics, sizeof(s_metrics));
    xSemaphoreGive(s_metrics_mutex);
}

static twai_message_t build_obd_request(uint16_t header, uint8_t service, uint8_t pid, uint8_t ext_addr)
{
    twai_message_t msg = {};

    msg.identifier = header;
    msg.data_length_code = 8;

    if (ext_addr != 0) {
        // Extended addressing format: [ext_addr, length, service, pid, ...]
        msg.data[0] = ext_addr;
        msg.data[1] = 0x02;
        msg.data[2] = service;
        msg.data[3] = pid;
    } else {
        // Normal format: [length, service, pid, ...]
        msg.data[0] = 0x02;
        msg.data[1] = service;
        msg.data[2] = pid;
    }

    return msg;
}

static bool is_obd_response_id(uint32_t identifier)
{
    return (identifier >= OBD_RESPONSE_ID_MIN && identifier <= OBD_RESPONSE_ID_MAX) ||
           (identifier == 0x7B8) ||  // ABS ECU response
           (identifier == 0x7C8) ||  // Meter ECU response
           (identifier == WHEEL_SPEED_BROADCAST_ID);  // Wheel speed broadcast
}

static void handle_standard_response(const twai_message_t *msg)
{
    uint8_t length = msg->data[0];
    uint8_t pid = msg->data[2];

    xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);

    switch (pid) {
        case 0x0C: {
            if (length >= 4) {
                uint16_t raw = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                s_metrics.rpm = raw / 4.0f;
                s_metrics.rpm_valid = true;
            }
            break;
        }
        case 0x42: {
            if (length >= 4) {
                uint16_t raw = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                s_metrics.vbatt_v = raw / 1000.0f;
                s_metrics.vbatt_valid = true;
            }
            break;
        }
        case 0x0F: {
            if (length >= 3) {
                s_metrics.iat_c = (float)msg->data[3] - 40.0f;
                s_metrics.iat_valid = true;
            }
            break;
        }
        case 0x33: {
            if (length >= 3) {
                s_metrics.baro_kpa = (float)msg->data[3];
                s_metrics.baro_valid = true;
            }
            break;
        }
        default:
            break;
    }

    xSemaphoreGive(s_metrics_mutex);
}

static void handle_broadcast_wheel_speed(const twai_message_t *msg)
{
    // Wheel speed is broadcast on 0x0AA
    // Format: 4x 16-bit big-endian values (one per wheel)
    // [W1_hi, W1_lo, W2_hi, W2_lo, W3_hi, W3_lo, W4_hi, W4_lo]
    // Raw value at 0 kph is approximately 6750 (0x1A5E)
    // km/h = (raw_value - 6750) / 100.0
    if (msg->data_length_code < 8) {
        return;
    }

    xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);

    // Parse 16-bit big-endian wheel speed values
    // Wheel order assumed same as diagnostic: FR, FL, RR, RL
    uint16_t raw_fr = ((uint16_t)msg->data[0] << 8) | msg->data[1];
    uint16_t raw_fl = ((uint16_t)msg->data[2] << 8) | msg->data[3];
    uint16_t raw_rr = ((uint16_t)msg->data[4] << 8) | msg->data[5];
    uint16_t raw_rl = ((uint16_t)msg->data[6] << 8) | msg->data[7];

    // Subtract offset (6750) and scale by 0.01 to get km/h
    // Use signed arithmetic to handle values slightly below offset
    static const int16_t k_wheel_speed_offset = 6750;
    s_metrics.bcast_wheel_fr_kph = ((int16_t)raw_fr - k_wheel_speed_offset) / 100.0f;
    s_metrics.bcast_wheel_fl_kph = ((int16_t)raw_fl - k_wheel_speed_offset) / 100.0f;
    s_metrics.bcast_wheel_rr_kph = ((int16_t)raw_rr - k_wheel_speed_offset) / 100.0f;
    s_metrics.bcast_wheel_rl_kph = ((int16_t)raw_rl - k_wheel_speed_offset) / 100.0f;
    s_metrics.bcast_wheel_speed_valid = true;

    xSemaphoreGive(s_metrics_mutex);
}

static void handle_extended_response(const twai_message_t *msg)
{
    uint8_t length = msg->data[0];
    uint8_t pid = msg->data[2];

    xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);

    switch (pid) {
        case 0x82: {
            if (length >= 6) {
                uint16_t raw_pan = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                s_metrics.atf_pan_c = (raw_pan / 256.0f) - 40.0f;

                uint16_t raw_tqc = (uint16_t)(msg->data[5] << 8) | msg->data[6];
                s_metrics.atf_tqc_c = (raw_tqc / 256.0f) - 40.0f;
                s_metrics.atf_valid = true;
            }
            break;
        }
        case 0x85: {
            if (length >= 5) {
                s_metrics.gear = msg->data[3];
                s_metrics.tqc_lockup = (msg->data[4] & 0x80) != 0;
                s_metrics.gear_valid = true;
            }
            break;
        }
        case 0x28: {
            if (length >= 5) {
                s_metrics.odo_km = ((uint32_t)msg->data[3] << 16)
                    | ((uint32_t)msg->data[4] << 8)
                    | (uint32_t)msg->data[5];
                s_metrics.odo_valid = true;
            }
            break;
        }
        case 0x29: {
            if (length >= 3) {
                // Fuel level: (raw * 500) / 3785 gallons
                uint8_t raw_fuel = msg->data[3];
                s_metrics.fli_vol_gal = (raw_fuel * 500.0f) / 3785.0f;
                s_metrics.fuel_valid = true;
                ESP_LOGI(TAG, "Fuel level: raw=0x%02X (%.2f gal)", raw_fuel, s_metrics.fli_vol_gal);
            }
            break;
        }
        case 0x03: {
            if (length >= 7) {
                // Diagnostic wheel speeds: (raw * 256) / 200 kph
                s_metrics.diag_wheel_fr_kph = (msg->data[3] * 256.0f) / 200.0f;
                s_metrics.diag_wheel_fl_kph = (msg->data[4] * 256.0f) / 200.0f;
                s_metrics.diag_wheel_rr_kph = (msg->data[5] * 256.0f) / 200.0f;
                s_metrics.diag_wheel_rl_kph = (msg->data[6] * 256.0f) / 200.0f;
                s_metrics.diag_wheel_speed_valid = true;
            }
            break;
        }
        default:
            break;
    }

    xSemaphoreGive(s_metrics_mutex);
}

static void process_obd_response(const twai_message_t *msg)
{
    if (!msg) {
        return;
    }

    // Handle wheel speed broadcast (0x0AA)
    if (msg->identifier == WHEEL_SPEED_BROADCAST_ID) {
        handle_broadcast_wheel_speed(msg);
        return;
    }

    if (!is_obd_response_id(msg->identifier)) {
        return;
    }

    if (msg->data_length_code < 3) {
        return;
    }

    uint8_t length = msg->data[0];
    if (length < 2) {
        return;
    }

    uint8_t service = msg->data[1];

    // Log responses from meter ECU for debugging
    if (msg->identifier == 0x7C8) {
        ESP_LOGI(TAG, "Meter RX: %02X %02X %02X %02X %02X %02X %02X %02X",
                 msg->data[0], msg->data[1], msg->data[2], msg->data[3],
                 msg->data[4], msg->data[5], msg->data[6], msg->data[7]);
    }

    if (service == 0x41) {
        handle_standard_response(msg);
    } else if (service == 0x61) {
        handle_extended_response(msg);
    }
}

static void can_rx_task(void *arg)
{
    (void)arg;
    twai_message_t rx_msg = {};

    ESP_LOGI(TAG, "CAN RX task started");

    while (1) {
        if (can_state_is_paused()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        esp_err_t err = twai_receive(&rx_msg, pdMS_TO_TICKS(100));
        if (err == ESP_OK) {
            // Log message if logging is active
            if (can_logger_is_running()) {
                can_logger_message_t log_msg = {
                    .identifier = rx_msg.identifier,
                    .data_length_code = rx_msg.data_length_code,
                    .data = {0}
                };
                memcpy(log_msg.data, rx_msg.data, 8);
                can_logger_log_message(esp_timer_get_time(), &log_msg);
            }

            process_obd_response(&rx_msg);
            update_can_error_state(true, false);
        }
    }
}

static void can_tx_task(void *arg)
{
    (void)arg;
    size_t request_index = 0;

    ESP_LOGI(TAG, "CAN TX task started");

    while (1) {
        if (can_state_is_paused()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const obd_request_t *req = &k_request_sequence[request_index];
        twai_message_t msg = build_obd_request(req->header, req->service, req->pid, req->ext_addr);

        // Log fuel requests for debugging
        if (req->header == METER_REQUEST_ID) {
            ESP_LOGI(TAG, "TX to 0x%03X: %02X %02X %02X %02X %02X %02X %02X %02X",
                     msg.identifier,
                     msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                     msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
        }

        esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "OBD request 0x%03X 0x%02X 0x%02X (ext:0x%02X) failed: %s",
                     req->header, req->service, req->pid, req->ext_addr, esp_err_to_name(err));
            update_can_error_state(false, true);
        }

        request_index = (request_index + 1) % (sizeof(k_request_sequence) / sizeof(k_request_sequence[0]));
        vTaskDelay(pdMS_TO_TICKS(OBD_POLL_INTERVAL_MS));
    }
}

typedef struct {
    int page_index;
    lv_obj_t *rpm_value;
    lv_obj_t *vbatt_value;
    lv_obj_t *iat_value;
    lv_obj_t *baro_value;
    lv_obj_t *page_counter;
    lv_obj_t *error_label;
    lv_obj_t *can_toggle_label;
} diag_page_data_t;

static void diag_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    diag_page_data_t *data = (diag_page_data_t *)calloc(1, sizeof(diag_page_data_t));
    if (!data) {
        return;
    }

    data->page_index = 0;

    page->user_data = data;
    page->container = lv_obj_create(parent);
    lv_obj_set_size(page->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(page->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(page->container, 14, 0);
    lv_obj_set_style_pad_row(page->container, 8, 0);
    apply_page_theme(page->container);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    create_header_block(page->container, "Diagnostics", "OBD-II live metrics",
                        &data->page_counter, &data->error_label);
    s_diag_error_label = data->error_label;

    lv_obj_t *grid = create_metrics_grid(page->container);

    lv_obj_t *card = create_metric_card(grid, "RPM", &data->rpm_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    card = create_metric_card(grid, "Battery (V)", &data->vbatt_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    card = create_metric_card(grid, "IAT (C)", &data->iat_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    card = create_metric_card(grid, "Baro (kPa)", &data->baro_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    create_nav_bar(page->container, &data->can_toggle_label);
    s_diag_can_toggle_label = data->can_toggle_label;

    page->is_created = true;
}

static void diag_page_on_destroy(dm_page_t *page)
{
    if (page->user_data) {
        free(page->user_data);
    }
}

static void diag_page_on_show(dm_page_t *page)
{
    diag_page_data_t *data = (diag_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    s_active_page = data->page_index;
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);
}

static void diag_page_on_hide(dm_page_t *page)
{
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
}

static void diag_page_on_update(dm_page_t *page)
{
    diag_page_data_t *data = (diag_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    can_metrics_t snap = {};
    metrics_get_snapshot(&snap);

    char buf[48];

    if (snap.rpm_valid) {
        snprintf(buf, sizeof(buf), "%.0f", snap.rpm);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->rpm_value, buf);

    if (snap.vbatt_valid) {
        snprintf(buf, sizeof(buf), "%.2f", snap.vbatt_v);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->vbatt_value, buf);

    if (snap.iat_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.iat_c);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->iat_value, buf);

    if (snap.baro_valid) {
        snprintf(buf, sizeof(buf), "%.0f", snap.baro_kpa);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->baro_value, buf);

    update_page_counter(data->page_counter, data->page_index);
}

typedef struct {
    int page_index;
    lv_obj_t *atf_pan_value;
    lv_obj_t *atf_tqc_value;
    lv_obj_t *tqc_lockup_value;
    lv_obj_t *gear_value;
    lv_obj_t *fuel_value;
    lv_obj_t *odo_value;
    lv_obj_t *page_counter;
    lv_obj_t *error_label;
    lv_obj_t *can_toggle_label;
} fourrunner_page_data_t;

static void fourrunner_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    fourrunner_page_data_t *data = (fourrunner_page_data_t *)calloc(1, sizeof(fourrunner_page_data_t));
    if (!data) {
        return;
    }

    data->page_index = 1;

    page->user_data = data;
    page->container = lv_obj_create(parent);
    lv_obj_set_size(page->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(page->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(page->container, 14, 0);
    lv_obj_set_style_pad_row(page->container, 8, 0);
    apply_page_theme(page->container);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    create_header_block(page->container, "4Runner Data", "Toyota PIDs",
                        &data->page_counter, &data->error_label);
    s_fourrunner_error_label = data->error_label;

    lv_obj_t *grid = create_metrics_grid(page->container);

    lv_obj_t *card = create_metric_card(grid, "ATF Pan (C)", &data->atf_pan_value);
    lv_obj_set_size(card, LV_PCT(31), 110);

    card = create_metric_card(grid, "ATF TQC (C)", &data->atf_tqc_value);
    lv_obj_set_size(card, LV_PCT(31), 110);

    card = create_metric_card(grid, "TQC Lockup", &data->tqc_lockup_value);
    lv_obj_set_size(card, LV_PCT(31), 110);

    card = create_metric_card(grid, "Gear", &data->gear_value);
    lv_obj_set_size(card, LV_PCT(31), 110);

    card = create_metric_card(grid, "Fuel (gal)", &data->fuel_value);
    lv_obj_set_size(card, LV_PCT(31), 110);

    card = create_metric_card(grid, "Odometer (km)", &data->odo_value);
    lv_obj_set_size(card, LV_PCT(31), 110);

    create_nav_bar(page->container, &data->can_toggle_label);
    s_fourrunner_can_toggle_label = data->can_toggle_label;

    page->is_created = true;
}

static void fourrunner_page_on_destroy(dm_page_t *page)
{
    if (page->user_data) {
        free(page->user_data);
    }
}

static void fourrunner_page_on_show(dm_page_t *page)
{
    fourrunner_page_data_t *data = (fourrunner_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    s_active_page = data->page_index;
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);
}

static void fourrunner_page_on_hide(dm_page_t *page)
{
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
}

static void fourrunner_page_on_update(dm_page_t *page)
{
    fourrunner_page_data_t *data = (fourrunner_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    can_metrics_t snap = {};
    metrics_get_snapshot(&snap);

    char buf[48];

    if (snap.atf_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.atf_pan_c);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->atf_pan_value, buf);

    if (snap.atf_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.atf_tqc_c);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->atf_tqc_value, buf);

    if (snap.gear_valid) {
        snprintf(buf, sizeof(buf), "%s", snap.tqc_lockup ? "ON" : "OFF");
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->tqc_lockup_value, buf);

    if (snap.gear_valid) {
        snprintf(buf, sizeof(buf), "%d", snap.gear);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->gear_value, buf);

    if (snap.fuel_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.fli_vol_gal);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->fuel_value, buf);

    if (snap.odo_valid) {
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)snap.odo_km);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->odo_value, buf);

    update_page_counter(data->page_counter, data->page_index);
}

static dm_page_t *diag_page_create(void)
{
    return page_create(
        "Diagnostics",
        diag_page_on_create,
        diag_page_on_destroy,
        diag_page_on_show,
        diag_page_on_hide,
        diag_page_on_update
    );
}

static dm_page_t *fourrunner_page_create(void)
{
    return page_create(
        "4Runner",
        fourrunner_page_on_create,
        fourrunner_page_on_destroy,
        fourrunner_page_on_show,
        fourrunner_page_on_hide,
        fourrunner_page_on_update
    );
}

typedef struct {
    int page_index;
    // Diagnostic wheel speeds (from 0x7B0 PID 0x03)
    lv_obj_t *diag_fl_value;
    lv_obj_t *diag_fr_value;
    lv_obj_t *diag_rl_value;
    lv_obj_t *diag_rr_value;
    // Broadcast wheel speeds (from 0x0AA)
    lv_obj_t *bcast_fl_value;
    lv_obj_t *bcast_fr_value;
    lv_obj_t *bcast_rl_value;
    lv_obj_t *bcast_rr_value;
    lv_obj_t *page_counter;
    lv_obj_t *error_label;
    lv_obj_t *can_toggle_label;
} tire_page_data_t;

static void tire_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    tire_page_data_t *data = (tire_page_data_t *)calloc(1, sizeof(tire_page_data_t));
    if (!data) {
        return;
    }

    data->page_index = 2;

    page->user_data = data;
    page->container = lv_obj_create(parent);
    lv_obj_set_size(page->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(page->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(page->container, 14, 0);
    lv_obj_set_style_pad_row(page->container, 8, 0);
    apply_page_theme(page->container);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    create_header_block(page->container, "Wheel Speed", "Diagnostic vs Broadcast",
                        &data->page_counter, &data->error_label);
    s_tire_error_label = data->error_label;

    lv_obj_t *grid = create_metrics_grid(page->container);

    // Row 1: Diagnostic wheel speeds (from 0x7B0 PID 0x03)
    lv_obj_t *card = create_metric_card(grid, "Diag FL", &data->diag_fl_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Diag FR", &data->diag_fr_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Diag RL", &data->diag_rl_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Diag RR", &data->diag_rr_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    // Row 2: Broadcast wheel speeds (from 0x0AA)
    card = create_metric_card(grid, "Bcast FL", &data->bcast_fl_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Bcast FR", &data->bcast_fr_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Bcast RL", &data->bcast_rl_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Bcast RR", &data->bcast_rr_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    create_nav_bar(page->container, &data->can_toggle_label);
    s_tire_can_toggle_label = data->can_toggle_label;

    page->is_created = true;
}

static void tire_page_on_destroy(dm_page_t *page)
{
    if (page->user_data) {
        free(page->user_data);
    }
}

static void tire_page_on_show(dm_page_t *page)
{
    tire_page_data_t *data = (tire_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    s_active_page = data->page_index;
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);
}

static void tire_page_on_hide(dm_page_t *page)
{
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
}

static void tire_page_on_update(dm_page_t *page)
{
    tire_page_data_t *data = (tire_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    can_metrics_t snap = {};
    metrics_get_snapshot(&snap);

    char buf[48];

    // Diagnostic wheel speeds (from 0x7B0 PID 0x03)
    if (snap.diag_wheel_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.diag_wheel_fl_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->diag_fl_value, buf);

    if (snap.diag_wheel_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.diag_wheel_fr_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->diag_fr_value, buf);

    if (snap.diag_wheel_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.diag_wheel_rl_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->diag_rl_value, buf);

    if (snap.diag_wheel_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.diag_wheel_rr_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->diag_rr_value, buf);

    // Broadcast wheel speeds (from 0x0AA)
    if (snap.bcast_wheel_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.bcast_wheel_fl_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->bcast_fl_value, buf);

    if (snap.bcast_wheel_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.bcast_wheel_fr_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->bcast_fr_value, buf);

    if (snap.bcast_wheel_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.bcast_wheel_rl_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->bcast_rl_value, buf);

    if (snap.bcast_wheel_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.bcast_wheel_rr_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->bcast_rr_value, buf);

    update_page_counter(data->page_counter, data->page_index);
}

static dm_page_t *tire_page_create(void)
{
    return page_create(
        "Wheel Speed",
        tire_page_on_create,
        tire_page_on_destroy,
        tire_page_on_show,
        tire_page_on_hide,
        tire_page_on_update
    );
}

// ============================================================================
// Logging Page
// ============================================================================

typedef struct {
    int page_index;
    lv_obj_t *sd_status_value;
    lv_obj_t *sd_space_value;
    lv_obj_t *log_file_value;
    lv_obj_t *log_state_value;
    lv_obj_t *msgs_logged_value;
    lv_obj_t *msgs_dropped_value;
    lv_obj_t *bytes_written_value;
    lv_obj_t *write_errors_value;
    lv_obj_t *start_stop_btn;
    lv_obj_t *start_stop_label;
    lv_obj_t *page_counter;
} logging_page_data_t;

static void logging_toggle_event_cb(lv_event_t *e)
{
    (void)e;
    if (can_logger_is_running()) {
        can_logger_stop();
    } else {
        can_logger_start();
    }
}

static void logging_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    logging_page_data_t *data = (logging_page_data_t *)calloc(1, sizeof(logging_page_data_t));
    if (!data) {
        return;
    }

    data->page_index = 3;

    page->user_data = data;
    page->container = lv_obj_create(parent);
    lv_obj_set_size(page->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(page->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(page->container, 14, 0);
    lv_obj_set_style_pad_row(page->container, 8, 0);
    apply_page_theme(page->container);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *header = lv_obj_create(page->container);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_PCT(10));
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *title_label = lv_label_create(header);
    lv_label_set_text(title_label, "CAN Logging");
    lv_obj_set_style_text_font(title_label, k_title_font, 0);
    lv_obj_set_style_text_color(title_label, k_text_color, 0);
    lv_obj_add_flag(title_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    data->page_counter = lv_label_create(header);
    lv_label_set_text(data->page_counter, "4/4");
    lv_obj_set_style_text_font(data->page_counter, k_label_font, 0);
    lv_obj_set_style_text_color(data->page_counter, k_muted_text_color, 0);
    lv_obj_add_flag(data->page_counter, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *grid = create_metrics_grid(page->container);

    // Row 1: SD card status
    lv_obj_t *card = create_metric_card(grid, "SD Card", &data->sd_status_value);
    lv_obj_set_size(card, LV_PCT(31), 90);

    card = create_metric_card(grid, "Free Space", &data->sd_space_value);
    lv_obj_set_size(card, LV_PCT(31), 90);

    card = create_metric_card(grid, "Log State", &data->log_state_value);
    lv_obj_set_size(card, LV_PCT(31), 90);

    // Row 2: Current log file
    card = create_metric_card(grid, "Current File", &data->log_file_value);
    lv_obj_set_size(card, LV_PCT(98), 70);

    // Row 3: Statistics
    card = create_metric_card(grid, "Messages", &data->msgs_logged_value);
    lv_obj_set_size(card, LV_PCT(23), 80);

    card = create_metric_card(grid, "Dropped", &data->msgs_dropped_value);
    lv_obj_set_size(card, LV_PCT(23), 80);

    card = create_metric_card(grid, "Bytes", &data->bytes_written_value);
    lv_obj_set_size(card, LV_PCT(23), 80);

    card = create_metric_card(grid, "Errors", &data->write_errors_value);
    lv_obj_set_size(card, LV_PCT(23), 80);

    // Navigation bar with start/stop button
    lv_obj_t *bar = lv_obj_create(page->container);
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

    // Start/Stop logging button
    data->start_stop_btn = lv_btn_create(bar);
    lv_obj_set_size(data->start_stop_btn, 180, 44);
    lv_obj_set_style_bg_color(data->start_stop_btn, k_accent_color, 0);
    lv_obj_set_style_bg_opa(data->start_stop_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(data->start_stop_btn, 14, 0);
    lv_obj_set_style_border_width(data->start_stop_btn, 1, 0);
    lv_obj_set_style_border_color(data->start_stop_btn, k_card_border, 0);
    lv_obj_set_style_shadow_width(data->start_stop_btn, 0, 0);
    lv_obj_add_event_cb(data->start_stop_btn, logging_toggle_event_cb, LV_EVENT_CLICKED, NULL);

    data->start_stop_label = lv_label_create(data->start_stop_btn);
    lv_label_set_text(data->start_stop_label, "Start Logging");
    lv_obj_set_style_text_font(data->start_stop_label, k_label_font, 0);
    lv_obj_set_style_text_color(data->start_stop_label, lv_color_hex(0x000000), 0);
    lv_obj_center(data->start_stop_label);

    create_nav_button(bar, ">", nav_next_event_cb);

    page->is_created = true;
}

static void logging_page_on_destroy(dm_page_t *page)
{
    if (page->user_data) {
        free(page->user_data);
    }
}

static void logging_page_on_show(dm_page_t *page)
{
    logging_page_data_t *data = (logging_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    s_active_page = data->page_index;
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);
}

static void logging_page_on_hide(dm_page_t *page)
{
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
}

static void logging_page_on_update(dm_page_t *page)
{
    logging_page_data_t *data = (logging_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    char buf[64];

    // SD card status
    sd_card_info_t sd_info = {};
    sd_card_get_info(&sd_info);

    const char *status_text = "Unknown";
    switch (sd_info.status) {
        case SD_CARD_STATUS_NOT_INITIALIZED:
            status_text = "Not Init";
            break;
        case SD_CARD_STATUS_MOUNTED:
            status_text = sd_info.card_name[0] ? sd_info.card_name : "Mounted";
            break;
        case SD_CARD_STATUS_MOUNT_FAILED:
            status_text = "Mount Fail";
            break;
        case SD_CARD_STATUS_NO_CARD:
            status_text = "No Card";
            break;
        case SD_CARD_STATUS_ERROR:
            status_text = "Error";
            break;
    }
    lv_label_set_text(data->sd_status_value, status_text);

    // Free space
    if (sd_info.status == SD_CARD_STATUS_MOUNTED && sd_info.free_bytes > 0) {
        if (sd_info.free_bytes >= 1024 * 1024 * 1024) {
            snprintf(buf, sizeof(buf), "%.1f GB",
                     (float)sd_info.free_bytes / (1024 * 1024 * 1024));
        } else {
            snprintf(buf, sizeof(buf), "%.1f MB",
                     (float)sd_info.free_bytes / (1024 * 1024));
        }
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->sd_space_value, buf);

    // Logger stats
    can_logger_stats_t stats = {};
    can_logger_get_stats(&stats);

    // Log state
    const char *state_text = "Stopped";
    lv_color_t btn_color = k_accent_color;
    const char *btn_text = "Start Logging";

    switch (stats.state) {
        case CAN_LOGGER_STOPPED:
            state_text = "Stopped";
            btn_color = k_accent_color;
            btn_text = "Start Logging";
            break;
        case CAN_LOGGER_RUNNING:
            state_text = "Recording";
            btn_color = k_warning_color;
            btn_text = "Stop Logging";
            break;
        case CAN_LOGGER_ERROR:
            state_text = "Error";
            btn_color = lv_color_hex(0xcc4444);
            btn_text = "Retry";
            break;
    }
    lv_label_set_text(data->log_state_value, state_text);
    lv_obj_set_style_bg_color(data->start_stop_btn, btn_color, 0);
    lv_label_set_text(data->start_stop_label, btn_text);

    // Current file
    if (stats.current_file[0]) {
        // Extract just the filename from the path
        const char *filename = strrchr(stats.current_file, '/');
        if (filename) {
            filename++;
        } else {
            filename = stats.current_file;
        }
        lv_label_set_text(data->log_file_value, filename);
    } else {
        lv_label_set_text(data->log_file_value, "--");
    }

    // Statistics
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats.messages_logged);
    lv_label_set_text(data->msgs_logged_value, buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats.messages_dropped);
    lv_label_set_text(data->msgs_dropped_value, buf);

    if (stats.bytes_written >= 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1f MB",
                 (float)stats.bytes_written / (1024 * 1024));
    } else if (stats.bytes_written >= 1024) {
        snprintf(buf, sizeof(buf), "%.1f KB",
                 (float)stats.bytes_written / 1024);
    } else {
        snprintf(buf, sizeof(buf), "%lu B", (unsigned long)stats.bytes_written);
    }
    lv_label_set_text(data->bytes_written_value, buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)stats.write_errors);
    lv_label_set_text(data->write_errors_value, buf);

    update_page_counter(data->page_counter, data->page_index);
}

static dm_page_t *logging_page_create(void)
{
    return page_create(
        "Logging",
        logging_page_on_create,
        logging_page_on_destroy,
        logging_page_on_show,
        logging_page_on_hide,
        logging_page_on_update
    );
}

// ============================================================================
// RTC Settings Page
// ============================================================================

typedef struct {
    int page_index;
    lv_obj_t *page_counter;
    lv_obj_t *current_time_value;
    lv_obj_t *current_date_value;
    lv_obj_t *status_value;
    lv_obj_t *year_value;
    lv_obj_t *month_value;
    lv_obj_t *day_value;
    lv_obj_t *hour_value;
    lv_obj_t *min_value;
    lv_obj_t *sec_value;
    lv_obj_t *set_btn;
    lv_obj_t *set_label;
    // Edit values
    pcf_datetime_t edit_time;
    bool editing;
} rtc_page_data_t;

static rtc_page_data_t *s_rtc_page_data = NULL;

static void rtc_update_edit_display(rtc_page_data_t *data)
{
    if (!data) return;

    char buf[16];
    snprintf(buf, sizeof(buf), "%04d", data->edit_time.year);
    lv_label_set_text(data->year_value, buf);

    snprintf(buf, sizeof(buf), "%02d", data->edit_time.month);
    lv_label_set_text(data->month_value, buf);

    snprintf(buf, sizeof(buf), "%02d", data->edit_time.day);
    lv_label_set_text(data->day_value, buf);

    snprintf(buf, sizeof(buf), "%02d", data->edit_time.hour);
    lv_label_set_text(data->hour_value, buf);

    snprintf(buf, sizeof(buf), "%02d", data->edit_time.min);
    lv_label_set_text(data->min_value, buf);

    snprintf(buf, sizeof(buf), "%02d", data->edit_time.sec);
    lv_label_set_text(data->sec_value, buf);
}

static void rtc_adj_year_up(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.year < 2099) {
        s_rtc_page_data->edit_time.year++;
        s_rtc_page_data->edit_time.dotw = pcf_rtc_calculate_dotw(
            s_rtc_page_data->edit_time.year,
            s_rtc_page_data->edit_time.month,
            s_rtc_page_data->edit_time.day);
        rtc_update_edit_display(s_rtc_page_data);
    }
}

static void rtc_adj_year_down(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.year > 2000) {
        s_rtc_page_data->edit_time.year--;
        s_rtc_page_data->edit_time.dotw = pcf_rtc_calculate_dotw(
            s_rtc_page_data->edit_time.year,
            s_rtc_page_data->edit_time.month,
            s_rtc_page_data->edit_time.day);
        rtc_update_edit_display(s_rtc_page_data);
    }
}

static void rtc_adj_month_up(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.month < 12) {
        s_rtc_page_data->edit_time.month++;
    } else {
        s_rtc_page_data->edit_time.month = 1;
    }
    s_rtc_page_data->edit_time.dotw = pcf_rtc_calculate_dotw(
        s_rtc_page_data->edit_time.year,
        s_rtc_page_data->edit_time.month,
        s_rtc_page_data->edit_time.day);
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_month_down(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.month > 1) {
        s_rtc_page_data->edit_time.month--;
    } else {
        s_rtc_page_data->edit_time.month = 12;
    }
    s_rtc_page_data->edit_time.dotw = pcf_rtc_calculate_dotw(
        s_rtc_page_data->edit_time.year,
        s_rtc_page_data->edit_time.month,
        s_rtc_page_data->edit_time.day);
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_day_up(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.day < 31) {
        s_rtc_page_data->edit_time.day++;
    } else {
        s_rtc_page_data->edit_time.day = 1;
    }
    s_rtc_page_data->edit_time.dotw = pcf_rtc_calculate_dotw(
        s_rtc_page_data->edit_time.year,
        s_rtc_page_data->edit_time.month,
        s_rtc_page_data->edit_time.day);
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_day_down(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.day > 1) {
        s_rtc_page_data->edit_time.day--;
    } else {
        s_rtc_page_data->edit_time.day = 31;
    }
    s_rtc_page_data->edit_time.dotw = pcf_rtc_calculate_dotw(
        s_rtc_page_data->edit_time.year,
        s_rtc_page_data->edit_time.month,
        s_rtc_page_data->edit_time.day);
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_hour_up(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.hour < 23) {
        s_rtc_page_data->edit_time.hour++;
    } else {
        s_rtc_page_data->edit_time.hour = 0;
    }
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_hour_down(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.hour > 0) {
        s_rtc_page_data->edit_time.hour--;
    } else {
        s_rtc_page_data->edit_time.hour = 23;
    }
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_min_up(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.min < 59) {
        s_rtc_page_data->edit_time.min++;
    } else {
        s_rtc_page_data->edit_time.min = 0;
    }
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_min_down(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.min > 0) {
        s_rtc_page_data->edit_time.min--;
    } else {
        s_rtc_page_data->edit_time.min = 59;
    }
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_sec_up(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.sec < 59) {
        s_rtc_page_data->edit_time.sec++;
    } else {
        s_rtc_page_data->edit_time.sec = 0;
    }
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_adj_sec_down(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;
    if (s_rtc_page_data->edit_time.sec > 0) {
        s_rtc_page_data->edit_time.sec--;
    } else {
        s_rtc_page_data->edit_time.sec = 59;
    }
    rtc_update_edit_display(s_rtc_page_data);
}

static void rtc_set_time_event(lv_event_t *e)
{
    (void)e;
    if (!s_rtc_page_data) return;

    esp_err_t err = pcf_rtc_set_time(&s_rtc_page_data->edit_time);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "RTC time set successfully");
    } else {
        ESP_LOGE(TAG, "Failed to set RTC time: %s", esp_err_to_name(err));
    }
}

static lv_obj_t *create_adj_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb)
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

static lv_obj_t *create_time_field(lv_obj_t *parent, const char *label_text,
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

static void rtc_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    rtc_page_data_t *data = (rtc_page_data_t *)calloc(1, sizeof(rtc_page_data_t));
    if (!data) {
        return;
    }

    data->page_index = 4;
    s_rtc_page_data = data;

    // Initialize edit time from current RTC
    if (pcf_rtc_get_time(&data->edit_time) != ESP_OK) {
        // Set default time if RTC read fails
        data->edit_time.year = 2025;
        data->edit_time.month = 1;
        data->edit_time.day = 1;
        data->edit_time.hour = 12;
        data->edit_time.min = 0;
        data->edit_time.sec = 0;
        data->edit_time.dotw = pcf_rtc_calculate_dotw(2025, 1, 1);
    }

    page->user_data = data;
    page->container = lv_obj_create(parent);
    lv_obj_set_size(page->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(page->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(page->container, 14, 0);
    lv_obj_set_style_pad_row(page->container, 8, 0);
    apply_page_theme(page->container);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Header
    lv_obj_t *header = lv_obj_create(page->container);
    lv_obj_set_width(header, LV_PCT(100));
    lv_obj_set_height(header, LV_PCT(10));
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(header, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *title_label = lv_label_create(header);
    lv_label_set_text(title_label, "RTC Settings");
    lv_obj_set_style_text_font(title_label, k_title_font, 0);
    lv_obj_set_style_text_color(title_label, k_text_color, 0);
    lv_obj_add_flag(title_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    data->page_counter = lv_label_create(header);
    lv_label_set_text(data->page_counter, "5/5");
    lv_obj_set_style_text_font(data->page_counter, k_label_font, 0);
    lv_obj_set_style_text_color(data->page_counter, k_muted_text_color, 0);
    lv_obj_add_flag(data->page_counter, LV_OBJ_FLAG_GESTURE_BUBBLE);

    // Current time display row
    lv_obj_t *current_row = lv_obj_create(page->container);
    lv_obj_set_width(current_row, LV_PCT(100));
    lv_obj_set_height(current_row, 70);
    lv_obj_set_style_bg_opa(current_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(current_row, 0, 0);
    lv_obj_set_style_pad_all(current_row, 0, 0);
    lv_obj_set_flex_flow(current_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(current_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(current_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(current_row, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *card = create_metric_card(current_row, "Current Date", &data->current_date_value);
    lv_obj_set_size(card, LV_PCT(30), 65);

    card = create_metric_card(current_row, "Current Time", &data->current_time_value);
    lv_obj_set_size(card, LV_PCT(30), 65);

    card = create_metric_card(current_row, "Status", &data->status_value);
    lv_obj_set_size(card, LV_PCT(30), 65);

    // Time edit fields row
    lv_obj_t *edit_row = lv_obj_create(page->container);
    lv_obj_set_width(edit_row, LV_PCT(100));
    lv_obj_set_flex_grow(edit_row, 1);
    lv_obj_set_style_bg_opa(edit_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(edit_row, 0, 0);
    lv_obj_set_style_pad_all(edit_row, 0, 0);
    lv_obj_set_style_pad_column(edit_row, 8, 0);
    lv_obj_set_flex_flow(edit_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(edit_row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(edit_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(edit_row, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *field = create_time_field(edit_row, "Year", &data->year_value,
                                         rtc_adj_year_up, rtc_adj_year_down);
    lv_obj_set_size(field, 70, 150);

    field = create_time_field(edit_row, "Month", &data->month_value,
                               rtc_adj_month_up, rtc_adj_month_down);
    lv_obj_set_size(field, 60, 150);

    field = create_time_field(edit_row, "Day", &data->day_value,
                               rtc_adj_day_up, rtc_adj_day_down);
    lv_obj_set_size(field, 60, 150);

    field = create_time_field(edit_row, "Hour", &data->hour_value,
                               rtc_adj_hour_up, rtc_adj_hour_down);
    lv_obj_set_size(field, 60, 150);

    field = create_time_field(edit_row, "Min", &data->min_value,
                               rtc_adj_min_up, rtc_adj_min_down);
    lv_obj_set_size(field, 60, 150);

    field = create_time_field(edit_row, "Sec", &data->sec_value,
                               rtc_adj_sec_up, rtc_adj_sec_down);
    lv_obj_set_size(field, 60, 150);

    // Initialize edit display
    rtc_update_edit_display(data);

    // Navigation bar with set button
    lv_obj_t *bar = lv_obj_create(page->container);
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

    // Set Time button
    data->set_btn = lv_btn_create(bar);
    lv_obj_set_size(data->set_btn, 140, 44);
    lv_obj_set_style_bg_color(data->set_btn, k_accent_color, 0);
    lv_obj_set_style_bg_opa(data->set_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(data->set_btn, 14, 0);
    lv_obj_set_style_border_width(data->set_btn, 1, 0);
    lv_obj_set_style_border_color(data->set_btn, k_card_border, 0);
    lv_obj_set_style_shadow_width(data->set_btn, 0, 0);
    lv_obj_add_event_cb(data->set_btn, rtc_set_time_event, LV_EVENT_CLICKED, NULL);

    data->set_label = lv_label_create(data->set_btn);
    lv_label_set_text(data->set_label, "Set Time");
    lv_obj_set_style_text_font(data->set_label, k_label_font, 0);
    lv_obj_set_style_text_color(data->set_label, lv_color_hex(0x000000), 0);
    lv_obj_center(data->set_label);

    create_nav_button(bar, ">", nav_next_event_cb);

    page->is_created = true;
}

static void rtc_page_on_destroy(dm_page_t *page)
{
    if (page->user_data) {
        s_rtc_page_data = NULL;
        free(page->user_data);
    }
}

static void rtc_page_on_show(dm_page_t *page)
{
    rtc_page_data_t *data = (rtc_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    s_active_page = data->page_index;
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);

    // Refresh edit time from RTC when showing page
    if (pcf_rtc_get_time(&data->edit_time) == ESP_OK) {
        rtc_update_edit_display(data);
    }
}

static void rtc_page_on_hide(dm_page_t *page)
{
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
}

static void rtc_page_on_update(dm_page_t *page)
{
    rtc_page_data_t *data = (rtc_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    char buf[24];
    pcf_datetime_t now;

    if (pcf_rtc_get_time(&now) == ESP_OK) {
        pcf_rtc_format_date(buf, sizeof(buf), &now);
        lv_label_set_text(data->current_date_value, buf);

        pcf_rtc_format_time(buf, sizeof(buf), &now);
        lv_label_set_text(data->current_time_value, buf);

        if (pcf_rtc_is_time_valid()) {
            lv_label_set_text(data->status_value, "OK");
            lv_obj_set_style_text_color(data->status_value, k_accent_color, 0);
        } else {
            lv_label_set_text(data->status_value, "Not Set");
            lv_obj_set_style_text_color(data->status_value, k_warning_color, 0);
        }
    } else {
        lv_label_set_text(data->current_date_value, "Error");
        lv_label_set_text(data->current_time_value, "Error");
        lv_label_set_text(data->status_value, "Error");
        lv_obj_set_style_text_color(data->status_value, lv_color_hex(0xcc4444), 0);
    }

    update_page_counter(data->page_counter, data->page_index);
}

static dm_page_t *rtc_page_create(void)
{
    return page_create(
        "RTC Settings",
        rtc_page_on_create,
        rtc_page_on_destroy,
        rtc_page_on_show,
        rtc_page_on_hide,
        rtc_page_on_update
    );
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "4Runner CAN Bus Display starting");
    ESP_LOGI(TAG, "TX GPIO: %d, RX GPIO: %d", TX_GPIO_NUM, RX_GPIO_NUM);

    s_metrics_mutex = xSemaphoreCreateMutex();
    if (!s_metrics_mutex) {
        ESP_LOGE(TAG, "Failed to create metrics mutex");
        return;
    }

    s_can_state_mutex = xSemaphoreCreateMutex();
    if (!s_can_state_mutex) {
        ESP_LOGE(TAG, "Failed to create CAN state mutex");
        return;
    }
    if (xSemaphoreTake(s_can_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_can_state.last_rx_ms = get_time_ms();
        xSemaphoreGive(s_can_state_mutex);
    }

    // Initialize TWAI driver with old API
    esp_err_t twai_err = twai_driver_install(&g_config, &t_config, &f_config);
    if (twai_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver: %s", esp_err_to_name(twai_err));
        return;
    }
    ESP_LOGI(TAG, "TWAI driver installed");

    // TEMPORARY: Start with CAN paused for bench testing
    // Uncomment below to auto-start CAN on boot
#if 0
    twai_err = twai_start();
    if (twai_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TWAI: %s", esp_err_to_name(twai_err));
        return;
    }
    ESP_LOGI(TAG, "TWAI started");
#else
    ESP_LOGI(TAG, "TWAI paused on startup (bench test mode)");
    if (s_can_state_mutex) {
        xSemaphoreTake(s_can_state_mutex, portMAX_DELAY);
        s_can_state.paused = true;
        xSemaphoreGive(s_can_state_mutex);
    }
#endif

    display_config_t display_config = {
        .h_res = lcd_h_res,
        .v_res = lcd_v_res,
        .pixel_clock_hz = lcd_pixel_clock_hz,
        .hsync_pulse_width = lcd_hsync_pulse_width,
        .hsync_back_porch = lcd_hsync_back_porch,
        .hsync_front_porch = lcd_hsync_front_porch,
        .vsync_pulse_width = lcd_vsync_pulse_width,
        .vsync_back_porch = lcd_vsync_back_porch,
        .vsync_front_porch = lcd_vsync_front_porch,
        .data_width = lcd_data_width,
        .bits_per_pixel = lcd_bits_per_pixel,
        .num_fbs = lcd_num_fbs,
        .bounce_buffer_size_px = lcd_bounce_buffer_size_px,
        .fb_in_psram = lcd_fb_in_psram,
        .hsync_io_num = lcd_hsync_io_num,
        .vsync_io_num = lcd_vsync_io_num,
        .de_io_num = lcd_de_io_num,
        .pclk_io_num = lcd_pclk_io_num,
        .disp_io_num = lcd_disp_io_num,
        .data_io_nums = {0},
        .i2c_port = lcd_i2c_port,
        .i2c_sda_io_num = lcd_i2c_sda_io_num,
        .i2c_scl_io_num = lcd_i2c_scl_io_num,
        .i2c_freq_hz = lcd_i2c_freq_hz,
        .touch_reset_io_num = lcd_touch_reset_io_num,
        .touch_int_io_num = lcd_touch_int_io_num,
        .touch_enabled = true,
        .draw_buf_lines = 20,
        .tick_period_ms = 2,
        .orientation = DISPLAY_ORIENTATION_LANDSCAPE,
        .x_offset = 0,
        .y_offset = 0
    };
    memcpy(display_config.data_io_nums, lcd_data_io_nums, sizeof(lcd_data_io_nums));

    s_display = display_manager_init(&display_config);
    if (!s_display) {
        ESP_LOGE(TAG, "Failed to initialize display manager");
        return;
    }

    lv_display_t *display = display_manager_get_display(s_display);
    if (display) {
        lv_obj_t *screen = lv_display_get_screen_active(display);
        if (screen) {
            lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(screen, page_swipe_event_cb, LV_EVENT_GESTURE, NULL);
        }
    }

    // Initialize RTC (uses I2C which was initialized by display_manager)
    esp_err_t rtc_err = pcf_rtc_init(lcd_i2c_port);
    if (rtc_err != ESP_OK) {
        ESP_LOGW(TAG, "RTC init failed: %s", esp_err_to_name(rtc_err));
    } else {
        ESP_LOGI(TAG, "RTC initialized");
    }

    // Initialize SD card (uses I2C which was initialized by display_manager)
    esp_err_t sd_err = sd_card_init(lcd_i2c_port);
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "SD card init failed: %s (logging disabled)", esp_err_to_name(sd_err));
    } else {
        ESP_LOGI(TAG, "SD card initialized");

        // Initialize CAN logger with 512 message ring buffer
        esp_err_t log_err = can_logger_init(512);
        if (log_err != ESP_OK) {
            ESP_LOGW(TAG, "CAN logger init failed: %s", esp_err_to_name(log_err));
        } else {
            ESP_LOGI(TAG, "CAN logger initialized");
        }
    }

    dm_page_t *diag_page = diag_page_create();
    dm_page_t *fourrunner_page = fourrunner_page_create();
    dm_page_t *tire_page = tire_page_create();
    dm_page_t *logging_page = logging_page_create();
    dm_page_t *rtc_page = rtc_page_create();

    if (diag_page) {
        display_manager_add_page(s_display, diag_page);
        s_page_count++;
    }

    if (fourrunner_page) {
        display_manager_add_page(s_display, fourrunner_page);
        s_page_count++;
    }

    if (tire_page) {
        display_manager_add_page(s_display, tire_page);
        s_page_count++;
    }

    if (logging_page) {
        display_manager_add_page(s_display, logging_page);
        s_page_count++;
    }

    if (rtc_page) {
        display_manager_add_page(s_display, rtc_page);
        s_page_count++;
    }

    if (s_page_count > 0) {
        s_active_page = 0;
        display_manager_switch_to_page(s_display, s_active_page);
    }

    xTaskCreatePinnedToCore(can_rx_task, "CAN_RX", 4096, NULL, 5, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(can_tx_task, "CAN_TX", 4096, NULL, 4, NULL, tskNO_AFFINITY);
    ESP_LOGI(TAG, "CAN tasks started");
}
