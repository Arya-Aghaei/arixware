#include "eyes_renderer.h"
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "ui_orient.h"
#include "audio.h"
#include "mmap_generate_sounds.h"
#include "eyes_raster.h"
#include "eyes_panel.h"
#include "eyes_dream.h"
#include "esp_log.h"


static const char *TAG = "eyes";

/* Set to 1 only for the sleep/dream visual test; normal idle behavior is the default. */
#ifndef EYES_SLEEP_DREAM_TEST
#define EYES_SLEEP_DREAM_TEST 0
#endif

#define EYE_W 170
#define EYE_H 170
#define EYE_GAP 20
#define FACE_SIZE 466
/* Eyes are rasterized into per-eye sprites (largest pose incl. glow/ticks/overshoot fits
 * well inside 256x256) and streamed to the panel in strips of DMA-capable internal RAM. */
#define SPRITE_CAP 256
#define STRIP_ROWS 22 /* full-width rows per DMA strip buffer */
#define STRIP_PIXELS (FACE_SIZE * STRIP_ROWS)
#ifdef EYES_PREVIEW_NO_ROTATE
#define EYES_ROTATION 0
#else
#define EYES_ROTATION UI_ROTATION_DEG
#endif

/* All motion is driven by one frame clock (frame_timer_cb) that integrates springs and
 * time-based curves, re-rasterizes an eye only when its shape changed, and streams just the
 * changed panel area. Pure gaze/bounce motion only moves the existing sprites. */
#define FRAME_TIMER_MS 16
#define FRAME_MAX_DT_S 0.05f
#define SPRING_STEP_S 0.004f

/* Lid accelerates shut (ease-in), opens more slowly (ease-out), like a real blink. */
#define BLINK_CLOSE_MS 95
#define BLINK_HOLD_MS 35
#define BLINK_OPEN_MS 170
#define BLINK_MIN_FACTOR 0.06f
#define BLINK_STRETCH 0.06f /* eye widens slightly as it closes (squash & stretch) */
#define BLINK_LID_DROP 0.2f /* closing eye sinks a little: the upper lid travels further */
#define DOUBLE_BLINK_ODDS 5 /* 1 in N blinks is a quick double blink */
#define DOUBLE_BLINK_GAP_MS 70
#define IDLE_MIN_MS 1200
#define IDLE_MAX_MS 3200
#define IDLE_BLINK_ODDS 3
typedef enum { IDLE_AWAKE, IDLE_BORED, IDLE_DROWSY, IDLE_NAPPING, IDLE_WAKING } idle_mood_t;
#define SACCADE_RANGE_X 8
#define SACCADE_RANGE_Y 6
/* Gaze is a slightly under-damped spring whose settle time scales with jump distance
 * (short glances are quick flicks, long ones sweep), so retargeting keeps velocity. */
#define SACCADE_MIN_DIST 3.0f /* sub-3px hops read as glitches, not glances */
#define SACCADE_BASE_MS 110
#define SACCADE_MS_PER_PX 9
#define SACCADE_MAX_MS 320
#define SACCADE_ZETA 0.78f
#define SACCADE_BLINK_DIST 9.0f /* large gaze shifts sometimes carry a blink */
#define SACCADE_BLINK_ODDS 3

/* Expression changes: same shape -> parameter morph; different shape -> squash the old
 * shape to a slit, swap at the slit, then spring the new one open with a little overshoot. */
#define EXPRESSION_MORPH_MS 380
#define SQUASH_CLOSE_OMEGA 34.0f
#define SQUASH_CLOSE_ZETA 1.0f
#define SQUASH_OPEN_OMEGA 22.0f
#define SQUASH_OPEN_ZETA 0.6f
#define SQUASH_SWAP_AT 0.12f
#define SQUASH_STRETCH 0.1f
#define TICKS_FADE_TAU_S 0.08f

/* Dizzy spiral: spins fast while shaken, keeps spinning and winds down after, then the eyes
 * settle back to neutral. Phase is in spiral arm periods and quantized so frames repeat. */
#define SPIN_SHAKE_SPEED 2.4f /* arm periods per second while shaken */
#define SPIN_IDLE_SPEED 0.35f /* floor while dizzy (also the steady dizzy expression) */
#define SPIN_DECAY_TAU_S 0.9f
#define SPIN_LINGER_MS 2200
#define SPIN_PHASES 32
#define SURPRISED_FADE_TAU_S 0.12f

/* Pickup reaction: alternate curious glances, collapsing the trailing eye inward. */
#define EXPRESSION_SURPRISED EYES_EXPRESSION_SURPRISED
#define EXPRESSION_DIZZY EYES_EXPRESSION_HYPNOTIZED
#define SURPRISED_DURATION_MS 4000
#define SURPRISED_IDLE_MIN_MS 400
#define SURPRISED_IDLE_MAX_MS 700
#define SURPRISED_SACCADE_RANGE_X 18
#define SURPRISED_SACCADE_RANGE_Y 8
#define SURPRISED_MAX_COLLAPSE 0.55f /* a squint, not a full wink */
#define EYE_MIN_GAP 16.0f            /* must stay <= EYE_GAP or eyes can never settle to rest */
#define BOUNCE_STEP_S 0.008f
#define BOUNCE_ACTIVE_SPRING 0.4f
#define BOUNCE_ACTIVE_DAMPING 2.4f
#define BOUNCE_HOME_SPRING 20.0f
#define BOUNCE_HOME_DAMPING 8.8f
#define BOUNCE_RESTITUTION 0.6f
#define BOUNCE_MAX_SPEED 450.0f
#define DISPLAY_SAFE_RADIUS 208.0f
#define SHAKE_ACCEL_DEADBAND 0.35f
#define SHAKE_FORCE_SCALE 1700.0f
#define SHAKE_FORCE_LIMIT 12000.0f
#define SHAKE_FORCE_DEADBAND 0.5f

typedef struct
{
    eyes_shape_t shape;
    float w;        /* fraction of EYE_W */
    float h;        /* fraction of EYE_H */
    float openness; /* 0..1, blink multiplies into this for CAPSULE/CHEVRON only */
    int8_t dir;     /* shape-specific orientation (arch up/down, chevron point side, crescent bite side) */
    eyes_accent_t accent;
    eyes_glass_t glass; /* the Glass style's version of this eye: lids, pupil, size */
} eye_desc_t;

typedef struct
{
    eye_desc_t left;
    eye_desc_t right;
} expression_t;

/* The tables for every pack are generated from the JSON packs in eyes/ by tools/eyepack/eyepack.py. */
typedef struct { eyes_glass_t l, r; } glass_pair_t;
/* clang-format off */
#include "generated/eye_packs.inc"
/* clang-format on */

#define EXPRESSION_NEUTRAL EYES_EXPRESSION_DEFAULT
#define EXPRESSION_COUNT (sizeof(EXPRESSIONS) / sizeof(EXPRESSIONS[0]))
_Static_assert(EXPRESSION_COUNT == EYES_EXPRESSION_COUNT, "Expression chart and API must agree");

_Static_assert(sizeof(GLASS) / sizeof(GLASS[0]) == EYES_EXPRESSION_COUNT, "One glass pair per expression");
_Static_assert(sizeof(GLASS_ACCENT) / sizeof(GLASS_ACCENT[0]) == EYES_EXPRESSION_COUNT, "One glass accent pair per expression");

/* One sound per expression, in catalog order. The original 30 moods have a clip each; the moods added for
 * the Square pack reuse the closest existing clip. */
static const int EXPRESSION_SOUND[] = {
    MMAP_SOUNDS_01_NEUTRAL_ACK_WAV, /* Default */
    MMAP_SOUNDS_03_HAPPY_WAV,       /* Happy */
    MMAP_SOUNDS_04_EXCITED_WAV,     /* Excited */
    MMAP_SOUNDS_14_DELIGHTED_WAV,   /* Pleased */
    MMAP_SOUNDS_11_SMILING_WAV,     /* Smile */
    MMAP_SOUNDS_10_STARRY_WAV,      /* Big Smile */
    MMAP_SOUNDS_21_SQUEEZING_WAV,   /* Content */
    MMAP_SOUNDS_24_UNIMPRESSED_WAV, /* Relaxed */
    -1,                             /* Blink: a held blink should not play an unrelated sound. */
    MMAP_SOUNDS_12_WINKING_WAV,     /* Wink (L) */
    MMAP_SOUNDS_23_PLAYFUL_WINK_WAV, /* Wink (R) */
    MMAP_SOUNDS_02_CURIOUS_WAV,     /* Curious */
    MMAP_SOUNDS_16_SURPRISED_WAV,   /* Surprised */
    MMAP_SOUNDS_30_AMAZED_WAV,      /* Amazed */
    MMAP_SOUNDS_17_LOVE_WAV,        /* Love */
    MMAP_SOUNDS_08_SIDE_GLANCE_WAV, /* Blushing */
    MMAP_SOUNDS_20_PLEADING_WAV,    /* Shy */
    MMAP_SOUNDS_27_ALERT_WAV,       /* Nervous */
    MMAP_SOUNDS_13_ANGRY_WAV,       /* Angry */
    MMAP_SOUNDS_28_DETERMINED_WAV,  /* Determined */
    MMAP_SOUNDS_29_SUSPICIOUS_WAV,  /* Suspicious */
    MMAP_SOUNDS_22_TIRED_WAV,       /* Sad */
    MMAP_SOUNDS_06_ATTENTIVE_WAV,   /* Worried */
    MMAP_SOUNDS_25_BLANK_WAV,       /* Confused */
    MMAP_SOUNDS_26_SLEEPING_WAV,    /* Sleeping */
    MMAP_SOUNDS_07_BORED_WAV,       /* Sleeping (zzz) */
    MMAP_SOUNDS_05_SLEEPY_CUTE_WAV, /* Dreamy */
    MMAP_SOUNDS_18_DIZZY_WAV,       /* Hypnotized */
    MMAP_SOUNDS_09_INTERESTED_WAV,  /* Focused */
    MMAP_SOUNDS_15_LAUGHING_WAV,    /* Laugh */
    MMAP_SOUNDS_27_ALERT_WAV,       /* Startled */
    MMAP_SOUNDS_06_ATTENTIVE_WAV,   /* Thinking */
    MMAP_SOUNDS_10_STARRY_WAV,      /* Realization */
    MMAP_SOUNDS_29_SUSPICIOUS_WAV,  /* Skeptical */
    MMAP_SOUNDS_24_UNIMPRESSED_WAV, /* Annoyed */
    MMAP_SOUNDS_20_PLEADING_WAV,    /* Crying */
    MMAP_SOUNDS_22_TIRED_WAV,       /* Disappointed */
    MMAP_SOUNDS_07_BORED_WAV,       /* Tired */
    MMAP_SOUNDS_23_PLAYFUL_WINK_WAV, /* Playful */
    MMAP_SOUNDS_18_DIZZY_WAV,       /* Glitch */
    MMAP_SOUNDS_08_SIDE_GLANCE_WAV, /* Smug */
};
_Static_assert(sizeof(EXPRESSION_SOUND) / sizeof(EXPRESSION_SOUND[0]) == EXPRESSION_COUNT,
               "Every expression needs an explicit sound mapping");

