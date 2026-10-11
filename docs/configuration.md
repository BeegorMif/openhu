# Configuration

Everything below can be changed on the settings page of the shell (**Einstellungen**). Changes are saved right
away; the bar at the top of the settings tells when a change needs a reconnect of the phone or a restart.
This page is the reference for editing files by hand, provisioning devices, or debugging.

## Files

| File | Location | Content |
|---|---|---|
| `openhu.ini` | working directory of OpenHU: `/var/lib/openhu` (systemd service), `docker/config/` (container) | all settings below; `openauto.ini` is read if `openhu.ini` does not exist yet |
| `openhu-logs.ini` | same directory, optional | Boost.Log filter/format settings |
| `keymap.json` | data directory | key → action mapping, only the entries that differ from the defaults (`{"F5": "aa.back", "H": ""}`) |
| `sources.list` | data directory | store sources, see [plugins.md](plugins.md#repositories-store-sources) |
| `plugins/<id>/` | data directory | installed plugins |
| `plugins.json`, `installed.json` | data directory | enabled/dock state of plugins, origin of installed packages |
| `plugin-data/<id>/` | data directory | data plugins keep across updates |
| `logs/<id>.log` | data directory | output of plugin processes |
| `cache/` | data directory | store indexes for offline use |

The data directory is `~/.openhu` of the user running OpenHU (`/root/.openhu` for the service, `docker/data/` in
the container).

## openhu.ini

### [General]

| Key | Default | |
|---|---|---|
| `HandednessOfTrafficType` | `0` | driver side: `0` left-hand drive, `1` right-hand drive (Android Auto puts its controls on the driver's side) |
| `ShowCursor` | `false` | mouse cursor (for mouse instead of touchscreen) |
| `NameSuffix` | empty | the phone shows "OpenHU *suffix*". The vehicle id is derived from it, so differently named head units are separate cars on the phone (own settings), the same name stays the same car |

### [Video]

| Key | Default | |
|---|---|---|
| `AutoResolution` | `true` | pick the smallest Android Auto resolution that covers the area of the shell (480p, 720p, 1080p; portrait 720×1280, 1080×1920) |
| `Resolution` | `1` | used when `AutoResolution=false`: `1` 800×480, `2` 1280×720, `3` 1920×1080, `4` 2560×1440 |
| `FPS` | `2` | `1` = 60 fps, `2` = 30 fps |
| `ScreenDPI` | `140` | larger = bigger text and icons in Android Auto |
| `MarginWidth`, `MarginHeight` | `0` | `0`/`0` = computed so the projection fills the area undistorted |
| `Decoder` | `auto` | H.264 decoder (FFmpeg): `auto` = hardware decoder where available (`h264_v4l2m2m` on ARM), else software; `software`; or the name of an FFmpeg decoder. The log shows which one runs and its speed every 10 s |

### [Input]

| Key | Default | |
|---|---|---|
| `EnableTouchscreen` | `true` | touch input for Android Auto |

### [AudioChannel], [Audio]

| Key | Default | |
|---|---|---|
| `MediaEnabled`, `GuidanceEnabled`, `SystemEnabled`, `TelephonyEnabled` | `true` | Android Auto audio channels played by OpenHU |
| `Audio.OutputBackendType` | `0` | `0` RtAudio (lower latency), `1` Qt Multimedia |

### [Bluetooth]

| Key | Default | |
|---|---|---|
| `WirelessProjectionEnabled` | `false` | wireless Android Auto (makes the WiFi card a hotspot) |
| `AdapterType` | `0` | `1`: offer pairing over the USB cable also without wireless Android Auto |
| `AdapterAddress` | empty | Bluetooth adapter to use, empty = the first one |

### [Wireless]

| Key | Default | |
|---|---|---|
| `Ssid` | empty | hotspot name, empty = "OpenHU" + name suffix |
| `Password` | generated | 16 random characters on first start; the phone gets it over Bluetooth |
| `Band` | `5` | `5`, `2.4` or `auto` (5 GHz, 2.4 GHz if 5 GHz does not start) |
| `Channel` | `0` | `0` = 36 (5 GHz) / 6 (2.4 GHz); a fixed channel needs a fixed band |
| `Country` | `DE` | regulatory domain, set with `iw reg set` (5 GHz access points need it) |
| `Interface` | empty | WiFi device, empty = the first one NetworkManager knows |
| `Address` | `10.42.0.1/24` | OpenHU in the hotspot network (gateway and DNS for the phone) |
| `DhcpRange` | empty | e.g. `10.42.0.50-10.42.0.60`, empty = NetworkManager default (needs NetworkManager ≥ 1.42) |
| `Hotspot` | `always` | `always` or `demand`: started when a paired phone asks for it, off after 2 minutes without a wireless session |
| `Priority` | `usb` | phone on the cable while wireless is possible: `usb` (cable wins, no WiFi credentials for it) or `wifi` |
| `LastDevice` | set by OpenHU | Bluetooth address of the phone that is connected first |

### [CarAudio]

| Key | Default | |
|---|---|---|
| `Enabled` | `false` | sound to the Bluetooth car radio |
| `Device` | set by OpenHU | Bluetooth address of the radio (chosen on the settings page) |
| `Pin` | `0000` | PIN for legacy pairing of old radios |

### [Api]

| Key | Default | |
|---|---|---|
| `Port` | `5520` | WebSocket API, see [api.md](api.md) |
| `Listen` | `127.0.0.1` | `0.0.0.0` = reachable from the network (the API has no login: trusted networks only) |
| `AllowHotspot` | `false` | accept API clients from the wireless hotspot network |
