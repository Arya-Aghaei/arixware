#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The colors of the glossy orb material (0xRRGGBB). */
typedef struct {
    uint32_t fill_top;    /* gradient body, top */
    uint32_t fill_mid;
    uint32_t fill_bottom;
    uint32_t rim;         /* dark band inside the edge */
    uint32_t ring;        /* thin bright outer ring */
    uint32_t glow;        /* soft outer bloom */
    uint32_t dot;         /* halftone dots */
    uint32_t edge;        /* accent ticks */
    uint32_t spiral;      /* dizzy spiral stripes */
} eyes_theme_t;

/* Built-in color themes. Index 0 is the original amber. */
int eyes_themes_count(void);
const char *eyes_themes_name(int index);
const eyes_theme_t *eyes_themes_get(int index);

int eyes_themes_selected(void);
/** Selects a theme for every eye drawn from now on (out of range selects 0). Does not repaint an
 *  eye that is already on screen: call the renderer's refresh for that. */
void eyes_themes_select(int index);

/* Eye styles ("packs"): the outline the eyes are drawn with. 0 = round orbs, 1 = rounded squares. */
int eyes_styles_count(void);
const char *eyes_styles_name(int index);
int eyes_styles_selected(void);
void eyes_styles_select(int index);
/** The moods a pack shows, in its own order (mood numbers are catalog indices, see eyes_expression.h). */
int eyes_pack_mood_count(int style);
int eyes_pack_mood(int style, int n);
const char *eyes_mood_id(int mood);
const char *eyes_mood_label(int mood);
/** The color theme a style looks best in (its pack says which). */
int eyes_styles_default_theme(int index);

#ifdef __cplusplus
}
#endif
