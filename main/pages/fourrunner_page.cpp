/*
 * 4Runner Data Page Implementation
 */

#include "fourrunner_page.h"

#include <stdio.h>
#include <stdlib.h>

#include "lvgl.h"

#include "app_state.h"
#include "page_utils.h"

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
    g_fourrunner_error_label = data->error_label;

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
    g_fourrunner_can_toggle_label = data->can_toggle_label;

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

    app_state_set_active_page(data->page_index);
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

dm_page_t *fourrunner_page_create(void)
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
