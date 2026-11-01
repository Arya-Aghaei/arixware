#pragma once
#include "FreeRTOS.h"
typedef void *TaskHandle_t;
uint32_t ulTaskNotifyTake(int, TickType_t);
void vTaskDelay(TickType_t);
int xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, TaskHandle_t *);
void xTaskNotifyGive(TaskHandle_t);
