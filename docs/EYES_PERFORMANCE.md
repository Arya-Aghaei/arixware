# Eyes rendering performance

The eyes redraw while a mood changes, while they blink and while they idle. On the ESP32-S3 the
software rasterizer is the heaviest thing the display does, so a small change to it can make mood
changes stutter. This page records what is measured, why the code is shaped the way it is, and how
to check a change.

## Measured on the device

Time to draw one eye sprite (about 150 x 170 px) on the chip, from a timing log around
`eyes_raster_render()` while moods were changed from the phone remote:

| Case | Time |
| --- | --- |
| Idle, round eyes (capsule, interior fast path) | about 10 ms |
| Glossy shapes (arch, half, slope, heart, oval, worried), full quality | 70 to 100 ms |
| Egg (Curious), full quality | 110 to 130 ms |
| Same shapes as half-resolution drafts | 20 to 40 ms |

The host numbers from `eyes_bench` are roughly 200 to 300 times smaller, so use them to compare a
change against the old code, not as absolute figures.

## Rules that keep mood changes smooth

1. **Frames in motion are drafts.** While a pose is changing, `plan_raster()` draws it with
   `eyes_raster_pose_t.draft`, which shades 2x2 pixel blocks (about 3x cheaper). When the pose
   stops, one full-quality draw replaces the draft (`s_drawn_draft`). Keep both halves: a draft
   must always be followed by a full draw, and shapes that mirror rows (capsule) ignore the flag.
2. **Never wait for the raster helper on the UI thread.** `render_frame()` uses
   `eyes_panel_try_join()` and skips the frame while the helper is still drawing. A full draw can
   take over 100 ms; `eyes_panel_join()` is only for a forced full repaint.
3. **Everything in a shade function runs per pixel.** Hoist constants into `setup_*()` (see the
   egg's `egg_ix`/`egg_iy`), avoid divisions, trigonometry and extra square roots, and gate
   optional effects on a cheap test first (the highlight streak only runs inside its distance band).
4. **A blink flattens, it does not scale.** Shapes that size themselves from the box must scale
   each axis separately (egg, worried). A uniform scale reads as a zoom in and out on every blink.
5. **Keep the sprite small.** Cost grows with the sprite area: the glow radius, the rim widths and
   the 300 x 300 sprite cap all feed it.
6. **Distances only need to be approximate, but never too large.** Shape distances are used for
   anti-aliasing and for skipping empty blocks. An estimate that is too small only thickens the
   glow; one that is too large cuts parts of the shape off.

## Adding or changing a shape

Pack data (sizes, lids, pupils) lives in `eyes/*.json`: see `docs/EYE_PACKS.md`. This page covers the rasterizer.


1. Add the shape to the three `switch` lists in `eyes_raster.c` (`RASTER_BANDS` selection and
   `setup_geom`) and, if it blinks, to `is_blinkable()` in `expressive_eyes.c`.
2. Run the checks below, and compare `eyes_bench` before and after.
3. Look at the new frames next to the reference chart (`/tmp/arixware-eyes/eyes_preview --all <dir>`).

## Checks

Host timing and tests:

```sh
cmake -S tools/eyes_preview -B /tmp/arixware-eyes -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/arixware-eyes -j 8
/tmp/arixware-eyes/eyes_bench
ctest --test-dir /tmp/arixware-eyes --output-on-failure
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined \
  components/eyes_renderer/eyes_raster.c components/eyes_renderer/test/test_eyes_raster.c \
  -lm -o /tmp/eyes-raster-test && /tmp/eyes-raster-test
```

On the device, wrap the call in `raster_job()` (`expressive_eyes.c`) with `esp_timer_get_time()`,
log the duration per shape, and change moods from the phone remote. Opening the serial port resets
the chip, so start the capture first and then change moods. Targets: drafts under about 40 ms, a
full draw under about 130 ms, and nothing that blocks the UI thread.
