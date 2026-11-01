# Boot welcome checks

```sh
cmake -S tests/welcome -B /tmp/arixware-welcome
cmake --build /tmp/arixware-welcome -j 8
ctest --test-dir /tmp/arixware-welcome --output-on-failure
```

The checks use real LVGL GIF decoding and the generated splash. They verify
a large clock-font greeting over the playing splash, the saved owner name, `Hello User` on missing/empty
names, one transition to eyes, cancellation on screen deletion, and missing-asset
fallback. Device profile reads and the flash mapping are host fixtures.
The greeting screenshot is written to `/tmp/arixware-welcome.ppm` in physical
panel orientation; rotate it counterclockwise to inspect the logical screen.

To replace the boot art, update `components/assets/gif/splash.gif`, then run
`python3 tools/generate_splash.py` (requires ffmpeg) and rebuild the firmware.
The output preserves the source's 89 frames and 4.7-second timing and is rotated
90 degrees for the mounted panel, avoiding a live full-screen transform.
The GIF is packaged in the existing `assets` partition. Flash with `idf.py flash`
so the new asset partition image is included; an app-only flash does not install
or update the GIF. The decoder falls back to the greeting if the asset is absent.
