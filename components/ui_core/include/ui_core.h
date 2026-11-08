#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "eyes_expression.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_EXPRESSION_DEFAULT = EYES_EXPRESSION_DEFAULT,
    UI_EXPRESSION_HAPPY = EYES_EXPRESSION_HAPPY,
    UI_EXPRESSION_EXCITED = EYES_EXPRESSION_EXCITED,
    UI_EXPRESSION_PLEASED = EYES_EXPRESSION_PLEASED,
    UI_EXPRESSION_SMILE = EYES_EXPRESSION_SMILE,
    UI_EXPRESSION_BIG_SMILE = EYES_EXPRESSION_BIG_SMILE,
    UI_EXPRESSION_CONTENT = EYES_EXPRESSION_CONTENT,
    UI_EXPRESSION_RELAXED = EYES_EXPRESSION_RELAXED,
    UI_EXPRESSION_BLINK = EYES_EXPRESSION_BLINK,
    UI_EXPRESSION_WINK_LEFT = EYES_EXPRESSION_WINK_LEFT,
    UI_EXPRESSION_WINK_RIGHT = EYES_EXPRESSION_WINK_RIGHT,
    UI_EXPRESSION_CURIOUS = EYES_EXPRESSION_CURIOUS,
    UI_EXPRESSION_SURPRISED = EYES_EXPRESSION_SURPRISED,
    UI_EXPRESSION_AMAZED = EYES_EXPRESSION_AMAZED,
    UI_EXPRESSION_LOVE = EYES_EXPRESSION_LOVE,
    UI_EXPRESSION_BLUSHING = EYES_EXPRESSION_BLUSHING,
    UI_EXPRESSION_SHY = EYES_EXPRESSION_SHY,
    UI_EXPRESSION_NERVOUS = EYES_EXPRESSION_NERVOUS,
    UI_EXPRESSION_ANGRY = EYES_EXPRESSION_ANGRY,
    UI_EXPRESSION_DETERMINED = EYES_EXPRESSION_DETERMINED,
    UI_EXPRESSION_SUSPICIOUS = EYES_EXPRESSION_SUSPICIOUS,
    UI_EXPRESSION_SAD = EYES_EXPRESSION_SAD,
    UI_EXPRESSION_WORRIED = EYES_EXPRESSION_WORRIED,
    UI_EXPRESSION_CONFUSED = EYES_EXPRESSION_CONFUSED,
    UI_EXPRESSION_SLEEPING = EYES_EXPRESSION_SLEEPING,
    UI_EXPRESSION_SLEEPING_ZZZ = EYES_EXPRESSION_SLEEPING_ZZZ,
    UI_EXPRESSION_DREAMY = EYES_EXPRESSION_DREAMY,
    UI_EXPRESSION_HYPNOTIZED = EYES_EXPRESSION_HYPNOTIZED,
    UI_EXPRESSION_FOCUSED = EYES_EXPRESSION_FOCUSED,
    UI_EXPRESSION_LAUGH = EYES_EXPRESSION_LAUGH,
    UI_EXPRESSION_STARTLED = EYES_EXPRESSION_STARTLED,
    UI_EXPRESSION_THINKING = EYES_EXPRESSION_THINKING,
    UI_EXPRESSION_REALIZATION = EYES_EXPRESSION_REALIZATION,
    UI_EXPRESSION_SKEPTICAL = EYES_EXPRESSION_SKEPTICAL,
    UI_EXPRESSION_ANNOYED = EYES_EXPRESSION_ANNOYED,
    UI_EXPRESSION_CRYING = EYES_EXPRESSION_CRYING,
    UI_EXPRESSION_DISAPPOINTED = EYES_EXPRESSION_DISAPPOINTED,
    UI_EXPRESSION_TIRED = EYES_EXPRESSION_TIRED,
    UI_EXPRESSION_PLAYFUL = EYES_EXPRESSION_PLAYFUL,
    UI_EXPRESSION_GLITCH = EYES_EXPRESSION_GLITCH,
    UI_EXPRESSION_SMUG = EYES_EXPRESSION_SMUG,
    UI_EXPRESSION_COUNT = EYES_EXPRESSION_COUNT,
} ui_expression_t;

typedef struct {
    int (*get_brightness)(void);
    int (*get_volume)(void);
    /* Apply immediately; persist only when commit is true. */
    void (*set_brightness)(int value, bool commit);
    void (*set_volume)(int value, bool commit);
    bool (*get_dnd)(void);
    void (*set_dnd)(bool enabled);
    void (*sleep)(void);
    void (*wake)(void); /* optional: turn the display back on */
    int (*get_eyes_theme)(void);       /* optional: index into eyes_themes_*() */
    void (*set_eyes_theme)(int index); /* optional: apply and save */
    int (*get_eyes_style)(void);       /* optional: 0 orb, 1 squircle */
    void (*set_eyes_style)(int index); /* optional: apply and save */
} ui_settings_controls_t;

/** Configure hardware controls before settings can be opened. Copies the callbacks. */
void ui_core_configure_settings(const ui_settings_controls_t *controls);

/** One-time setup; call after display bring-up and before showing any screen. */
void ui_core_init(void);

/** Load the boot splash screen. */
void ui_core_show_welcome(void);

/** Fade-transition into the idle eyes screen. */
void ui_core_show_eyes(void);

/** Notify the active eyes renderer that the device was picked up/handled. */
void ui_core_notify_pickup(void);

/** Forward filtered device motion to the active eyes renderer. */
void ui_core_notify_shake(float accel_x, float accel_y, bool active);

/** Select a built-in expression; its matching WAV sound plays when it changes. */
void ui_core_set_expression(ui_expression_t expression);

/** Remote control (phone): show this eye mood, going to the eyes screen first if needed. */
void ui_core_remote_mood(int expression);

/** The mood being shown (or about to be, after a remote request); -1 when the eyes are not on screen. */
int ui_core_get_expression(void);

/** Firmware update screen (call with the display lock held). It replaces whatever is showing,
 *  wakes the display, and ignores swipes until ui_core_update_fail() returns to the eyes. */
void ui_core_update_begin(const char *version);
void ui_core_update_progress(int percent);
/** Shows the reason for a few seconds, then goes back to the eyes. */
void ui_core_update_fail(const char *reason);
/** Installed: shows the restart notice. The screen stays up until the device reboots. */
void ui_core_update_done(void);

/** Pause/resume eye animation for display standby (screen off/on). */
void ui_core_set_standby(bool standby);

#ifdef __cplusplus
}
#endif
