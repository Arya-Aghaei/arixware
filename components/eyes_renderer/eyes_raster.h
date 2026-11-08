#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "include/eyes_theme.h"

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
    EYES_SHAPE_SLEEP,    /* closed eye: a glowing neon smile-curve */
    EYES_SHAPE_OVAL,     /* ellipse with continuously curved sides */
    EYES_SHAPE_EGG,      /* tilted egg whose pointed end falls to the outer side; dir -1 left eye, +1 right */
    EYES_SHAPE_LENS,     /* thin almond lens, the blink */
    EYES_SHAPE_GLASS,    /* Glass style: a glassy rounded eye with a pupil under two black eyelids */
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
    EYES_ACCENT_TOP_RAYS, /* three rays above the gap between the eyes; the left eye draws them */
    EYES_ACCENT_ELLIPSIS,   /* three dots above the eye: thinking */
    EYES_ACCENT_BULB,       /* a light bulb: realization */
    EYES_ACCENT_TARGET,     /* a crosshair over the eye */
    EYES_ACCENT_ANGER_MARK, /* the comic anger cross */
    EYES_ACCENT_TEARS,      /* two tears running from the eye */
    EYES_ACCENT_STRESS,     /* short stress strokes above the eye */
    EYES_ACCENT_EYEBAGS,    /* two arcs under the eye */
    EYES_ACCENT_TONGUE,     /* a tongue under the eye */
    EYES_ACCENT_GLITCH,     /* displaced horizontal bars across the eye */
} eyes_accent_t;

/* Glass style eye: a tall rounded body, an optional pupil and two eyelids. Positions are fractions of
 * the half size; "toward the nose" is + for x. */
typedef struct {
    float w, h;                              /* full size as a fraction of the base eye */
    float corner;                            /* body corner radius, fraction of the half width */
    float top, top_slope, top_bow;           /* upper lid: cover 0..1, tilt (+ lowers it toward the nose), bow (+ sags the middle) */
    float bot, bot_slope, bot_bow;           /* lower lid: cover, tilt, bow (+ lifts the middle) */
    float pupil, pupil_x, pupil_y;           /* pupil radius (fraction of half width, 0 = none) and offset */
    float round_top;                         /* upper corner roundness as a fraction of the half height: 0 leaves the body open to the lid line, 1 is a dome */
    float lid_gap;                           /* floating lid line above the body, fraction of the half width; 0 = no line */
    float lid_len;                           /* lid line half length, fraction of the half width (0 = 1.18) */
    int8_t pupil_kind;                       /* 0 disc, 1 heart, 2 rings */
} eyes_glass_t;

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
    bool draft;           /* shade in 2x2 pixel blocks (~4x faster) for frames that are about to be replaced */
    eyes_glass_t glass;   /* EYES_SHAPE_GLASS only; dir > 0 means the nose is toward +x */
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

/* Colors for every render from now on. Takes effect at the start of the next render, so it is safe
 * to call while the helper core rasterizes. Without a call the original amber theme is used. */
void eyes_raster_set_theme(const eyes_theme_t *theme);

/* Eye style: 0 = round orbs, 1 = glass (tall glassy eyes with pupils and eyelids). Takes effect on the
 * next render; repaint the eyes on screen to see it. */
#define EYES_STYLE_ORB 0
#define EYES_STYLE_GLASS 1
void eyes_raster_set_style(int style);
int eyes_raster_get_style(void);
