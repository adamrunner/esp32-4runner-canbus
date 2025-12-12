/*
 * SPDX-FileCopyrightText: 2010-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/*
 * The following example demonstrates a Listen Only node in a TWAI network. The
 * Listen Only node will not take part in any TWAI bus activity (no acknowledgments
 * and no error frames). This example will execute multiple iterations, with each
 * iteration the Listen Only node will do the following:
 * 1) Listen for ping and ping response
 * 2) Listen for start command
 * 3) Listen for data messages
 * 4) Listen for stop and stop response
 */
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"

/* --------------------- Definitions and static variables ------------------ */
//Example Configuration
#define RX_TASK_PRIO                    5  // Lowered priority to allow idle task to run
#define TX_GPIO_NUM                     15  // TJA1050 TXD
#define RX_GPIO_NUM                     16  // TJA1050 RXD
#define EXAMPLE_TAG                     "TWAI Listen Only"

static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_500KBITS();
//Set TX queue length to 0 due to listen only mode
static const twai_general_config_t g_config = {.mode = TWAI_MODE_LISTEN_ONLY,
                                               .tx_io = TX_GPIO_NUM, .rx_io = RX_GPIO_NUM,
                                               .clkout_io = TWAI_IO_UNUSED, .bus_off_io = TWAI_IO_UNUSED,
                                               .tx_queue_len = 0, .rx_queue_len = 20,
                                               .alerts_enabled = TWAI_ALERT_NONE,
                                               .clkout_divider = 0
                                              };

static SemaphoreHandle_t rx_sem;

/* --------------------------- Tasks and Functions -------------------------- */

static void twai_receive_task(void *arg)
{
    xSemaphoreTake(rx_sem, portMAX_DELAY);

    uint32_t msg_count = 0;
    while (1) {
        twai_message_t rx_msg;
        twai_receive(&rx_msg, portMAX_DELAY);

        // Print CAN message in readable format
        ESP_LOGI(EXAMPLE_TAG, "ID: 0x%03lX DLC: %d Data: %02X %02X %02X %02X %02X %02X %02X %02X",
                 rx_msg.identifier,
                 rx_msg.data_length_code,
                 rx_msg.data[0], rx_msg.data[1], rx_msg.data[2], rx_msg.data[3],
                 rx_msg.data[4], rx_msg.data[5], rx_msg.data[6], rx_msg.data[7]);

        // Yield every 10 messages to allow idle task to run
        if (++msg_count % 10 == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));  // Small delay to ensure idle task runs
        }
    }

    xSemaphoreGive(rx_sem);
    vTaskDelete(NULL);
}

void app_main(void)
{
    rx_sem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(twai_receive_task, "TWAI_rx", 4096, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);  // Pin to CPU 1

    //Install and start TWAI driver
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(EXAMPLE_TAG, "Driver installed");
    ESP_ERROR_CHECK(twai_start());
    ESP_LOGI(EXAMPLE_TAG, "Driver started");

    xSemaphoreGive(rx_sem);                     //Start RX task

    // Task runs continuously, no need to wait for completion
}