static inline void play_expression_sound(size_t expression)
{
    if (expression < EXPRESSION_COUNT && EXPRESSION_SOUND[expression] >= 0)
    {
        audio_play_index(EXPRESSION_SOUND[expression]);
    }
}

typedef struct
{
    int32_t x0, y0, x1, y1; /* panel pixels, end-exclusive */
} rect_t;

typedef struct
{
    int32_t base_x;
    float bounce_x, bounce_y;
    float velocity_x, velocity_y;
    rect_t rect; /* panel area this eye's sprite currently occupies */
} eye_render_t;

/* Everything that determines an eye's pixels (but not its position). */
typedef struct
{
    eye_desc_t d;
    float lid;     /* blink * curious squint, applied to blinkable shapes */
    float squash;  /* expression-swap squash, 1 = fully open */
    float ticks;   /* decoration opacity 0..1 */
    float spin;    /* spiral phase (quantized), spiral only */
    float stripes; /* spiral stripe opacity, spiral only */
    int8_t side;
} eye_pose_t;

static lv_obj_t *s_wrapper;
static eye_render_t s_eyes[2];
static lv_timer_t *s_idle_timer;
static lv_timer_t *s_frame_timer;
static size_t s_expression = EXPRESSION_NEUTRAL;
/* Autonomous poses never replace an explicitly requested expression. */
static bool s_idle_owned = true;
static uint32_t s_idle_since, s_idle_pose_until, s_idle_blink_at;
static float s_idle_breath;
static int s_idle_beat;
static float s_idle_direction = 1.0f;
static float s_idle_lid = 1.0f, s_idle_slouch;
static idle_mood_t s_idle_mood;
static uint32_t s_idle_mood_since, s_idle_mood_until, s_idle_detail_at, s_idle_detail_since;
static uint32_t s_idle_dream_since; /* Independent of yawn/flutter timing. */
static int s_idle_detail; /* 0: none, 1: yawn/stretch, 3: eyelid flutter */
static float s_idle_yawn, s_idle_dream, s_idle_flutter;
#if !EYES_SLEEP_DREAM_TEST
static float s_idle_wake_lid, s_idle_wake_slouch;
#endif
#if EYES_SLEEP_DREAM_TEST
static eyes_dream_variant_t s_sleep_test_variant;
#endif
static float s_blink_factor = 1.0f;
static float s_blink_ms;
static bool s_blinking;
static bool s_blink_repeat;
static float s_gaze_x, s_gaze_y;
static float s_gaze_vx, s_gaze_vy;
static float s_gaze_tx, s_gaze_ty;
static float s_surprised_phase;
static float s_gaze_omega = 20.0f;
static uint32_t s_curious_until;
static float s_spin_phase, s_spin_speed, s_spiral_amt;
static uint32_t s_dizzy_until; /* after a shake: keep the spiral until this tick */
static float s_curious_weight;
static uint32_t s_frame_tick;
static float s_shake_force_x, s_shake_force_y;
static bool s_frame_paused = true;
static bool s_bounce_live;
static bool s_shake_active;
static int32_t s_curious_direction = 1;
/* Per-eye expression state: s_morph_from/to share shape+dir and are lerped by s_morph_p;
 * s_target is the requested look, reached via a squash-swap when its shape/dir differs. */
static eye_desc_t s_morph_from[2];
static eye_desc_t s_morph_to[2];
static eye_desc_t s_target[2];
static float s_morph_p[2];
static bool s_swapping[2];
static float s_squash[2];
static float s_squash_v[2];
static float s_ticks_alpha[2];
static eye_pose_t s_drawn[2];
static bool s_drawn_draft[2]; /* the sprite on screen is a half-resolution draft */
/* Two sprite buffers per eye: the front one is on screen, the back one is rasterized for the next
 * frame on the helper core while this frame streams to the panel (shapes trail positions by one
 * frame, which is invisible). */
static eyes_sprite_t s_sprites[2][2];
static int s_front[2];
static const eyes_sprite_t *s_eye_sprite[2]; /* what each eye shows; eye 1 may show eye 0's */
static bool s_eye_mirror[2];                 /* shown left/right mirrored (eye 1 reusing eye 0's) */
/* A logical left/right mirror reverses sprite rows at 90/270 (rows are logical columns) and
 * sprite columns at 0/180. */
#define MIRROR_ROWS (EYES_ROTATION == 90 || EYES_ROTATION == 270)
typedef struct
{
    bool render[2]; /* rasterize eye i's pose into its back buffer */
    bool share;     /* eye 1 follows eye 0's new sprite (identical or mirrored poses) */
    bool share_mirror;
    bool draft[2];  /* shade at half resolution: the frame is replaced as soon as the pose settles */
    eye_pose_t pose[2];
} raster_req_t;
static raster_req_t s_req;
static bool s_req_pending; /* dispatched; result not applied yet */
static uint16_t *s_strip[2];
static int s_strip_idx;
static bool s_panel_ready;
static bool s_force_full;
/* Outgoing cross-fade (fade_out_to): eyes frozen, panel blended toward s_fade_image. */
static const uint16_t *s_fade_image;
static float s_fade_ms, s_fade_elapsed_ms;
static void (*s_fade_done)(void *ctx);
static void *s_fade_ctx;

static bool is_curious(void)
{
    return s_curious_until != 0 && lv_tick_get() < s_curious_until;
}

static int32_t rand_range(int32_t lo, int32_t hi)
{
    return lo + (int32_t)(esp_random() % (uint32_t)(hi - lo + 1));
}

