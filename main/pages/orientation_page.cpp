/*
 * Orientation Page Implementation
 *
 * Displays vehicle orientation data from ABS module (0x7B0):
 * - PID 0x47: Lateral G, Longitudinal G, Yaw Rate, Steering Angle
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
    lv_obj_t *cand_1d0_value;
    lv_obj_t *cand_2c1_value;
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

    data->page_index = 5;

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

    // Live orientation data (from 0x7B0 PID 0x47)
    lv_obj_t *card = create_metric_card(grid, "Lat G", &data->lateral_g_value);
    lv_obj_set_size(card, LV_PCT(48), 120);

    card = create_metric_card(grid, "Long G", &data->longitudinal_g_value);
    lv_obj_set_size(card, LV_PCT(48), 120);

    card = create_metric_card(grid, "Yaw Rate", &data->yaw_rate_value);
    lv_obj_set_size(card, LV_PCT(48), 120);

    card = create_metric_card(grid, "Steer Ang", &data->steering_angle_value);
    lv_obj_set_size(card, LV_PCT(48), 120);

    lv_obj_t *cand_row = lv_obj_create(page->container);
    lv_obj_set_width(cand_row, LV_PCT(100));
    lv_obj_set_height(cand_row, 48);
    lv_obj_set_style_bg_opa(cand_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cand_row, 0, 0);
    lv_obj_set_style_pad_all(cand_row, 0, 0);
    lv_obj_set_style_pad_row(cand_row, 4, 0);
    lv_obj_set_flex_flow(cand_row, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(cand_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(cand_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cand_row, LV_OBJ_FLAG_GESTURE_BUBBLE);

    data->cand_1d0_value = lv_label_create(cand_row);
    lv_label_set_text(data->cand_1d0_value, "1D0: --");
    lv_obj_set_style_text_font(data->cand_1d0_value, k_label_font, 0);
    lv_obj_set_style_text_color(data->cand_1d0_value, k_muted_text_color, 0);
    lv_obj_set_width(data->cand_1d0_value, LV_PCT(100));
    lv_label_set_long_mode(data->cand_1d0_value, LV_LABEL_LONG_CLIP);
    lv_obj_add_flag(data->cand_1d0_value, LV_OBJ_FLAG_GESTURE_BUBBLE);

    data->cand_2c1_value = lv_label_create(cand_row);
    lv_label_set_text(data->cand_2c1_value, "2C1: --");
    lv_obj_set_style_text_font(data->cand_2c1_value, k_label_font, 0);
    lv_obj_set_style_text_color(data->cand_2c1_value, k_muted_text_color, 0);
    lv_obj_set_width(data->cand_2c1_value, LV_PCT(100));
    lv_label_set_long_mode(data->cand_2c1_value, LV_LABEL_LONG_CLIP);
    lv_obj_add_flag(data->cand_2c1_value, LV_OBJ_FLAG_GESTURE_BUBBLE);

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

    if (snap.cand_1d0_valid) {
        snprintf(buf, sizeof(buf), "1D0: %02X %02X %02X %02X %02X %02X %02X %02X",
                 snap.cand_1d0_raw[0], snap.cand_1d0_raw[1], snap.cand_1d0_raw[2],
                 snap.cand_1d0_raw[3], snap.cand_1d0_raw[4], snap.cand_1d0_raw[5],
                 snap.cand_1d0_raw[6], snap.cand_1d0_raw[7]);
    } else {
        snprintf(buf, sizeof(buf), "1D0: --");
    }
    lv_label_set_text(data->cand_1d0_value, buf);

    if (snap.cand_2c1_valid) {
        snprintf(buf, sizeof(buf), "2C1: %02X %02X %02X %02X %02X %02X %02X %02X",
                 snap.cand_2c1_raw[0], snap.cand_2c1_raw[1], snap.cand_2c1_raw[2],
                 snap.cand_2c1_raw[3], snap.cand_2c1_raw[4], snap.cand_2c1_raw[5],
                 snap.cand_2c1_raw[6], snap.cand_2c1_raw[7]);
    } else {
        snprintf(buf, sizeof(buf), "2C1: --");
    }
    lv_label_set_text(data->cand_2c1_value, buf);

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
