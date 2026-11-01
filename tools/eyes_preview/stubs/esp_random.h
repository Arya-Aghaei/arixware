#pragma once
#include <stdlib.h>
#include <stdint.h>

/* Host preview stub: real esp_random() is a HWRNG; rand() is fine for a visual check. */
static inline uint32_t esp_random(void)
{
    return (uint32_t)rand();
}
