# Security and privacy

Do not post credentials, device memory dumps, or exploit details in public issues.
Use GitHub private vulnerability reporting if the repository owner has enabled it.
Otherwise arrange a private contact with the maintainer before sending sensitive details.

## Device data

Wi-Fi credentials and the owner profile are stored in device NVS. They are not intended
to be committed to this repository. Do not upload NVS dumps, flash dumps, provisioning
exports, signing keys, local environment files, or serial logs containing private data.
If a real credential enters Git history, revoke or rotate it before cleaning history.
Adding a file to `.gitignore` does not remove existing commits.

The local phone remote uses HTTP and a six-digit pairing code. Traffic is not encrypted.
The pairing URL and browser storage can contain the code. Use only on a trusted local
network; do not expose the device through internet port forwarding. Firmware rate limits
incorrect pairing attempts, but the pairing code is not a substitute for transport security.

Automatic timezone detection contacts `ipwho.is` over HTTPS. That service sees the network's
public IP address. The request does not include the Wi-Fi password, SSID, or owner name.
See [Wi-Fi documentation](docs/WIFI_SETUP.md) for persistence and lookup behavior.

## Before sharing diagnostics

Remove owner names, pairing codes, Wi-Fi identifiers and credentials, private addresses,
and any personal information from logs or screenshots. Git commit metadata can also expose
personal email addresses even when current source files contain no secrets.
