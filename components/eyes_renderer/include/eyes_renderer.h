#pragma once
#include <stdbool.h>
#include "lvgl.h"
#include <stdint.h>
#include "eyes_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Swappable eye-style interface; future uploaded eye packs implement the same shape. */
typedef struct {
    void (*create)(lv_obj_t *parent);
    void (*destroy)(void);
    /** Optional: react to the device being picked up/handled. May be NULL. */
    void (*on_pickup)(void);
    /** Optional: receive filtered acceleration and shake state. May be NULL. */
    void (*on_shake)(float accel_x, float accel_y, bool active);
    /** Optional: select a built-in eyes_expression_t value. */
    void (*set_expression)(uint8_t expression);
    /** Optional: pause/resume animation for display standby. May be NULL. */
    void (*on_standby)(bool standby);
    /** Optional: cross-fade the panel from the eyes to a full-screen image, then call done(ctx)
     *  (from an LVGL timer; the renderer stays alive until destroy()). image is native RGB565,
     *  466x466, already in panel orientation, and must stay valid until done. May be NULL. */
    void (*fade_out_to)(const uint16_t *image, uint32_t duration_ms, void (*done)(void *ctx), void *ctx);
    /** Optional: the eyes_expression_t currently shown (or being shown). May be NULL. */
    uint8_t (*get_expression)(void);
} eyes_renderer_api_t;

/** Built-in procedural "arix glow" eyes, registered as the default MVP style. */
const eyes_renderer_api_t *eyes_renderer_get_default(void);

#ifdef __cplusplus
}
#endif