static float smootherstep(float t)
{
    t = LV_CLAMP(0.0f, t, 1.0f);
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/* Exponential approach; frame-rate independent. */
static float approach(float cur, float target, float dt, float tau)
{
    float next = target + (cur - target) * expf(-dt / tau);
    return (fabsf(next - target) < 0.002f) ? target : next;
}

/* Damped spring, sub-stepped so stiff settings stay stable under frame-time jitter. */
static void spring_advance(float *x, float *v, float target, float omega, float zeta, float dt)
{
    while (dt > 0.0f)
    {
        float h = (dt < SPRING_STEP_S) ? dt : SPRING_STEP_S;
        float a = omega * omega * (target - *x) - 2.0f * zeta * omega * (*v);
        *v += a * h;
        *x += *v * h;
        dt -= h;
    }
}

static void wake_frame_timer(void)
{
    if (s_frame_timer && s_frame_paused)
    {
        s_frame_tick = lv_tick_get();
        lv_timer_resume(s_frame_timer);
        s_frame_paused = false;
    }
}

static void keep_eye_inside_display(eye_render_t *eye)
{
    /* Reserve separate lanes for the full eyes, including their glow. inner_x is set so
     * the two eyes' lanes always keep at least EYE_MIN_GAP between their near edges, by
     * construction (both lanes share the same bound, symmetric about the centerline) — no
     * separate pairwise check needed. Work in face coordinates so a concurrent gaze animation
     * cannot invalidate bounds. */
    const float half_w = EYE_W / 2.0f;
    const float half_h = EYE_H / 2.0f;
    const float inner_x = half_w + EYE_MIN_GAP / 2.0f;
    const float side = eye->base_x < 0 ? -1.0f : 1.0f;
    float x = side * (eye->base_x + s_gaze_x + eye->bounce_x);
    float y = s_gaze_y + eye->bounce_y;
    float max_y = sqrtf(DISPLAY_SAFE_RADIUS * DISPLAY_SAFE_RADIUS -
                        (inner_x + half_w) * (inner_x + half_w)) -
                  half_h;
    if (fabsf(y) > max_y)
    {
        if (y * eye->velocity_y > 0.0f)
        {
            eye->velocity_y *= -BOUNCE_RESTITUTION;
        }
        y = LV_CLAMP(-max_y, y, max_y);
    }
    float extent_y = fabsf(y) + half_h;
    float max_x = sqrtf(DISPLAY_SAFE_RADIUS * DISPLAY_SAFE_RADIUS -
                        extent_y * extent_y) -
                  half_w;
    float outward_speed = side * eye->velocity_x;
    if ((x < inner_x && outward_speed < 0.0f) ||
        (x > max_x && outward_speed > 0.0f))
    {
        eye->velocity_x *= -BOUNCE_RESTITUTION;
    }
    x = LV_CLAMP(inner_x, x, max_x);
    eye->bounce_x = side * x - eye->base_x - s_gaze_x;
    eye->bounce_y = y - s_gaze_y;
}

static void integrate_bounce_step(float dt)
{
    float spring = s_shake_active ? BOUNCE_ACTIVE_SPRING : BOUNCE_HOME_SPRING;
    float damping = s_shake_active ? BOUNCE_ACTIVE_DAMPING : BOUNCE_HOME_DAMPING;
    for (int i = 0; i < 2; i++)
    {
        eye_render_t *eye = &s_eyes[i];
        float ax = -spring * eye->bounce_x - damping * eye->velocity_x + s_shake_force_x;
        float ay = -spring * eye->bounce_y - damping * eye->velocity_y + s_shake_force_y;
        eye->velocity_x += ax * dt;
        eye->velocity_y += ay * dt;
        eye->bounce_x += eye->velocity_x * dt;
        eye->bounce_y += eye->velocity_y * dt;
        keep_eye_inside_display(eye);

        float speed = sqrtf(eye->velocity_x * eye->velocity_x + eye->velocity_y * eye->velocity_y);
        if (speed > BOUNCE_MAX_SPEED)
        {
            float speed_scale = BOUNCE_MAX_SPEED / speed;
            eye->velocity_x *= speed_scale;
            eye->velocity_y *= speed_scale;
        }
    }
}

static bool is_blinkable(const eye_desc_t *d)
{
    return d->shape == EYES_SHAPE_CAPSULE || d->shape == EYES_SHAPE_CHEVRON ||
           d->shape == EYES_SHAPE_HALF || d->shape == EYES_SHAPE_SLOPE ||
           d->shape == EYES_SHAPE_HEART || d->shape == EYES_SHAPE_WORRIED || d->shape == EYES_SHAPE_EGG;
}

/* Same sizing rules the LVGL renderer used: blink scales only blinkable shapes, closing eyes
 * widen slightly and sink (the upper lid travels further), squash flattens and widens. */
static void pose_to_raster(const eye_pose_t *p, eyes_raster_pose_t *r)
{
    const eye_desc_t *d = &p->d;
    memset(r, 0, sizeof(*r));
    if (eyes_raster_get_style() == EYES_STYLE_GLASS && d->glass.w > 0.001f)
    {
        /* Glass: the blink closes the eyelids, there is no squash. The sprite is the whole eye. */
        r->glass = d->glass;
        /* The pupils follow the gaze, the eyes a little out of step with each other. */
        float look = LV_CLAMP(-1.0f, s_gaze_x / (float)SURPRISED_SACCADE_RANGE_X, 1.0f);
        float lift = LV_CLAMP(-1.0f, s_gaze_y / (float)SURPRISED_SACCADE_RANGE_X, 1.0f);
        r->glass.pupil_x += (p->side < 0 ? 1.0f : -1.15f) * look * 0.15f;
        r->glass.pupil_y += lift * 0.08f;
        float open = p->lid * (s_idle_owned ? d->openness : 1.0f);
        float closing = 1.0f - LV_CLAMP(0.0f, open, 1.0f);
        /* The upper lid, with its line, travels down over the eye. */
        r->glass.top += (1.0f - r->glass.top) * LV_MIN(1.0f, closing * 1.05f);
        if (closing > 0.6f)
        {
            /* Shut eyes become the Sleeping mood's soft curve, not a lid line over nothing. */
            float k = LV_CLAMP(0.0f, (closing - 0.6f) / 0.4f, 1.0f);
            k = k * k * (3.0f - 2.0f * k);
            const eyes_glass_t *z = p->side < 0 ? &GLASS[EYES_EXPRESSION_SLEEPING].l : &GLASS[EYES_EXPRESSION_SLEEPING].r;
            if (z->w > 0.0f)
            {
                float *o = &r->glass.w;
                const float *b = &z->w;
                for (int n = 0; n < 15; n++) o[n] += (b[n] - o[n]) * k;
            }
        }
        r->shape = EYES_SHAPE_GLASS;
        r->dir = p->side < 0 ? 1 : -1;
        r->w = (int32_t)(EYE_W * r->glass.w);
        r->h = (int32_t)(EYE_H * r->glass.h);
        r->stroke_basis = (float)r->h;
        r->squash = 1.0f;
        r->accent = d->accent;
        r->accent_opacity = p->ticks;
        r->side = d->accent == EYES_ACCENT_NONE ? 0 : p->side;
        return;
    }
    bool blinkable = is_blinkable(d);
    float squash = LV_MAX(p->squash, 0.02f);
    float sx = 1.0f + SQUASH_STRETCH * (1.0f - LV_MIN(squash, 1.0f));
    float openness = d->openness;
    if (blinkable)
    {
        sx *= 1.0f + BLINK_STRETCH * (1.0f - p->lid);
        openness *= p->lid;
    }
    float full_h = EYE_H * d->h * d->openness * squash;
    float lid_h = EYE_H * d->h * openness * squash;
    r->shape = d->shape;
    r->dir = d->dir;
    r->w = (int32_t)(EYE_W * d->w * sx);
    r->h = (int32_t)lid_h;
    if (r->h < 6)
    {
        r->h = 6;
    }
    r->stroke_basis = EYE_H * d->h;
    r->squash = squash;
    r->cy = blinkable ? (int32_t)lroundf((full_h - lid_h) * BLINK_LID_DROP) : 0;
    if (d->shape == EYES_SHAPE_ARCH)
    {
        /* Centre the dome's bounds, rather than its underlying circle. */
        r->cy = (int32_t)lroundf((r->w - r->stroke_basis * 0.55f) * 0.25f * squash);
    }
    r->spin = p->spin;
    r->stripes = p->stripes;
    r->accent = d->accent;
    r->accent_opacity = p->ticks;
    r->side = d->accent == EYES_ACCENT_NONE && d->shape != EYES_SHAPE_WORRIED && d->dir != 2 ? 0 : p->side;
    r->ticks = 0.0f;
}

static bool same_kind(const eye_desc_t *a, const eye_desc_t *b)
{
    return a->shape == b->shape && a->dir == b->dir && a->accent == b->accent;
}

/* Lerps the continuous parameters of two descriptors of the same shape+dir; shape/dir changes
 * never come through here (they go through the squash-swap in step_expression). */
static eye_desc_t blend_desc(const eye_desc_t *from, const eye_desc_t *to, float t)
{
    eye_desc_t out = *to;
    out.w = from->w + (to->w - from->w) * t;
    out.h = from->h + (to->h - from->h) * t;
    out.openness = from->openness + (to->openness - from->openness) * t;
    /* Glass parameters are all plain floats: morph each one. */
    const float *a = &from->glass.w, *b = &to->glass.w;
    float *o = &out.glass.w;
    for (int k = 0; k < 15; k++) o[k] = a[k] + (b[k] - a[k]) * t;
    out.glass.pupil_kind = t < 0.5f ? from->glass.pupil_kind : to->glass.pupil_kind;
    return out;
}

static eye_desc_t current_desc(int i)
{
    return blend_desc(&s_morph_from[i], &s_morph_to[i], smootherstep(s_morph_p[i]));
}

static void compute_pose(int i, eye_pose_t *p)
{
    p->d = current_desc(i);
    if (s_idle_owned)
    {
        /* Keep one face: attention, fatigue and breathing deform it continuously.
         * No preset swaps, decorations, or squash-to-slit transitions during idle. */
        float breath = sinf(s_idle_breath);
        float trailing = (i == 0 ? s_gaze_x : -s_gaze_x) / 8.0f;
        p->d.h *= (1.0f + breath * 0.012f - s_idle_slouch * 0.08f - s_idle_yawn * 0.22f);
        p->d.w *= (1.0f - breath * 0.006f + s_idle_slouch * 0.025f + s_idle_yawn * 0.07f);
        p->d.openness *= LV_CLAMP(0.025f, s_idle_lid * (1.0f - s_idle_yawn * 0.8f) +
                                  (i == 1 ? s_idle_flutter : 0.0f), 1.0f) *
                                  (1.0f - fmaxf(0, trailing) * 0.08f);

    }
    p->side = i == 0 ? -1 : 1;
    /* Curious glance: squint the trailing eye as the gaze turns, short of a full wink. */
    float direction = s_gaze_x / (float)SURPRISED_SACCADE_RANGE_X;
    float collapse = LV_CLAMP(0.0f, (i == 0) ? direction : -direction, 1.0f) *
                     SURPRISED_MAX_COLLAPSE * s_curious_weight;
    /* Surprise and nervousness hold their eyes open throughout the reaction. */
    bool hold_open = s_expression == EXPRESSION_SURPRISED || s_expression == EYES_EXPRESSION_NERVOUS;
    p->lid = hold_open ? 1.0f :
             s_blink_factor * (1.0f - collapse * 0.93f);
    p->squash = s_squash[i];
    p->ticks = s_ticks_alpha[i] * LV_CLAMP(0.0f, s_squash[i], 1.0f);
    bool spiral = p->d.shape == EYES_SHAPE_SPIRAL;
    p->spin = spiral ? floorf(s_spin_phase * SPIN_PHASES) / SPIN_PHASES : 0.0f;
    p->stripes = spiral ? s_spiral_amt : 0.0f;
}

static bool raster_equal(const eyes_raster_pose_t *a, const eyes_raster_pose_t *b)
{
    return a->shape == b->shape && a->dir == b->dir && a->w == b->w && a->h == b->h &&
           a->stroke_basis == b->stroke_basis && a->squash == b->squash && a->cy == b->cy &&
           a->ticks == b->ticks && a->spin == b->spin && a->stripes == b->stripes &&
           a->accent == b->accent && a->side == b->side && a->accent_opacity == b->accent_opacity &&
           memcmp(&a->glass, &b->glass, sizeof(a->glass)) == 0;
}

/* Equal when they rasterize identically (e.g. a spiral ignores the curious squint). */
static bool pose_equal(const eye_pose_t *a, const eye_pose_t *b)
{
    eyes_raster_pose_t ra, rb;
    pose_to_raster(a, &ra);
    pose_to_raster(b, &rb);
    return raster_equal(&ra, &rb);
}

/* Eye 1's pose is eye 0's flipped left/right (e.g. "> <" chevrons), so one sprite serves both. */
static bool pose_mirror_equal(const eye_pose_t *a, const eye_pose_t *b)
{
    eyes_raster_pose_t ra, rb;
    pose_to_raster(a, &ra);
    pose_to_raster(b, &rb);
    /* A mirrored spiral also spins the other way, so dizzy eyes counter-rotate for free. */
    /* Decorations include asymmetric symbols and glints; render them independently. */
    if (ra.accent != EYES_ACCENT_NONE || rb.accent != EYES_ACCENT_NONE)
    {
        return false;
    }
    bool sided = ra.shape == EYES_SHAPE_CHEVRON || ra.shape == EYES_SHAPE_CRESCENT ||
                 ra.shape == EYES_SHAPE_SPIRAL || ra.shape == EYES_SHAPE_HALF || ra.shape == EYES_SHAPE_EGG || ra.shape == EYES_SHAPE_GLASS ||
                 ra.shape == EYES_SHAPE_SLOPE;
    if (!sided || ra.dir == 0)
    {
        return false;
    }
    rb.dir = (int8_t)-rb.dir;
    return raster_equal(&ra, &rb);
}

/* Runs on the helper core: only writes back buffers, which nothing on screen references. */
static void raster_job(void *arg)
{
    const raster_req_t *req = arg;
    for (int i = 0; i < 2; i++)
    {
        if (req->render[i])
        {
            eyes_raster_pose_t rp;
            pose_to_raster(&req->pose[i], &rp);
            rp.draft = req->draft[i];
            eyes_raster_render(&rp, EYES_ROTATION, &s_sprites[i][!s_front[i]]);
        }
    }
}

/* Promotes finished back buffers to the front; marks eyes whose pixels changed. */
static void apply_raster_results(bool sprite_dirty[2])
{
    if (!s_req_pending)
    {
        return;
    }
    s_req_pending = false;
    for (int i = 0; i < 2; i++)
    {
        if (s_req.render[i])
        {
            s_front[i] ^= 1;
            s_eye_sprite[i] = &s_sprites[i][s_front[i]];
            s_eye_mirror[i] = false;
            s_drawn[i] = s_req.pose[i];
            s_drawn_draft[i] = s_req.draft[i];
            sprite_dirty[i] = true;
        }
    }
    if (s_req.share)
    {
        s_eye_sprite[1] = s_eye_sprite[0];
        s_eye_mirror[1] = s_req.share_mirror;
        s_drawn[1] = s_req.pose[1];
        s_drawn_draft[1] = s_req.draft[0];
        sprite_dirty[1] = true;
    }
}

/* Decides which eyes need rasterizing for the current poses (identical eyes share one sprite).
 * Returns true if anything must be rendered. */
static bool plan_raster(const eye_pose_t pose[2], bool force, bool sprite_dirty[2])
{
    /* A pose that is still moving is drawn as a cheap half-resolution draft; once it stops, the
     * draft on screen is replaced by one full-quality draw. */
    bool moved0 = !force && !pose_equal(&pose[0], &s_drawn[0]);
    bool moved1 = !force && !pose_equal(&pose[1], &s_drawn[1]);
    bool changed0 = force || moved0 || s_drawn_draft[0];
    bool changed1 = force || moved1 || s_drawn_draft[1];
    bool eye1_shares = s_eye_sprite[1] == s_eye_sprite[0];
    memset(&s_req, 0, sizeof(s_req));
    s_req.pose[0] = pose[0];
    s_req.pose[1] = pose[1];
    s_req.draft[0] = moved0;
    s_req.draft[1] = moved1 || (eye1_shares && moved0);
    s_req.render[0] = changed0;
    bool same = pose_equal(&pose[1], &pose[0]);
    bool mirrored = !same && pose_mirror_equal(&pose[1], &pose[0]);
    if (same || mirrored)
    {
        if (changed0)
        {
            s_req.share = true;
            s_req.share_mirror = mirrored;
        }
        else if (changed1 || !eye1_shares || s_eye_mirror[1] != mirrored)
        {
            /* Eye 0's sprite on screen already matches: point eye 1 at it right away. */
            s_eye_sprite[1] = s_eye_sprite[0];
            s_eye_mirror[1] = mirrored;
            s_drawn[1] = pose[1];
            s_drawn_draft[1] = s_drawn_draft[0];
            sprite_dirty[1] = true;
        }
    }
    else if (changed1 || (eye1_shares && changed0))
    {
        /* Also when eye 1 was borrowing eye 0's sprite, which is about to be recycled. */
        s_req.render[1] = true;
    }
    return s_req.render[0] || s_req.render[1];
}

static bool rect_empty(const rect_t *r)
{
    return r->x1 <= r->x0 || r->y1 <= r->y0;
}

static rect_t rect_union(const rect_t *a, const rect_t *b)
{
    if (rect_empty(a))
    {
        return *b;
    }
    if (rect_empty(b))
    {
        return *a;
    }
    rect_t r = {LV_MIN(a->x0, b->x0), LV_MIN(a->y0, b->y0), LV_MAX(a->x1, b->x1), LV_MAX(a->y1, b->y1)};
    return r;
}

static bool rect_overlaps(const rect_t *a, const rect_t *b)
{
    return !rect_empty(a) && !rect_empty(b) && a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 &&
           b->y0 < a->y1;
}

/* The panel only accepts areas starting on even pixels with even sizes. */
static rect_t rect_panel_align(rect_t r)
{
    r.x0 = LV_MAX(0, r.x0 & ~1);
    r.y0 = LV_MAX(0, r.y0 & ~1);
    r.x1 = LV_MIN(FACE_SIZE, (r.x1 + 1) & ~1);
    r.y1 = LV_MIN(FACE_SIZE, (r.y1 + 1) & ~1);
    return r;
}

static inline uint16_t rgb565_max_swapped(uint16_t a, uint16_t b)
{
    a = (uint16_t)((a >> 8) | (a << 8));
    b = (uint16_t)((b >> 8) | (b << 8));
    uint16_t m = (uint16_t)(LV_MAX(a & 0xF800, b & 0xF800) | LV_MAX(a & 0x07E0, b & 0x07E0) |
                            LV_MAX(a & 0x001F, b & 0x001F));
    return (uint16_t)((m >> 8) | (m << 8));
}

/* Composes black + both sprites for panel rows [y0, y1) and columns [x0, x1). Sprites are
 * opaque-on-black, so where the two overlap (glow edges) they are merged with a per-channel max. */
static void compose_strip(uint16_t *dst, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int32_t width = x1 - x0;
    memset(dst, 0, (size_t)width * (size_t)(y1 - y0) * sizeof(uint16_t));
    for (int i = 0; i < 2; i++)
    {
        const eyes_sprite_t *spr = s_eye_sprite[i];
        const rect_t *r = &s_eyes[i].rect;
        int32_t cx0 = LV_MAX(x0, r->x0);
        int32_t cx1 = LV_MIN(x1, r->x1);
        int32_t cy0 = LV_MAX(y0, r->y0);
        int32_t cy1 = LV_MIN(y1, r->y1);
        if (cx0 >= cx1 || cy0 >= cy1)
        {
            continue;
        }
        const rect_t *other = &s_eyes[0].rect;
        int32_t ox0 = (i == 1) ? LV_MAX(cx0, other->x0) : cx1;
        int32_t ox1 = (i == 1) ? LV_MIN(cx1, other->x1) : cx1;
        bool flip_rows = s_eye_mirror[i] && MIRROR_ROWS;
        bool flip_cols = s_eye_mirror[i] && !MIRROR_ROWS;
        for (int32_t y = cy0; y < cy1; y++)
        {
            int32_t sv = flip_rows ? spr->h - 1 - (y - r->y0) : y - r->y0;
            const uint16_t *row = spr->px + (size_t)sv * (size_t)spr->w;
            uint16_t *out = dst + (size_t)(y - y0) * (size_t)width + (size_t)(cx0 - x0);
            bool row_overlaps = i == 1 && ox0 < ox1 && y >= other->y0 && y < other->y1;
            if (!row_overlaps && !flip_cols)
            {
                memcpy(out, row + (cx0 - r->x0), (size_t)(cx1 - cx0) * sizeof(uint16_t));
                continue;
            }
            for (int32_t x = cx0; x < cx1; x++, out++)
            {
                int32_t su = flip_cols ? spr->w - 1 - (x - r->x0) : x - r->x0;
                uint16_t px = row[su];
                *out = (row_overlaps && x >= ox0 && x < ox1) ? rgb565_max_swapped(*out, px) : px;
            }
        }
    }
    eyes_dream_compose(dst, x0, y0, x1, y1, EYES_ROTATION);
}

static void send_rects(const rect_t *rects, int count)
{
    int last_rect = -1;
    for (int k = 0; k < count; k++)
    {
        if (!rect_empty(&rects[k]))
        {
            last_rect = k;
        }
    }
    for (int k = 0; k <= last_rect; k++)
    {
        const rect_t *r = &rects[k];
        if (rect_empty(r))
        {
            continue;
        }
        int32_t width = r->x1 - r->x0;
        /* Narrow areas get taller strips: fewer transfers, less per-transfer command overhead. */
        int32_t rows_per_strip = (STRIP_PIXELS / width) & ~1;
        for (int32_t y = r->y0; y < r->y1; y += rows_per_strip)
        {
            int32_t y_end = LV_MIN(r->y1, y + rows_per_strip);
            uint16_t *buf = s_strip[s_strip_idx];
            compose_strip(buf, r->x0, y, r->x1, y_end);
            /* Composing the next strip overlaps this one's DMA; the frame's last strip waits so
             * nothing is in flight between frames (or when the panel is handed back). */
            bool last = (k == last_rect) && (y_end >= r->y1);
            eyes_panel_blit(r->x0, y, r->x1, y_end, buf, last);
            s_strip_idx ^= 1;
        }
    }
}

static inline uint16_t swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

/* RGB565 lerp, alpha in 0..32 (exact at both ends), all three channels in one 32-bit multiply. */
static inline uint16_t blend565(uint16_t bg, uint16_t fg, uint32_t alpha32)
{
    uint32_t b = (bg | ((uint32_t)bg << 16)) & 0x07E0F81Fu;
    uint32_t f = (fg | ((uint32_t)fg << 16)) & 0x07E0F81Fu;
    uint32_t r = ((((f - b) * alpha32) >> 5) + b) & 0x07E0F81Fu;
    return (uint16_t)((r >> 16) | r);
}

/* The eyes stay frozen during the fade, so they are composed once into a PSRAM snapshot; each
 * frame then only blends two images, split across both cores with 32-bit loads. */
static uint16_t *s_fade_snapshot;

typedef struct
{
    uint16_t *out;
    int32_t y0, y1, strip_y, x0, width;
    uint32_t alpha32;
} fade_job_t;

static void blend_fade_rows(void *arg)
{
    const fade_job_t *job = arg;
    for (int32_t row = job->y0; row < job->y1; row++)
    {
        size_t offset = (size_t)row * FACE_SIZE + (size_t)job->x0;
        const uint32_t *a = (const uint32_t *)(s_fade_snapshot + offset);
        const uint32_t *b = (const uint32_t *)(s_fade_image + offset);
        uint32_t *out = (uint32_t *)(job->out + (size_t)(row - job->strip_y) * (size_t)job->width);
        for (int32_t k = 0; k < job->width / 2; k++)
        {
            uint32_t pa = a[k], pb = b[k];
            uint16_t lo = blend565((uint16_t)pa, (uint16_t)pb, job->alpha32);
            uint16_t hi = blend565((uint16_t)(pa >> 16), (uint16_t)(pb >> 16), job->alpha32);
            out[k] = (uint32_t)swap16(lo) | ((uint32_t)swap16(hi) << 16);
        }
    }
}

/* The round panel hides its corners: blend and send only the span of each strip the circle touches. */
static void visible_span(int32_t y0, int32_t y1, int32_t *x0, int32_t *x1)
{
    const float c = FACE_SIZE / 2.0f;
    float dy = (float)y1 <= c ? c - (float)y1 : ((float)y0 >= c ? (float)y0 - c : 0.0f);
    float half = sqrtf(LV_MAX(0.0f, c * c - dy * dy)) + 2.0f;
    *x0 = LV_MAX(0, (int32_t)(c - half)) & ~1;
    *x1 = LV_MIN(FACE_SIZE, ((int32_t)ceilf(c + half) + 1) & ~1);
}

static void take_fade_snapshot(void)
{
    s_fade_snapshot = heap_caps_malloc((size_t)FACE_SIZE * FACE_SIZE * sizeof(uint16_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_fade_snapshot)
    {
        return;
    }
    eyes_panel_join();
    int32_t rows = STRIP_PIXELS / FACE_SIZE;
    for (int32_t y = 0; y < FACE_SIZE; y += rows)
    {
        int32_t y_end = LV_MIN(FACE_SIZE, y + rows);
        uint16_t *dst = s_fade_snapshot + (size_t)y * FACE_SIZE;
        compose_strip(dst, 0, y, FACE_SIZE, y_end);
        for (size_t k = 0; k < (size_t)(y_end - y) * FACE_SIZE; k++)
        {
            dst[k] = swap16(dst[k]);
        }
    }
}

static void free_fade_snapshot(void)
{
    heap_caps_free(s_fade_snapshot);
    s_fade_snapshot = NULL;
}

/* One full-screen frame of the outgoing cross-fade: eyes (as last drawn) blended toward
 * s_fade_image. Full frames go out in ~22 ms, far faster than LVGL can fade this panel. */
static void send_fade_frame(uint32_t alpha32)
{
    int32_t rows = STRIP_PIXELS / FACE_SIZE;
    for (int32_t y = 0; y < FACE_SIZE; y += rows)
    {
        int32_t y_end = LV_MIN(FACE_SIZE, y + rows);
        uint16_t *buf = s_strip[s_strip_idx];
        int32_t x0 = 0, x1 = FACE_SIZE;
        if (s_fade_snapshot)
        {
            visible_span(y, y_end, &x0, &x1);
            int32_t mid = y + (y_end - y) / 2;
            fade_job_t helper = {buf, mid, y_end, y, x0, x1 - x0, alpha32};
            fade_job_t own = {buf, y, mid, y, x0, x1 - x0, alpha32};
            eyes_panel_run_async(blend_fade_rows, &helper);
            blend_fade_rows(&own);
            eyes_panel_join();
        }
        else
        {
            const uint16_t *img = s_fade_image + (size_t)y * FACE_SIZE;
            size_t n = (size_t)(y_end - y) * FACE_SIZE;
            compose_strip(buf, 0, y, FACE_SIZE, y_end);
            for (size_t k = 0; k < n; k++)
            {
                buf[k] = swap16(blend565(swap16(buf[k]), img[k], alpha32));
            }
        }
        eyes_panel_blit(x0, y, x1, y_end, buf, y_end >= FACE_SIZE);
        s_strip_idx ^= 1;
    }
}

static void step_fade(lv_timer_t *timer, float dt)
{
    s_fade_elapsed_ms += dt * 1000.0f;
    float t = LV_MIN(1.0f, s_fade_elapsed_ms / s_fade_ms);
    send_fade_frame((uint32_t)lroundf(smootherstep(t) * 32.0f));
    if (t < 1.0f)
    {
        return;
    }
    void (*done)(void *ctx) = s_fade_done;
    void *ctx = s_fade_ctx;
    s_fade_image = NULL;
    s_fade_done = NULL;
    free_fade_snapshot();
    lv_timer_pause(timer);
    s_frame_paused = true;
    if (done)
    {
        done(ctx);
    }
}

/* Shows the sprites rasterized last frame at this frame's positions, streaming only the changed
 * panel area (old ∪ new bounds of each eye that changed), and starts rasterizing this frame's
 * shapes on the helper core in the meantime. */
static void render_frame(bool force)
{
    if (!s_panel_ready)
    {
        return;
    }
    if (s_force_full)
    {
        force = true;
    }
    /* A full-quality draw can take ~100 ms on the helper core; do not stall the UI waiting for it. */
    if (force) {
        eyes_panel_join();
    } else if (!eyes_panel_try_join()) {
        return;
    }
    bool sprite_dirty[2] = {false, false};
    apply_raster_results(sprite_dirty);

    eye_pose_t pose[2];
    compute_pose(0, &pose[0]);
    compute_pose(1, &pose[1]);
    if (plan_raster(pose, force, sprite_dirty))
    {
        s_req_pending = true;
        if (force)
        {
            /* A full repaint needs every sprite now (first frame, waking from standby). */
            raster_job(&s_req);
            apply_raster_results(sprite_dirty);
        }
        else
        {
            eyes_panel_run_async(raster_job, &s_req);
        }
    }

    rect_t dirty[3] = {{0}, {0}, {0}};
    bool dream_changed = eyes_dream_update(lv_tick_get() - s_idle_dream_since,
        s_idle_owned ? s_idle_dream : 0.0f);
    if (dream_changed)
    {
        eyes_dream_bounds(EYES_ROTATION, &dirty[2].x0, &dirty[2].y0, &dirty[2].x1, &dirty[2].y1);
        dirty[2] = rect_panel_align(dirty[2]);
    }
    for (int i = 0; i < 2; i++)
    {
        eye_render_t *eye = &s_eyes[i];
        int32_t lx = (int32_t)lroundf(FACE_SIZE / 2 + eye->base_x + s_gaze_x + eye->bounce_x);
        int32_t ly = (int32_t)lroundf(FACE_SIZE / 2 + s_gaze_y + eye->bounce_y);
        int32_t px, py;
        switch (EYES_ROTATION)
        {
        case 90:
            px = FACE_SIZE - 1 - ly;
            py = lx;
            break;
        case 180:
            px = FACE_SIZE - 1 - lx;
            py = FACE_SIZE - 1 - ly;
            break;
        case 270:
            px = ly;
            py = FACE_SIZE - 1 - lx;
            break;
        default:
            px = lx;
            py = ly;
            break;
        }
        const eyes_sprite_t *spr = s_eye_sprite[i];
        /* Mirroring x -> -x maps offsets [o, o + n - 1] to [-(o + n - 1), -o]. */
        int32_t ox = (s_eye_mirror[i] && !MIRROR_ROWS) ? -(spr->ox + spr->w - 1) : spr->ox;
        int32_t oy = (s_eye_mirror[i] && MIRROR_ROWS) ? -(spr->oy + spr->h - 1) : spr->oy;
        rect_t now = {px + ox, py + oy, px + ox + spr->w, py + oy + spr->h};
        if (force || sprite_dirty[i] || memcmp(&now, &eye->rect, sizeof(now)) != 0)
        {
            dirty[i] = rect_panel_align(rect_union(&eye->rect, &now));
        }
        eye->rect = now;
    }

    if (force)
    {
        rect_t full = {0, 0, FACE_SIZE, FACE_SIZE};
        s_force_full = false;
        send_rects(&full, 1);
        return;
    }
    if (rect_overlaps(&dirty[0], &dirty[1]))
    {
        dirty[0] = rect_union(&dirty[0], &dirty[1]);
        dirty[1] = (rect_t){0};
    }
    send_rects(dirty, 3);
}

/* Starts (or redirects, mid-flight) a smooth change to a new expression; every state
 * variable stays continuous, so re-triggering never jumps. */
static void start_expression_transition(uint8_t new_expression)
{
    const expression_t *to = &EXPRESSIONS[new_expression];
    for (int i = 0; i < 2; i++)
    {
        eye_desc_t current = current_desc(i);
        s_target[i] = (i == 0) ? to->left : to->right;
        s_target[i].glass = (i == 0) ? GLASS[new_expression].l : GLASS[new_expression].r;
        if (eyes_raster_get_style() == EYES_STYLE_GLASS)
        {
            s_target[i].accent = GLASS_ACCENT[new_expression][i];
            if (s_target[i].glass.w < 0.0f)
            {
                /* The square pack draws this mood with an orb shape (the laugh's chevrons): w, h carry its size. */
                s_target[i].w = -s_target[i].glass.w;
                s_target[i].h = s_target[i].glass.h;
                s_target[i].glass.w = 0.0f;
            }
        }
        s_morph_from[i] = current;
        bool surprised_orb = new_expression == EXPRESSION_SURPRISED &&
                             (current.shape == EYES_SHAPE_CAPSULE || current.shape == EYES_SHAPE_OVAL);
        bool nervous_orb = current.shape == EYES_SHAPE_CAPSULE && s_target[i].shape == EYES_SHAPE_CAPSULE &&
                           (new_expression == EYES_EXPRESSION_NERVOUS || current.accent == EYES_ACCENT_NERVOUS);
        if (eyes_raster_get_style() == EYES_STYLE_GLASS || same_kind(&current, &s_target[i]) || surprised_orb || nervous_orb)
        {
            /* A round idle eye can expand directly into an oval without closing first. */
            if (surprised_orb) s_morph_from[i].shape = EYES_SHAPE_OVAL;
            s_morph_to[i] = s_target[i];
            s_morph_p[i] = 0.0f;
            s_swapping[i] = false;
        }
        else
        {
            s_morph_to[i] = current;
            s_morph_p[i] = 1.0f;
            s_swapping[i] = true;
        }
    }
    wake_frame_timer();
}

static float blink_factor_at(float t_ms)
{
    if (t_ms < 0.0f)
    {
        return 1.0f;
    }
    if (t_ms < BLINK_CLOSE_MS)
    {
        float p = t_ms / BLINK_CLOSE_MS;
        return 1.0f - (1.0f - BLINK_MIN_FACTOR) * p * p;
    }
    t_ms -= BLINK_CLOSE_MS;
    if (t_ms < BLINK_HOLD_MS)
    {
        return BLINK_MIN_FACTOR;
    }
    t_ms -= BLINK_HOLD_MS;
    if (t_ms < BLINK_OPEN_MS)
    {
        float q = 1.0f - t_ms / BLINK_OPEN_MS;
        return BLINK_MIN_FACTOR + (1.0f - BLINK_MIN_FACTOR) * (1.0f - q * q * q);
    }
    return 1.0f;
}

static void trigger_blink(void)
{
    if (s_blinking || s_expression == EYES_EXPRESSION_NERVOUS)
    {
        return;
    }
    s_blinking = true;
    s_blink_ms = 0.0f;
    s_blink_repeat = (esp_random() % DOUBLE_BLINK_ODDS) == 0;
    wake_frame_timer();
}

static bool step_blink(float dt_ms)
{
    if (!s_blinking)
    {
        s_blink_factor = 1.0f;
        return false;
    }
    s_blink_ms += dt_ms;
    if (s_blink_ms >= BLINK_CLOSE_MS + BLINK_HOLD_MS + BLINK_OPEN_MS)
    {
        if (s_blink_repeat)
        {
            s_blink_repeat = false;
            s_blink_ms = -DOUBLE_BLINK_GAP_MS;
        }
        else
        {
            s_blinking = false;
        }
    }
    s_blink_factor = s_blinking ? blink_factor_at(s_blink_ms) : 1.0f;
    return s_blinking;
}

static bool step_gaze(float dt)
{
    bool surprised = s_expression == EXPRESSION_SURPRISED;
    if (surprised) {
        /* Gentle 1 Hz positional shake, with no change to eye size. */
        s_surprised_phase = fmodf(s_surprised_phase + dt * 6.28318531f, 6.28318531f);
        s_gaze_tx = 3.0f * sinf(s_surprised_phase);
        s_gaze_ty = 1.5f * sinf(s_surprised_phase * 2.0f);
    }
    spring_advance(&s_gaze_x, &s_gaze_vx, s_gaze_tx, s_gaze_omega, SACCADE_ZETA, dt);
    spring_advance(&s_gaze_y, &s_gaze_vy, s_gaze_ty, s_gaze_omega, SACCADE_ZETA, dt);
    if (fabsf(s_gaze_x - s_gaze_tx) < 0.02f && fabsf(s_gaze_y - s_gaze_ty) < 0.02f &&
        fabsf(s_gaze_vx) < 0.5f && fabsf(s_gaze_vy) < 0.5f)
    {
        s_gaze_x = s_gaze_tx;
        s_gaze_y = s_gaze_ty;
        s_gaze_vx = 0.0f;
        s_gaze_vy = 0.0f;
        return surprised;
    }
    return true;
}

static bool step_expression(float dt)
{
    bool active = false;
    for (int i = 0; i < 2; i++)
    {
        if (s_morph_p[i] < 1.0f)
        {
            s_morph_p[i] = LV_MIN(1.0f, s_morph_p[i] + dt * 1000.0f / EXPRESSION_MORPH_MS);
            active = true;
        }
        if (s_swapping[i])
        {
            spring_advance(&s_squash[i], &s_squash_v[i], 0.0f, SQUASH_CLOSE_OMEGA, SQUASH_CLOSE_ZETA, dt);
            if (s_squash[i] <= SQUASH_SWAP_AT)
            {
                /* Swap at the slit; the opening spring carries the closing velocity through. */
                s_morph_from[i] = s_target[i];
                s_morph_to[i] = s_target[i];
                s_morph_p[i] = 1.0f;
                s_swapping[i] = false;
            }
            active = true;
        }
        else
        {
            spring_advance(&s_squash[i], &s_squash_v[i], 1.0f, SQUASH_OPEN_OMEGA, SQUASH_OPEN_ZETA, dt);
            if (fabsf(s_squash[i] - 1.0f) < 0.001f && fabsf(s_squash_v[i]) < 0.01f)
            {
                s_squash[i] = 1.0f;
                s_squash_v[i] = 0.0f;
            }
            else
            {
                active = true;
            }
        }
        float ticks_target = (!s_swapping[i] && s_morph_to[i].accent != EYES_ACCENT_NONE) ? 1.0f : 0.0f;
        s_ticks_alpha[i] = approach(s_ticks_alpha[i], ticks_target, dt, TICKS_FADE_TAU_S);
        active |= s_ticks_alpha[i] != ticks_target;
    }
    return active;
}

static bool spiral_showing(void)
{
    for (int i = 0; i < 2; i++)
    {
        if (s_morph_to[i].shape == EYES_SHAPE_SPIRAL || s_target[i].shape == EYES_SHAPE_SPIRAL)
        {
            return true;
        }
    }
    return false;
}

static void reset_idle_personality(bool owned);

static bool step_spin(float dt)
{
    if (s_dizzy_until != 0 && !s_shake_active && (int32_t)(lv_tick_get() - s_dizzy_until) >= 0)
    {
        s_dizzy_until = 0;
        reset_idle_personality(true);
        s_expression = EXPRESSION_NEUTRAL;
        start_expression_transition(EXPRESSION_NEUTRAL);
        play_expression_sound(s_expression);
    }
    /* The rings show as soon as the spiral orb is the target; the rasterizer draws them directly while
     * the orb is still morphing and caches them once it holds still. */
    bool spiral_target = s_target[0].shape == EYES_SHAPE_SPIRAL && s_target[1].shape == EYES_SHAPE_SPIRAL;
    float amt_target = spiral_target ? 1.0f : 0.0f;
    s_spiral_amt = approach(s_spiral_amt, amt_target, dt, 0.03f);
    bool fading = s_spiral_amt != amt_target;
    if (!spiral_showing())
    {
        s_spin_speed = 0.0f;
        return fading;
    }
    if (s_shake_active)
    {
        s_spin_speed = approach(s_spin_speed, SPIN_SHAKE_SPEED, dt, 0.15f);
    }
    else
    {
        s_spin_speed = SPIN_IDLE_SPEED + (s_spin_speed - SPIN_IDLE_SPEED) * expf(-dt / SPIN_DECAY_TAU_S);
        s_spin_speed = LV_MAX(s_spin_speed, SPIN_IDLE_SPEED);
    }
    s_spin_phase += s_spin_speed * dt;
    s_spin_phase -= floorf(s_spin_phase);
    return true;
}

static bool step_curious(float dt)
{
    float target = is_curious() ? 1.0f : 0.0f;
    s_curious_weight = approach(s_curious_weight, target, dt, SURPRISED_FADE_TAU_S);
    return s_curious_weight != target;
}

static bool step_bounce(float dt)
{
    if (!s_bounce_live)
    {
        return false;
    }
    float remaining = LV_MIN(dt, 0.04f);
    while (remaining > 0.0f)
    {
        float step = (remaining < BOUNCE_STEP_S) ? remaining : BOUNCE_STEP_S;
        integrate_bounce_step(step);
        remaining -= step;
    }

    bool moving = false;
    for (int i = 0; i < 2; i++)
    {
        eye_render_t *eye = &s_eyes[i];
        if (!s_shake_active && fabsf(eye->bounce_x) < 0.5f && fabsf(eye->bounce_y) < 0.5f &&
            fabsf(eye->velocity_x) < 2.0f && fabsf(eye->velocity_y) < 2.0f)
        {
            eye->bounce_x = 0.0f;
            eye->bounce_y = 0.0f;
            eye->velocity_x = 0.0f;
            eye->velocity_y = 0.0f;
        }
        if (fabsf(eye->bounce_x) > 0.5f || fabsf(eye->bounce_y) > 0.5f ||
            fabsf(eye->velocity_x) > 2.0f || fabsf(eye->velocity_y) > 2.0f)
        {
            moving = true;
        }
    }
    s_bounce_live = moving || fabsf(s_shake_force_x) >= SHAKE_FORCE_DEADBAND ||
                    fabsf(s_shake_force_y) >= SHAKE_FORCE_DEADBAND;
    return s_bounce_live;
}

static bool step_idle_personality(float dt);

static void frame_timer_cb(lv_timer_t *timer)
{
    uint32_t now = lv_tick_get();
    uint32_t elapsed_ms = now - s_frame_tick;
    s_frame_tick = now;
    if (elapsed_ms == 0)
    {
        return;
    }
    float dt = LV_MIN(elapsed_ms / 1000.0f, FRAME_MAX_DT_S);
    if (s_fade_image)
    {
        step_fade(timer, dt);
        return;
    }

    bool active = false;
    if (s_idle_owned)
    {
        step_idle_personality(dt);
        s_idle_breath += dt * (1.7f - s_idle_slouch * 0.4f);
        if (s_idle_breath > 6.2831853f) s_idle_breath -= 6.2831853f;
        active = true;
    }
    active |= step_gaze(dt);
    active |= step_bounce(dt);
    active |= step_blink(dt * 1000.0f);
    active |= step_expression(dt);
    active |= step_curious(dt);
    active |= step_spin(dt);

    render_frame(false);

    if (!active && !s_req_pending)
    {
        lv_timer_pause(timer);
        s_frame_paused = true;
    }
}

static void trigger_bounce(void)
{
    for (int i = 0; i < 2; i++)
    {
        /* Small opposing impulses keep the reaction readable within each lane. */
        int32_t direction = (i == 0) ? -1 : 1;
        s_eyes[i].velocity_x += direction * (float)rand_range(180, 280);
        s_eyes[i].velocity_y += (float)rand_range(-220, 220);
    }
    s_bounce_live = true;
    wake_frame_timer();
}

static void reset_idle_personality(bool owned)
{
    s_idle_owned = owned;
    s_idle_since = lv_tick_get();
    s_idle_dream_since = s_idle_since;
    s_idle_mood = IDLE_AWAKE;
    s_idle_mood_since = s_idle_since;
    s_idle_mood_until = s_idle_since + rand_range(45000, 90000);
    s_idle_detail_at = s_idle_since + rand_range(20000, 40000);
    s_idle_detail = 0;
    s_idle_yawn = s_idle_dream = s_idle_flutter = 0.0f;
    s_idle_pose_until = s_idle_since;
    s_idle_beat = 0;
    s_idle_direction = s_gaze_x < 0 ? -1.0f : 1.0f;
    s_idle_blink_at = s_idle_since + rand_range(2600, 5200);
    s_idle_breath = 0.0f;
    s_gaze_tx = s_gaze_ty = 0.0f;
    s_blinking = s_blink_repeat = false;
    s_blink_factor = 1.0f;
#if EYES_SLEEP_DREAM_TEST
    if (owned)
    {
        s_idle_mood = IDLE_NAPPING;
        s_idle_detail = 0;
        s_idle_detail_since = s_idle_since;
        s_sleep_test_variant = EYES_DREAM_SKY;
        eyes_dream_select(s_sleep_test_variant);
    }
#endif
}

/* Each glance has intent: notice, inspect in the same direction, return, rest.
 * Randomness changes pacing and amplitude, never the character's emotion. */
static bool step_idle_personality(float dt)
{
    if (!s_idle_owned || is_curious() || s_shake_active || s_dizzy_until)
        return false;
    uint32_t now = lv_tick_get();
#if EYES_SLEEP_DREAM_TEST
    /* Stay asleep and cycle the preview clips without hiding the cloud. */
    uint32_t elapsed = now - s_idle_detail_since;
    if (elapsed >= 14000u)
    {
        s_idle_detail_since = now;
        s_idle_dream_since = now;
        elapsed = 0;
        s_sleep_test_variant = (eyes_dream_variant_t)((s_sleep_test_variant + 1) % EYES_DREAM_COUNT);
        eyes_dream_select(s_sleep_test_variant);
    }
    s_idle_dream = approach(s_idle_dream, 1.0f, dt, 0.25f);
    s_idle_lid = approach(s_idle_lid, 0.035f, dt, 0.65f);
    s_idle_slouch = approach(s_idle_slouch, 1.0f, dt, 1.5f);
    s_gaze_omega = 7.0f;
    s_gaze_tx = s_idle_dream * 4.0f * sinf(elapsed * 0.0021f);
    s_gaze_ty = 7.0f + s_idle_dream * 1.5f * sinf(elapsed * 0.0034f);
    return true;
#else
    if ((int32_t)(now - s_idle_mood_until) >= 0)
    {
        idle_mood_t next;
        uint32_t duration;
        /* Sleep pressure builds only while unattended. Interaction and a completed
         * nap reset it; failed attempts to stay awake do not restart the wait. */
        uint32_t unattended_ms = now - s_idle_since;
        unsigned drowsy_chance = unattended_ms >= 300000u ? 100 : unattended_ms >= 120000u ? 75 : 35;
        unsigned nap_chance = unattended_ms >= 300000u ? 100 : unattended_ms >= 120000u ? 80 : 40;
        switch (s_idle_mood)
        {
        case IDLE_AWAKE:
            next = IDLE_BORED; duration = rand_range(20000, 40000); break;
        case IDLE_BORED:
            next = esp_random() % 100 < drowsy_chance ? IDLE_DROWSY : IDLE_AWAKE;
            duration = next == IDLE_DROWSY ? rand_range(12000, 22000) : rand_range(45000, 90000);
            break;
        case IDLE_DROWSY:
            next = esp_random() % 100 < nap_chance ? IDLE_NAPPING : IDLE_WAKING;
            duration = next == IDLE_NAPPING ? rand_range(25000, 50000) : 5000;
            break;
        case IDLE_NAPPING:
            s_idle_since = now; /* Rested: spend some time awake before the next nap. */
            next = IDLE_WAKING; duration = 6500; break;
        default:
            next = IDLE_AWAKE; duration = rand_range(60000, 120000); break;
        }
        if (next == IDLE_WAKING)
        {
            s_idle_wake_lid = s_idle_lid;
            s_idle_wake_slouch = s_idle_slouch;
        }
        s_idle_mood = next;
        s_idle_mood_since = now;
        s_idle_mood_until = now + duration;
        s_idle_detail = 0;
        if (next == IDLE_NAPPING)
        {
            s_idle_dream_since = now;
            eyes_dream_select((eyes_dream_variant_t)(esp_random() % EYES_DREAM_COUNT));
        }
        s_idle_detail_at = now + (next == IDLE_NAPPING ? rand_range(7000, 11000) : rand_range(2000, 6000));
    }
    bool sleeping = s_idle_mood == IDLE_NAPPING;
    bool waking = s_idle_mood == IDLE_WAKING;
    bool drowsy = s_idle_mood == IDLE_DROWSY;
    bool bored = s_idle_mood == IDLE_BORED;
    float fatigue = sleeping ? 1.0f : drowsy ? 0.75f : bored ? 0.3f : 0.0f;
    float lid = 1.0f - 0.48f * fatigue;
    float t = (now - s_idle_mood_since) / 1000.0f;

    /* Details belong to a situation, with long quiet gaps between them. */
    if (s_idle_detail == 0 && (int32_t)(now - s_idle_detail_at) >= 0)
    {
        s_idle_detail_since = now;
        if (sleeping) s_idle_detail = 3;
        else if (drowsy || bored) s_idle_detail = 1;
        s_idle_detail_at = now + rand_range(14000, 28000);
    }
    float detail_ms = (float)(now - s_idle_detail_since);
    float duration = s_idle_detail == 1 ? 3600.0f : 650.0f;
    float envelope = detail_ms < duration ? powf(sinf(3.14159265f * detail_ms / duration), 2.0f) : 0.0f;
    s_idle_yawn = approach(s_idle_yawn, s_idle_detail == 1 ? envelope : 0.0f, dt, 0.12f);
    /* The same clip loops throughout the nap; fade only on entry and waking. */
    s_idle_dream = approach(s_idle_dream, sleeping ? 1.0f : 0.0f, dt, 0.25f);
    s_idle_flutter = approach(s_idle_flutter, s_idle_detail == 3 ?
        envelope * (0.07f + 0.07f * sinf(detail_ms * 0.045f)) : 0.0f, dt, 0.035f);
    if (s_idle_detail && detail_ms >= duration) s_idle_detail = 0;

    if (sleeping)
    {
        lid = 0.035f;
        s_gaze_omega = 7.0f;
        s_gaze_tx = s_idle_dream * 4.0f * sinf(t * 2.1f);
        s_gaze_ty = 7.0f + s_idle_dream * 1.5f * sinf(t * 3.4f);
    }
    else if (waking)
    {
        float wake = smootherstep(LV_CLAMP(0.0f, t / 5.0f, 1.0f));
        lid = s_idle_wake_lid + (1.0f - s_idle_wake_lid) * wake;
        s_gaze_tx = 0.0f;
        s_gaze_ty = 7.0f * (1.0f - wake);
        fatigue = s_idle_wake_slouch * (1.0f - wake);
    }
    else if (drowsy)
    {
        float nod = powf(0.5f - 0.5f * cosf(t * 0.85f), 3.0f);
        lid *= 1.0f - nod * 0.7f;
        s_gaze_tx *= expf(-dt * 1.5f);
        s_gaze_ty = 3.0f + 5.0f * nod + s_idle_yawn * 2.0f;
        s_gaze_omega = 7.0f;
    }
    else if ((int32_t)(now - s_idle_pose_until) >= 0)
    {
        s_gaze_omega = bored ? 9.0f : 16.0f;
        switch (s_idle_beat)
        {
        case 0: /* Notice something off to one side. */
            s_idle_direction = -s_idle_direction;
            s_gaze_tx = s_idle_direction * (float)rand_range(4, 6);
            s_gaze_ty = bored ? 4.0f : -2.0f;
            s_idle_pose_until = now + rand_range(700, 1300);
            break;
        case 1: /* Lean into the same look and linger. */
            s_gaze_tx = s_idle_direction * 8.0f;
            s_gaze_ty = bored ? 6.0f : (float)rand_range(-4, 1);
            s_idle_pose_until = now + rand_range(1400, 2600);
            break;
        default: /* Lose interest, return home, then be still for a while. */
            s_gaze_tx = 0.0f;
            s_gaze_ty = bored ? 4.0f : 0.0f;
            s_idle_pose_until = now + (bored ? rand_range(5000, 8500) : rand_range(2800, 4800));
            break;
        }
        s_idle_beat = (s_idle_beat + 1) % 3;
    }
    if (!sleeping && !waking && (int32_t)(now - s_idle_blink_at) >= 0)
    {
        trigger_blink();
        s_idle_blink_at = now + rand_range(3000, 6500);
    }
    s_idle_lid = approach(s_idle_lid, lid, dt, sleeping ? 1.8f : 0.65f);
    s_idle_slouch = approach(s_idle_slouch, fatigue, dt, 1.5f);
    return true;
#endif
}

static void expressive_eyes_on_shake(float accel_x, float accel_y, bool active)
{
#if EYES_SLEEP_DREAM_TEST
    return; /* Keep this temporary visual test asleep through touch and motion. */
#endif
    if (!s_wrapper || !s_frame_timer)
    {
        return;
    }
    bool was_active = s_shake_active;
    s_shake_active = active;
    if (!active)
    {
        s_shake_force_x = 0.0f;
        s_shake_force_y = 0.0f;
        if (was_active)
        {
            /* Let the home spring carry the eyes back from wherever the shake left them; the
             * spiral keeps spinning down for a while before the eyes recover (step_spin). */
            s_bounce_live = true;
            s_dizzy_until = lv_tick_get() + SPIN_LINGER_MS;
            wake_frame_timer();
        }
        return;
    }

    if (fabsf(accel_x) < SHAKE_ACCEL_DEADBAND)
    {
        accel_x = 0.0f;
    }
    if (fabsf(accel_y) < SHAKE_ACCEL_DEADBAND)
    {
        accel_y = 0.0f;
    }
    s_shake_force_x = LV_CLAMP(-SHAKE_FORCE_LIMIT, -accel_x * SHAKE_FORCE_SCALE, SHAKE_FORCE_LIMIT);
    s_shake_force_y = LV_CLAMP(-SHAKE_FORCE_LIMIT, -accel_y * SHAKE_FORCE_SCALE, SHAKE_FORCE_LIMIT);
    if (!was_active)
    {
        reset_idle_personality(false);
        /* Distinct from a pickup: dizzy rings instead of the curious collapsing glance. */
        s_curious_until = 0;
        s_expression = EXPRESSION_DIZZY;
        start_expression_transition(EXPRESSION_DIZZY);
        play_expression_sound(s_expression);
        trigger_bounce();
    }
    else
    {
        s_bounce_live = true;
        wake_frame_timer();
    }
}

static void trigger_saccade(void)
{
    if (s_expression == EXPRESSION_SURPRISED) {
        wake_frame_timer();
        return;
    }
    bool curious = is_curious();
    int32_t range_x = curious ? SURPRISED_SACCADE_RANGE_X : SACCADE_RANGE_X;
    int32_t range_y = curious ? SURPRISED_SACCADE_RANGE_Y : SACCADE_RANGE_Y;
    int32_t tx = rand_range(-range_x, range_x);
    int32_t ty = rand_range(-range_y, range_y);
    if (curious)
    {
        /* Vary amplitude/direction so it reads as scanning around, not a metronomic wink-flip. */
        int32_t amplitude = rand_range(range_x * 5 / 10, range_x);
        if ((esp_random() % 10) < 3)
        {
            /* Keep glancing the same way sometimes, as if following something. */
            tx = s_curious_direction * amplitude;
        }
        else
        {
            s_curious_direction = -s_curious_direction;
            tx = s_curious_direction * amplitude;
        }
        ty = rand_range(-4, 2);
    }
    else
    {
        for (int tries = 0; tries < 4 && hypotf(tx - s_gaze_tx, ty - s_gaze_ty) < SACCADE_MIN_DIST; tries++)
        {
            tx = rand_range(-range_x, range_x);
            ty = rand_range(-range_y, range_y);
        }
    }

    float distance = hypotf(tx - s_gaze_x, ty - s_gaze_y);
    float settle_s = LV_MIN(SACCADE_BASE_MS + SACCADE_MS_PER_PX * distance, (float)SACCADE_MAX_MS) / 1000.0f;
    s_gaze_omega = 4.0f / (SACCADE_ZETA * settle_s);
    s_gaze_tx = (float)tx;
    s_gaze_ty = (float)ty;
    if (!curious && distance > SACCADE_BLINK_DIST && (esp_random() % SACCADE_BLINK_ODDS) == 0)
    {
        trigger_blink();
    }
    wake_frame_timer();
}

static void idle_timer_cb(lv_timer_t *timer)
{
    bool quiet = s_idle_owned;
    bool curious = is_curious();
    if (s_curious_until != 0 && !curious)
    {
        /* Curious window just ended; settle back to neutral. */
        reset_idle_personality(true);
        quiet = true;
        s_curious_until = 0;
        s_expression = EXPRESSION_NEUTRAL;
        start_expression_transition(EXPRESSION_NEUTRAL);
        play_expression_sound(s_expression);
    }
    if (quiet)
    {
        wake_frame_timer();
    }
    else if (s_expression == EXPRESSION_SURPRISED)
    {
        /* The frame clock owns the held-open surprise tremble. */
        wake_frame_timer();
    }
    else if (curious)
    {
        trigger_saccade();
    }
    else if ((esp_random() % IDLE_BLINK_ODDS) == 0)
    {
        trigger_blink();
    }
    else
    {
        trigger_saccade();
    }
    int32_t next = curious ? rand_range(SURPRISED_IDLE_MIN_MS, SURPRISED_IDLE_MAX_MS)
                           : rand_range(IDLE_MIN_MS, IDLE_MAX_MS);
    lv_timer_set_period(timer, next);
}

static void expressive_eyes_on_pickup(void)
{
#if EYES_SLEEP_DREAM_TEST
    return; /* Keep this temporary visual test asleep through touch and motion. */
#endif
    if (!s_wrapper)
    {
        return;
    }
    reset_idle_personality(false);
    s_expression = EXPRESSION_SURPRISED;
    s_dizzy_until = 0;
    s_curious_until = lv_tick_get() + SURPRISED_DURATION_MS;
    s_curious_direction = s_gaze_x > 0 ? -1 : 1;
    start_expression_transition(EXPRESSION_SURPRISED);
    play_expression_sound(s_expression);
    trigger_saccade();
    if (s_idle_timer)
    {
        lv_timer_set_period(s_idle_timer, SURPRISED_IDLE_MIN_MS);
        lv_timer_reset(s_idle_timer);
    }
}

static void expressive_eyes_set_expression(uint8_t expression)
{
#if EYES_SLEEP_DREAM_TEST
    return; /* Keep this temporary visual test asleep through touch and motion. */
#endif
    if (!s_wrapper || expression >= EXPRESSION_COUNT)
    {
        return;
    }
    reset_idle_personality(expression == EXPRESSION_NEUTRAL);
    s_expression = expression;
    s_curious_until = 0;
    s_dizzy_until = 0;
    start_expression_transition(expression);
    play_expression_sound(s_expression);
    if (s_idle_timer)
    {
        lv_timer_set_period(s_idle_timer, rand_range(IDLE_MIN_MS, IDLE_MAX_MS));
    }
}

static void free_buffers(void)
{
    eyes_dream_reset();
    eyes_panel_join();
    s_req_pending = false;
    for (int i = 0; i < 2; i++)
    {
        for (int b = 0; b < 2; b++)
        {
            if (s_sprites[i][b].px)
            {
                heap_caps_free(s_sprites[i][b].px);
            }
            memset(&s_sprites[i][b], 0, sizeof(s_sprites[i][b]));
        }
        if (s_strip[i])
        {
            eyes_panel_free_dma(s_strip[i]);
            s_strip[i] = NULL;
        }
    }
}

static bool alloc_buffers(void)
{
    for (int i = 0; i < 2; i++)
    {
        size_t sprite_bytes = SPRITE_CAP * SPRITE_CAP * sizeof(uint16_t);
        for (int b = 0; b < 2; b++)
        {
            /* PSRAM: internal RAM is a scarce pool shared with Wi-Fi and the SPI DMA strips. */
            eyes_sprite_t *spr = &s_sprites[i][b];
            spr->px = heap_caps_malloc(sprite_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!spr->px)
            {
                spr->px = heap_caps_malloc(sprite_bytes, MALLOC_CAP_8BIT);
            }
            spr->cap_w = SPRITE_CAP;
            spr->cap_h = SPRITE_CAP;
            if (!spr->px)
            {
                free_buffers();
                return false;
            }
        }
        s_strip[i] = eyes_panel_alloc_dma(STRIP_PIXELS * sizeof(uint16_t));
        if (!s_strip[i])
        {
            free_buffers();
            return false;
        }
        s_front[i] = 0;
    }
    s_eye_sprite[0] = &s_sprites[0][0];
    s_eye_sprite[1] = &s_sprites[1][0];
    s_eye_mirror[0] = s_eye_mirror[1] = false;
    s_req_pending = false;
    s_strip_idx = 0;
    return true;
}

static void expressive_eyes_create(lv_obj_t *parent)
{
    /* Placeholder LVGL object: the eyes themselves are drawn straight to the panel. */
    s_wrapper = lv_obj_create(parent);
    lv_obj_remove_style_all(s_wrapper);
    lv_obj_set_size(s_wrapper, FACE_SIZE, FACE_SIZE);
    lv_obj_center(s_wrapper);
    lv_obj_clear_flag(s_wrapper, LV_OBJ_FLAG_SCROLLABLE);

    int32_t half_gap = EYE_GAP / 2;
    memset(s_eyes, 0, sizeof(s_eyes));
    s_eyes[0].base_x = -(EYE_W / 2 + half_gap);
    s_eyes[1].base_x = EYE_W / 2 + half_gap;

    reset_idle_personality(true);
    s_idle_lid = EYES_SLEEP_DREAM_TEST ? 0.035f : 1.0f;
    s_idle_slouch = EYES_SLEEP_DREAM_TEST ? 1.0f : 0.0f;
    s_expression = EXPRESSION_NEUTRAL;
    s_blink_factor = 1.0f;
    s_blinking = false;
    s_blink_repeat = false;
    s_gaze_x = 0.0f;
    s_gaze_y = 0.0f;
    s_gaze_vx = 0.0f;
    s_gaze_vy = 0.0f;
    s_gaze_tx = 0.0f;
    s_gaze_ty = 0.0f;
    s_surprised_phase = 0.0f;
    s_curious_until = 0;
    s_curious_weight = 0.0f;
    s_spin_phase = 0.0f;
    s_spin_speed = 0.0f;
    s_spiral_amt = 0.0f;
    s_dizzy_until = 0;
    s_shake_force_x = 0.0f;
    s_shake_force_y = 0.0f;
    s_shake_active = false;
    s_bounce_live = false;
    for (int i = 0; i < 2; i++)
    {
        const eye_desc_t *d = (i == 0) ? &EXPRESSIONS[EXPRESSION_NEUTRAL].left
                                       : &EXPRESSIONS[EXPRESSION_NEUTRAL].right;
        s_morph_from[i] = *d;
        s_morph_from[i].glass = (i == 0) ? GLASS[EXPRESSION_NEUTRAL].l : GLASS[EXPRESSION_NEUTRAL].r;
        if (eyes_raster_get_style() == EYES_STYLE_GLASS) s_morph_from[i].accent = GLASS_ACCENT[EXPRESSION_NEUTRAL][i];
        s_morph_to[i] = s_morph_from[i];
        s_target[i] = s_morph_from[i];
        s_morph_p[i] = 1.0f;
        s_swapping[i] = false;
        /* Open from a slit on the squash spring: the eyes "wake up" instead of popping in. */
        s_squash[i] = SQUASH_SWAP_AT;
        s_squash_v[i] = 0.0f;
        s_ticks_alpha[i] = d->accent != EYES_ACCENT_NONE ? 1.0f : 0.0f;
    }

    s_panel_ready = alloc_buffers() && eyes_panel_begin();
    if (!s_panel_ready)
    {
        ESP_LOGE(TAG, "direct panel drawing unavailable; eyes will not be shown");
        free_buffers();
    }
    s_force_full = true;
    render_frame(true);
    play_expression_sound(s_expression);

    s_idle_timer = lv_timer_create(idle_timer_cb, rand_range(IDLE_MIN_MS, IDLE_MAX_MS), NULL);
    s_frame_timer = lv_timer_create(frame_timer_cb, FRAME_TIMER_MS, NULL);
    s_frame_paused = true;
    lv_timer_pause(s_frame_timer);
    wake_frame_timer();
}

static void expressive_eyes_on_standby(bool standby)
{
    if (!s_idle_timer)
    {
        return;
    }
    if (standby)
    {
        lv_timer_pause(s_idle_timer);
        if (s_frame_timer)
        {
            lv_timer_pause(s_frame_timer);
            s_frame_paused = true;
        }
    }
    else
    {
        reset_idle_personality(s_idle_owned);
        if (s_idle_owned)
        {
            s_expression = EXPRESSION_NEUTRAL;
            start_expression_transition(EXPRESSION_NEUTRAL);
        }
        lv_timer_set_period(s_idle_timer, rand_range(IDLE_MIN_MS, IDLE_MAX_MS));
        lv_timer_resume(s_idle_timer);
        /* Repaint everything on wake, and finish anything standby froze mid-flight. */
        s_force_full = true;
        wake_frame_timer();
    }
}

static uint8_t expressive_eyes_get_expression(void)
{
    return (uint8_t)s_expression;
}

static void expressive_eyes_fade_out_to(const uint16_t *image, uint32_t duration_ms, void (*done)(void *ctx),
                                        void *ctx)
{
    if (!s_panel_ready || !image || !s_frame_timer)
    {
        if (done)
        {
            done(ctx);
        }
        return;
    }
    s_fade_image = image;
    s_fade_ms = (float)LV_MAX(duration_ms, 1u);
    s_fade_elapsed_ms = 0.0f;
    s_fade_done = done;
    s_fade_ctx = ctx;
    take_fade_snapshot();
    /* Freeze the eyes; the fade owns every frame until it hands off. */
    if (s_idle_timer)
    {
        lv_timer_pause(s_idle_timer);
    }
    wake_frame_timer();
}

static void expressive_eyes_destroy(void)
{
    if (s_idle_timer)
    {
        lv_timer_del(s_idle_timer);
        s_idle_timer = NULL;
    }
    if (s_frame_timer)
    {
        lv_timer_del(s_frame_timer);
        s_frame_timer = NULL;
        s_frame_paused = true;
    }
    if (s_panel_ready)
    {
        /* Every frame ends with a waited blit, so no DMA still reads the strips here. */
        eyes_panel_end();
        s_panel_ready = false;
    }
    free_buffers();
    if (s_wrapper)
    {
        lv_obj_del(s_wrapper);
        s_wrapper = NULL;
    }
    s_curious_until = 0;
    s_shake_force_x = 0.0f;
    s_shake_force_y = 0.0f;
    s_dizzy_until = 0;
    s_shake_active = false;
    s_bounce_live = false;
    s_blinking = false;
    s_fade_image = NULL;
    s_fade_done = NULL;
    free_fade_snapshot();
    memset(s_eyes, 0, sizeof(s_eyes));
    memset(s_drawn, 0, sizeof(s_drawn));
    memset(s_drawn_draft, 0, sizeof(s_drawn_draft));
}

static const eyes_renderer_api_t s_api = {
    .create = expressive_eyes_create,
    .destroy = expressive_eyes_destroy,
    .on_pickup = expressive_eyes_on_pickup,
    .on_shake = expressive_eyes_on_shake,
    .set_expression = expressive_eyes_set_expression,
    .on_standby = expressive_eyes_on_standby,
    .fade_out_to = expressive_eyes_fade_out_to,
    .get_expression = expressive_eyes_get_expression,
};

void eyes_renderer_refresh_theme(void)
{
    s_force_full = true;
    wake_frame_timer();
}

const eyes_renderer_api_t *eyes_renderer_get_default(void)
{
    return &s_api;
}

#ifdef EYES_PREVIEW_NO_ROTATE
/* Host preview only: enter a dream without waiting for a probabilistic nap. */
void eyes_preview_start_dream(void)
{
    reset_idle_personality(true);
    uint32_t now = lv_tick_get();
    s_idle_mood = IDLE_NAPPING;
    s_idle_mood_since = now;
    s_idle_mood_until = now + 60000;
    s_idle_detail = 0;
    s_idle_detail_since = now;
    s_idle_dream_since = now;
    s_idle_detail_at = now + 60000;
    s_idle_lid = 0.035f;
    s_idle_slouch = 1.0f;
}
#endif
