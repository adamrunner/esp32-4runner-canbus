/*
 * Wheel Speed Page Implementation
 */

#include "wheel_speed_page.h"

#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"

#include "app_state.h"
#include "page_utils.h"

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
    // Vehicle speed
    lv_obj_t *diag_vehicle_speed_value;   // from OBD-II PID 0x0D
    lv_obj_t *bcast_vehicle_speed_value;  // from broadcast 0x0B4
    lv_obj_t *page_counter;
    lv_obj_t *error_label;
    lv_obj_t *can_toggle_label;
} wheel_speed_page_data_t;

static void wheel_speed_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    wheel_speed_page_data_t *data = (wheel_speed_page_data_t *)calloc(1, sizeof(wheel_speed_page_data_t));
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
    g_tire_error_label = data->error_label;

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

    // Row 3: Vehicle speed (diagnostic vs broadcast)
    card = create_metric_card(grid, "Diag Speed", &data->diag_vehicle_speed_value);
    lv_obj_set_size(card, LV_PCT(48), 100);

    card = create_metric_card(grid, "Bcast Speed", &data->bcast_vehicle_speed_value);
    lv_obj_set_size(card, LV_PCT(48), 100);

    create_nav_bar(page->container, &data->can_toggle_label);
    g_tire_can_toggle_label = data->can_toggle_label;

    page->is_created = true;
}

static void wheel_speed_page_on_destroy(dm_page_t *page)
{
    if (page->user_data) {
        free(page->user_data);
    }
}

static void wheel_speed_page_on_show(dm_page_t *page)
{
    wheel_speed_page_data_t *data = (wheel_speed_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    app_state_set_active_page(data->page_index);
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);
}

static void wheel_speed_page_on_hide(dm_page_t *page)
{
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
}

static void wheel_speed_page_on_update(dm_page_t *page)
{
    wheel_speed_page_data_t *data = (wheel_speed_page_data_t *)page->user_data;
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

    // Vehicle speed (diagnostic from OBD-II PID 0x0D)
    if (snap.diag_vehicle_speed_valid) {
        snprintf(buf, sizeof(buf), "%.0f kph", snap.diag_vehicle_speed_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->diag_vehicle_speed_value, buf);

    // Vehicle speed (broadcast from 0x0B4)
    if (snap.bcast_vehicle_speed_valid) {
        snprintf(buf, sizeof(buf), "%.1f kph", snap.bcast_vehicle_speed_kph);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->bcast_vehicle_speed_value, buf);

    update_page_counter(data->page_counter, data->page_index);
}

dm_page_t *wheel_speed_page_create(void)
{
    return page_create(
        "Wheel Speed",
        wheel_speed_page_on_create,
        wheel_speed_page_on_destroy,
        wheel_speed_page_on_show,
        wheel_speed_page_on_hide,
        wheel_speed_page_on_update
    );
}
