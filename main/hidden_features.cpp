#include <stddef.h>
#include <stdint.h>

#include <esp_log.h>

#include "app_state.h"
#include "display_manager.h"
#include "display_manager/page.h"
#include "hidden_features.h"
#include "rtc_page.h"

static const char *TAG = "HIDDEN";

static constexpr uint16_t k_hold_time_ms = 2000;
static constexpr int32_t k_title_touch_margin_px = 8;
static constexpr int k_page_not_added = -1;

typedef struct {
    dm_page_t *(*create)(void);
    int page_index;
} hidden_page_t;

// Add new hidden pages here. The first entry is the page shown when the mode is enabled.
static hidden_page_t s_hidden_pages[] = {
    {rtc_page_create, k_page_not_added},
};
static hidden_page_t *const s_rtc_page = &s_hidden_pages[0];

static int add_hidden_page(hidden_page_t *hidden_page)
{
    if (hidden_page->page_index != k_page_not_added) {
        return hidden_page->page_index;
    }

    display_manager_handle_t display = app_state_get_display();
    dm_page_t *page = hidden_page->create();
    if (!display || !page) {
        ESP_LOGE(TAG, "Failed to create hidden page");
        return k_page_not_added;
    }

    int page_index = display_manager_get_page_count(display);
    display_manager_add_page(display, page);
    if (display_manager_get_page_count(display) != page_index + 1) {
        ESP_LOGE(TAG, "Failed to add hidden page");
        return k_page_not_added;
    }

    hidden_page->page_index = page_index;
    app_state_set_page_count(page_index + 1);
    return page_index;
}

bool hidden_features_add_rtc_page(void)
{
    return add_hidden_page(s_rtc_page) != k_page_not_added;
}

void hidden_features_enable(void)
{
    int first_page = k_page_not_added;
    for (hidden_page_t &hidden_page : s_hidden_pages) {
        int page_index = add_hidden_page(&hidden_page);
        if (first_page == k_page_not_added) {
            first_page = page_index;
        }
    }

    if (first_page == k_page_not_added) {
        return;
    }

    ESP_LOGI(TAG, "Hidden features enabled until reboot");
    app_state_set_active_page(first_page);
    display_manager_switch_to_page(app_state_get_display(), first_page);
}

static void title_hold_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_t *indev = lv_indev_get_act();
        if (indev) {
            lv_indev_set_long_press_time(indev, k_hold_time_ms);
        }
    } else if (code == LV_EVENT_LONG_PRESSED) {
        hidden_features_enable();
    }
}

void hidden_features_attach_title(lv_obj_t *title_label)
{
    lv_obj_add_flag(title_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(title_label, k_title_touch_margin_px);
    lv_obj_add_event_cb(title_label, title_hold_event_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(title_label, title_hold_event_cb, LV_EVENT_LONG_PRESSED, NULL);
}
