# Public release audit

Audit date: 2026-11-01. Scope: tracked working files and locally reachable Git history.
This is a technical preparation record, not a legal clearance or a guarantee that no secret exists.
Repository visibility and existing Git history have not been changed.

## Completed preparation

- Renamed legacy project branding to Arix in source, browser UI, comments, and documentation.
- Preserved the former collected quote JSON byte-for-byte in ignored
  `private/quotes/demotivational_quotes.json`. Replaced the public collection and firmware
  table with 32 original lines in `tools/data/arix_quotes.json`.
- Preserved the valid IANA timezone identifier `Asia/Ujung_Pandang`.
- Added project build instructions, security/privacy documentation, and third-party notices.
- Expanded ignore rules for local credentials, signing material, device dumps, and build output.
- Kept the existing Fredoka copyright notice and full OFL text.
- Made the registry-only `dependencies.lock` eligible for version control to preserve resolved versions.
- Scanned reachable Git history with Gitleaks using `--log-opts=--all --redact`.
  Three findings were synthetic `012345abcdef` setup-password fixtures in host tests,
  not confirmed credentials. Current copies have line-specific fixture annotations.
- Inspected tracked filenames and text for private paths, credentials, and personal identifiers.
  No confirmed live credential was identified. Ignored local build products are not publication inputs.

## Validation results

- Firmware: ESP-IDF 5.5 builds passed in the working tree and a fresh exported source tree
  using only `sdkconfig.defaults`, the dependency lock, and downloaded managed components.
  The fresh export initially had no generated headers or local configuration. Both builds
  generated the 35-sound bank. Project name: `arix_deskbuddy`. No device was flashed.
- Host checks: portal JavaScript, setup access, timezone persistence/lookup, settings UI
  (3 tests), eyes (2 tests), welcome (1 test), and the 500,000-step eye physics check passed.
- Gitleaks scan of the final tracked and non-ignored publication files: no findings.
- The archived collected JSON and timezone table are byte-identical to HEAD. The new
  quote table regenerates deterministically; `git diff --check` passed.
- Replacement quotes: all 32 fit the production text area using the production LVGL
  fonts; the updated firmware build passed.
- Hardware behavior and GitHub-hosted content were not tested.

## Release status

- Original code, documentation, new quotes, and AI-generated project assets use MIT.
- The splash animation is AI generated, including its rotated firmware derivative.
- The three dream animation sources have been verified and are distributed, with their
  derivatives, under the included Lottie Simple License.
- Collected third-party quote text is excluded from the public snapshot and firmware.
- The public repository (`Arya-Aghaei/arixware`) is a separate history of curated commits with a
  GitHub noreply author address, with none of the private history. The creator's public UI credit remains.
- `design/square-reference.svg` is the author's own design sheet for the Square eye pack.
- Private history is unchanged. Do not change the existing private repository's visibility;
  publish the fresh snapshot to avoid exposing old content and personal commit metadata.

Before distributing firmware binaries, assemble notices for the exact dependency set.
Before publishing on GitHub, check repository settings, enable available secret scanning
and push protection, and enable private vulnerability reporting. Hosted releases, Actions
artifacts, issues, and attachments require separate inspection if any are uploaded.

## Repeatable checks

```sh
gitleaks git --redact --log-opts=--all .
node tests/net/setup_page.test.mjs
node tests/net/setup_client.test.mjs
python3 tests/net/check_timezone.py
cmake -S tests/settings_ui -B /tmp/arixware-settings-test
cmake --build /tmp/arixware-settings-test -j 8
ctest --test-dir /tmp/arixware-settings-test --output-on-failure
cmake -S tools/eyes_preview -B /tmp/arixware-eyes
cmake --build /tmp/arixware-eyes -j 8
ctest --test-dir /tmp/arixware-eyes --output-on-failure
```

The historical fixture findings remain visible in the unmodified history. Do not suppress
other findings globally. For a current-tree scan, copy tracked working files into a temporary
directory and run `gitleaks dir --redact` on that directory, excluding ignored build products.

## References

- GitHub repository licensing: https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/customizing-your-repository/licensing-a-repository
- Copyright and permission: https://www.loc.gov/legal/security-copyright-and-privacy/understanding-copyright/
- Fredoka upstream license: https://github.com/google/fonts/blob/main/ofl/fredoka/OFL.txt

Dependency lock guidance: https://docs.espressif.com/projects/idf-component-manager/en/latest/reference/dependencies_lock.html

## Unused-file cleanup

Runtime call sites, asset packing configuration, generators, host fixtures, and documentation
were checked before removal. Callback registrations and generated asset constants count as
uses. All four GIFs, their generated derivatives, and fonts with runtime consumers remain.
No unreferenced runtime function was proven safe to remove.

Removed 26 files totaling 1,880,200 bytes. The settings host fixture now uses the same
background PNG as firmware instead of obsolete artwork. Removed font sources, declarations,
and build entries for unused Bold 24 and Bold 32. Updated the font documentation.
The sound build tracks WAV-list membership and orders header generation before dependent
compilation. Reconfiguration after deletion now repacks 35 sounds instead of retaining 43.

Removed files:

- `components/assets/wav/charging_start.wav`
- `components/assets/wav/low_battery.wav`
- `components/assets/wav/error_soft.wav`
- `components/assets/wav/charging_complete.wav`
- `components/assets/wav/notification.wav`
- `components/assets/wav/19_annoyed.wav`
- `components/assets/wav/thinking.wav`
- `components/assets/wav/blink.wav`
- `components/assets/backgrounds/png/12_loading_blackhole_466.png`
- `components/assets/backgrounds/png/09_status_moon_466.png`
- `components/assets/backgrounds/png/10_credits_nebula_466.png`
- `components/assets/backgrounds/png/07_music_galaxy_466.png`
- `components/assets/backgrounds/png/08_weather_sunset_466.png`
- `components/assets/backgrounds/png/11_focus_dawn_466.png`
- `components/assets/backgrounds/png/03_qr_orbits_466.png`
- `components/assets/backgrounds/png/06_sleep_eclipse_466.png`
- `components/assets/eyes/oval/arixware_eyes_moonbean_466.svg`
- `components/assets/eyes/oval/arixware_eyes_moonbean_466.png`
- `components/assets/eyes/oval/arixware_eyes_moonbean_transparent_466.svg`
- `components/assets/backgrounds/clock_background.png`
- `components/assets/backgrounds/settings_background.png`
- `components/assets/backgrounds/arixware_clock_background_466.svg`
- `components/assets/backgrounds/arixware_settings_background_466.svg`
- `components/assets/backgrounds/arixware_qr_background_466.svg`
- `components/fonts/fredoka_bold_24.c`
- `components/fonts/fredoka_bold_32.c`

## Snapshot delivery

A separate source-only snapshot and archive were prepared outside the private repository.
The snapshot has one initial Git commit using a GitHub noreply address, no remote, and
no private history, local configuration, downloaded components, or generated build headers.
The archive contains only files from that new commit. The private repository's HEAD and
history remain unchanged. The privately archived collected JSON remains byte-identical to the original.

The earlier quote and splash provenance questions are addressed: original quotes replace
collected text in the public source and firmware, and the splash is an AI-generated project
asset. The private archive is excluded from the snapshot and archive. No repository was
published and no device was flashed.
