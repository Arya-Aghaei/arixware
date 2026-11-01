# Third-party materials

Original code and documentation are covered by the root MIT LICENSE. The exceptions and
material provenance below define what that license does not relicense.

## Fredoka fonts

`components/fonts/fredoka_*.c` contains converted Fredoka font data. Conversion commands
appear in the generated files. The copyright notice and full SIL Open Font License 1.1
are preserved in [OFL-Fredoka.txt](components/fonts/OFL-Fredoka.txt).
Upstream: https://github.com/google/fonts/tree/main/ofl/fredoka.

## ESP-IDF and managed components

ESP-IDF and managed dependencies are downloaded separately, not vendored in this repository.
Direct requirements are in `main/idf_component.yml`; transitive components depend on the
resolved versions and configuration. Preserve the licenses, copyright notices, and required
notices for the exact versions included when distributing firmware or dependency sources.

The locally inspected dependency set includes Apache-2.0 components from Espressif and
Waveshare, MIT-licensed LVGL, QOI and ESP JPEG code, FreeType, libpng, and zlib. These
components have separate license conditions; this list is not a complete firmware notice bundle.
Inspect each resolved component's LICENSE, LICENCE, COPYING, NOTICE and source SPDX headers,
including bundled libraries. FreeType includes license alternatives; select and satisfy the
applicable terms before binary distribution. Do not assume every dependency has the same license.

## Timezone rules

`components/net/timezone_rules.inc` is generated from installed IANA TZif POSIX footers
by `tools/generate_timezone_rules.py`. Preserve IANA identifiers, including
`Asia/Ujung_Pandang`; these are data identifiers, not project branding.
Upstream timezone database: https://www.iana.org/time-zones.
Record the tzdata version when regenerating for a release.

## Original quotes

`tools/data/arix_quotes.json` contains 32 original, AI-assisted lines written for Arixware.
The collection and generated `components/ui_core/quotes_data.inc` are distributed under
MIT. Run `python3 tools/generate_quotes.py` to regenerate the firmware table.
Previously collected third-party quotes are not included in the public source snapshot
or firmware. Their original JSON is retained unchanged in the ignored private archive.

## AI-generated images, sounds, and splash animation

The project images, sounds, and splash animation were created with AI tools for Arixware.
These assets are distributed under the root MIT license to the extent copyright applies:

- WAV files in `components/assets/wav/`.
- Background PNGs and button SVGs/PNGs in `components/assets/backgrounds/`.
- PNG files in `components/net/remote/`.
- `components/assets/gif/splash.gif` and its rotated derivative
  `components/assets/generated/splash_panel.gif`.

The three third-party dream animations and their derived frame data use the separate
license below.

## LottieFiles dream animations

On 2026-11-01 each animation page identified the
animation as free to use under the Lottie Simple License:

| Local source | Creator credited by source page | Source |
| --- | --- | --- |
| `components/assets/gif/Counting Sheep.gif` | Yellow Slice Design | https://lottiefiles.com/free-animation/counting-sheep-qwFmCZB2bT |
| `components/assets/gif/Saving.gif` | Priyanshu | https://lottiefiles.com/free-animation/saving-eDDmmQolqY |
| `components/assets/gif/Counting Sheeps.gif` | Guilherme | https://lottiefiles.com/free-animation/counting-sheeps-w0pf5vg2p3 |

The three GIFs and derived animation data in `components/eyes_renderer/dream_fence_frames.inc`,
`dream_saving_frames.inc`, and `dream_frames.inc` are expressly distributed under the
[Lottie Simple License](LICENSES/Lottie-Simple-License.txt), not MIT. Changes include scaling,
matte removal, cloud clipping, resampling to 10 fps, and RGB332 RLE encoding. The generator
code itself is original code under MIT. Preserve this notice and the Lottie license when
redistributing the GIFs or derived animation data, including in firmware.

Official license: https://lottiefiles.com/page/license.
