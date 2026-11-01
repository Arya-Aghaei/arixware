#pragma once
#include <stdio.h>
#define ESP_LOGW(tag, ...) do { fprintf(stderr, "%s: ", tag); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#define ESP_LOGI ESP_LOGW
