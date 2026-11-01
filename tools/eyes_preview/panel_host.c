/* Host stand-in for the device's direct panel access: blits land in an in-memory framebuffer. */
#include "eyes_panel.h"
#include "panel_host.h"
#include <stdlib.h>
#include <string.h>

static uint16_t s_fb[PANEL_HOST_W * PANEL_HOST_H];
static unsigned long s_blit_px;

bool eyes_panel_begin(void)
{
    memset(s_fb, 0, sizeof(s_fb));
    return true;
}

void eyes_panel_end(void)
{
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
    if (x0 < 0 || y0 < 0 || x1 > PANEL_HOST_W || y1 > PANEL_HOST_H || x0 >= x1 || y0 >= y1 ||
        (x0 | y0 | x1 | y1) & 1) {
        abort(); /* the real panel rejects odd or out-of-range areas */
    }
    const uint16_t *src = buf;
    for (int y = y0; y < y1; y++) {
        memcpy(&s_fb[y * PANEL_HOST_W + x0], src, (size_t)(x1 - x0) * sizeof(uint16_t));
        src += x1 - x0;
    }
    s_blit_px += (unsigned long)(x1 - x0) * (unsigned long)(y1 - y0);
    return true;
}

void eyes_panel_run_async(void (*fn)(void *arg), void *arg)
{
    fn(arg);
}

void eyes_panel_join(void)
{
}

const uint16_t *panel_host_fb(void)
{
    return s_fb;
}

unsigned long panel_host_blit_pixels(void)
{
    return s_blit_px;
}
