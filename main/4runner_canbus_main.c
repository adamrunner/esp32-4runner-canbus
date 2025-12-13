/*
 * 4Runner CAN Bus - Tire Pressure Monitor
 *
 * Reads tire pressure values from the 2018 Toyota 4Runner TPMS ECU
 * via UDS (Unified Diagnostic Services) over CAN bus.
 *
 * Hardware: ESP32 with TJA1050 CAN transceiver
 * CAN Speed: 500 kbps
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"

/* --------------------- Definitions and static variables ------------------ */
#define TAG                     "4Runner CAN"
#define TX_GPIO_NUM             15  // TJA1050 TXD
#define RX_GPIO_NUM             16  // TJA1050 RXD

// TPMS ECU addresses
#define TPMS_REQUEST_ID         0x750
#define TPMS_RESPONSE_ID        0x758

// UDS Service IDs
#define UDS_READ_DATA_BY_ID     0x21
#define UDS_POSITIVE_RESPONSE   0x61
#define TPMS_PID                0x30

// Polling interval (400ms = 2.5 Hz)
#define TPMS_POLL_INTERVAL_MS   400

// Task priorities
#define TPMS_TASK_PRIO          5
#define RX_TASK_PRIO            4
#define STATUS_TASK_PRIO        3

// Debug settings
#define DEBUG_MODE              1  // Set to 1 for debug (log all CAN), 0 for TPMS only

/* --------------------- Tire Pressure Data Structure ---------------------- */
typedef struct {
    float front_left_psi;
    float front_right_psi;
    float rear_left_psi;
    float rear_right_psi;
    float spare_psi;
    bool valid;
} tire_pressure_t;

static tire_pressure_t tire_pressure = {0};
static SemaphoreHandle_t tpms_mutex;

/* --------------------- TWAI Configuration -------------------------------- */
static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
static const twai_general_config_t g_config = {
    .mode = TWAI_MODE_NORMAL,
    .tx_io = TX_GPIO_NUM,
    .rx_io = RX_GPIO_NUM,
    .clkout_io = TWAI_IO_UNUSED,
    .bus_off_io = TWAI_IO_UNUSED,
    .tx_queue_len = 5,
    .rx_queue_len = 20,
    .alerts_enabled = TWAI_ALERT_NONE,
    .clkout_divider = 0
};

/* --------------------- UDS Request Messages ------------------------------- */
// UDS Start Diagnostic Session (service 0x10, default session 0x01)
// Format: [extended_addr][length][service][session][padding...]
static const twai_message_t diag_session_request = {
    .identifier = TPMS_REQUEST_ID,
    .data_length_code = 8,
    .data = {0x2A, 0x02, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00}
};

// UDS request: Service 0x21, PID 0x30 with extended address
// Format: [extended_addr][length][service][pid][padding...]
static const twai_message_t tpms_request = {
    .identifier = TPMS_REQUEST_ID,
    .data_length_code = 8,
    .data = {0x2A, 0x03, UDS_READ_DATA_BY_ID, TPMS_PID, 0x00, 0x00, 0x00, 0x00}
};

/* --------------------- Helper Functions ---------------------------------- */

/**
 * Convert raw tire pressure byte to PSI
 * Formula from OBDb: (raw / 58.0) - 0.5 = bars, then convert to PSI
 */
static float raw_to_psi(uint8_t raw) {
    float bars = (raw / 58.0f) - 0.5f;
    if (bars < 0) bars = 0;  // Clamp negative values
    return bars * 14.5038f;  // Convert bars to PSI
}

/**
 * Parse TPMS response and update tire pressure values
 * Response format: [ext_addr][length][0x61][0x30][FL][FR][RL][RR][SPARE]
 */
static bool parse_tpms_response(const twai_message_t *msg) {
    // Verify response ID
    if (msg->identifier != TPMS_RESPONSE_ID) {
        return false;
    }

    // Verify extended address and positive response
    // data[0] = 0x2A (extended address)
    // data[1] = length
    // data[2] = 0x61 (positive response to 0x21)
    // data[3] = 0x30 (PID)
    if (msg->data[0] != 0x2A || msg->data[2] != UDS_POSITIVE_RESPONSE || msg->data[3] != TPMS_PID) {
        return false;
    }

    // Extract tire pressure values (bytes 4-8)
    xSemaphoreTake(tpms_mutex, portMAX_DELAY);
    tire_pressure.front_left_psi = raw_to_psi(msg->data[4]);
    tire_pressure.front_right_psi = raw_to_psi(msg->data[5]);
    tire_pressure.rear_left_psi = raw_to_psi(msg->data[6]);
    tire_pressure.rear_right_psi = raw_to_psi(msg->data[7]);

    // Spare tire might be in next message or not present
    if (msg->data_length_code >= 8) {
        tire_pressure.spare_psi = 0;  // Not in this frame
    }
    tire_pressure.valid = true;
    xSemaphoreGive(tpms_mutex);

    return true;
}

/* --------------------- Tasks --------------------------------------------- */

/**
 * General CAN receive task
 * Logs all CAN messages for debugging
 */
