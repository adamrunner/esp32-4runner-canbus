/*
 * CAN Logging Page Implementation
 */

#include "logging_page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <driver/twai.h>
#include <esp_timer.h>

#include "lvgl.h"

#include "app_state.h"
#include "page_utils.h"
#include "sd_card.h"
#include "can_logger.h"

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
    lv_obj_t *log_rate_value;
    lv_obj_t *drop_pct_value;
    lv_obj_t *rx_missed_value;
    lv_obj_t *rx_overrun_value;
    lv_obj_t *start_stop_btn;
    lv_obj_t *start_stop_label;
    lv_obj_t *page_counter;
    int64_t last_stats_ms;
    uint32_t last_logged;
    uint32_t last_dropped;
    uint32_t last_rx_missed;
    uint32_t last_rx_overrun;
} logging_page_data_t;

static const lv_color_t k_log_start_color = lv_color_hex(0x40A840);

static uint32_t delta_u32(uint32_t current, uint32_t last)
{
    return current >= last ? (current - last) : 0;
}

static void logging_toggle_event_cb(lv_event_t *e)
{
    lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (!target || lv_obj_has_state(target, LV_STATE_DISABLED)) {
        return;
    }

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
    lv_obj_set_height(header, LV_PCT(7));
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
    lv_label_set_text(data->page_counter, "4/6");
    lv_obj_set_style_text_font(data->page_counter, k_label_font, 0);
    lv_obj_set_style_text_color(data->page_counter, k_muted_text_color, 0);
    lv_obj_add_flag(data->page_counter, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *grid = create_metrics_grid(page->container);
    lv_obj_add_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);

    // Row 1: SD card status
    lv_obj_t *card = create_metric_card(grid, "SD Card", &data->sd_status_value);
    lv_obj_set_size(card, LV_PCT(31), 80);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 4, 0);

    card = create_metric_card(grid, "Free Space", &data->sd_space_value);
    lv_obj_set_size(card, LV_PCT(31), 80);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 4, 0);

    card = create_metric_card(grid, "Log State", &data->log_state_value);
    lv_obj_set_size(card, LV_PCT(31), 80);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 4, 0);

    // Row 2: Current log file
    card = create_metric_card(grid, "Current File", &data->log_file_value);
    lv_obj_set_size(card, LV_PCT(98), 70);

    // Row 3: Statistics
    card = create_metric_card(grid, "Messages", &data->msgs_logged_value);
    lv_obj_set_size(card, LV_PCT(23), 80);
    lv_obj_set_style_pad_bottom(card, 7, 0);

    card = create_metric_card(grid, "Dropped", &data->msgs_dropped_value);
    lv_obj_set_size(card, LV_PCT(23), 80);
    lv_obj_set_style_pad_bottom(card, 7, 0);

    card = create_metric_card(grid, "Bytes", &data->bytes_written_value);
    lv_obj_set_size(card, LV_PCT(23), 80);
    lv_obj_set_style_pad_bottom(card, 7, 0);

    card = create_metric_card(grid, "Errors", &data->write_errors_value);
    lv_obj_set_size(card, LV_PCT(23), 80);
    lv_obj_set_style_pad_bottom(card, 7, 0);

    // Row 4: Telemetry
    card = create_metric_card(grid, "Log/s", &data->log_rate_value);
    lv_obj_set_size(card, LV_PCT(23), 80);

    card = create_metric_card(grid, "Drop %", &data->drop_pct_value);
    lv_obj_set_size(card, LV_PCT(23), 80);

    card = create_metric_card(grid, "RX Miss/s", &data->rx_missed_value);
    lv_obj_set_size(card, LV_PCT(23), 80);

    card = create_metric_card(grid, "RX Ovr/s", &data->rx_overrun_value);
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
    lv_obj_set_style_bg_color(data->start_stop_btn, k_log_start_color, 0);
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

    app_state_set_active_page(data->page_index);
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
    bool sd_ready = (sd_info.status == SD_CARD_STATUS_MOUNTED);

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
    bool logger_ready = (can_logger_get_stats(&stats) == ESP_OK);

    // Log state
    const char *state_text = "Stopped";
    lv_color_t btn_color = k_accent_color;
    const char *btn_text = "Start Logging";
    lv_color_t btn_text_color = lv_color_hex(0x000000);
    bool enable_button = true;

    if (!sd_ready) {
        state_text = "No Card";
        btn_text = "No SD Card";
        btn_color = k_card_border;
        btn_text_color = k_muted_text_color;
        enable_button = false;
    } else if (!logger_ready) {
        state_text = "Unavailable";
        btn_text = "Logger N/A";
        btn_color = k_card_border;
        btn_text_color = k_muted_text_color;
        enable_button = false;
    } else {
        switch (stats.state) {
            case CAN_LOGGER_STOPPED:
                state_text = "Stopped";
                btn_color = k_log_start_color;
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
    }
    lv_label_set_text(data->log_state_value, state_text);
    lv_obj_set_style_bg_color(data->start_stop_btn, btn_color, 0);
    lv_label_set_text(data->start_stop_label, btn_text);
    lv_obj_set_style_text_color(data->start_stop_label, btn_text_color, 0);
    if (enable_button) {
        lv_obj_clear_state(data->start_stop_btn, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(data->start_stop_btn, LV_STATE_DISABLED);
    }

    // Current file
    if (logger_ready && stats.current_file[0]) {
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
    if (logger_ready) {
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
    } else {
        lv_label_set_text(data->msgs_logged_value, "--");
        lv_label_set_text(data->msgs_dropped_value, "--");
        lv_label_set_text(data->bytes_written_value, "--");
        lv_label_set_text(data->write_errors_value, "--");
    }

    // Telemetry
    int64_t now_ms = esp_timer_get_time() / 1000;
    float interval_s = 0.0f;
    if (data->last_stats_ms > 0) {
        interval_s = (now_ms - data->last_stats_ms) / 1000.0f;
        if (interval_s < 0.01f) {
            interval_s = 0.0f;
        }
    }
    data->last_stats_ms = now_ms;

    twai_status_info_t status = {};
    bool have_status = (twai_get_status_info(&status) == ESP_OK);

    if (logger_ready && interval_s > 0.0f) {
        uint32_t logged_delta = delta_u32(stats.messages_logged, data->last_logged);
        uint32_t dropped_delta = delta_u32(stats.messages_dropped, data->last_dropped);
        uint32_t total_delta = logged_delta + dropped_delta;
        float drop_pct = total_delta > 0 ? (dropped_delta * 100.0f) / total_delta : 0.0f;
        float log_rate = logged_delta / interval_s;

        snprintf(buf, sizeof(buf), "%.0f/s", log_rate);
        lv_label_set_text(data->log_rate_value, buf);

        snprintf(buf, sizeof(buf), "%.1f%%", drop_pct);
        lv_label_set_text(data->drop_pct_value, buf);

        data->last_logged = stats.messages_logged;
        data->last_dropped = stats.messages_dropped;
    } else {
        lv_label_set_text(data->log_rate_value, "--");
        lv_label_set_text(data->drop_pct_value, "--");
        if (logger_ready) {
            data->last_logged = stats.messages_logged;
            data->last_dropped = stats.messages_dropped;
        }
    }

    if (have_status && interval_s > 0.0f) {
        uint32_t rx_missed_delta = delta_u32(status.rx_missed_count, data->last_rx_missed);
        uint32_t rx_overrun_delta = delta_u32(status.rx_overrun_count, data->last_rx_overrun);
        float rx_missed_rate = rx_missed_delta / interval_s;
        float rx_overrun_rate = rx_overrun_delta / interval_s;

        snprintf(buf, sizeof(buf), "%.1f/s", rx_missed_rate);
        lv_label_set_text(data->rx_missed_value, buf);

        snprintf(buf, sizeof(buf), "%.1f/s", rx_overrun_rate);
        lv_label_set_text(data->rx_overrun_value, buf);

        data->last_rx_missed = status.rx_missed_count;
        data->last_rx_overrun = status.rx_overrun_count;
    } else {
        lv_label_set_text(data->rx_missed_value, "--");
        lv_label_set_text(data->rx_overrun_value, "--");
        if (have_status) {
            data->last_rx_missed = status.rx_missed_count;
            data->last_rx_overrun = status.rx_overrun_count;
        }
    }

    update_page_counter(data->page_counter, data->page_index);
}

dm_page_t *logging_page_create(void)
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
