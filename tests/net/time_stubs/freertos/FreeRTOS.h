#pragma once
#include <stdint.h>
typedef uint32_t TickType_t;
#define portMAX_DELAY UINT32_MAX
#define pdTRUE 1
#define pdPASS 1
#define configTICK_RATE_HZ 1000u
#define pdMS_TO_TICKS(ms) ((TickType_t)(((TickType_t)(ms)*(TickType_t)configTICK_RATE_HZ)/1000u))
