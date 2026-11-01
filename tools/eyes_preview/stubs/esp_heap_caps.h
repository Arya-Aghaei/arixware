#pragma once
#include <stdlib.h>
#include <stdint.h>

/* Host preview stub: PSRAM capability flags are meaningless off-device. */
#define MALLOC_CAP_SPIRAM 0u
#define MALLOC_CAP_8BIT   0u

static inline void *heap_caps_malloc(size_t size, uint32_t caps)
{
    (void)caps;
    return malloc(size);
}

static inline void heap_caps_free(void *ptr)
{
    free(ptr);
}
