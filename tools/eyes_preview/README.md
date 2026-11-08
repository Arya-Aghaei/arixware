# Eyes host preview

Normal idle behavior is enabled. Sleep pressure increases after two minutes
without interaction; after five minutes the next drowsy sequence leads to a nap.
Interaction and a completed nap reset the pressure, so the robot has an awake
period between naps.

Set `EYES_SLEEP_DREAM_TEST` to `1` in
`components/eyes_renderer/expressive_eyes.c` to stay asleep and cycle through
all three dreams every 14 seconds for visual testing. Set it back to `0` and
rebuild to restore normal behavior.

Build and run the behavior and animation checks:

```sh
cmake -S tools/eyes_preview -B /tmp/arixware-eyes
cmake --build /tmp/arixware-eyes -j 8
ctest --test-dir /tmp/arixware-eyes --output-on-failure
/tmp/arixware-eyes/eyes_preview --dream 6000
```

`EYES_STYLE=1 EYES_THEME=7` previews the Square pack in Cyan. `eyes_bench` times the rasterizer for every shape (and the Square eyes) (see `docs/EYES_PERFORMANCE.md`).

The last command renders a sheep dream at six seconds to `/tmp/eyes_preview.ppm`.
`--dream-fence <milliseconds>` previews the orange sheep jumping over a fence.
`--dream-saving <milliseconds>` previews the wallet and banknotes.
`--idle <milliseconds>` previews the autonomous behavior without forcing sleep.

Dream playback randomly chooses from `Counting Sheeps.gif` (blue sky),
`Counting Sheep.gif` (orange fence), and `Saving.gif` (wallet and banknotes) once
per dream and retains that clip until the dream ends. The source GIFs live under
`components/assets/gif/`.
After replacing a GIF, run `python3 tools/eyes_preview/generate_dream.py`
(requires ffmpeg) and rebuild. The committed `dream_frames.inc` and
`dream_fence_frames.inc`, and `dream_saving_frames.inc` avoid requiring ffmpeg for normal firmware builds.
The clips contain 86, 21, and 16 frames at 10 fps, clipped to the same cloud with two
thought bubbles and encoded as RLE RGB332. The GIF fills the cloud at the original
190-pixel scale; narrower side lobes and a fuller lower lobe shape the cloud around the scene.
All three clips share 30,400 bytes of playback PSRAM. The saving scene uses a
smaller source placement to keep the wallet visible above the cloud base. The same clip loops for the entire nap, fading in on entry and out on waking.
The cloud clears immediately when an interaction interrupts sleep, and
follows the eyes' display rotation.

Dream opacity uses 257 levels with a fixed 4×4 spatial dither for RGB565.
A precomputed fade palette keeps the usual black-background composition to a
lookup per pixel; overlapping eyes still use source-over blending. The fade
regression check freezes the GIF and verifies over 200 distinct, monotonically
increasing brightness levels, in addition to rotation and strip checks.
