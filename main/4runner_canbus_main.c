/*
 * 4Runner CAN Bus - Tire Pressure Monitor with WiFi WebSocket Streaming
 *
 * Passively listens to the 5th-gen Toyota 4Runner CAN bus and decodes the
 * TPMS broadcast frame (CAN ID 0x0AA). Data is streamed to connected WebSocket
 * clients via WiFi access point.
 *
 * Hardware: ESP32 with TJA1050 CAN transceiver
 * CAN Speed: 500 kbps
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "driver/twai.h"

#include "4runner_can_decoder_verified.h"
#include "wifi_ap.h"
#include "web_server.h"
#include "can_data_router.h"

/* --------------------- Definitions and static variables ------------------ */
#define TAG "4Runner CAN"

// Use Kconfig values or defaults for GPIO
#ifndef CONFIG_CAN_TX_GPIO_NUM
#define CONFIG_CAN_TX_GPIO_NUM 15
#endif

#ifndef CONFIG_CAN_RX_GPIO_NUM
#define CONFIG_CAN_RX_GPIO_NUM 16
#endif

#define TX_GPIO_NUM CONFIG_CAN_TX_GPIO_NUM
#define RX_GPIO_NUM CONFIG_CAN_RX_GPIO_NUM

// TPMS broadcast CAN ID (verified)
#define TPMS_BROADCAST_ID 0x0AA

// Task priorities
#define RX_TASK_PRIO     5
#define STATUS_TASK_PRIO 4

// Print interval and staleness detection
#define TPMS_PRINT_INTERVAL_MS 1000
#define TPMS_STALE_TIMEOUT_MS  2500

typedef struct
{
    tire_pressure_t pressures;
    bool valid;
    int64_t last_update_us;
} tpms_state_t;

static tpms_state_t tpms_state = {0};
static SemaphoreHandle_t tpms_mutex;

/* --------------------- TWAI Configuration -------------------------------- */
static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
static const twai_general_config_t g_config = {
    .mode = TWAI_MODE_LISTEN_ONLY,
    .tx_io = TX_GPIO_NUM,
    .rx_io = RX_GPIO_NUM,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 5,
    .rx_queue_len = 40,
    .alerts_enabled = TWAI_ALERT_NONE,
    .clkout_divider = 0,
};

static void handle_tpms_frame(const twai_message_t *msg)
{
    if (msg->identifier != TPMS_BROADCAST_ID || msg->data_length_code != 8)
    {
        return;
    }

    tire_pressure_t decoded = decode_tire_pressure(msg->data);

    xSemaphoreTake(tpms_mutex, portMAX_DELAY);
    tpms_state.pressures = decoded;
    tpms_state.valid = true;
    tpms_state.last_update_us = esp_timer_get_time();
    xSemaphoreGive(tpms_mutex);
}

/* --------------------- Tasks --------------------------------------------- */

static void can_receive_task(void *arg)
{
    (void)arg;

    twai_message_t rx_msg;

    ESP_LOGI(TAG, "CAN receive task started");

    while (1)
    {
        if (twai_receive(&rx_msg, pdMS_TO_TICKS(100)) == ESP_OK)
        {
            // Always process TPMS frames to update state
            handle_tpms_frame(&rx_msg);

            // Route frame to WebSocket clients (based on current mode)
            can_data_router_process_frame(&rx_msg);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

static void tpms_status_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "TPMS status task started");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(TPMS_PRINT_INTERVAL_MS));

        tire_pressure_t pressures = {0};
        bool valid = false;
        int64_t last_update_us = 0;

        xSemaphoreTake(tpms_mutex, portMAX_DELAY);
        pressures = tpms_state.pressures;
        valid = tpms_state.valid;
        last_update_us = tpms_state.last_update_us;
        xSemaphoreGive(tpms_mutex);

        int64_t now_us = esp_timer_get_time();
        int64_t age_ms = (last_update_us > 0) ? ((now_us - last_update_us) / 1000) : -1;

        if (!valid || age_ms < 0 || age_ms > TPMS_STALE_TIMEOUT_MS)
        {
            // Only log to serial if no WebSocket client connected
            if (!can_data_router_has_client())
            {
                ESP_LOGW(TAG, "No recent TPMS frames (age=%lld ms)", (long long)age_ms);
            }
            continue;
        }

        // Send TPMS data to WebSocket clients
        can_data_router_send_tpms(&pressures, now_us);

        // Only log to serial if no WebSocket client connected
        if (!can_data_router_has_client())
        {
            ESP_LOGI(TAG, "TPMS: FL=%.1f FR=%.1f RL=%.1f RR=%.1f PSI",
                     pressures.front_left_psi,
                     pressures.front_right_psi,
                     pressures.rear_left_psi,
                     pressures.rear_right_psi);
        }
    }
}

/* --------------------- Main Application ---------------------------------- */

void app_main(void)
{
    ESP_LOGI(TAG, "4Runner CAN Bus - TPMS Passive Decoder with WiFi");
    ESP_LOGI(TAG, "TX GPIO: %d, RX GPIO: %d", TX_GPIO_NUM, RX_GPIO_NUM);

    // Create TPMS mutex
    tpms_mutex = xSemaphoreCreateMutex();
    if (tpms_mutex == NULL)
    {
        ESP_LOGE(TAG, "Failed to create TPMS mutex");
        return;
    }

    // Initialize CAN data router
    if (can_data_router_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize CAN data router");
        return;
    }

    // Initialize WiFi AP
    if (wifi_ap_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to initialize WiFi AP");
        return;
    }

    ESP_LOGI(TAG, "WiFi AP started: %s", wifi_ap_get_ip());
    ESP_LOGI(TAG, "Connect to http://%s.local or http://%s",
             wifi_ap_get_hostname(), wifi_ap_get_ip());

    // Start web server
    if (web_server_start() != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start web server");
        return;
    }

    // Install and start TWAI driver
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "TWAI driver installed");

    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "TWAI driver started");

    // Create tasks
    xTaskCreatePinnedToCore(can_receive_task, "CAN_RX", 4096, NULL,
                            RX_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(tpms_status_task, "TPMS_STATUS", 3072, NULL,
                            STATUS_TASK_PRIO, NULL, tskNO_AFFINITY);

    ESP_LOGI(TAG, "System initialized successfully");
}
