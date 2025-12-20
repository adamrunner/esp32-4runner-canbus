/*
 * 4Runner CAN Bus Display
 *
 * Polls OBD-II and Toyota-specific PIDs over CAN and displays metrics
 * on the Waveshare ESP32-S3-LCD-1.9 panel using LVGL.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_err.h>
#include <esp_log.h>

#include "button_bsp.h"
#include "display_manager.h"
#include "display_manager/page.h"
#include "lvgl.h"

static const char *TAG = "4RUNNER_CAN";

#define TX_GPIO_NUM GPIO_NUM_17
#define RX_GPIO_NUM GPIO_NUM_16

#define OBD_REQUEST_ID 0x7E0
#define OBD_RESPONSE_ID 0x7E8

#define BUTTON_EVENT_NEXT (1 << 0)
#define BUTTON_EVENT_ALT (1 << 1)

#define OBD_POLL_INTERVAL_MS 150

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
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(page->container);
    lv_label_set_text(title, "Diagnostics");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    data->rpm_label = lv_label_create(page->container);
    data->vbatt_label = lv_label_create(page->container);
    data->iat_label = lv_label_create(page->container);
    data->baro_label = lv_label_create(page->container);

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
    lv_obj_add_flag(page->container, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(page->container);
    lv_label_set_text(title, "4Runner Data");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    data->atf_label = lv_label_create(page->container);
    data->gear_label = lv_label_create(page->container);
    data->odo_label = lv_label_create(page->container);

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

static void ui_button_task(void *arg)
{
    (void)arg;
    int page_index = 0;

    if (s_page_count == 0) {
        ESP_LOGW(TAG, "No pages registered, button task idle");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Button task started");

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(
            key_groups,
            BUTTON_EVENT_NEXT | BUTTON_EVENT_ALT,
            pdTRUE,
            pdFALSE,
            portMAX_DELAY
        );

        if (bits & (BUTTON_EVENT_NEXT | BUTTON_EVENT_ALT)) {
            page_index = (page_index + 1) % s_page_count;
            display_manager_switch_to_page(s_display, page_index);
            ESP_LOGI(TAG, "Switched to page %d", page_index);
        }
    }
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
        .spi_host = SPI3_HOST,
        .dma_chan = SPI_DMA_CH_AUTO,
        .sclk_io_num = 10,
        .mosi_io_num = 13,
        .miso_io_num = -1,
        .dc_io_num = 11,
        .cs_io_num = 12,
        .rst_io_num = 9,
        .bk_light_io_num = 14,
        .h_res = 170,
        .v_res = 320,
        .pixel_clock_hz = 20 * 1000 * 1000,
        .cmd_bits = 8,
        .param_bits = 8,
        .draw_buf_lines = 20,
        .tick_period_ms = 2,
        .bk_light_on_level = 0,
        .bk_light_off_level = 1,
        .orientation = DISPLAY_ORIENTATION_LANDSCAPE,
        .x_offset = 0,
        .y_offset = 35
    };

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
        display_manager_switch_to_page(s_display, 0);
    }

    button_Init();

    xTaskCreatePinnedToCore(can_rx_task, "CAN_RX", 4096, NULL, 5, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(can_tx_task, "CAN_TX", 4096, NULL, 4, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(ui_button_task, "BTN", 3072, NULL, 3, NULL, tskNO_AFFINITY);
}
