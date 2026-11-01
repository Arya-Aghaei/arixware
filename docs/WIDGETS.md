# Making your own widget screen

This tutorial shows how to add a new screen ("widget") next to the eyes and the clock.
Swiping left or right on the device moves between these screens.

Go through it once with the example below, then change the example into what you want.

---

## 1. How the screens work (2-minute background)

### Eye expressions

The built-in expressions follow the reference chart, left to right, then top to bottom:

| IDs | Expressions |
| --- | --- |
| 0–5 | Default, Happy, Excited, Pleased, Smile, Big Smile |
| 6–11 | Content, Relaxed, Blink, Wink (L), Wink (R), Curious |
| 12–17 | Surprised, Amazed, Love, Blushing, Shy, Nervous |
| 18–23 | Angry, Determined, Suspicious, Sad, Worried, Confused |
| 24–29 | Sleeping, Sleeping (zzz), Dreamy, Hypnotized, Focused, Laugh |

Use the named `UI_EXPRESSION_*` values from
[ui_core.h](../components/ui_core/include/ui_core.h) with `ui_core_set_expression()`.
These replace the previous expression IDs; callers must use the new names/order.
Default idle eyes, gaze, blink timing, and motion physics remain unchanged.
Pickup selects Curious; shaking selects Hypnotized; touch selects Angry.
Existing WAV assets are reused by mood; the held Blink expression is silent.
The host preview accepts `--all OUTPUT_DIRECTORY` to export all 30 settled frames
into an existing directory.

- The display is a round 466 x 466 pixel panel, mounted turned 90 degrees inside the arix.
- **You always draw upright.** The code turns your picture to match the panel.
- Coordinates: `x` goes right, `y` goes down, `(233, 233)` is the centre.
- Every screen is a function that returns an LVGL screen object: `lv_obj_t *ui_screen_xxx_create(void)`.
- The list of swipeable screens is `s_screens[]` in
  [components/ui_core/ui_core.c](../components/ui_core/ui_core.c). Index 0 (the eyes) is home.

