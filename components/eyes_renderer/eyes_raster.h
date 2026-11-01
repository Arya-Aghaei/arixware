#pragma once
#include <stdint.h>

/* Software rasterizer for one eye: anti-aliased signed-distance shapes rendered straight into a
 * panel-ready sprite (RGB565, byte-swapped, already rotated). No LVGL dependency. */

typedef enum {
    EYES_SHAPE_CAPSULE,  /* rounded rect; vertical (eye) or horizontal (flat/blank) */
    EYES_SHAPE_ARCH,     /* broad dome with a shallow concave underside; negative dir flips it */
    EYES_SHAPE_CHEVRON,  /* angle-bracket "<" / ">" shape */
    EYES_SHAPE_CRESCENT, /* circle with a bite taken out (half-lidded / wink) */
    EYES_SHAPE_SPIRAL,   /* orb with a hypnotic spiral (dizzy); dir = spin direction */
    EYES_SHAPE_HALF,     /* half-lidded orb; dir = 0 flat, positive top falls to the right */
    EYES_SHAPE_SLOPE,    /* strongly sloped lid; negative dir mirrors the positive lid */
    EYES_SHAPE_HEART,
    EYES_SHAPE_WORRIED,  /* two joined circles, the right one twice the left one's area */
    EYES_SHAPE_SLEEP,    /* closed almond lens */
    EYES_SHAPE_OVAL,     /* ellipse with continuously curved sides */
} eyes_shape_t;

typedef enum {
    EYES_ACCENT_NONE,
    EYES_ACCENT_RAYS,
    EYES_ACCENT_SPARKLES,
    EYES_ACCENT_SIDE_RAYS,
    EYES_ACCENT_STAR,
    EYES_ACCENT_QUESTION,
    EYES_ACCENT_HEARTS,
    EYES_ACCENT_BLUSH,
    EYES_ACCENT_NERVOUS,
    EYES_ACCENT_DROP,
    EYES_ACCENT_ZZZ,
    EYES_ACCENT_CLOUDS,
} eyes_accent_t;

typedef struct {
    eyes_shape_t shape;
    int8_t dir;         /* shape-specific orientation (arch up/down, chevron side, crescent bite side) */
    int32_t w, h;       /* drawn size in px, after stretch and lid */
    float stroke_basis; /* EYE_H * desc.h: arch/chevron stroke scale */
    float squash;       /* expression-swap squash, 1 = fully open */
    int32_t cy;         /* shape center offset below the eye center (blink lid drop) */
    float ticks;        /* accent tick opacity, 0..1 */
    float spin;         /* spiral phase, 0..1 (one arm period); spiral only */
    float stripes;      /* spiral stripe opacity, 0..1; spiral only */
    eyes_accent_t accent;
    int8_t side;        /* -1 left eye, +1 right eye: decorations stay on the outer side */
    float accent_opacity; /* 0..1; zero leaves the undecorated rendering unchanged */
} eyes_raster_pose_t;

typedef struct {
    uint16_t *px;   /* row-major, stride = w */
    int32_t cap_w;  /* allocated width/height */
    int32_t cap_h;
    int32_t w, h;   /* used size */
    int32_t ox, oy; /* panel-space offset of px[0] from the eye's center pixel */
} eyes_sprite_t;

/* Size the sprite for this pose will have (for sizing caches); safe to call while a render runs. */
void eyes_raster_bounds(const eyes_raster_pose_t *pose, int rotation, int32_t *w, int32_t *h);

/* rotation: 0/90/180/270 clockwise, matching an LVGL transform_rotation of the same angle.
 * Not reentrant (shared tables): one render at a time. */
void eyes_raster_render(const eyes_raster_pose_t *pose, int rotation, eyes_sprite_t *sprite);
