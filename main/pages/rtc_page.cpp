/*
 * RTC Settings Page Implementation
 */

#include "rtc_page.h"

#include <stdio.h>
#include <stdlib.h>

#include <esp_log.h>

#include "lvgl.h"

#include "app_state.h"
#include "page_utils.h"
#include "rtc_pcf85063a.h"

static const char *TAG = "RTC_PAGE";

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

static void rtc_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    rtc_page_data_t *data = (rtc_page_data_t *)calloc(1, sizeof(rtc_page_data_t));
    if (!data) {
        return;
    }

    data->page_index = 6;
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
    lv_label_set_text(data->page_counter, "5/6");
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

    app_state_set_active_page(data->page_index);
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

dm_page_t *rtc_page_create(void)
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