**The one rule to remember:** LVGL takes about 80 ms to redraw the whole screen on this board.
That is fine for content that changes once per second (like the clock). It is too slow for
smooth animation. For animation, see [section 8](#8-smooth-animation).

---

## 2. Create the header

Create `components/ui_core/include/ui_screen_hello.h`:

```c
#pragma once
#include "lvgl.h"
#include <stdint.h>

lv_obj_t *ui_screen_hello_create(void);
/** This screen's pixels exactly as the panel shows them, so the eyes can fade into it. */
const uint16_t *ui_screen_hello_panel_pixels(void);
```

---

## 3. Create the screen

Create `components/ui_core/ui_screen_hello.c`. This example shows a counter that goes up
once per second. Read the comments; the parts marked **CHANGE ME** are yours to edit.

```c
#include "ui_screen_hello.h"
#include "ui_orient.h"
#include "fonts.h"
#include "esp_heap_caps.h"
#include <stdio.h>

#define FACE_SIZE 466
#define UPDATE_MS 1000 /* CHANGE ME: how often redraw() runs */

static lv_obj_t *s_draw_canvas; /* hidden: you draw upright into this */
static lv_obj_t *s_canvas;      /* visible: the same picture, turned for the panel */
static uint16_t *s_draw_buf;
static uint16_t *s_buf;
static lv_timer_t *s_timer;
static int s_count;

/* Turns the upright picture to match the panel. Do not change. */
static void rotate_into(uint16_t *dst, const uint16_t *src)
{
    for (int y = 0; y < FACE_SIZE; y++) {
        for (int x = 0; x < FACE_SIZE; x++) {
            int px = x, py = y;
            switch (UI_ROTATION_DEG) {
            case 90: px = FACE_SIZE - 1 - y; py = x; break;
            case 180: px = FACE_SIZE - 1 - x; py = FACE_SIZE - 1 - y; break;
            case 270: px = y; py = FACE_SIZE - 1 - x; break;
            default: break;
            }
            dst[py * FACE_SIZE + px] = src[y * FACE_SIZE + x];
        }
    }
}

/* Draws text centred on (cx, cy). */
static void draw_text(lv_layer_t *layer, const char *text, const lv_font_t *font,
                      int cx, int cy, uint32_t color)
{
    int h = lv_font_get_line_height(font);
    lv_draw_label_dsc_t dsc;
    lv_draw_label_dsc_init(&dsc);
    dsc.text = text;
    dsc.text_local = 1; /* copy the text: it is drawn after this function returns */
    dsc.font = font;
    dsc.color = lv_color_hex(color);
    dsc.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t area = { cx - 200, cy - h / 2, cx + 200, cy + h / 2 };
    lv_draw_label(layer, &dsc, &area);
}

/* CHANGE ME: everything you want on screen is drawn here. */
static void redraw(void)
{
    lv_canvas_fill_bg(s_draw_canvas, lv_color_black(), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(s_draw_canvas, &layer);

    draw_text(&layer, "Hello arix", &fredoka_medium_26, 233, 190, 0xFFD49B);
    char text[16];
    snprintf(text, sizeof(text), "%d", s_count);
    draw_text(&layer, text, &fredoka_medium_26, 233, 250, 0xFFF4DF);

    /* A filled circle, as an example shape. */
    lv_draw_rect_dsc_t circle;
    lv_draw_rect_dsc_init(&circle);
    circle.bg_color = lv_color_hex(0xFF9347);
    circle.radius = LV_RADIUS_CIRCLE;
    lv_area_t box = { 223, 300, 243, 320 };
    lv_draw_rect(&layer, &circle, &box);

    lv_canvas_finish_layer(s_draw_canvas, &layer);
    rotate_into(s_buf, s_draw_buf);
    lv_obj_invalidate(s_canvas);
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_count++; /* CHANGE ME: update your data here */
    redraw();
}

/* Runs when you swipe away: stop the timer and free memory, or it leaks. */
static void delete_cb(lv_event_t *e)
{
    (void)e;
    lv_timer_delete(s_timer);
    s_timer = NULL;
    heap_caps_free(s_buf);
    heap_caps_free(s_draw_buf);
    s_buf = s_draw_buf = NULL;
    s_canvas = s_draw_canvas = NULL;
}

const uint16_t *ui_screen_hello_panel_pixels(void)
{
    return s_buf;
}

lv_obj_t *ui_screen_hello_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);

    size_t size = FACE_SIZE * FACE_SIZE * sizeof(uint16_t);
    s_buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);      /* PSRAM: plenty of space */
    s_draw_buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

    s_draw_canvas = lv_canvas_create(scr);
    lv_obj_set_hidden(s_draw_canvas, true);
    lv_canvas_set_buffer(s_draw_canvas, s_draw_buf, FACE_SIZE, FACE_SIZE, LV_COLOR_FORMAT_RGB565);

    s_canvas = lv_canvas_create(scr);
    lv_obj_remove_style_all(s_canvas);
    lv_canvas_set_buffer(s_canvas, s_buf, FACE_SIZE, FACE_SIZE, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(s_canvas);

    s_count = 0;
    redraw();
    s_timer = lv_timer_create(timer_cb, UPDATE_MS, NULL);
    lv_obj_add_event_cb(scr, delete_cb, LV_EVENT_DELETE, NULL);
    return scr;
}
```

For a full real example (glowing text, background image, date) read
[ui_screen_clock.c](../components/ui_core/ui_screen_clock.c). It follows the same pattern.

### Background images

The clock uses `01_clock_sunrise_466.png`; settings and its subpages use
`02_settings_planet_466.png`, from
[components/assets/backgrounds/png](../components/assets/backgrounds/png).
Both are upright 466 x 466 images; screens apply panel rotation after composition.
The [image component](../components/images/CMakeLists.txt) stages these PNGs alongside
the button skins and converts them to QOI for the image partition.
Backgrounds receive a 40% black mask once when decoded, improving content contrast
without dimming text or button skins or adding per-frame blending.
The remaining PNGs are reserved for matching widgets that are not implemented yet;
add only the assets a new widget uses so the image partition stays within its size limit.

---

## 4. Register the screen (3 small edits)

**a. Build it.** In [components/ui_core/CMakeLists.txt](../components/ui_core/CMakeLists.txt) add
`"ui_screen_hello.c"` to `SRCS`.

**b. Add it to the swipe list.** In [components/ui_core/ui_core.c](../components/ui_core/ui_core.c):

```c
#include "ui_screen_hello.h"   /* next to the other ui_screen_*.h includes */

static const ui_screen_t s_screens[] = {
    { ui_screen_eyes_create, NULL },
    { ui_screen_clock_create, ui_screen_clock_panel_pixels },
    { ui_screen_hello_create, ui_screen_hello_panel_pixels },   /* new */
};
```

The order in this list is the swipe order. The list wraps around at the ends.

**c. Keep the host test building.** The test in `tests/settings_ui/main.c` replaces the real
screens with small stand-ins. Add one for yours next to `ui_screen_clock_create` there:

```c
lv_obj_t *ui_screen_hello_create(void) { return lv_obj_create(NULL); }
const uint16_t *ui_screen_hello_panel_pixels(void) { return NULL; }
```

---

## 5. Build, flash and try it

### Every new terminal: load the ESP-IDF tools

```sh
cd ~/Projects/arixware
. ~/Projects/esp-idf/export.sh      # note the dot and space at the start
```

Without this step, `idf.py: command not found` appears.

### Find the device port

Connect the device with USB, then:

```sh
ls /dev/cu.usbmodem*
```

Usually it is `/dev/cu.usbmodem101`. Use what this command prints in the commands below.

### The commands

| What | Command |
|---|---|
| Build only (check your code compiles) | `idf.py build` |
| Build and flash | `idf.py -p /dev/cu.usbmodem101 build flash` |
| Build, flash and show the log | `idf.py -p /dev/cu.usbmodem101 build flash monitor` |
| Only show the log | `idf.py -p /dev/cu.usbmodem101 monitor` |

- Exit the monitor with **Ctrl + ]**.
- Only the changed files are rebuilt, so later builds take seconds, not minutes.
- Build errors look like `file.c:42:5: error: ...`. The numbers are the line and column.

