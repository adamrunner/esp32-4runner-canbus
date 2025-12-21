/*
 * 4Runner CAN Bus Display
 *
 * Polls OBD-II and Toyota-specific PIDs over CAN and displays metrics
 * on the Waveshare ESP32-S3 4.3-inch Touch LCD using LVGL.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_err.h>
#include <esp_log.h>

#include "display_manager.h"
#include "display_manager/page.h"
#include "lvgl.h"

static const char *TAG = "4RUNNER_CAN";

#define TX_GPIO_NUM GPIO_NUM_15
#define RX_GPIO_NUM GPIO_NUM_16

#define OBD_REQUEST_ID 0x7E0
#define OBD_RESPONSE_ID 0x7E8

#define OBD_POLL_INTERVAL_MS 150

static const int lcd_h_res = 800;
static const int lcd_v_res = 480;
static const int lcd_pixel_clock_hz = 16 * 1000 * 1000;
static const int lcd_hsync_pulse_width = 4;
static const int lcd_hsync_back_porch = 8;
static const int lcd_hsync_front_porch = 8;
static const int lcd_vsync_pulse_width = 4;
static const int lcd_vsync_back_porch = 8;
static const int lcd_vsync_front_porch = 8;
static const int lcd_data_width = 16;
static const int lcd_bits_per_pixel = 16;
static const int lcd_num_fbs = 2;
static const int lcd_bounce_buffer_size_px = 0;
static const bool lcd_fb_in_psram = true;
static const int lcd_hsync_io_num = GPIO_NUM_46;
static const int lcd_vsync_io_num = GPIO_NUM_3;
static const int lcd_de_io_num = GPIO_NUM_5;
static const int lcd_pclk_io_num = GPIO_NUM_7;
static const int lcd_disp_io_num = -1;
static const int lcd_data_io_nums[16] = {
    GPIO_NUM_14,
    GPIO_NUM_38,
    GPIO_NUM_18,
    GPIO_NUM_17,
    GPIO_NUM_10,
    GPIO_NUM_39,
    GPIO_NUM_0,
    GPIO_NUM_45,
    GPIO_NUM_48,
    GPIO_NUM_47,
    GPIO_NUM_21,
    GPIO_NUM_1,
    GPIO_NUM_2,
    GPIO_NUM_42,
    GPIO_NUM_41,
    GPIO_NUM_40,
};
static const int lcd_i2c_port = 0;
static const int lcd_i2c_sda_io_num = 8;
static const int lcd_i2c_scl_io_num = 9;
static const int lcd_i2c_freq_hz = 400000;
static const int lcd_touch_reset_io_num = 4;
static const int lcd_touch_int_io_num = -1;

typedef struct {
    float rpm;
    float vbatt_v;
    float iat_c;
    float baro_kpa;
    float atf_pan_c;
    uint32_t odo_km;
    int gear;
    bool tqc_lockup;
    bool rpm_valid;
    bool vbatt_valid;
    bool iat_valid;
    bool baro_valid;
    bool atf_valid;
    bool odo_valid;
    bool gear_valid;
} can_metrics_t;

static SemaphoreHandle_t s_metrics_mutex = NULL;
static can_metrics_t s_metrics = {};

typedef struct {
    uint8_t service;
    uint8_t pid;
} obd_request_t;

static const obd_request_t k_request_sequence[] = {
    {0x01, 0x0C},
    {0x01, 0x42},
    {0x01, 0x0F},
    {0x01, 0x33},
    {0x21, 0x82},
    {0x21, 0x85},
    {0x21, 0x28},
};

static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
static const twai_general_config_t g_config = {
    .controller_id = 0,
    .mode = TWAI_MODE_NORMAL,
    .tx_io = TX_GPIO_NUM,
    .rx_io = RX_GPIO_NUM,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 5,
    .rx_queue_len = 20,
    .alerts_enabled = TWAI_ALERT_NONE,
    .clkout_divider = 0,
    .intr_flags = 0,
    .general_flags = {
        .sleep_allow_pd = 0,
    },
};

static display_manager_handle_t s_display = NULL;
static int s_page_count = 0;
static int s_active_page = 0;

static const lv_color_t k_bg_color = lv_color_hex(0x0f1115);
static const lv_color_t k_text_color = lv_color_hex(0xe6e6e6);

static void apply_dark_theme(lv_obj_t *container)
{
    if (!container) {
        return;
    }

    lv_obj_set_style_bg_color(container, k_bg_color, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(container, k_text_color, 0);
}

static void page_tap_event_cb(lv_event_t *e)
{
    (void)e;

    if (!s_display || s_page_count <= 1) {
        return;
    }

    s_active_page = (s_active_page + 1) % s_page_count;
    display_manager_switch_to_page(s_display, s_active_page);
}

static void metrics_get_snapshot(can_metrics_t *out)
{
    if (!out) {
        return;
    }

    xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);
    memcpy(out, &s_metrics, sizeof(s_metrics));
    xSemaphoreGive(s_metrics_mutex);
}

static twai_message_t build_obd_request(uint8_t service, uint8_t pid)
{
    twai_message_t msg = {};

    msg.identifier = OBD_REQUEST_ID;
    msg.data_length_code = 8;
    msg.data[0] = (service == 0x21) ? 0x03 : 0x02;
    msg.data[1] = service;
    msg.data[2] = pid;

    return msg;
}

static void handle_standard_response(const twai_message_t *msg)
{
    uint8_t length = msg->data[0];
    uint8_t pid = msg->data[2];

    xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);

    switch (pid) {
        case 0x0C: {
            if (length >= 4) {
                uint16_t raw = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                s_metrics.rpm = raw / 4.0f;
                s_metrics.rpm_valid = true;
            }
            break;
        }
        case 0x42: {
            if (length >= 4) {
                uint16_t raw = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                s_metrics.vbatt_v = raw / 1000.0f;
                s_metrics.vbatt_valid = true;
            }
            break;
        }
        case 0x0F: {
            if (length >= 3) {
                s_metrics.iat_c = (float)msg->data[3] - 40.0f;
                s_metrics.iat_valid = true;
            }
            break;
        }
        case 0x33: {
            if (length >= 3) {
                s_metrics.baro_kpa = (float)msg->data[3];
                s_metrics.baro_valid = true;
            }
            break;
        }
        default:
            break;
    }

    xSemaphoreGive(s_metrics_mutex);
}

static void handle_extended_response(const twai_message_t *msg)
{
    uint8_t length = msg->data[0];
    uint8_t pid = msg->data[2];

    xSemaphoreTake(s_metrics_mutex, portMAX_DELAY);

    switch (pid) {
        case 0x82: {
            if (length >= 6) {
                uint16_t raw = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                s_metrics.atf_pan_c = (raw / 256.0f) - 40.0f;
                s_metrics.atf_valid = true;
            }
            break;
        }
        case 0x85: {
            if (length >= 5) {
                s_metrics.gear = msg->data[3];
                s_metrics.tqc_lockup = (msg->data[4] & 0x80) != 0;
                s_metrics.gear_valid = true;
            }
            break;
        }
        case 0x28: {
            if (length >= 5) {
                s_metrics.odo_km = ((uint32_t)msg->data[3] << 16)
                    | ((uint32_t)msg->data[4] << 8)
                    | (uint32_t)msg->data[5];
                s_metrics.odo_valid = true;
            }
            break;
        }
        default:
            break;
    }

    xSemaphoreGive(s_metrics_mutex);
}

static void process_obd_response(const twai_message_t *msg)
{
    if (!msg || msg->identifier != OBD_RESPONSE_ID) {
        return;
    }

    if (msg->data_length_code < 3) {
        return;
    }

    uint8_t length = msg->data[0];
    if (length < 2) {
        return;
    }

    uint8_t service = msg->data[1];

    if (service == 0x41) {
        handle_standard_response(msg);
    } else if (service == 0x61) {
        handle_extended_response(msg);
    }
}

static void can_rx_task(void *arg)
{
    (void)arg;
    twai_message_t rx_msg = {};

    ESP_LOGI(TAG, "CAN RX task started");

    while (1) {
        if (twai_receive(&rx_msg, pdMS_TO_TICKS(100)) == ESP_OK) {
            process_obd_response(&rx_msg);
        }
    }
}

static void can_tx_task(void *arg)
{
    (void)arg;
    size_t request_index = 0;

    ESP_LOGI(TAG, "CAN TX task started");

    while (1) {
        const obd_request_t *req = &k_request_sequence[request_index];
        twai_message_t msg = build_obd_request(req->service, req->pid);
        esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "OBD request 0x%02X 0x%02X failed: %s",
                     req->service, req->pid, esp_err_to_name(err));
        }

        request_index = (request_index + 1) % (sizeof(k_request_sequence) / sizeof(k_request_sequence[0]));
        vTaskDelay(pdMS_TO_TICKS(OBD_POLL_INTERVAL_MS));
    }
}

typedef struct {
    lv_obj_t *rpm_label;
    lv_obj_t *vbatt_label;
    lv_obj_t *iat_label;
    lv_obj_t *baro_label;
} diag_page_data_t;

static void diag_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    diag_page_data_t *data = (diag_page_data_t *)calloc(1, sizeof(diag_page_data_t));
    if (!data) {
        return;
    }

    page->user_data = data;
    page->container = lv_obj_create(parent);
    lv_obj_set_size(page->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(page->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(page->container, 6, 0);
    lv_obj_set_style_pad_row(page->container, 8, 0);
    lv_obj_set_style_border_width(page->container, 0, 0);
    apply_dark_theme(page->container);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page->container, page_tap_event_cb, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(page->container);
    lv_label_set_text(title, "Diagnostics");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, k_text_color, 0);

    data->rpm_label = lv_label_create(page->container);
    data->vbatt_label = lv_label_create(page->container);
    data->iat_label = lv_label_create(page->container);
    data->baro_label = lv_label_create(page->container);
    lv_obj_set_style_text_color(data->rpm_label, k_text_color, 0);
    lv_obj_set_style_text_color(data->vbatt_label, k_text_color, 0);
    lv_obj_set_style_text_color(data->iat_label, k_text_color, 0);
    lv_obj_set_style_text_color(data->baro_label, k_text_color, 0);

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
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
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
        snprintf(buf, sizeof(buf), "RPM: %.0f", snap.rpm);
    } else {
        snprintf(buf, sizeof(buf), "RPM: --");
    }
    lv_label_set_text(data->rpm_label, buf);

    if (snap.vbatt_valid) {
        snprintf(buf, sizeof(buf), "Batt: %.2f V", snap.vbatt_v);
    } else {
        snprintf(buf, sizeof(buf), "Batt: --");
    }
    lv_label_set_text(data->vbatt_label, buf);

    if (snap.iat_valid) {
        snprintf(buf, sizeof(buf), "IAT: %.1f C", snap.iat_c);
    } else {
        snprintf(buf, sizeof(buf), "IAT: --");
    }
    lv_label_set_text(data->iat_label, buf);

    if (snap.baro_valid) {
        snprintf(buf, sizeof(buf), "Baro: %.0f kPa", snap.baro_kpa);
    } else {
        snprintf(buf, sizeof(buf), "Baro: --");
    }
    lv_label_set_text(data->baro_label, buf);
}

typedef struct {
    lv_obj_t *atf_label;
    lv_obj_t *gear_label;
    lv_obj_t *odo_label;
} fourrunner_page_data_t;

static void fourrunner_page_on_create(dm_page_t *page, lv_obj_t *parent)
{
    fourrunner_page_data_t *data = (fourrunner_page_data_t *)calloc(1, sizeof(fourrunner_page_data_t));
    if (!data) {
        return;
    }

    page->user_data = data;
    page->container = lv_obj_create(parent);
    lv_obj_set_size(page->container, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(page->container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(page->container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(page->container, 6, 0);
    lv_obj_set_style_pad_row(page->container, 8, 0);
    lv_obj_set_style_border_width(page->container, 0, 0);
    apply_dark_theme(page->container);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(page->container, page_tap_event_cb, LV_EVENT_SHORT_CLICKED, NULL);

    lv_obj_t *title = lv_label_create(page->container);
    lv_label_set_text(title, "4Runner Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, k_text_color, 0);

    data->atf_label = lv_label_create(page->container);
    data->gear_label = lv_label_create(page->container);
    data->odo_label = lv_label_create(page->container);
    lv_obj_set_style_text_color(data->atf_label, k_text_color, 0);
    lv_obj_set_style_text_color(data->gear_label, k_text_color, 0);
    lv_obj_set_style_text_color(data->odo_label, k_text_color, 0);

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
    lv_obj_clear_flag(page->container, LV_OBJ_FLAG_HIDDEN);
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
        snprintf(buf, sizeof(buf), "ATF Pan: %.1f C", snap.atf_pan_c);
    } else {
        snprintf(buf, sizeof(buf), "ATF Pan: --");
    }
    lv_label_set_text(data->atf_label, buf);

    if (snap.gear_valid) {
        snprintf(buf, sizeof(buf), "Gear: %d", snap.gear);
    } else {
        snprintf(buf, sizeof(buf), "Gear: --");
    }
    lv_label_set_text(data->gear_label, buf);

    if (snap.odo_valid) {
        snprintf(buf, sizeof(buf), "ODO: %lu km", (unsigned long)snap.odo_km);
    } else {
        snprintf(buf, sizeof(buf), "ODO: --");
    }
    lv_label_set_text(data->odo_label, buf);
}

static dm_page_t *diag_page_create(void)
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

static dm_page_t *fourrunner_page_create(void)
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

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "4Runner CAN Bus Display starting");
    ESP_LOGI(TAG, "TX GPIO: %d, RX GPIO: %d", TX_GPIO_NUM, RX_GPIO_NUM);

    s_metrics_mutex = xSemaphoreCreateMutex();
    if (!s_metrics_mutex) {
        ESP_LOGE(TAG, "Failed to create metrics mutex");
        return;
    }

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "TWAI driver installed");

    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "TWAI driver started");

    display_config_t display_config = {
        .h_res = lcd_h_res,
        .v_res = lcd_v_res,
        .pixel_clock_hz = lcd_pixel_clock_hz,
        .hsync_pulse_width = lcd_hsync_pulse_width,
        .hsync_back_porch = lcd_hsync_back_porch,
        .hsync_front_porch = lcd_hsync_front_porch,
        .vsync_pulse_width = lcd_vsync_pulse_width,
        .vsync_back_porch = lcd_vsync_back_porch,
        .vsync_front_porch = lcd_vsync_front_porch,
        .data_width = lcd_data_width,
        .bits_per_pixel = lcd_bits_per_pixel,
        .num_fbs = lcd_num_fbs,
        .bounce_buffer_size_px = lcd_bounce_buffer_size_px,
        .fb_in_psram = lcd_fb_in_psram,
        .hsync_io_num = lcd_hsync_io_num,
        .vsync_io_num = lcd_vsync_io_num,
        .de_io_num = lcd_de_io_num,
        .pclk_io_num = lcd_pclk_io_num,
        .disp_io_num = lcd_disp_io_num,
        .data_io_nums = {0},
        .i2c_port = lcd_i2c_port,
        .i2c_sda_io_num = lcd_i2c_sda_io_num,
        .i2c_scl_io_num = lcd_i2c_scl_io_num,
        .i2c_freq_hz = lcd_i2c_freq_hz,
        .touch_reset_io_num = lcd_touch_reset_io_num,
        .touch_int_io_num = lcd_touch_int_io_num,
        .touch_enabled = true,
        .draw_buf_lines = 40,
        .tick_period_ms = 2,
        .orientation = DISPLAY_ORIENTATION_LANDSCAPE,
        .x_offset = 0,
        .y_offset = 0
    };
    memcpy(display_config.data_io_nums, lcd_data_io_nums, sizeof(lcd_data_io_nums));

    s_display = display_manager_init(&display_config);
    if (!s_display) {
        ESP_LOGE(TAG, "Failed to initialize display manager");
        return;
    }

    dm_page_t *diag_page = diag_page_create();
    dm_page_t *fourrunner_page = fourrunner_page_create();

    if (diag_page) {
        display_manager_add_page(s_display, diag_page);
        s_page_count++;
    }

    if (fourrunner_page) {
        display_manager_add_page(s_display, fourrunner_page);
        s_page_count++;
    }

    if (s_page_count > 0) {
        s_active_page = 0;
        display_manager_switch_to_page(s_display, s_active_page);
    }

    xTaskCreatePinnedToCore(can_rx_task, "CAN_RX", 4096, NULL, 5, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(can_tx_task, "CAN_TX", 4096, NULL, 4, NULL, tskNO_AFFINITY);
}
