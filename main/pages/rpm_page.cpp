/*
 * RPM Page Implementation
 */

#include "rpm_page.h"

#include <stdio.h>
#include <stdlib.h>

#include <esp_log.h>

#include "lvgl.h"

#include "app_state.h"
#include "page_utils.h"

static const char *TAG = "rpm_page";

typedef struct {
    int page_index;
    lv_obj_t *diag_rpm_value;
    lv_obj_t *bcast_rpm_value;
    lv_obj_t *page_counter;
    lv_obj_t *error_label;
    lv_obj_t *can_toggle_label;
} rpm_page_data_t;

static void rpm_page_on_create(dm_page_t *page, lv_obj_t *parent)
{

    rpm_page_data_t *data = (rpm_page_data_t *)calloc(1, sizeof(rpm_page_data_t));
    if (!data) {
        ESP_LOGE(TAG, "on_create: calloc failed");
        return;
    }

    data->page_index = 4;
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
    lv_label_set_text(title_label, "RPM");
    lv_obj_set_style_text_font(title_label, k_title_font, 0);
    lv_obj_set_style_text_color(title_label, k_text_color, 0);
    lv_obj_add_flag(title_label, LV_OBJ_FLAG_GESTURE_BUBBLE);

    data->page_counter = lv_label_create(header);
    lv_label_set_text(data->page_counter, "6/6");
    lv_obj_set_style_text_font(data->page_counter, k_label_font, 0);
    lv_obj_set_style_text_color(data->page_counter, k_muted_text_color, 0);
    lv_obj_add_flag(data->page_counter, LV_OBJ_FLAG_GESTURE_BUBBLE);

    lv_obj_t *grid = create_metrics_grid(page->container);

    lv_obj_t *card = create_metric_card(grid, "Diag RPM", &data->diag_rpm_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    card = create_metric_card(grid, "Bcast RPM (1C4)", &data->bcast_rpm_value);
    lv_obj_set_size(card, LV_PCT(48), 110);

    create_nav_bar(page->container, &data->can_toggle_label);
    g_rpm_can_toggle_label = data->can_toggle_label;

    page->is_created = true;
}

static void rpm_page_on_destroy(dm_page_t *page)
{
    if (page->user_data) {
        free(page->user_data);
    }
}

static void rpm_page_on_show(dm_page_t *page)
{
    rpm_page_data_t *data = (rpm_page_data_t *)page->user_data;
    if (!data) {
        return;
    }

    app_state_set_active_page(data->page_index);
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    update_page_counter(data->page_counter, data->page_index);
}

static void rpm_page_on_hide(dm_page_t *page)
{
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
}

static void rpm_page_on_update(dm_page_t *page)
{
    rpm_page_data_t *data = (rpm_page_data_t *)page->user_data;
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
    lv_label_set_text(data->diag_rpm_value, buf);

    if (snap.bcast_rpm_1c4_valid) {
        snprintf(buf, sizeof(buf), "%.0f", snap.bcast_rpm_1c4);
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    lv_label_set_text(data->bcast_rpm_value, buf);

    update_page_counter(data->page_counter, data->page_index);
}

dm_page_t *rpm_page_create(void)
{
    return page_create(
        "RPM",
        rpm_page_on_create,
        rpm_page_on_destroy,
        rpm_page_on_show,
        rpm_page_on_hide,
        rpm_page_on_update
    );
}
