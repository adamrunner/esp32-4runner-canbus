/*
 * Orientation Page Implementation
 *
 * Displays vehicle orientation data from ABS module (0x7B0):
 * - PID 0x47: Lateral G, Longitudinal G, Yaw Rate, Steering Angle
 * - PID 0x46: Zero points for deceleration and yaw rate
 */

#include "orientation_page.h"

#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"

#include "app_state.h"
#include "page_utils.h"

typedef struct {
    int page_index;
    // Live orientation data (from 0x7B0 PID 0x47)
    lv_obj_t *lateral_g_value;
    lv_obj_t *longitudinal_g_value;
    lv_obj_t *yaw_rate_value;
    lv_obj_t *steering_angle_value;
    // Zero point data (from 0x7B0 PID 0x46)
    lv_obj_t *zp_decel_1_value;
    lv_obj_t *zp_decel_2_value;
    lv_obj_t *zp_yaw_value;
    lv_obj_t *page_counter;
    lv_obj_t *error_label;
    lv_obj_t *can_toggle_label;
} orientation_page_data_t;

static void orientation_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    orientation_page_data_t *data = (orientation_page_data_t *)calloc(1, sizeof(orientation_page_data_t));
    if (!data) {
        return;
    }

    data->page_index = 6;

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

    create_header_block(page->container, "Orientation", "G-Force, Yaw & Steering",
                        &data->page_counter, &data->error_label);
    g_orientation_error_label = data->error_label;

    lv_obj_t *grid = create_metrics_grid(page->container);

    // Row 1: Live orientation data (from 0x7B0 PID 0x47)
    lv_obj_t *card = create_metric_card(grid, "Lat G", &data->lateral_g_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Long G", &data->longitudinal_g_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Yaw Rate", &data->yaw_rate_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    card = create_metric_card(grid, "Steer Ang", &data->steering_angle_value);
    lv_obj_set_size(card, LV_PCT(23), 100);

    // Row 2: Zero point data (from 0x7B0 PID 0x46)
    card = create_metric_card(grid, "ZP Dec 1", &data->zp_decel_1_value);
    lv_obj_set_size(card, LV_PCT(30), 100);

    card = create_metric_card(grid, "ZP Dec 2", &data->zp_decel_2_value);
    lv_obj_set_size(card, LV_PCT(30), 100);

    card = create_metric_card(grid, "ZP Yaw", &data->zp_yaw_value);
    lv_obj_set_size(card, LV_PCT(30), 100);

    create_nav_bar(page->container, &data->can_toggle_label);
    g_orientation_can_toggle_label = data->can_toggle_label;

    page->is_created = true;
}

static void orientation_page_on_destroy(dm_page_t *page)
{
    if (page->user_data) {
        free(page->user_data);
    }
}

static void orientation_page_on_show(dm_page_t *page)
{
    orientation_page_data_t *data = (orientation_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    app_state_set_active_page(data->page_index);
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);
}

static void orientation_page_on_hide(dm_page_t *page)
{
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
}

static void orientation_page_on_update(dm_page_t *page)
{
    orientation_page_data_t *data = (orientation_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    can_metrics_t snap = {};
    metrics_get_snapshot(&snap);

    char buf[48];

    // Live orientation data (from 0x7B0 PID 0x47)
    if (snap.orientation_valid) {
        snprintf(buf, sizeof(buf), "%.2f g", snap.lateral_g);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->lateral_g_value, buf);

    if (snap.orientation_valid) {
        snprintf(buf, sizeof(buf), "%.2f g", snap.longitudinal_g);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->longitudinal_g_value, buf);

    if (snap.orientation_valid) {
        snprintf(buf, sizeof(buf), "%.1f d/s", snap.yaw_rate_deg_sec);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->yaw_rate_value, buf);

    if (snap.orientation_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.steering_angle_deg);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->steering_angle_value, buf);

    // Zero point data (from 0x7B0 PID 0x46)
    if (snap.orientation_zp_valid) {
        snprintf(buf, sizeof(buf), "%.2f", snap.zp_decel_1);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->zp_decel_1_value, buf);

    if (snap.orientation_zp_valid) {
        snprintf(buf, sizeof(buf), "%.2f", snap.zp_decel_2);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->zp_decel_2_value, buf);

    if (snap.orientation_zp_valid) {
        snprintf(buf, sizeof(buf), "%.1f", snap.zp_yaw_rate);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->zp_yaw_value, buf);

    update_page_counter(data->page_counter, data->page_index);
}

dm_page_t *orientation_page_create(void)
{
    return page_create(
        "Orientation",
        orientation_page_on_create,
        orientation_page_on_destroy,
        orientation_page_on_show,
        orientation_page_on_hide,
        orientation_page_on_update
    );
}
