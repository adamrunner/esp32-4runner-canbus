/*
 * Application State - Shared state for CAN metrics and navigation
 *
 * This module provides thread-safe access to CAN bus metrics and
 * application state that is shared across all UI pages.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "display_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

// CAN bus metrics collected from OBD-II and Toyota-specific PIDs
typedef struct {
    float rpm;
    float vbatt_v;
    float iat_c;
    float baro_kpa;
    float atf_pan_c;
    float atf_tqc_c;
    float fli_vol_gal;
    uint32_t odo_km;
    int gear;
    bool tqc_lockup;
    // Diagnostic wheel speeds (from 0x7B0 PID 0x03)
    float diag_wheel_fl_kph;
    float diag_wheel_fr_kph;
    float diag_wheel_rl_kph;
    float diag_wheel_rr_kph;
    // Broadcast wheel speeds (from 0x0AA)
    float bcast_wheel_fl_kph;
    float bcast_wheel_fr_kph;
    float bcast_wheel_rl_kph;
    float bcast_wheel_rr_kph;
    // Broadcast RPM test decodings (from 0x2C1)
    float bcast_rpm_1;
    float bcast_rpm_2;
    float bcast_rpm_3;
    float bcast_rpm_4;
    bool bcast_rpm_valid;
    // Orientation data (from 0x7B0 PIDs 0x46 and 0x47)
    float lateral_g;
    float longitudinal_g;
    float yaw_rate_deg_sec;
    float steering_angle_deg;
    float zp_decel_1;
    float zp_decel_2;
    float zp_yaw_rate;
    bool orientation_valid;
    bool orientation_zp_valid;
    // Validity flags
    bool rpm_valid;
    bool vbatt_valid;
    bool iat_valid;
    bool baro_valid;
    bool atf_valid;
    bool fuel_valid;
    bool odo_valid;
    bool gear_valid;
    bool diag_wheel_speed_valid;
    bool bcast_wheel_speed_valid;
} can_metrics_t;

// CAN bus state (paused, error, etc.)
typedef struct {
    bool paused;
    bool error_active;
    int fail_count;
    int64_t last_rx_ms;
} can_state_t;

/**
 * @brief Initialize application state (mutexes, etc.)
 * @return true on success
 */
bool app_state_init(void);

/**
 * @brief Get thread-safe snapshot of current CAN metrics
 * @param out Pointer to struct to fill with current metrics
 */
void metrics_get_snapshot(can_metrics_t *out);

/**
 * @brief Get pointer to metrics struct for updating (caller must hold mutex)
 * @return Pointer to metrics struct, or NULL if mutex not held
 */
can_metrics_t *metrics_get_for_update(void);

/**
 * @brief Take the metrics mutex
 */
void metrics_lock(void);

/**
 * @brief Release the metrics mutex
 */
void metrics_unlock(void);

/**
 * @brief Check if CAN is currently paused
 * @return true if paused
 */
bool can_state_is_paused(void);

/**
 * @brief Set CAN paused state (starts/stops TWAI driver)
 * @param paused true to pause, false to resume
 */
void set_can_paused(bool paused);

/**
 * @brief Set CAN paused state internally (without TWAI start/stop)
 * Used during initialization before TWAI is started
 * @param paused true to pause, false to resume
 */
void app_state_set_can_paused_internal(bool paused);

/**
 * @brief Update CAN error state based on RX/TX results
 * @param rx_ok true if a valid message was received
 * @param tx_failed true if a transmit failed
 */
void update_can_error_state(bool rx_ok, bool tx_failed);

/**
 * @brief Get thread-safe snapshot of CAN state
 * @param out Pointer to struct to fill with current state
 */
void can_state_get_snapshot(can_state_t *out);

/**
 * @brief Schedule async UI update for CAN state changes
 */
void schedule_can_ui_update(void);

/**
 * @brief Set the display manager handle
 * @param display Display manager handle
 */
void app_state_set_display(display_manager_handle_t display);

/**
 * @brief Get the display manager handle
 * @return Display manager handle
 */
display_manager_handle_t app_state_get_display(void);

/**
 * @brief Get page count
 * @return Number of registered pages
 */
int app_state_get_page_count(void);

/**
 * @brief Set page count
 * @param count Number of pages
 */
void app_state_set_page_count(int count);

/**
 * @brief Get active page index
 * @return Current active page index
 */
int app_state_get_active_page(void);

/**
 * @brief Set active page index
 * @param page Page index
 */
void app_state_set_active_page(int page);

/**
 * @brief Switch to page by offset (circular navigation)
 * @param offset Offset from current page (+1 for next, -1 for prev)
 */
void switch_page_by_offset(int offset);

/**
 * @brief Get current time in milliseconds
 * @return Time since boot in milliseconds
 */
int64_t get_time_ms(void);

// Global error label pointers (set by pages, used by can_ui_update_cb)
extern lv_obj_t *g_diag_error_label;
extern lv_obj_t *g_fourrunner_error_label;
extern lv_obj_t *g_tire_error_label;
extern lv_obj_t *g_rpm_error_label;
extern lv_obj_t *g_orientation_error_label;

// Global CAN toggle label pointers (set by pages, used by can_ui_update_cb)
extern lv_obj_t *g_diag_can_toggle_label;
extern lv_obj_t *g_fourrunner_can_toggle_label;
extern lv_obj_t *g_tire_can_toggle_label;
extern lv_obj_t *g_rpm_can_toggle_label;
extern lv_obj_t *g_orientation_can_toggle_label;

#ifdef __cplusplus
}
#endif
