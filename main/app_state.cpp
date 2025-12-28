/*
 * Application State Implementation
 */

#include "app_state.h"

#include <string.h>

#include <driver/twai.h>
#include <esp_log.h>
#include <esp_timer.h>

#include "lvgl.h"

static const char *TAG = "APP_STATE";

// Error thresholds
static const int k_can_error_fail_threshold = 5;
static const int64_t k_can_error_stale_ms = 2000;

// State variables
static SemaphoreHandle_t s_metrics_mutex = NULL;
static can_metrics_t s_metrics = {};
static SemaphoreHandle_t s_can_state_mutex = NULL;
static can_state_t s_can_state = {};

static display_manager_handle_t s_display = NULL;
static int s_page_count = 0;
static int s_active_page = 0;

// Global error label pointers
lv_obj_t *g_diag_error_label = NULL;
lv_obj_t *g_fourrunner_error_label = NULL;
lv_obj_t *g_tire_error_label = NULL;
lv_obj_t *g_rpm_error_label = NULL;

// Global CAN toggle label pointers
lv_obj_t *g_diag_can_toggle_label = NULL;
lv_obj_t *g_fourrunner_can_toggle_label = NULL;
lv_obj_t *g_tire_can_toggle_label = NULL;
lv_obj_t *g_rpm_can_toggle_label = NULL;

bool app_state_init(void)
{
    s_metrics_mutex = xSemaphoreCreateMutex();
    if (!s_metrics_mutex) {
        ESP_LOGE(TAG, "Failed to create metrics mutex");
        return false;
    }

    s_can_state_mutex = xSemaphoreCreateMutex();
    if (!s_can_state_mutex) {
        ESP_LOGE(TAG, "Failed to create CAN state mutex");
        vSemaphoreDelete(s_metrics_mutex);
        s_metrics_mutex = NULL;
        return false;
    }

    memset(&s_metrics, 0, sizeof(s_metrics));
    memset(&s_can_state, 0, sizeof(s_can_state));

    return true;
}

int64_t get_time_ms(void)
{
    return esp_timer_get_time() / 1000;
}

void metrics_get_snapshot(can_metrics_t *out)
{
    if (!out || !s_metrics_mutex) {
        return;
    }

    xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);
    memcpy(out, &s_metrics, sizeof(s_metrics));
    xSemaphoreGive(s_metrics_mutex);
}

can_metrics_t *metrics_get_for_update(void)
{
    return &s_metrics;
}

void metrics_lock(void)
{
    if (s_metrics_mutex) {
        xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);
    }
}

void metrics_unlock(void)
{
    if (s_metrics_mutex) {
        xSemaphoreGive(s_metrics_mutex);
    }
}

bool can_state_is_paused(void)
{
    bool paused = false;
    if (s_can_state_mutex &&
        xSemaphoreTake(s_can_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        paused = s_can_state.paused;
        xSemaphoreGive(s_can_state_mutex);
    }
    return paused;
}

void can_state_get_snapshot(can_state_t *out)
{
    if (!out || !s_can_state_mutex) {
        return;
    }

    if (xSemaphoreTake(s_can_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        memcpy(out, &s_can_state, sizeof(s_can_state));
        xSemaphoreGive(s_can_state_mutex);
    }
}

static void can_ui_update_cb(void *arg)
{
    (void)arg;
    can_state_t snapshot = {};
    can_state_get_snapshot(&snapshot);

    const char *indicator = "";
    if (snapshot.paused) {
        indicator = "CAN PAUSED";
    } else if (snapshot.error_active) {
        indicator = "CAN ERROR";
    }

    if (g_diag_error_label) {
        lv_label_set_text(g_diag_error_label, indicator);
    }
    if (g_fourrunner_error_label) {
        lv_label_set_text(g_fourrunner_error_label, indicator);
    }
    if (g_tire_error_label) {
        lv_label_set_text(g_tire_error_label, indicator);
    }
    if (g_rpm_error_label) {
        lv_label_set_text(g_rpm_error_label, indicator);
    }

    const char *toggle_text = snapshot.paused ? "Resume CAN" : "Pause CAN";
    if (g_diag_can_toggle_label) {
        lv_label_set_text(g_diag_can_toggle_label, toggle_text);
    }
    if (g_fourrunner_can_toggle_label) {
        lv_label_set_text(g_fourrunner_can_toggle_label, toggle_text);
    }
    if (g_tire_can_toggle_label) {
        lv_label_set_text(g_tire_can_toggle_label, toggle_text);
    }
    if (g_rpm_can_toggle_label) {
        lv_label_set_text(g_rpm_can_toggle_label, toggle_text);
    }
}

void schedule_can_ui_update(void)
{
    lv_async_call(can_ui_update_cb, NULL);
}

void update_can_error_state(bool rx_ok, bool tx_failed)
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

void set_can_paused(bool paused)
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

void app_state_set_can_paused_internal(bool paused)
{
    if (!s_can_state_mutex) {
        return;
    }

    if (xSemaphoreTake(s_can_state_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_can_state.paused = paused;
        s_can_state.error_active = false;
        s_can_state.fail_count = 0;
        s_can_state.last_rx_ms = get_time_ms();
        xSemaphoreGive(s_can_state_mutex);
    }
}

void app_state_set_display(display_manager_handle_t display)
{
    s_display = display;
}

display_manager_handle_t app_state_get_display(void)
{
    return s_display;
}

int app_state_get_page_count(void)
{
    return s_page_count;
}

void app_state_set_page_count(int count)
{
    s_page_count = count;
}

int app_state_get_active_page(void)
{
    return s_active_page;
}

void app_state_set_active_page(int page)
{
    s_active_page = page;
}

void switch_page_by_offset(int offset)
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
