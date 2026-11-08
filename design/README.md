# Design sheets

`square-reference.svg` is the design sheet of the Square eye pack: every mood, left and right eye,
drawn by the project author and offered under the repository's MIT license.

`python3 tools/eyepack/import_svg.py design/square-reference.svg` turns it into the numbers in
`eyes/square.json`, and `python3 tools/eyepack/compare.py square` measures how closely the firmware
renders it. See [docs/EYE_PACKS.md](../docs/EYE_PACKS.md).
