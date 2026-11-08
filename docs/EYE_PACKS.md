# Eye packs

Every mood the arix can show is described in a JSON file, not in C. A pack is one file in `eyes/`;
`tools/eyepack/eyepack.py` validates it and generates the C tables the firmware includes. The device
and the host preview run the same rasterizer on the same generated numbers, so what you see on the
Mac is what the device draws.

```
eyes/orb.json     round glossy orbs, one shape per mood            (style 0)
eyes/square.json  rounded-square eyes with pupils and eyelids      (style 1)
        │  tools/eyepack/eyepack.py generate
        ▼
components/eyes_renderer/generated/eye_packs.inc, eye_pack_meta.inc   (committed)
        │  #include
        ▼
components/eyes_renderer/expressive_eyes.c   →   device and tools/eyes_preview
```

## Workflow

```sh
python3 tools/eyepack/eyepack.py check        # validate every pack (unknown field, out of range, wrong mood order)
python3 tools/eyepack/eyepack.py generate     # JSON -> generated C
cmake -S tools/eyes_preview -B /tmp/arixware-eyes && cmake --build /tmp/arixware-eyes -j 8
EYES_STYLE=0 /tmp/arixware-eyes/eyes_preview --all /tmp/frames-orb     # one PPM per mood (EYES_STYLE=1 for square,
EYES_STYLE=1 EYES_THEME=7 /tmp/arixware-eyes/eyes_preview --all /tmp/frames-square   # EYES_THEME=7 is Cyan)
ctest --test-dir /tmp/arixware-eyes --output-on-failure                # includes "generated tables match the JSON"
```

Never edit `generated/*.inc`: `ctest` fails when they differ from what the JSON produces.

## Moods and fallbacks

`eyes_expression.h` is the catalog of every mood the firmware knows (41). A pack defines the moods it has
a design for; for each catalog mood it does not define, `"fallbacks"` names the mood to show instead
(it must be one the pack defines). The phone remote lists only a pack's own moods, in the pack's order.
The orb pack has 30 moods and the square pack 31; adding a mood to the catalog means adding it to the
enum in `eyes_expression.h`, to `UI_EXPRESSION_*` in `ui_core.h`, to `EXPRESSION_SOUND` in
`expressive_eyes.c` (a sound clip, an existing one is fine) and to every pack (or its fallbacks).

## Pack format

Common to both packs: `pack`, `title` (shown in Settings), `style` (the file name), `default_theme` (the
color theme the pack looks best in: Amber, Rose, Violet, Ice, Mint, Lime, Mono or Cyan), `fallbacks`, an
optional `reference` (see below) and `expressions`: a list of moods, each with `id`, `label`, a `left` eye
and a `right` eye (`"same"` copies the left).

Both packs write an eye the same way, so a mood can be copied between them:

| field | meaning |
| --- | --- |
| `shape` | `capsule`, `arch`, `chevron`, `crescent`, `spiral`, `half`, `slope`, `heart`, `worried`, `sleep`, `oval`, `egg`, `lens` |
| `w`, `h` | size as a fraction of the base eye (170 px) |
| `openness` | 0..1, default 1 (orb only) |
| `dir` | orientation: arch 1 up / -1 down / 2 sad droop, slope and chevron 1 / -1 (the right eye mirrors the left) |
| `accent` | `none`, `rays`, `sparkles`, `side_rays`, `star`, `question`, `hearts`, `blush`, `nervous`, `drop`, `zzz`, `clouds`, `top_rays`, `ellipsis`, `bulb`, `target`, `anger_mark`, `tears`, `stress`, `eyebags`, `tongue`, `glitch` |

### Orb pack (style 0)

Each `shape` is drawn as that orb shape: a glossy amber (or any theme) body with a rim and gloss streak.

### Square pack (style 1)

