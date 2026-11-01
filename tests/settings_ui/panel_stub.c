/* Host stand-in for direct panel access: blits land in an in-memory, byte-swapped framebuffer. */
#include "eyes_panel.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

uint16_t g_panel_fb[466 * 466];
int g_panel_owners;
unsigned long g_panel_blits;

bool eyes_panel_begin(void)
{
    g_panel_owners++;
    return true;
}

void eyes_panel_end(void)
{
    assert(g_panel_owners > 0);
    g_panel_owners--;
}

void *eyes_panel_alloc_dma(size_t size)
{
    return malloc(size);
}

void eyes_panel_free_dma(void *buf)
{
    free(buf);
}

bool eyes_panel_blit(int x0, int y0, int x1, int y1, const void *buf, bool wait)
{
    (void)wait;
    /* The real panel rejects odd or out-of-range areas, and only an owner may draw. */
    assert(g_panel_owners > 0);
    assert(x0 >= 0 && y0 >= 0 && x1 <= 466 && y1 <= 466 && x0 < x1 && y0 < y1);
    assert(((x0 | y0 | x1 | y1) & 1) == 0);
    const uint16_t *src = buf;
    for (int y = y0; y < y1; y++) {
        memcpy(&g_panel_fb[y * 466 + x0], src, (size_t)(x1 - x0) * sizeof(uint16_t));
        src += x1 - x0;
    }
    g_panel_blits++;
    return true;
}

void eyes_panel_run_async(void (*fn)(void *arg), void *arg)
{
    fn(arg);
}

void eyes_panel_join(void)
{
}
