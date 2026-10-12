# OpenHU Packaging

Files that end up in the `openhu` Debian package (built with CPack, see `build.sh` / `docker/dev.sh`).

```
packaging/
├── debian/                 # maintainer scripts (postinst, prerm, postrm, preinst)
├── systemd/openhu.service  # runs /usr/bin/autoapp
└── udev/99-openhu.rules    # USB access for Android phones (also in accessory mode)
```

## Package

| | |
|---|---|
| Binary | `/usr/bin/autoapp` |
| Service | `openhu.service` (enabled on install, not started) |
| Depends | Qt 5 (Quick, Multimedia, WebSockets), GStreamer, `pulseaudio-utils` |
| Recommends | `bluez`, `network-manager`, `iw` – only needed for wireless Android Auto |
| Replaces | `openauto` (same binary name) |

## Wireless Android Auto on bare metal

OpenHU talks to BlueZ and NetworkManager over the system D-Bus; there is no separate Bluetooth service.
Requirements:

- `bluetoothd` running (package `bluez`)
- NetworkManager managing the WiFi card (default on Raspberry Pi OS Bookworm and Ubuntu); the card must support
  access point mode. OpenHU creates the connection `openhu-hotspot` and only activates it while wireless
  Android Auto is switched on.
- `iw` to set the WiFi country (needed for 5 GHz)

Enable it on the settings page under **Wireless**, then pair the phone there once.

## Service

```bash
sudo systemctl start openhu
journalctl -u openhu -f
```

The service runs as root with `eglfs` (no desktop needed). Edit the `Environment=` lines in
`/lib/systemd/system/openhu.service` for other display setups.
