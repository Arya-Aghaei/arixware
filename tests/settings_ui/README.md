# Settings checks

Run the real LVGL settings UI and controller tests on the host:

```sh
cmake -S tests/settings_ui -B /tmp/arixware-settings-test
cmake --build /tmp/arixware-settings-test -j 8
ctest --test-dir /tmp/arixware-settings-test --output-on-failure
```

The UI test drives rotated pointer input through LVGL. The settings shade draws straight to
the panel, so a stub panel (`panel_stub.c`) records the blits and panel ownership. The test
checks edge-only opening, that the panel shows exactly the composed frame, slider bounds and
release callbacks, every sub-page (Wi-Fi, Bluetooth, About), Back and
swipe-up navigation, that slider drags never close the shade, panel hand-back, and timer
cleanup over 30 open/close cycles. Network services are substituted. The controller test uses simulated NVS, brightness, and
audio services to check defaults, saved values, bounds, standby, and failure handling.

The host preview decodes the real settings PNG through the bundled LVGL PNG decoder.
Optionally pass a PPM output path to `settings_ui_test` to render an upright preview of the
main page; further paths render the sub-pages in button order (Wi-Fi, Bluetooth, About).

The firmware and host fixture use `components/assets/backgrounds/png/02_settings_planet_466.png`
at 466 by 466 pixels. The settings renderer applies the panel rotation after composition.

Firmware validation uses `idf.py build` after sourcing the ESP-IDF environment.
On the device, verify touch orientation, animation performance, circular clipping,
actual sound/brightness changes, reboot persistence, standby restoration, network
state changes, and memory stability. Host tests do not replace these checks.

The transparent button skins are rendered from `settings_button.svg` and
`settings_button_active.svg` at 86 by 86 pixels without rotation; the shade composes an
upright frame and rotates it once. The generated Fredoka Medium fonts (22px info, 24px captions, 36px headings) use a
static instance with `wght=500` and `wdth=100` and cover printable ASCII; conversion options
are recorded at the top of each C file.

DND checks cover toggling without changing volume and restoring the saved mode.
The controller test runs the actual power-button state machine: after UI Sleep,
the first physical click wakes and restores brightness. UI checks also verify
that dark-screen touches are ignored and swipes across Sleep do not activate it.
On hardware, additionally verify immediate muting of a playing sound, cancellation
of queued sounds, and volume changes while DND remains enabled. Sleep is display
standby; the device continues running and wakes with its physical BOOT button.

Settings render directly at panel resolution; do not zoom the composed framebuffer.
The clock uses native 144px Bold digits and 40px Bold seconds/date. Larger fonts
are generated from Google Fonts Fredoka (`ofl/fredoka/Fredoka[wdth,wght].ttf`),
instanced with fontTools at `wdth=100`, `wght=500` (Medium) or `700` (Bold),
then converted with the lv_font_conv options recorded in each C file.

About combines the former Display, System, and More information. Drag within its
list to scroll; its title and Back button stay fixed. Host checks cover all merged
fields, live time updates, scroll bounds in both directions, and Back navigation.
Set `ARIXWARE_ABOUT_PREVIEW` to a PPM path to capture the bottom of the list.

Wi-Fi setup checks cover the amber hotspot QR, orange portal URL QR after phone DHCP,
a perimeter spinner during connection, failure returning to the portal code, successful
save confirmation, and horizontally spaced Back/Set up buttons on the details page.
The portal tests in `tests/net/setup_page.test.mjs` cover network-row selection,
SSID injection safety, disabled rows during connection, scanning and retries.

For cache-size regression checks, build with AddressSanitizer and UndefinedBehaviorSanitizer.
The UI test starts with the small wizard Back button, then opens a page with the larger
Back button and repeats setup navigation; cached layers must grow before composition.

Connected Wi-Fi setup requires an explicit Yes/No confirmation, including taps on a
displayed QR. Tests verify No and Back cancel without a setup request, and only Yes
starts setup. The compact QR card uses a two-module white margin.

A rightward swipe starting within 60 logical pixels of the left edge also performs
Back: dismiss confirmation, return from a subpage, or close the main shade. Tests
exercise rotated input, About scrolling boundaries, and non-edge horizontal swipes.
