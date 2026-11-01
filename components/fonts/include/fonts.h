#pragma once
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Fredoka (OFL-licensed, see OFL-Fredoka.txt), converted with 4bpp antialiasing.
 * The clock uses Bold; compact settings captions and headings use Medium. */
extern const lv_font_t fredoka_medium_16; /* settings captions and info rows (printable ASCII, °, ×, •) */
extern const lv_font_t fredoka_medium_26; /* settings headings (printable ASCII) */
extern const lv_font_t fredoka_bold_72; /* timer and alarm digits */


/* Native-size fonts for the larger clock and settings layout. */
extern const lv_font_t fredoka_medium_22;
extern const lv_font_t fredoka_medium_24;
extern const lv_font_t fredoka_medium_28;
extern const lv_font_t fredoka_medium_36;
extern const lv_font_t fredoka_bold_144;
extern const lv_font_t fredoka_bold_40;

#ifdef __cplusplus
}
#endif
