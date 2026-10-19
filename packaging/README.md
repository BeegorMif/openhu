# OpenHU Packaging

Files that end up in the `openhu` Debian package (built with CPack, see `build.sh` / `docker/dev.sh`).

```
packaging/
├── debian/                 # maintainer scripts: preinst (removes the old openhu.service on updates),
│                           # postinst (reloads the udev rules)
└── udev/99-openhu.rules    # USB access for Android phones (also in accessory mode)
```

## Package

| | |
|---|---|
| Program | `/usr/libexec/openhu/openhu`, started as `/usr/bin/openhu` (link). OpenHU OS removes the link (its command line is `openhu-cli`) |
| Single instance | a second OpenHU ends right away: lock `/run/lock/openhu.lock` (holds the pid) |
| Service | none: OpenHU OS starts it with its `openhu-session.service`, elsewhere see [Start](../docs/installation.md#start) |
| Depends | Qt 6 (Quick, Multimedia, WebSockets), FFmpeg, `pulseaudio-utils` |
| Recommends | `bluez`, `network-manager`, `iw` – only needed for wireless Android Auto |
| Replaces | `openauto` (`/usr/bin/autoapp`) |

## Wireless Android Auto on bare metal

OpenHU talks to BlueZ and NetworkManager over the system D-Bus; there is no separate Bluetooth service.
Requirements:

- `bluetoothd` running (package `bluez`)
- NetworkManager managing the WiFi card (default on Raspberry Pi OS Bookworm and Ubuntu); the card must support
  access point mode. OpenHU creates the connection `openhu-hotspot` and only activates it while wireless
  Android Auto is switched on.
- `iw` to set the WiFi country (needed for 5 GHz)

Enable it on the settings page under **Wireless**, then pair the phone there once.

## Start

The package starts nothing by itself. How to run it outside OpenHU OS, on a desktop or in a kiosk session with
cage: [docs/installation.md](../docs/installation.md#start).
