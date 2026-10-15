# Configuration

Everything below can be changed on the settings page of the shell (**Settings**). Changes are saved right
away; the bar at the top of the settings tells when a change needs a reconnect of the phone or a restart.
This page is the reference for editing files by hand, provisioning devices, or debugging.

## Files

| File | Location | Content |
|---|---|---|
| `openhu.ini` | working directory of OpenHU: `/var/lib/openhu` (systemd service), `docker/config/` (container) | all settings below; `openauto.ini` is read if `openhu.ini` does not exist yet |
| `openhu-logs.ini` | same directory, optional | Boost.Log filter/format settings; without it the log level comes from the environment variable `OPENHU_LOG` (`trace`, `debug`, `info`, `warning`, `error`), default `info` |
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

The settings can also be changed without the shell through the API (`settings.list`, `settings.set`, see
[api.md](api.md)) or, on OpenHU OS, with `openhu config list|get|set`. Their keys follow the settings page
(`aa.fps`, `wireless.hotspot`), not the `openhu.ini` names below; `settings.keys` lists them with their values.

Where the keys are in the settings:

| Settings | Keys |
|---|---|
| General | `[General]` Language, HandednessOfTrafficType, NameSuffix, ShowCursor; `[Input]` OnScreenKeyboard |
| Android Auto | `[Video]`, `[General]` NightMode, `[Input]` EnableTouchscreen, `[AudioChannel]`; wireless Android Auto: `[Bluetooth]` WirelessProjectionEnabled, `[Wireless]` Priority |
| Audio | `[Audio]`, `[CarAudio]` |
| WiFi | `[Wireless]` (WiFi networks, hotspot) |
| Bluetooth | `[Bluetooth]` AdapterType, AdapterAddress |
| Network | `[Api]` |

### [General]

| Key | Default | |
|---|---|---|
| `Language` | empty | language of the shell and the on-screen keyboard: `de`, `en`. Empty: from the system locale if there is a translation for it, otherwise English (also without a locale, e.g. `C.UTF-8`). See [translations.md](translations.md) |
| `HandednessOfTrafficType` | `0` | driver side: `0` left-hand drive, `1` right-hand drive (Android Auto puts its controls on the driver's side) |
| `ShowCursor` | `false` | mouse cursor (for mouse instead of touchscreen) |
| `NightMode` | `auto` | night mode of Android Auto: `auto` (from plugins or the API, `daynight.set`), `day`, `night` |
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
| `OnScreenKeyboard` | `true` | on-screen keyboard for text fields in the shell (Qt Virtual Keyboard, layout in the language of the shell); off with a real keyboard |

### [AudioChannel]

| Key | Default | |
|---|---|---|
| `MediaEnabled`, `GuidanceEnabled`, `SystemEnabled`, `TelephonyEnabled` | `true` | Android Auto audio channels played by OpenHU |

### [Radio]

| Key | Default | |
|---|---|---|
| `AndroidAuto` | `true` | the radio plugin as a media source in Android Auto (radio channel); only announced while the plugin runs (`radio.state`) |

### [CarAudio]

| Key | Default | |
|---|---|---|
| `Enabled` | `false` | sound to the Bluetooth car radio |
| `Device` | set by OpenHU | Bluetooth address of the radio (chosen on the settings page) |
| `Pin` | `0000` | PIN for legacy pairing of old radios |

### [Wireless]

| Key | Default | |
|---|---|---|
| `Ssid` | empty | hotspot name, empty = "OpenHU" + name suffix |
| `Password` | generated | 16 random characters on first start; the phone gets it over Bluetooth |
| `Band` | `5` | `5`, `2.4` or `auto` (5 GHz, 2.4 GHz if 5 GHz does not start) |
| `Channel` | `0` | `0` = 36 (5 GHz) / 6 (2.4 GHz); a fixed channel needs a fixed band |
| `Country` | `DE` | regulatory domain, set with `iw reg set` (5 GHz access points need it) |
| `Interface` | empty | WiFi card of the hotspot, empty = the first one NetworkManager knows |
| `Client` | `true` | join saved WiFi networks (Settings → WiFi) whenever the hotspot does not need the card, see [installation.md](installation.md#wifi-networks) |
| `ClientInterface` | empty | WiFi card for those networks, empty = any free one; a card other than the hotspot's keeps both running |
| `Address` | `10.42.0.1/24` | OpenHU in the hotspot network (gateway and DNS for the phone) |
| `DhcpRange` | empty | e.g. `10.42.0.50-10.42.0.60`, empty = NetworkManager default (needs NetworkManager ≥ 1.42) |
| `Hotspot` | `always` | `always` or `demand`: started when a paired phone asks for it, off after 2 minutes without a wireless session |
| `Priority` | `usb` | phone on the cable while wireless is possible: `usb` (cable wins, no WiFi credentials for it) or `wifi` |
| `LastDevice` | set by OpenHU | Bluetooth address of the phone that is connected first |

### [Bluetooth]

| Key | Default | |
|---|---|---|
| `WirelessProjectionEnabled` | `false` | wireless Android Auto (makes the WiFi card a hotspot) |
| `AdapterType` | `0` | `1`: offer pairing over the USB cable also without wireless Android Auto |
| `AdapterAddress` | empty | Bluetooth adapter to use, empty = the first one |

### [Api]

| Key | Default | |
|---|---|---|
| `Port` | `5520` | WebSocket API, see [api.md](api.md) |
| `Listen` | `127.0.0.1` | `0.0.0.0` = reachable from the network over IPv4, `::` = IPv4 and IPv6; clients from the network need the token |
| `AllowHotspot` | `false` | accept API clients from the wireless hotspot network (IPv4 subnet and every address of the hotspot interface, IPv6 included) |
| `Token` | created at the first start | login for clients from the network and browsers, see [api.md](api.md#login-token); a new one in Settings → Network |
| `AllowedOrigins` | `*` | origins browsers may connect from, comma separated (`http://192.168.1.10:8080`); `*` = any, empty = no browser (API key `api.origins`) |
