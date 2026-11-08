#include "eyes_theme.h"
#include "eyes_raster.h"
#include <math.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    const char *name;
    float hue_shift; /* degrees, relative to the amber base */
    float saturation; /* multiplier */
} preset_t;

static const preset_t s_presets[] = {
    { "Amber", 0.0f, 1.0f },
    { "Rose", -45.0f, 1.0f },
    { "Violet", -115.0f, 1.0f },
    { "Ice", 170.0f, 1.0f },
    { "Mint", 120.0f, 1.0f },
    { "Lime", 60.0f, 1.0f },
    { "Mono", 0.0f, 0.08f },
    { "Cyan", 150.0f, 1.0f },
};
#define PRESET_COUNT ((int)(sizeof(s_presets) / sizeof(s_presets[0])))

static const eyes_theme_t s_amber = {
    .fill_top = 0xFFF9E3, .fill_mid = 0xFFD460, .fill_bottom = 0xFF6A10, .rim = 0x4A1E06,
    .ring = 0xFF9A2A, .glow = 0xFF7A0A, .dot = 0xFFF3CC, .edge = 0x8B4A1F, .spiral = 0x6A2C08,
};

#include "generated/eye_pack_meta.inc"

static eyes_theme_t s_themes[PRESET_COUNT];
static bool s_built;
static int s_selected;

static uint32_t shift_color(uint32_t rgb, float hue_shift, float saturation)
{
    float r = ((rgb >> 16) & 0xFF) / 255.0f, g = ((rgb >> 8) & 0xFF) / 255.0f, b = (rgb & 0xFF) / 255.0f;
    float max = fmaxf(r, fmaxf(g, b)), min = fminf(r, fminf(g, b)), d = max - min;
    float h = 0.0f;
    if (d > 0.0f) {
        if (max == r) h = fmodf((g - b) / d, 6.0f);
        else if (max == g) h = (b - r) / d + 2.0f;
        else h = (r - g) / d + 4.0f;
        h *= 60.0f;
    }
    float s = max > 0.0f ? d / max : 0.0f;
    h = fmodf(h + hue_shift + 720.0f, 360.0f);
    s = fminf(1.0f, s * saturation);
    float c = max * s, x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = max - c;
    float rr, gg, bb;
    switch ((int)(h / 60.0f) % 6) {
    case 0: rr = c; gg = x; bb = 0; break;
    case 1: rr = x; gg = c; bb = 0; break;
    case 2: rr = 0; gg = c; bb = x; break;
    case 3: rr = 0; gg = x; bb = c; break;
    case 4: rr = x; gg = 0; bb = c; break;
    default: rr = c; gg = 0; bb = x; break;
    }
    uint32_t R = (uint32_t)lroundf((rr + m) * 255.0f), G = (uint32_t)lroundf((gg + m) * 255.0f),
             B = (uint32_t)lroundf((bb + m) * 255.0f);
    return R << 16 | G << 8 | B;
}

static void build(void)
{
    if (s_built) return;
    for (int i = 0; i < PRESET_COUNT; i++) {
        const preset_t *p = &s_presets[i];
        const uint32_t *src = &s_amber.fill_top;
        uint32_t *dst = &s_themes[i].fill_top;
        for (int k = 0; k < 9; k++) dst[k] = shift_color(src[k], p->hue_shift, p->saturation);
    }
    s_themes[0] = s_amber; /* exact original, no rounding */
    for (int i = 1; i < PRESET_COUNT; i++) {
        if (s_presets[i].name[0] == 'C' && s_presets[i].name[1] == 'y') {
            /* Cyan is drawn from the Square reference art rather than hue-shifted from amber. */
            s_themes[i] = (eyes_theme_t){ .fill_top = 0x59E6FF, .fill_mid = 0x1ABDF7, .fill_bottom = 0x23CDFB, .rim = 0x0A5C80,
                .ring = 0x6CF5FF, .glow = 0x00DBFF, .dot = 0xC7FFFF, .edge = 0x0A6A8C, .spiral = 0x0A5C80 };
        }
    }
    s_built = true;
}

int eyes_themes_count(void)
{
    return PRESET_COUNT;
}

const char *eyes_themes_name(int index)
{
    return index >= 0 && index < PRESET_COUNT ? s_presets[index].name : "";
}

int eyes_pack_mood_count(int style)
{
    return style >= 0 && style < eyes_styles_count() ? PACK_MOOD_COUNT[style] : 0;
}

int eyes_pack_mood(int style, int n)
{
    return n >= 0 && n < eyes_pack_mood_count(style) ? PACK_MOODS[style][n] : 0;
}

const char *eyes_mood_id(int mood)
{
    return mood >= 0 && mood < (int)(sizeof(MOOD_IDS) / sizeof(MOOD_IDS[0])) ? MOOD_IDS[mood] : "";
}

const char *eyes_mood_label(int mood)
{
    return mood >= 0 && mood < (int)(sizeof(MOOD_LABELS) / sizeof(MOOD_LABELS[0])) ? MOOD_LABELS[mood] : "";
}

const eyes_theme_t *eyes_themes_get(int index)
{
    build();
    return &s_themes[index >= 0 && index < PRESET_COUNT ? index : 0];
}

int eyes_themes_selected(void)
{
    return s_selected;
}

void eyes_themes_select(int index)
{
    s_selected = index >= 0 && index < PRESET_COUNT ? index : 0;
    eyes_raster_set_theme(eyes_themes_get(s_selected));
}

static int s_style;

int eyes_styles_count(void)
{
    return (int)(sizeof(PACK_TITLES) / sizeof(PACK_TITLES[0]));
}

const char *eyes_styles_name(int index)
{
    return index >= 0 && index < eyes_styles_count() ? PACK_TITLES[index] : "";
}

int eyes_styles_default_theme(int index)
{
    if (index < 0 || index >= eyes_styles_count()) return 0;
    for (int i = 0; i < PRESET_COUNT; i++) {
        if (!strcmp(s_presets[i].name, PACK_THEMES[index])) return i;
    }
    return 0;
}

int eyes_styles_selected(void)
{
    return s_style;
}

void eyes_styles_select(int index)
{
    s_style = index >= 0 && index < eyes_styles_count() ? index : 0;
    eyes_raster_set_style(s_style);
}
