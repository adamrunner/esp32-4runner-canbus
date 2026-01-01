/*
 * 4Runner CAN Bus Display
 *
 * Polls OBD-II and Toyota-specific PIDs over CAN and displays metrics
 * on the Waveshare ESP32-S3 4.3-inch Touch LCD using LVGL.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>
#include "display_manager.h"
#include "display_manager/page.h"
#include "lvgl.h"
#include "sd_card.h"
#include "can_logger.h"
#include "rtc_pcf85063a.h"

#include "app_state.h"
#include "page_utils.h"
#include "settings_store.h"
#include "diag_page.h"
#include "fourrunner_page.h"
#include "wheel_speed_page.h"
#include "logging_page.h"
#include "rtc_page.h"
#include "rpm_page.h"
#include "orientation_page.h"

static const char *TAG = "4RUNNER_CAN";

static void log_lvgl_mem(const char *context)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "%s: lvgl mem free=%lu/%lu, biggest=%lu, used=%u%%, frag=%u%%",
             context,
             (unsigned long)mon.free_size,
             (unsigned long)mon.total_size,
             (unsigned long)mon.free_biggest_size,
             (unsigned int)mon.used_pct,
             (unsigned int)mon.frag_pct);
}

// CAN GPIO Configuration
#define TX_GPIO_NUM GPIO_NUM_15
#define RX_GPIO_NUM GPIO_NUM_16

// OBD-II CAN IDs
#define OBD_REQUEST_ID 0x7E0
#define OBD_RESPONSE_ID_MIN 0x7E8
#define OBD_RESPONSE_ID_MAX 0x7EF
#define ABS_REQUEST_ID 0x7B0
#define METER_REQUEST_ID 0x7C0
#define WHEEL_SPEED_BROADCAST_ID 0x0AA
#define RPM_TEST_BROADCAST_ID 0x2C1

#define OBD_POLL_INTERVAL_MS 150

// LCD Configuration
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

// OBD Request Definition
typedef struct {
    uint16_t header;
    uint8_t service;
    uint8_t pid;
    uint8_t ext_addr;
} obd_request_t;

static const obd_request_t k_request_sequence[] = {
    {OBD_REQUEST_ID, 0x01, 0x0C, 0},
    {OBD_REQUEST_ID, 0x01, 0x42, 0},
    {OBD_REQUEST_ID, 0x01, 0x0F, 0},
    {OBD_REQUEST_ID, 0x01, 0x33, 0},
    {OBD_REQUEST_ID, 0x21, 0x82, 0},
    {OBD_REQUEST_ID, 0x21, 0x85, 0},
    {OBD_REQUEST_ID, 0x21, 0x28, 0},
    {METER_REQUEST_ID, 0x21, 0x29, 0},
    {ABS_REQUEST_ID, 0x21, 0x03, 0},
    {ABS_REQUEST_ID, 0x21, 0x46, 0},  // Orientation zero points
    {ABS_REQUEST_ID, 0x21, 0x47, 0},  // Orientation live data
};

// TWAI Configuration
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

// CAN Message Building
static twai_message_t build_obd_request(uint16_t header, uint8_t service, uint8_t pid, uint8_t ext_addr)
{
    twai_message_t msg = {};

    msg.identifier = header;
    msg.data_length_code = 8;

    if (ext_addr != 0) {
        msg.data[0] = ext_addr;
        msg.data[1] = 0x02;
        msg.data[2] = service;
        msg.data[3] = pid;
    } else {
        msg.data[0] = 0x02;
        msg.data[1] = service;
        msg.data[2] = pid;
    }

    return msg;
}

static bool is_obd_response_id(uint32_t identifier)
{
    return (identifier >= OBD_RESPONSE_ID_MIN && identifier <= OBD_RESPONSE_ID_MAX) ||
           (identifier == 0x7B8) ||
           (identifier == 0x7C8) ||
           (identifier == WHEEL_SPEED_BROADCAST_ID);
}

// CAN Response Handlers
static void handle_standard_response(const twai_message_t *msg)
{
    uint8_t length = msg->data[0];
    uint8_t pid = msg->data[2];

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();

    switch (pid) {
        case 0x0C: {
            if (length >= 4) {
                uint16_t raw = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                m->rpm = raw / 4.0f;
                m->rpm_valid = true;
            }
            break;
        }
        case 0x42: {
            if (length >= 4) {
                uint16_t raw = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                m->vbatt_v = raw / 1000.0f;
                m->vbatt_valid = true;
            }
            break;
        }
        case 0x0F: {
            if (length >= 3) {
                m->iat_c = (float)msg->data[3] - 40.0f;
                m->iat_valid = true;
            }
            break;
        }
        case 0x33: {
            if (length >= 3) {
                m->baro_kpa = (float)msg->data[3];
                m->baro_valid = true;
            }
            break;
        }
        default:
            break;
    }

    metrics_unlock();
}

static void handle_broadcast_wheel_speed(const twai_message_t *msg)
{
    if (msg->data_length_code < 8) {
        return;
    }

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();

    uint16_t raw_fr = ((uint16_t)msg->data[0] << 8) | msg->data[1];
    uint16_t raw_fl = ((uint16_t)msg->data[2] << 8) | msg->data[3];
    uint16_t raw_rr = ((uint16_t)msg->data[4] << 8) | msg->data[5];
    uint16_t raw_rl = ((uint16_t)msg->data[6] << 8) | msg->data[7];

    static const int16_t k_wheel_speed_offset = 6770;
    m->bcast_wheel_fr_kph = ((int16_t)raw_fr - k_wheel_speed_offset) / 100.0f;
    m->bcast_wheel_fl_kph = ((int16_t)raw_fl - k_wheel_speed_offset) / 100.0f;
    m->bcast_wheel_rr_kph = ((int16_t)raw_rr - k_wheel_speed_offset) / 100.0f;
    m->bcast_wheel_rl_kph = ((int16_t)raw_rl - k_wheel_speed_offset) / 100.0f;
    m->bcast_wheel_speed_valid = true;

    metrics_unlock();
}

static void handle_broadcast_rpm_test(const twai_message_t *msg)
{
    if (msg->data_length_code < 8) {
        return;
    }

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();

    m->bcast_rpm_1 = msg->data[2] * 8.0f;
    m->bcast_rpm_2 = msg->data[4] * 8.0f;
    m->bcast_rpm_3 = msg->data[7] * 8.0f;

    uint16_t raw_16 = ((uint16_t)msg->data[5] << 8) | msg->data[4];
    m->bcast_rpm_4 = raw_16 * 0.125f;

    m->bcast_rpm_valid = true;

    metrics_unlock();
}

static void handle_extended_response(const twai_message_t *msg)
{
    uint8_t length = msg->data[0];
    uint8_t pid = msg->data[2];

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();

    switch (pid) {
        case 0x82: {
            if (length >= 6) {
                uint16_t raw_pan = (uint16_t)(msg->data[3] << 8) | msg->data[4];
                m->atf_pan_c = (raw_pan / 256.0f) - 40.0f;

                uint16_t raw_tqc = (uint16_t)(msg->data[5] << 8) | msg->data[6];
                m->atf_tqc_c = (raw_tqc / 256.0f) - 40.0f;
                m->atf_valid = true;
            }
            break;
        }
        case 0x85: {
            if (length >= 5) {
                m->gear = msg->data[3];
                m->tqc_lockup = (msg->data[4] & 0x80) != 0;
                m->gear_valid = true;
            }
            break;
        }
        case 0x28: {
            if (length >= 5) {
                m->odo_km = ((uint32_t)msg->data[3] << 16)
                    | ((uint32_t)msg->data[4] << 8)
                    | (uint32_t)msg->data[5];
                m->odo_valid = true;
            }
            break;
        }
        case 0x29: {
            if (length >= 3) {
                uint8_t raw_fuel = msg->data[3];
                m->fli_vol_gal = (raw_fuel * 500.0f) / 3785.0f;
                m->fuel_valid = true;
                ESP_LOGI(TAG, "Fuel level: raw=0x%02X (%.2f gal)", raw_fuel, m->fli_vol_gal);
            }
            break;
        }
        case 0x03: {
            if (length >= 7) {
                m->diag_wheel_fr_kph = (msg->data[3] * 256.0f) / 200.0f;
                m->diag_wheel_fl_kph = (msg->data[4] * 256.0f) / 200.0f;
                m->diag_wheel_rr_kph = (msg->data[5] * 256.0f) / 200.0f;
                m->diag_wheel_rl_kph = (msg->data[6] * 256.0f) / 200.0f;
                m->diag_wheel_speed_valid = true;
            }
            break;
        }
        case 0x46: {
            // Orientation zero points (from ABS module 0x7B0)
            if (length >= 5) {
                // Zero point of deceleration: value * 0.1961568627 - 25.11 (m/s^2)
                m->zp_decel_1 = (msg->data[3] * 0.1961568627f) - 25.11f;
                m->zp_decel_2 = (msg->data[4] * 0.1961568627f) - 25.11f;
                // Zero point of yaw rate: value - 128 (degrees/sec)
                m->zp_yaw_rate = (float)msg->data[5] - 128.0f;
                m->orientation_zp_valid = true;
            }
            break;
        }
        case 0x47: {
            // Orientation live data (from ABS module 0x7B0)
            if (length >= 7) {
                // Lateral g: signed value / 50 (gravity)
                m->lateral_g = (int8_t)msg->data[3] / 50.0f;
                // Longitudinal g: signed value / 50 (gravity)
                m->longitudinal_g = (int8_t)msg->data[4] / 50.0f;
                // Yaw rate: value - 128 (degrees/sec)
                m->yaw_rate_deg_sec = (float)msg->data[5] - 128.0f;
                // Steering wheel angle: 16-bit value / 10 - 3276.8 (degrees)
                uint16_t raw_steer = ((uint16_t)msg->data[6] << 8) | msg->data[7];
                m->steering_angle_deg = (raw_steer / 10.0f) - 3276.8f;
                m->orientation_valid = true;
            }
            break;
        }
        default:
            break;
    }

    metrics_unlock();
}

static void process_obd_response(const twai_message_t *msg)
{
    if (!msg) {
        return;
    }

    if (msg->identifier == WHEEL_SPEED_BROADCAST_ID) {
        handle_broadcast_wheel_speed(msg);
        return;
    }

    if (msg->identifier == RPM_TEST_BROADCAST_ID) {
        handle_broadcast_rpm_test(msg);
        return;
    }

    if (!is_obd_response_id(msg->identifier)) {
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

    if (msg->identifier == 0x7C8) {
        ESP_LOGI(TAG, "Meter RX: %02X %02X %02X %02X %02X %02X %02X %02X",
                 msg->data[0], msg->data[1], msg->data[2], msg->data[3],
                 msg->data[4], msg->data[5], msg->data[6], msg->data[7]);
    }

    if (service == 0x41) {
        handle_standard_response(msg);
    } else if (service == 0x61) {
        handle_extended_response(msg);
    }
}

// CAN Tasks
static void can_rx_task(void *arg)
{
    (void)arg;
    twai_message_t rx_msg = {};

    ESP_LOGI(TAG, "CAN RX task started");

    while (1) {
        if (can_state_is_paused()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        esp_err_t err = twai_receive(&rx_msg, pdMS_TO_TICKS(100));
        if (err == ESP_OK) {
            if (can_logger_is_running()) {
                can_logger_message_t log_msg = {
                    .identifier = rx_msg.identifier,
                    .data_length_code = rx_msg.data_length_code,
                    .data = {0}
                };
                memcpy(log_msg.data, rx_msg.data, 8);
                can_logger_log_message(esp_timer_get_time(), &log_msg);
            }

            process_obd_response(&rx_msg);
            update_can_error_state(true, false);
        }
    }
}

static void can_tx_task(void *arg)
{
    (void)arg;
    size_t request_index = 0;

    ESP_LOGI(TAG, "CAN TX task started");

    while (1) {
        if (can_state_is_paused()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        const obd_request_t *req = &k_request_sequence[request_index];
        twai_message_t msg = build_obd_request(req->header, req->service, req->pid, req->ext_addr);

        if (req->header == METER_REQUEST_ID) {
            ESP_LOGI(TAG, "TX to 0x%03X: %02X %02X %02X %02X %02X %02X %02X %02X",
                     msg.identifier,
                     msg.data[0], msg.data[1], msg.data[2], msg.data[3],
                     msg.data[4], msg.data[5], msg.data[6], msg.data[7]);
        }

        esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(50));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "OBD request 0x%03X 0x%02X 0x%02X (ext:0x%02X) failed: %s",
                     req->header, req->service, req->pid, req->ext_addr, esp_err_to_name(err));
            update_can_error_state(false, true);
        }

        request_index = (request_index + 1) % (sizeof(k_request_sequence) / sizeof(k_request_sequence[0]));
        vTaskDelay(pdMS_TO_TICKS(OBD_POLL_INTERVAL_MS));
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "4Runner CAN Bus Display starting");
    ESP_LOGI(TAG, "TX GPIO: %d, RX GPIO: %d", TX_GPIO_NUM, RX_GPIO_NUM);

    if (!app_state_init()) {
        ESP_LOGE(TAG, "Failed to initialize app state");
        return;
    }

    // Initialize TWAI driver
    esp_err_t twai_err = twai_driver_install(&g_config, &t_config, &f_config);
    if (twai_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install TWAI driver: %s", esp_err_to_name(twai_err));
        return;
    }
    ESP_LOGI(TAG, "TWAI driver installed");

    bool auto_start_can = false;
    settings_get_can_autostart(&auto_start_can);
    ESP_LOGI(TAG, "CAN auto-start on boot: %s", auto_start_can ? "enabled" : "disabled");
    if (auto_start_can) {
        twai_err = twai_start();
        if (twai_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start TWAI: %s", esp_err_to_name(twai_err));
            return;
        }
        ESP_LOGI(TAG, "TWAI started");
    } else {
        ESP_LOGI(TAG, "TWAI paused on startup (auto-start disabled)");
        app_state_set_can_paused_internal(true);
    }

    // Initialize display
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
        .draw_buf_lines = 20,
        .tick_period_ms = 2,
        .orientation = DISPLAY_ORIENTATION_LANDSCAPE,
        .x_offset = 0,
        .y_offset = 0
    };
    memcpy(display_config.data_io_nums, lcd_data_io_nums, sizeof(lcd_data_io_nums));

    display_manager_handle_t display = display_manager_init(&display_config);
    if (!display) {
        ESP_LOGE(TAG, "Failed to initialize display manager");
        return;
    }
    app_state_set_display(display);

    lv_display_t *lv_disp = display_manager_get_display(display);
    if (lv_disp) {
        lv_obj_t *screen = lv_display_get_screen_active(lv_disp);
        if (screen) {
            lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_event_cb(screen, page_swipe_event_cb, LV_EVENT_GESTURE, NULL);
        }
    }

    // Initialize RTC
    esp_err_t rtc_err = pcf_rtc_init(lcd_i2c_port);
    if (rtc_err != ESP_OK) {
        ESP_LOGW(TAG, "RTC init failed: %s", esp_err_to_name(rtc_err));
    } else {
        ESP_LOGI(TAG, "RTC initialized");
    }

    // Initialize SD card
    esp_err_t sd_err = sd_card_init(lcd_i2c_port);
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "SD card init failed: %s (logging disabled)", esp_err_to_name(sd_err));
    } else {
        ESP_LOGI(TAG, "SD card initialized");

        esp_err_t log_err = can_logger_init(2048);  // ~2.4 sec buffer at 857 msg/sec
        if (log_err != ESP_OK) {
            ESP_LOGW(TAG, "CAN logger init failed: %s", esp_err_to_name(log_err));
        } else {
            ESP_LOGI(TAG, "CAN logger initialized");
        }
    }

    // Create and register pages
    int page_count = 0;

    ESP_LOGI(TAG, "Free heap before pages: %lu", (unsigned long)esp_get_free_heap_size());
    log_lvgl_mem("LVGL before pages");

    ESP_LOGI(TAG, "Creating diag page...");
    dm_page_t *diag_page = diag_page_create();
    if (diag_page) {
        ESP_LOGI(TAG, "Adding diag page...");
        display_manager_add_page(display, diag_page);
        page_count++;
        ESP_LOGI(TAG, "Diag page added, heap: %lu", (unsigned long)esp_get_free_heap_size());
        log_lvgl_mem("LVGL after diag page");
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Creating fourrunner page...");
    dm_page_t *fourrunner_page = fourrunner_page_create();
    if (fourrunner_page) {
        ESP_LOGI(TAG, "Adding fourrunner page...");
        display_manager_add_page(display, fourrunner_page);
        page_count++;
        ESP_LOGI(TAG, "Fourrunner page added, heap: %lu", (unsigned long)esp_get_free_heap_size());
        log_lvgl_mem("LVGL after fourrunner page");
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Creating wheel_speed page...");
    dm_page_t *wheel_speed_page = wheel_speed_page_create();
    if (wheel_speed_page) {
        ESP_LOGI(TAG, "Adding wheel_speed page...");
        display_manager_add_page(display, wheel_speed_page);
        page_count++;
        ESP_LOGI(TAG, "Wheel speed page added, heap: %lu", (unsigned long)esp_get_free_heap_size());
        log_lvgl_mem("LVGL after wheel_speed page");
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Creating logging page...");
    dm_page_t *log_page = logging_page_create();
    if (log_page) {
        ESP_LOGI(TAG, "Adding logging page...");
        display_manager_add_page(display, log_page);
        page_count++;
        ESP_LOGI(TAG, "Logging page added, heap: %lu", (unsigned long)esp_get_free_heap_size());
        log_lvgl_mem("LVGL after logging page");
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Creating rtc page...");
    dm_page_t *rtc_settings_page = rtc_page_create();
    if (rtc_settings_page) {
        ESP_LOGI(TAG, "Adding rtc page...");
        display_manager_add_page(display, rtc_settings_page);
        page_count++;
        ESP_LOGI(TAG, "RTC page added, heap: %lu", (unsigned long)esp_get_free_heap_size());
        log_lvgl_mem("LVGL after rtc page");
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Creating rpm page...");
    dm_page_t *rpm_test_page = rpm_page_create();
    if (rpm_test_page) {
        ESP_LOGI(TAG, "Adding rpm page...");
        display_manager_add_page(display, rpm_test_page);
        page_count++;
        ESP_LOGI(TAG, "RPM page added, heap: %lu", (unsigned long)esp_get_free_heap_size());
        log_lvgl_mem("LVGL after rpm page");
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "Creating orientation page...");
    dm_page_t *orient_page = orientation_page_create();
    if (orient_page) {
        ESP_LOGI(TAG, "Adding orientation page...");
        display_manager_add_page(display, orient_page);
        page_count++;
        ESP_LOGI(TAG, "Orientation page added, heap: %lu", (unsigned long)esp_get_free_heap_size());
        log_lvgl_mem("LVGL after orientation page");
    }

    ESP_LOGI(TAG, "All pages created, count=%d", page_count);
    app_state_set_page_count(page_count);

    // Start the LVGL task AFTER all pages are created to avoid race condition
    // between page creation and LVGL's timer handler doing layout updates
    if (!display_manager_start(display)) {
        ESP_LOGE(TAG, "Failed to start display manager");
        return;
    }

    if (page_count > 0) {
        app_state_set_active_page(0);
        display_manager_switch_to_page(display, 0);
    }

    // Start CAN tasks
    xTaskCreatePinnedToCore(can_rx_task, "CAN_RX", 4096, NULL, 5, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(can_tx_task, "CAN_TX", 4096, NULL, 4, NULL, tskNO_AFFINITY);
    ESP_LOGI(TAG, "CAN tasks started");
}