Cyan "shield" eyes from the design sheet `design/square-reference.svg`: straight sides, an elliptical
bottom, a thin bright lid line floating above the body, a large dark pupil clipped by the body, a gloss
arc and a fine dot texture. Pupils follow the gaze, a little out of step between the two eyes. A blink
(and an idle nap) lowers the upper lid, with its line, over the eye. Moods morph by interpolating every
number.

**Every eye of the Square pack carries a `glass` block with the renderer's numbers.** Do not rely on the
orb-style fields (`shape`, `w`, `h`) for the look: they only decide the accessory placement and the
orb fallback. Everything is a fraction of the 170 px base eye:

```json
"left": { "shape": "capsule", "w": 0.9, "h": 0.8, "accent": "sparkles",
  "glass": {
    "w": 0.83, "h": 0.56,            // body size (full width / height)
    "corner": 0.79,                  // bottom roundness: the bottom ellipse's height, in half widths
    "round_top": 0,                  // 0: the lid line cuts the top; 1: the top is a dome (surprised, happy)
    "lid_gap": 0.26, "lid_len": 1.18,// lid line height above the body (half widths) and half length; 0 gap = no line
    "top":    { "cover": 0.05, "slope": -0.04, "bow": -0.05 },   // cut at the top: cover (of the height), tilt (+ lowers it toward the nose), bow (+ sags)
    "bottom": { "cover": 0, "slope": 0, "bow": 0 },              // cut at the bottom: a hollow underside (happy, wink)
    "pupil":  { "size": 0.4, "x": 0.2, "y": -0.5, "kind": "disc" }  // size in half widths; x toward the nose; y from the body centre in half heights; disc | heart | rings (spiral)
  } }
```

`"glass": { "orb": true, "w": .., "h": .. }` hands the mood to the orb renderer (the laugh's chevrons).

`"accent"` is separate from the look: it adds the accessories (sweat drop, sparkles, question mark, ...)
and must be set on the eye that carries it, usually the right one. Only the eye blocks differ: a right
eye equal to the left one may be written `"right": "same"`.

The validator rejects what would only show on the device: unknown keys, numbers out of range, an eye too
big for its sprite and a pupil hidden under the lid.

**Importing a design sheet.** `python3 tools/eyepack/import_svg.py design/square-reference.svg` reads the
body, pupil and lid line of every mood from the SVG and writes the `glass` blocks (it never touches
accents). It warns when the sheet draws an accessory the JSON has no accent for, and it keeps most of a
pupil visible even when the sheet hides it under a heavy lid. A mood that is not in the sheet, or that
should differ from it, is marked `"hand_made": true` and left alone by the importer (Playful is one).
Re-running it after a sheet change is safe.

## Matching a design

`reference` points at the design sheet and its layout:

```json
"reference": { "image": "design/orb-reference.svg", "grid": { "cols": 6, "cell": [256, 187] }, "min_iou": 0.5 }
```

```sh
python3 tools/eyepack/compare.py orb                 # table: overlap with the reference and eye sizes, per mood
python3 tools/eyepack/compare.py fit orb             # propose w/h changes that match the reference sizes
python3 tools/eyepack/compare.py fit orb --write     # apply them (then look at the sheet!)
```

`compare` renders every mood with the host preview, cuts the matching cell from the reference and
compares the silhouettes: the overlap after centering (with a 2 px tolerance) and the size of each eye.
It exits 1 when a mood is below `min_iou`, so a regression is a failing command, not something noticed
on the device. `fit` fixes sizes only; shape (corner radius, lid curve, tip rounding) is still judged by
eye. Accessories that touch an eye can fool it, hence the review step and `--skip`.

## Adding or changing a mood

1. Edit the numbers in `eyes/<pack>.json`.
2. `generate`, rebuild the preview, look at the sheet and run `compare`.
3. Commit the JSON and the regenerated tables together.

A new orb *shape* or glass *field* is a renderer change: add it to `eyes_raster.c`, to the allowed values
in `tools/eyepack/eyepack.py` and to the generator, and describe it here. Keep rendering fast: see
`docs/EYES_PERFORMANCE.md`.