static void can_receive_task(void *arg) {
    twai_message_t rx_msg;
    uint32_t msg_count = 0;
    uint32_t tx_count = 0;
    TickType_t last_tx_time = 0;

    ESP_LOGI(TAG, "CAN receive task started - logging all messages");

    while (1) {
        // In debug mode, send diagnostic session + TPMS request every 2 seconds
        if ((xTaskGetTickCount() - last_tx_time) >= pdMS_TO_TICKS(2000)) {
            // First, start diagnostic session
            esp_err_t err = twai_transmit(&diag_session_request, pdMS_TO_TICKS(100));
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "TX[%lu] Diagnostic session 0x750 [2A 02 10 01]", tx_count);
            } else {
                ESP_LOGW(TAG, "TX diag session failed: %s", esp_err_to_name(err));
            }

            // Wait 50ms for session response
            vTaskDelay(pdMS_TO_TICKS(50));

            // Then send TPMS request
            err = twai_transmit(&tpms_request, pdMS_TO_TICKS(100));
            if (err == ESP_OK) {
                tx_count++;
                ESP_LOGI(TAG, "TX[%lu] TPMS request 0x750 [2A 03 21 30]", tx_count);
            } else {
                ESP_LOGW(TAG, "TX TPMS failed: %s", esp_err_to_name(err));
            }
            last_tx_time = xTaskGetTickCount();
        }

        if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
            // Highlight TPMS-related messages
            if (rx_msg.identifier == TPMS_RESPONSE_ID || rx_msg.identifier == TPMS_REQUEST_ID) {
                ESP_LOGW(TAG, ">>> TPMS ID:0x%03lX DLC:%d Data:%02X %02X %02X %02X %02X %02X %02X %02X",
                         rx_msg.identifier,
                         rx_msg.data_length_code,
                         rx_msg.data[0], rx_msg.data[1], rx_msg.data[2], rx_msg.data[3],
                         rx_msg.data[4], rx_msg.data[5], rx_msg.data[6], rx_msg.data[7]);

                // Check for negative response (service 0x7F)
                if (rx_msg.data[0] == 0x2A && rx_msg.data[2] == 0x7F) {
                    ESP_LOGE(TAG, "!!! Negative response: Service=0x%02X NRC=0x%02X",
                             rx_msg.data[3], rx_msg.data[4]);
                }
            } else {
                ESP_LOGI(TAG, "RX ID:0x%03lX DLC:%d Data:%02X %02X %02X %02X %02X %02X %02X %02X",
                         rx_msg.identifier,
                         rx_msg.data_length_code,
                         rx_msg.data[0], rx_msg.data[1], rx_msg.data[2], rx_msg.data[3],
                         rx_msg.data[4], rx_msg.data[5], rx_msg.data[6], rx_msg.data[7]);
            }
            msg_count++;

            // Log message count every 100 messages
            if (msg_count % 100 == 0) {
                ESP_LOGI(TAG, "Received %lu CAN messages", msg_count);
            }
        }

        // Yield every iteration to let other tasks run
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/**
 * TPMS polling task
 * Sends UDS request and waits for response, then logs tire pressures
 */
static void tpms_task(void *arg) {
    twai_message_t rx_msg;
    TickType_t last_request_time = 0;

    ESP_LOGI(TAG, "TPMS task started");

    while (1) {
        // Send TPMS request
        esp_err_t err = twai_transmit(&tpms_request, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to send TPMS request: %s", esp_err_to_name(err));
        }

        last_request_time = xTaskGetTickCount();

        // Wait for response (with timeout)
        while ((xTaskGetTickCount() - last_request_time) < pdMS_TO_TICKS(100)) {
            if (twai_receive(&rx_msg, pdMS_TO_TICKS(50)) == ESP_OK) {
                if (parse_tpms_response(&rx_msg)) {
                    // Successfully parsed TPMS response
                    xSemaphoreTake(tpms_mutex, portMAX_DELAY);
                    ESP_LOGI(TAG, "TPMS: FL=%.1f FR=%.1f RL=%.1f RR=%.1f SPARE=%.1f PSI",
                             tire_pressure.front_left_psi,
                             tire_pressure.front_right_psi,
                             tire_pressure.rear_left_psi,
                             tire_pressure.rear_right_psi,
                             tire_pressure.spare_psi);
                    xSemaphoreGive(tpms_mutex);
                    break;
                }
            }
        }

        // Wait for next polling interval
        vTaskDelayUntil(&last_request_time, pdMS_TO_TICKS(TPMS_POLL_INTERVAL_MS));
    }
}

/* --------------------- Main Application ---------------------------------- */

void app_main(void) {
    ESP_LOGI(TAG, "4Runner CAN Bus - Tire Pressure Monitor");
    ESP_LOGI(TAG, "TX GPIO: %d, RX GPIO: %d", TX_GPIO_NUM, RX_GPIO_NUM);

    // Create mutex for tire pressure data
    tpms_mutex = xSemaphoreCreateMutex();
    if (tpms_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    // Install and start TWAI driver
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(TAG, "TWAI driver installed");

    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(TAG, "TWAI driver started");

#if DEBUG_MODE
    // Debug mode: log all CAN traffic
    ESP_LOGW(TAG, "*** DEBUG MODE: Logging all CAN messages ***");
    xTaskCreatePinnedToCore(can_receive_task, "CAN_RX", 4096, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);
#else
    // Normal mode: TPMS polling
    xTaskCreatePinnedToCore(tpms_task, "TPMS", 4096, NULL, TPMS_TASK_PRIO, NULL, tskNO_AFFINITY);
    ESP_LOGI(TAG, "Polling TPMS ECU at %.1f Hz...", 1000.0f / TPMS_POLL_INTERVAL_MS);
#endif
}
