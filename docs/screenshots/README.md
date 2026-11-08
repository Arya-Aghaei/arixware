# Screenshot sources

These PNGs are captures of the production renderers running in the existing host previews.
They are 466 × 466 pixels, not photographs of hardware or conceptual mockups. They include
only project UI and project-generated art; the third-party dream clips are not pictured.

## Eyes

Use the [eyes preview](../../tools/eyes_preview/README.md) and export settled expressions:

```sh
mkdir -p /tmp/arixware-readme-eyes
/tmp/arixware-eyes/eyes_preview --all /tmp/arixware-readme-eyes
```

The published images use IDs 0 (Default), 1 (Happy), 11 (Curious), 13 (Amazed),
18 (Angry), and 24 (Sleeping). Convert the corresponding PPM files to PNG with ffmpeg.
No rotation is needed because the host eyes preview exports upright frames.

The `square-*.png` images are the Square pack in the Cyan theme
(`python3 tools/eyepack/compare.py square` renders every mood to `/tmp/arixware-eyes/frames-square`;
the IDs are the same moods: default, happy, curious, excited, angry, sleeping).

## Panel

Run the [settings host checks](../../tests/settings_ui/README.md) with screenshot outputs:

```sh
ARIXWARE_SETUP_PREVIEW=/tmp/arixware-readme-wifi.ppm /tmp/arixware-settings-test/settings_ui_test /tmp/arixware-readme-settings.ppm
```

These captures are already upright. Wi-Fi credentials are fixed synthetic test fixtures.
Run the [welcome host checks](../../tests/welcome/README.md) for
`/tmp/arixware-welcome.ppm`. Rotate that physical-panel capture counterclockwise:

```sh
ffmpeg -i /tmp/arixware-welcome.ppm -vf transpose=cclock -frames:v 1 panel-welcome.png
```

The greeting uses the fixture name Arya. The screenshots are covered by the project's MIT
license to the extent copyright applies, alongside the font's existing OFL terms.
