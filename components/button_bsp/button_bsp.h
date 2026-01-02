#ifndef BUTTON_BSP_H
#define BUTTON_BSP_H

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>

#ifdef __cplusplus
extern "C" {
#endif

extern EventGroupHandle_t key_groups;

void button_Init(void);

#ifdef __cplusplus
}
#endif

#endif
