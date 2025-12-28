/*
 * Diagnostics Page Implementation
 */

#include "diag_page.h"

#include <stdio.h>
#include <stdlib.h>

#include <esp_log.h>

#include "lvgl.h"

#include "app_state.h"
#include "page_utils.h"
#include "settings_store.h"

static const char *TAG = "DIAG_PAGE";

static bool s_autostart_ui_updating = false;

typedef struct {
    int page_index;
    lv_obj_t *rpm_value;
    lv_obj_t *vbatt_value;
    lv_obj_t *iat_value;
    lv_obj_t *baro_value;
    lv_obj_t *page_counter;
    lv_obj_t *error_label;
    lv_obj_t *can_toggle_label;
    lv_obj_t *autostart_switch;
} diag_page_data_t;

static void autostart_switch_event_cb(lv_event_t *e)
{
    if (s_autostart_ui_updating) {
        return;
    }

    lv_obj_t *target = (lv_obj_t *)lv_event_get_target(e);
    if (!target) {
        return;
    }

    bool enabled = lv_obj_has_state(target, LV_STATE_CHECKED);
    if (!settings_set_can_autostart(enabled)) {
        ESP_LOGW(TAG, "Failed to update CAN auto-start flag");
        s_autostart_ui_updating = true;
        if (enabled) {
            lv_obj_clear_state(target, LV_STATE_CHECKED);
        } else {
            lv_obj_add_state(target, LV_STATE_CHECKED);
        }
        s_autostart_ui_updating = false;
    }
}

static void set_autostart_switch_state(lv_obj_t *sw, bool enabled)
{
    if (!sw) {
        return;
    }

    s_autostart_ui_updating = true;
    if (enabled) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
    s_autostart_ui_updating = false;
}

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
    g_diag_error_label = data->error_label;

    lv_obj_t *grid = create_metrics_grid(page->container);

    lv_obj_t *card = create_metric_card(grid, "RPM", &data->rpm_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    card = create_metric_card(grid, "Battery (V)", &data->vbatt_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    card = create_metric_card(grid, "IAT (C)", &data->iat_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    card = create_metric_card(grid, "Baro (kPa)", &data->baro_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    lv_obj_t *autostart_row = lv_obj_create(page->container);
    lv_obj_set_width(autostart_row, LV_PCT(100));
    lv_obj_set_height(autostart_row, 44);
    lv_obj_set_style_bg_opa(autostart_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(autostart_row, 0, 0);
    lv_obj_set_style_pad_all(autostart_row, 0, 0);
    lv_obj_set_style_pad_left(autostart_row, 4, 0);
    lv_obj_set_style_pad_right(autostart_row, 4, 0);
    lv_obj_set_flex_flow(autostart_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(autostart_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(autostart_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(autostart_row, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *autostart_label = lv_label_create(autostart_row);
    lv_label_set_text(autostart_label, "Auto-start CAN on boot");
    lv_obj_set_style_text_font(autostart_label, k_label_font, 0);
    lv_obj_set_style_text_color(autostart_label, k_muted_text_color, 0);
    lv_obj_add_flag(autostart_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    data->autostart_switch = lv_switch_create(autostart_row);
    lv_obj_add_event_cb(data->autostart_switch, autostart_switch_event_cb, LV_EVENT_VALUE_CHANGED,
                        NULL);

    bool auto_start = false;
    settings_get_can_autostart(&auto_start);
    set_autostart_switch_state(data->autostart_switch, auto_start);

    create_nav_bar(page->container, &data->can_toggle_label);
    g_diag_can_toggle_label = data->can_toggle_label;

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

    app_state_set_active_page(data->page_index);
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);

    bool auto_start = false;
    settings_get_can_autostart(&auto_start);
    set_autostart_switch_state(data->autostart_switch, auto_start);
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

dm_page_t *diag_page_create(void)
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