### If something goes wrong

| Problem | Fix |
|---|---|
| `idf.py: command not found` | Run the `export.sh` line again. |
| Port busy, or `Failed to connect` | Close any other monitor or serial program. Unplug and replug the USB cable, then try again. |
| Still cannot connect | Hold **BOOT**, press and release **RESET**, release **BOOT**, then flash. |
| Device keeps restarting, log shows `Guru Meditation` | Your code crashed. Copy the `Backtrace:` line; the monitor prints the file and line under it. |
| Strange build errors after big changes | `idf.py fullclean`, then build again (takes a few minutes). |

### Try it

On the device, swipe left from the eyes until your screen appears.

---

## 6. Testing

Test in this order: the fast checks on your Mac first, then the device.

### a. Automatic tests on your Mac (no device needed)

```sh
cd ~/Projects/arixware
cmake -S tests/settings_ui -B /tmp/st        # once (or after adding files)
cmake --build /tmp/st -j 8                   # after every change
ctest --test-dir /tmp/st --output-on-failure
```

- `100% tests passed` means everything is fine.
- A failure prints `Assertion failed` with a file and line. That line is the check that broke.
- These tests run the real touch, swipe, settings and screen-switching code. Run them after any
  change in `components/ui_core` or `main/settings.c`.

The eye bounce physics has its own quick check:

```sh
python3 tests/check_eye_bounce.py
```

### b. See the picture without the device

**Settings pages.** Render them to image files and open them:

```sh
/tmp/st/settings_ui_test /tmp/main.ppm /tmp/wifi.ppm /tmp/bt.ppm /tmp/display.ppm /tmp/system.ppm /tmp/more.ppm
open /tmp/main.ppm
```

**Eyes.** Expressions are numbers, in the order of `ui_expression_t` in
[ui_core.h](../components/ui_core/include/ui_core.h) (0 = neutral, 2 = happy, 17 = dizzy):

```sh
cmake -S tools/eyes_preview -B tools/eyes_preview/build      # once
cmake --build tools/eyes_preview/build
tools/eyes_preview/build/eyes_preview 2                      # one frame -> /tmp/eyes_preview.ppm
tools/eyes_preview/build/eyes_preview 0 2 10 400             # neutral to happy, 10 frames over 400 ms
open /tmp/eyes_preview.ppm                                   # transition: /tmp/eyes_transition_00.ppm ...
```

The previews are drawn turned for the panel (for the eyes, this does not matter).

