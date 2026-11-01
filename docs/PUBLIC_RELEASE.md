# Release notes for contributors

## Licensing

- Original code, documentation, original quotes (`tools/data/arix_quotes.json`), and
  AI-generated project assets (including the splash animation) are MIT licensed.
- The three dream animation sources and their derivatives are distributed under the
  included Lottie Simple License (`LICENSES/`).
- Fredoka fonts keep their original copyright notice and the full OFL text.
- See `THIRD_PARTY_NOTICES.md`. Before distributing firmware binaries, assemble notices
  for the exact dependency set used.

## Repeatable checks

```sh
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

Scan for secrets with `gitleaks git --redact --log-opts=--all .`. Hardware behavior has
not been tested by these checks.
