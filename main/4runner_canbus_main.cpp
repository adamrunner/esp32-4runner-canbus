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
#include "rpm_page.h"
#include "orientation_page.h"
#include "rtc_page.h"

#define ENABLE_RTC_SETTINGS_PAGE 0

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
#define VEHICLE_SPEED_BROADCAST_ID 0x0B4
#define KINEMATICS_BROADCAST_ID_024 0x024
#define GEAR_BROADCAST_ID_025 0x025  // DBC maps 0x025 to steering angle sensor
#define RPM_BROADCAST_ID_1C4 0x1C4

// Signal definitions for CAN ID 0x024 (Kinematics)
// All signals are 10-bit with offset -512 (raw 0-1023 maps to -512 to +511)
#define KINEMATICS_YAW_START_BIT      1
#define KINEMATICS_YAW_LENGTH         10
#define KINEMATICS_TORQUE_START_BIT   17
#define KINEMATICS_TORQUE_LENGTH      10
#define KINEMATICS_ACCEL_START_BIT    33
#define KINEMATICS_ACCEL_LENGTH       10
#define KINEMATICS_OFFSET             512  // Subtract from raw to get signed value

// Signal definitions for CAN ID 0x025 (Steering Angle)
// 12-bit signed value with scale factor 1.5 deg/LSB
#define STEER_ANGLE_START_BIT         3
#define STEER_ANGLE_LENGTH            12
#define STEER_ANGLE_SCALE             1.5f
#define RPM_TEST_BROADCAST_ID 0x2C1
#define ORIENTATION_CAND_ID_1D0 0x1D0  // Gear candidate from correlation: byte 4
#define CAN_LOGGER_RING_BUFFER_BYTES (4 * 1024 * 1024)  // 4 MB ring buffer (PSRAM)

#define OBD_POLL_INTERVAL_MS 150
#define CAN_TELEMETRY_INTERVAL_MS 2000

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

// Extract a big-endian signal where start_bit is the LSB position.
// Bits are collected from start_bit upward (in big-endian bit numbering)
// and assembled so the first bit becomes the LSB of the result.
static uint32_t extract_be_signal_lsb_start(const uint8_t *data, uint8_t start_bit, uint8_t length)
{
    uint32_t value = 0;
    int byte_index = start_bit / 8;
    int bit_index = start_bit % 8;  // 0 = LSB of byte

    for (uint8_t i = 0; i < length; i++) {
        uint8_t bit = (data[byte_index] >> bit_index) & 0x01;
        value |= (uint32_t)bit << i;  // First bit -> bit 0 (LSB)
        if (bit_index == 0) {
            byte_index++;
            bit_index = 7;
        } else {
            bit_index--;
        }
    }

    return value;
}

static int32_t sign_extend(uint32_t value, uint8_t bit_length)
{
    if (bit_length == 0 || bit_length >= 32) {
        return (int32_t)value;
    }

    uint32_t sign_bit = 1u << (bit_length - 1);
    if (value & sign_bit) {
        uint32_t mask = (1u << bit_length) - 1u;
        value |= ~mask;
    }

    return (int32_t)value;
}

// OBD Request Definition
typedef struct {
    uint16_t header;
    uint8_t service;
    uint8_t pid;
    uint8_t ext_addr;
} obd_request_t;