### c. On the device

Flash with the monitor open (section 5), then check:

- [ ] It boots without `Guru Meditation` or `E (...)` error lines in the log.
- [ ] Your screen appears when you swipe, and it looks right side up.
- [ ] Taps land where you touch.
- [ ] Swiping away and back 10 times still works. If it gets slow or crashes, memory leaks:
      check your delete callback.
- [ ] Animation looks smooth, not jumpy.
- [ ] After pressing the BOOT button (screen off) and again (screen on), your screen still works.

To print your own values to the log while testing:

```c
#include "esp_log.h"
ESP_LOGI("hello", "count=%d", s_count);   /* shows as: I (1234) hello: count=5 */
```

Remove these lines when you are done.

---

## 7. Common additions

### React to a tap

Add this in `ui_screen_hello_create()`, before `return scr;`:

```c
lv_obj_add_event_cb(scr, tap_cb, LV_EVENT_CLICKED, NULL);
```

And this function above `ui_screen_hello_create()`:

```c
static void tap_cb(lv_event_t *e)
{
    (void)e;
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    int y = 465 - p.x; /* touch point turned upright (UI_ROTATION_DEG = 90); upright x = p.y */
    if (y > 200) {     /* CHANGE ME: tapped the lower half */
        s_count = 0;
        redraw();
    }
}
```

Do not use left/right swipes inside a widget: they switch screens. A swipe down from the top
edge opens Settings.

### Play a sound when something happens

Sounds are WAV files in `components/assets/wav/`. The build packs them, and each file gets a
number named after it in `mmap_generate_sounds.h`:
`touch.wav` becomes `MMAP_SOUNDS_TOUCH_WAV`, and `03_happy.wav` becomes `MMAP_SOUNDS_03_HAPPY_WAV`.

1. Let ui_core use the audio component: in
   [components/ui_core/CMakeLists.txt](../components/ui_core/CMakeLists.txt) add `audio` to
   `PRIV_REQUIRES`.
2. In your screen file:

```c
#include "audio.h"
#include "mmap_generate_sounds.h"
```

3. Call `audio_play_index()` in the action, for example at the start of `tap_cb`:

```c
audio_play_index(MMAP_SOUNDS_TOUCH_WAV);
```

That's all. The sound plays in the background, so your code does not wait for it.
It is silent while Do Not Disturb is on, and it uses the Settings volume.

**Add your own sound:**

1. Save it as WAV, mono, 16000 Hz, 16-bit (the same format as the existing files). With the
   free tool `ffmpeg`: `ffmpeg -i input.mp3 -ac 1 -ar 16000 -sample_fmt s16 my_sound.wav`
2. Put it in `components/assets/wav/`. Use lower case, digits and `_` in the name.
3. Build once. Then `MMAP_SOUNDS_MY_SOUND_WAV` exists and you can play it.

Adding a file renumbers the sounds after it alphabetically. Always use the names, never the
numbers.

### Use a background image

1. Make a 466 x 466 PNG, **upright** (not turned). Put it in `components/assets/backgrounds/`,
   for example `hello_background.png`. The build packs it automatically.
2. In [components/images/include/images.h](../components/images/include/images.h) add
   `IMAGE_HELLO_BACKGROUND` to `image_id_t`.
3. In [components/images/images.c](../components/images/images.c):
   - In `mmap_index_for()` add
     `case IMAGE_HELLO_BACKGROUND: return MMAP_IMAGES_HELLO_BACKGROUND_QOI;`.
     The name is the file name in capitals plus `_QOI`.
   - Raise `MAX_CACHED_IMAGES` by one (all 4 slots are already used).
4. Add `#include "images.h"` to your file. Then in `redraw()`, right after
   `lv_canvas_init_layer(...)`:

```c
const lv_image_dsc_t *bg = images_get(IMAGE_HELLO_BACKGROUND);
if (bg) {
    lv_draw_image_dsc_t img;
    lv_draw_image_dsc_init(&img);
    img.src = bg;
    lv_area_t area = { 0, 0, FACE_SIZE - 1, FACE_SIZE - 1 };
    lv_draw_image(&layer, &img, &area);
}
```

### Fonts

