# Wi-Fi setup

The device uses a temporary WPA2 hotspot and an entirely local setup page. There is no phone
app or external website involved. ESP32-S3 supports 2.4 GHz Wi-Fi; 5 GHz-only networks will not
appear in the scan.

## On the device

1. Open Settings → Wi-Fi. Swipe right from the left edge to go back. On first boot, the setup hotspot starts automatically.
2. If the page shows connection details, tap **Set up** to change networks. When already
   connected, confirm **Set up Wi-Fi again?** with **Yes**; **No** keeps the connection.
   Tapping a displayed QR while already connected shows the same confirmation.
3. **1. Join Wi-Fi** shows a large QR code in an amber rounded card. Scan it to join
   the displayed `Arixware-Setup-XXXX` hotspot; the password is shown below the code.
4. Once your phone receives an IP address, the device changes to **2. Open setup**.
   The card border turns orange and a different QR code appears. Scan this second code
   to open **http://192.168.4.1/**. If a captive sign-in window already opened, use it.
   Accept staying on the setup Wi-Fi even though it has no internet connection.
5. Enter the device owner’s name, then tap a network in the portal's nearby-network list, enter its password, and press
   **Connect**. Use **Other or hidden network** to enter an SSID manually, or
   **Refresh** to scan again. Leave the password blank for an open network.
6. While connecting, an amber arc spins around the device's screen. Wait for the
   connection confirmation. Incorrect credentials return to the portal QR for retry
   while the phone remains joined. Successful credentials are saved in NVS, the device
   starts time synchronization, and the setup hotspot closes after a brief two-second confirmation window.
   The phone can then reconnect to its normal Wi-Fi. The connection-details screen has Back and
   Set up buttons spaced side by side.

The hotspot has a device-specific suffix and a random 12-character password regenerated at
boot. Setup requests are accepted only from the setup subnet. Trial credentials stay in RAM;
the Wi-Fi driver's automatic persistence is disabled. Existing saved credentials are not
changed by failed connection attempts. Saved Wi-Fi is retried three times before setup becomes
available again. Setup can also be reopened from Settings while already connected.

Phone captive-portal behavior varies. DHCP advertises the device as DNS server. DNS
answers and HTTP redirects, including root requests on connectivity-check hostnames,
allow the phone to detect the captive portal. DHCP option 114 is deliberately omitted:
Android expects an HTTPS RFC 8908 JSON API there, rather than the local HTML page.
After updating firmware, forget and rejoin the setup hotspot to trigger fresh detection.
HTTPS requests cannot be redirected transparently. Android may show a **Sign in to Wi-Fi**
notification instead of opening a window; the firmware cannot force Android to open an app.
The first QR joins the hotspot; the second QR opens its local web address. The manual
address above remains the fallback.

For detection failures, serial output should show `setup_dns: Listening on UDP port 53`,
then `Setup client DNS query` and `net_http: Captive HTTP probe` when the phone joins.
`Serving local Wi-Fi setup page` confirms the portal was fetched. No DNS requests points
to the phone's network detection/DNS path; DNS requests without HTTP probes narrows the
problem to the next detection step. These messages contain no credentials or DNS names.
If the page loads but its network list does not, check `/api/wifi/scan`. A response saying
to join the setup Wi-Fi indicates an access-check failure, not an empty scan. The access
check accepts both IPv4 and IPv4-mapped IPv6 socket addresses from ESP-IDF's dual-stack
HTTP listener, while still requiring the setup interface, subnet, and POST origin.

## Verification

```sh
node tests/net/setup_page.test.mjs
node tests/net/setup_client.test.mjs
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I components/net components/net/net_dns_packet.c tests/net/test_dns.c \
  -o /tmp/arixware-dns-test
/tmp/arixware-dns-test
cmake -S tests/settings_ui -B /tmp/arixware-settings-test
cmake --build /tmp/arixware-settings-test -j 8
ctest --test-dir /tmp/arixware-settings-test --output-on-failure
```

The settings UI tests exercise the actual Set up button, both QR payloads, phone-join transition,
connection progress, perimeter-only spinner animation, retry and connection confirmation,
return to the connection-details screen, and panel composition. To capture the upright setup
screen, run the tests with `ARIXWARE_SETUP_PREVIEW=/tmp/arixware-wifi-qr.ppm`.
Use `ARIXWARE_PORTAL_PREVIEW`, `ARIXWARE_CONNECTING_PREVIEW`,
`ARIXWARE_CONNECTED_PREVIEW`, and `ARIXWARE_WIFI_DETAILS_PREVIEW` for the other steps.

For firmware builds, enable `CONFIG_LV_USE_QRCODE=y` in existing configurations (it is included
in `sdkconfig.defaults` for fresh configurations), source ESP-IDF's `export.sh`, then run
`idf.py build`.

Hardware checks are still required: iOS/Android QR joining and captive-portal behavior,
scanning while a phone is connected, wrong-password retries, 32-byte and hidden SSIDs, open
networks, power-cycle persistence, lost-router recovery, hotspot shutdown, and changing a
working network through Settings. Reopening setup while connected still scans nearby networks
and shows the existing connection without treating it as a completed replacement. Host tests do not exercise the radio or actual NVS writes.

## Owner profile

The portal requires an owner name (up to 64 UTF-8 bytes), trims surrounding spaces,
and pre-fills the previously saved name when setup is reopened. A successful setup
connection saves it to NVS namespace `device_profile`, key `owner_name`. Failed
connection attempts leave the existing owner name unchanged. This is separate from
Wi-Fi credentials, and normal boots do not overwrite it.

Future splash-screen code can include `net_profile.h` and call
`net_profile_get_owner(buffer, sizeof(buffer))` after NVS initialization, without
waiting for Wi-Fi initialization. The splash screen itself is unchanged. The
setup-only `/api/profile` endpoint reads the name; it is not included in the public
status response.

Profile checks (mock NVS, actual persistence module):

```sh
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I tests/net/profile_stubs -I components/net/include \
  components/net/net_profile.c tests/net/test_profile.c -o /tmp/arixware-profile-test
/tmp/arixware-profile-test
```

## Automatic local time

SNTP synchronizes UTC after Wi-Fi connects. A separate background task then looks
up the network's public IP through `https://ipwho.is/?fields=success,timezone.id`
and applies the returned IANA timezone using the bundled POSIX timezone rules.
No Wi-Fi password, SSID, or owner name is included in that request. IP-based
location can reflect a VPN or an ISP's exit location rather than the device's
physical location.

The detected timezone is saved in NVS and restored before reconnecting, including
its daylight-saving rule. A successful lookup is refreshed every six hours and
on reconnection; unsuccessful lookups retry after a minute and retain the saved
zone. TLS validation waits for a valid SNTP clock. Before the first successful
detection, UTC is used and Settings → About shows `Detecting...` for the timezone.

The IANA-name/POSIX lookup table is generated from the installed system tzdata
with `python3 tools/generate_timezone_rules.py`; regenerate it when timezone laws
change and rebuild. The POSIX footer describes recurring rules rather than a
complete historical timezone database.

Run `python3 tests/net/check_timezone.py` to check timezone persistence, DST
transitions, fractional offsets, malformed responses, lookup failures, and
background refresh timing. Transport tests use ESP-IDF's cJSON source; set
`IDF_PATH` if ESP-IDF isn't in the sibling `esp-idf` directory.