static const obd_request_t k_request_sequence[] = {
    {OBD_REQUEST_ID, 0x01, 0x0C, 0},
    {OBD_REQUEST_ID, 0x01, 0x0D, 0},  // Vehicle speed
    {OBD_REQUEST_ID, 0x01, 0x11, 0},  // Throttle position
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
    .tx_queue_len = 20,
    .rx_queue_len = 100,
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

static const char *twai_state_to_str(twai_state_t state)
{
    switch (state) {
        case TWAI_STATE_STOPPED:
            return "stopped";
        case TWAI_STATE_RUNNING:
            return "running";
        case TWAI_STATE_BUS_OFF:
            return "bus_off";
        case TWAI_STATE_RECOVERING:
            return "recovering";
        default:
            return "unknown";
    }
}

static uint32_t delta_u32(uint32_t current, uint32_t last)
{
    return current >= last ? (current - last) : 0;
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
        case 0x0D: {
            // Vehicle speed (OBD-II standard: single byte, KPH)
            if (length >= 3) {
                m->diag_vehicle_speed_kph = (float)msg->data[3];
                m->diag_vehicle_speed_valid = true;
            }
            break;
        }
        case 0x11: {
            // Throttle position (OBD-II standard: 0-100%)
            if (length >= 3) {
                m->throttle_pct = (msg->data[3] * 100.0f) / 255.0f;
                m->throttle_valid = true;
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

static void handle_broadcast_vehicle_speed(const twai_message_t *msg)
{
    if (msg->data_length_code < 8) {
        return;
    }

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();

    // Speed is in bytes 5-6 as big-endian 16-bit, divided by 100 for KPH
    uint16_t raw_speed = ((uint16_t)msg->data[5] << 8) | msg->data[6];
    m->bcast_vehicle_speed_kph = raw_speed / 100.0f;
    m->bcast_vehicle_speed_valid = true;
    memcpy(m->cand_0b4_raw, msg->data, sizeof(m->cand_0b4_raw));
    m->cand_0b4_valid = true;

    metrics_unlock();
}

static void handle_broadcast_rpm_1c4(const twai_message_t *msg)
{
    if (msg->data_length_code < 2) {
        return;
    }

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();

    uint16_t raw_rpm = ((uint16_t)msg->data[0] << 8) | msg->data[1];
    // Derived from correlation with PID 0x0C: rpm ~= raw * 25 / 32.
    static const float k_rpm_scale = 25.0f / 32.0f;
    m->bcast_rpm_1c4 = raw_rpm * k_rpm_scale;
    m->bcast_rpm_1c4_valid = true;

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
    memcpy(m->cand_2c1_raw, msg->data, sizeof(m->cand_2c1_raw));
    m->cand_2c1_valid = true;

    metrics_unlock();
}

static void handle_broadcast_kinematics_024(const twai_message_t *msg)
{
    if (msg->data_length_code < 8) {
        return;
    }

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();

    uint32_t raw_yaw = extract_be_signal_lsb_start(msg->data,
        KINEMATICS_YAW_START_BIT, KINEMATICS_YAW_LENGTH);
    uint32_t raw_torque = extract_be_signal_lsb_start(msg->data,
        KINEMATICS_TORQUE_START_BIT, KINEMATICS_TORQUE_LENGTH);
    uint32_t raw_accel = extract_be_signal_lsb_start(msg->data,
        KINEMATICS_ACCEL_START_BIT, KINEMATICS_ACCEL_LENGTH);

    // Convert from unsigned 10-bit (0-1023) to signed (-512 to +511)
    int32_t yaw_rate = (int32_t)raw_yaw - KINEMATICS_OFFSET;
    int32_t steer_torque = (int32_t)raw_torque - KINEMATICS_OFFSET;
    int32_t accel_y = (int32_t)raw_accel - KINEMATICS_OFFSET;

    m->bcast_yaw_rate_deg_sec = (float)yaw_rate;
    m->bcast_steering_torque = (float)steer_torque;
    // Lateral G conversion: empirically derived scale and offset from OBD correlation
    m->bcast_lateral_g = (accel_y * -0.002121f) - 0.0126f;
    m->bcast_kinematics_valid = true;

    metrics_unlock();
}

static void handle_broadcast_candidate_1d0(const twai_message_t *msg)
{
    if (msg->data_length_code < 8) {
        return;
    }

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();
    memcpy(m->cand_1d0_raw, msg->data, sizeof(m->cand_1d0_raw));
    m->cand_1d0_valid = true;
    metrics_unlock();
}

static void handle_broadcast_candidate_025(const twai_message_t *msg)
{
    if (msg->data_length_code < 8) {
        return;
    }

    metrics_lock();
    can_metrics_t *m = metrics_get_for_update();
    uint32_t raw_angle = extract_be_signal_lsb_start(msg->data,
        STEER_ANGLE_START_BIT, STEER_ANGLE_LENGTH);
    int32_t signed_angle = sign_extend(raw_angle, STEER_ANGLE_LENGTH);
    m->bcast_steering_angle_deg = signed_angle * STEER_ANGLE_SCALE;
    m->bcast_steer_angle_valid = true;
    memcpy(m->cand_025_raw, msg->data, sizeof(m->cand_025_raw));
    m->cand_025_valid = true;
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

    if (msg->identifier == VEHICLE_SPEED_BROADCAST_ID) {
        handle_broadcast_vehicle_speed(msg);
        return;
    }

    if (msg->identifier == RPM_BROADCAST_ID_1C4) {
        handle_broadcast_rpm_1c4(msg);
        return;
    }

    if (msg->identifier == RPM_TEST_BROADCAST_ID) {
        handle_broadcast_rpm_test(msg);
        return;
    }

    if (msg->identifier == KINEMATICS_BROADCAST_ID_024) {
        handle_broadcast_kinematics_024(msg);
        return;
    }

    if (msg->identifier == ORIENTATION_CAND_ID_1D0) {
        handle_broadcast_candidate_1d0(msg);
        return;
    }

    if (msg->identifier == GEAR_BROADCAST_ID_025) {
        handle_broadcast_candidate_025(msg);
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

static void can_telemetry_task(void *arg)
{
    (void)arg;

    int64_t last_ms = esp_timer_get_time() / 1000;
    uint32_t last_rx_missed = 0;
    uint32_t last_rx_overrun = 0;
    uint32_t last_tx_failed = 0;
    uint32_t last_arb_lost = 0;
    uint32_t last_bus_error = 0;
    uint32_t last_logged = 0;
    uint32_t last_dropped = 0;
    uint32_t last_buf_overrun = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(CAN_TELEMETRY_INTERVAL_MS));

        int64_t now_ms = esp_timer_get_time() / 1000;
        float interval_s = (now_ms - last_ms) / 1000.0f;
        if (interval_s <= 0.0f) {
            interval_s = 1.0f;
        }
        last_ms = now_ms;

        if (can_state_is_paused()) {
            continue;
        }

        twai_status_info_t status = {};
        esp_err_t err = twai_get_status_info(&status);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Telemetry: failed to read TWAI status: %s", esp_err_to_name(err));
            continue;
        }

        uint32_t rx_missed_delta = delta_u32(status.rx_missed_count, last_rx_missed);
        uint32_t rx_overrun_delta = delta_u32(status.rx_overrun_count, last_rx_overrun);
        uint32_t tx_failed_delta = delta_u32(status.tx_failed_count, last_tx_failed);
        uint32_t arb_lost_delta = delta_u32(status.arb_lost_count, last_arb_lost);
        uint32_t bus_error_delta = delta_u32(status.bus_error_count, last_bus_error);

        last_rx_missed = status.rx_missed_count;
        last_rx_overrun = status.rx_overrun_count;
        last_tx_failed = status.tx_failed_count;
        last_arb_lost = status.arb_lost_count;
        last_bus_error = status.bus_error_count;

        if (can_logger_is_running()) {
            can_logger_stats_t log_stats = {};
            if (can_logger_get_stats(&log_stats) == ESP_OK) {
                uint32_t logged_delta = delta_u32(log_stats.messages_logged, last_logged);
                uint32_t dropped_delta = delta_u32(log_stats.messages_dropped, last_dropped);
                uint32_t buf_overrun_delta = delta_u32(log_stats.buffer_overruns, last_buf_overrun);
                uint32_t total_delta = logged_delta + dropped_delta;
                float drop_pct = total_delta > 0 ? (dropped_delta * 100.0f) / total_delta : 0.0f;
                float log_rate = logged_delta / interval_s;

                ESP_LOGI(TAG,
                         "CAN telem %.1fs state=%s rx_q=%u tx_q=%u "
                         "rx_miss=%u(+%u) rx_ovr=%u(+%u) tx_fail=%u(+%u) "
                         "arb_lost=%u(+%u) bus_err=%u(+%u) "
                         "log=%u(+%u,%.1f/s) drop=%u(+%u,%.1f%%) buf_ovr=%u(+%u) wr_err=%u",
                         interval_s,
                         twai_state_to_str(status.state),
                         status.msgs_to_rx,
                         status.msgs_to_tx,
                         status.rx_missed_count,
                         rx_missed_delta,
                         status.rx_overrun_count,
                         rx_overrun_delta,
                         status.tx_failed_count,
                         tx_failed_delta,
                         status.arb_lost_count,
                         arb_lost_delta,
                         status.bus_error_count,
                         bus_error_delta,
                         log_stats.messages_logged,
                         logged_delta,
                         log_rate,
                         log_stats.messages_dropped,
                         dropped_delta,
                         drop_pct,
                         log_stats.buffer_overruns,
                         buf_overrun_delta,
                         log_stats.write_errors);

                last_logged = log_stats.messages_logged;
                last_dropped = log_stats.messages_dropped;
                last_buf_overrun = log_stats.buffer_overruns;
            }
        } else {
            ESP_LOGI(TAG,
                     "CAN telem %.1fs state=%s rx_q=%u tx_q=%u "
                     "rx_miss=%u(+%u) rx_ovr=%u(+%u) tx_fail=%u(+%u) "
                     "arb_lost=%u(+%u) bus_err=%u(+%u)",
                     interval_s,
                     twai_state_to_str(status.state),
                     status.msgs_to_rx,
                     status.msgs_to_tx,
                     status.rx_missed_count,
                     rx_missed_delta,
                     status.rx_overrun_count,
                     rx_overrun_delta,
                     status.tx_failed_count,
                     tx_failed_delta,
                     status.arb_lost_count,
                     arb_lost_delta,
                     status.bus_error_count,
                     bus_error_delta);
        }
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
        esp_err_t sync_err = pcf_rtc_sync_system_time();
        if (sync_err != ESP_OK && sync_err != ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "RTC system time sync failed: %s", esp_err_to_name(sync_err));
        }
    }

    // Initialize SD card
    esp_err_t sd_err = sd_card_init(lcd_i2c_port);
    if (sd_err != ESP_OK) {
        ESP_LOGW(TAG, "SD card init failed: %s (logging disabled)", esp_err_to_name(sd_err));
    } else {
        ESP_LOGI(TAG, "SD card initialized");

        esp_err_t log_err = can_logger_init(CAN_LOGGER_RING_BUFFER_BYTES);
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
    vTaskDelay(pdMS_TO_TICKS(10));

#if ENABLE_RTC_SETTINGS_PAGE
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
#endif

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
    xTaskCreatePinnedToCore(can_telemetry_task, "CAN_TLM", 4096, NULL, 2, NULL, tskNO_AFFINITY);
    ESP_LOGI(TAG, "CAN tasks started");
}