| Font | Use | Characters |
|---|---|---|
| `fredoka_medium_16` | small text | all plain ASCII, plus `°`, `×`, `•` |
| `fredoka_medium_26` | headings | all plain ASCII |
| `fredoka_bold_40` | clock date, seconds, and welcome heading | see the generated font range |
| `fredoka_bold_144` | clock hours and minutes | digits and `:` |
| `fredoka_bold_72` | timer and alarm digits | digits and `:` |

Characters missing from a font show as boxes. To add some, regenerate the font with the
command written at the top of its `.c` file in `components/fonts/`.

### Colours

Write colours as `0xRRGGBB`. The amber theme uses:

| Use | Colour |
|---|---|
| Main text | `0xFFF4DF` |
| Secondary text | `0xFFD49B` |
| Accent / glow | `0xFF9347` |

---

## 8. Smooth animation

If your widget has to move every frame (a bouncing ball, a progress ring), LVGL redraws are too
slow. Draw straight to the panel instead, like the eyes and Settings do:

- [ui_screen_settings.c](../components/ui_core/ui_screen_settings.c) is the reference: it draws
  into its own buffers, turns only the changed rectangle, and sends it with `ui_panel_send()`.
- Take the panel with `eyes_panel_begin()` when the screen is created and give it back with
  `eyes_panel_end()` in the delete callback.
- Redraw only the small area that changed, about 60 times per second (a 16 ms `lv_timer`).

This is more work. Start from the LVGL version above and switch only if it looks choppy.

### Eye shape rendering performance

The eyes are drawn by a software rasterizer,
[eyes_raster.c](../components/eyes_renderer/eyes_raster.c), that shades every pixel from a
signed distance function (SDF: negative inside the shape, positive outside). To stay fast it
skips most of that per-pixel work using 8x8 pixel blocks (`RASTER_BANDS`, `BLOCK_REACH`):
background blocks are `memset` instead of shaded, and for `CAPSULE`/`SPIRAL` deep-interior
blocks are filled directly from the precomputed gradient instead of evaluating the SDF per pixel.

That interior fast-fill is only safe for shapes with an **exact** SDF, like `sd_stadium` (used by
`CAPSULE`/`SPIRAL`): if a block's center is at least `BLOCK_REACH` pixels inside the shape, every
pixel in that block is guaranteed to be interior too, because an exact SDF never changes faster
than 1 pixel of distance per pixel moved (it's 1-Lipschitz).

The reference-chart shapes (`ARCH`, `HALF`, `SLOPE`, `HEART`, `WORRIED`, `SLEEP`, all shaded via
`reference_sd()`) blend and round together several distance functions. That makes them cheaper to
express but **not** exact SDFs, and not reliably 1-Lipschitz — so the same interior fast-fill
isn't safe for them. They only get the background-culling half of the optimization
(`outer_reference`), the same restriction `CHEVRON`/`CRESCENT` already have.

This was verified the hard way: adding an interior fast-fill for the reference shapes looked
correct in a 30-expression visual check (`eyes_preview --all`, pixel-identical output), but broke
rotation invariance — real, large color errors, not rounding noise. It was only caught by the
project's own test suite. **Before trusting any change to `eyes_raster.c`'s shading or block
classification, run it:**

```sh
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  components/eyes_renderer/eyes_raster.c components/eyes_renderer/test/test_eyes_raster.c \
  -lm -o /tmp/.eyes-raster-test && /tmp/.eyes-raster-test
```

A visual diff across all expressions is a useful sanity check but is not sufficient on its own —
it doesn't exercise rotation, so it can miss exactly this class of bug.

---

## 9. Rules that prevent crashes and flicker

1. Never delete a screen from inside its own event or timer callback. Schedule it with
   `lv_async_call(...)` instead (see `ui_core.c`).
2. Always delete your timers and free your buffers in the `LV_EVENT_DELETE` callback.
3. Do not use LVGL fades, rotation or other transforms on full-screen content. They are very
   slow on this panel.
4. Allocate big buffers in PSRAM (`MALLOC_CAP_SPIRAM`). Internal RAM is small and needed by the
   display driver.

---

## 10. Save your work

```sh
git add -A
git commit -m "feat(ui): add hello widget"
git push origin main
```

Commit messages use the `type(scope): summary` format. Types: `feat`, `fix`, `docs`,
`refactor`, `perf`, `test`, `chore`.
