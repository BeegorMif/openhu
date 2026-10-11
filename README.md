<div align="center">

# OpenHU

**Open head unit for Raspberry Pi and Linux: Android Auto in a modern touch shell,
wireless, Bluetooth car radio, plugins and an API for everything around it.**

[![CI](https://github.com/average-web-dev/openhu/actions/workflows/ci.yml/badge.svg)](https://github.com/average-web-dev/openhu/actions/workflows/ci.yml)
[![License: GPL v3](https://img.shields.io/badge/license-GPLv3-blue.svg)](LICENSE)
![Qt 6.8](https://img.shields.io/badge/Qt-6.8%20QML-41cd52?logo=qt)
![Platforms](https://img.shields.io/badge/platform-x86__64%20%7C%20arm64-lightgrey)
[![Based on OpenAuto](https://img.shields.io/badge/based%20on-OpenAuto-6e7781)](https://github.com/opencardev/openauto)

<img src="docs/images/home.png" alt="OpenHU home screen with navigation and now playing" width="760">

</div>

---

> [!CAUTION]
> ## 🤖 Largely AI-generated, experimental, use at your own risk
>
> **Most of OpenHU's own code was written by an AI model (Anthropic Claude)** and reviewed and tested by a single
> hobbyist with one phone, one PC and a pair of Bluetooth earbuds. It has **not** been through professional code
> review, a security audit, or any automotive qualification.
>
> - **Do not operate the head unit while driving.** It is not safety-certified and must not be relied on for
>   anything that affects driving.
> - It changes system settings when you enable features: wireless Android Auto turns the WiFi card into a hotspot
>   and sets the WiFi country, the car radio feature changes the default audio output.
> - The WebSocket API has no authentication. Keep it on localhost or trusted networks.
> - There is **no warranty** of any kind (GPLv3). Not affiliated with or endorsed by Google, opencardev or
>   f1x.studio.

---

## Features

**Android Auto**
- Runs **inside the main window**: dock and status bar stay usable, the projection is never distorted, touch is
  mapped into it. Resolution is picked automatically for the available area (landscape and portrait).
- **Wireless** built in: pair the phone once (in the settings or just by plugging it in), then it connects by
  itself over Bluetooth and OpenHU's WiFi hotspot. Hotspot always on or only on demand, 5/2.4 GHz, USB or WiFi
  priority when the cable is plugged in.
- Driver side and the name shown on the phone are configurable; differently named head units are separate cars on
  the phone.

**In the car**
- **Bluetooth car radio** as audio output: OpenHU connects the radio whenever it is on, its buttons control Android
  Auto (play/pause, next, previous; answer and hang up calls), its display shows title and artist.
- **Home screen** with the next maneuver and the current track, tapping them opens Maps or the media app.
- **Steering wheel keys, keyboard, CAN** → actions (`media.next`, `volume.up`, `call.answer`, …), key map editable
  in the settings by pressing the key.

**Extensible**
- **WebSocket/JSON API** with navigation, media, phone, vehicle and volume data and control of the shell –
  [docs/api.md](docs/api.md)
- **Plugins**: QML pages, overlays and background processes in any language, installed from GitHub repositories
  configured like apt sources – [docs/plugins.md](docs/plugins.md). Official plugins
  ([openhu-plugins](https://github.com/average-web-dev/openhu-plugins)): CAN bus, steering wheel keys,
  reverse camera, FM/AM radio.

<table>
  <tr>
    <td><img src="docs/images/settings-aa.png" alt="Android Auto settings"></td>
    <td><img src="docs/images/store.png" alt="Plugin store"></td>
  </tr>
  <tr>
    <td align="center"><sub>Settings in the shell</sub></td>
    <td align="center"><sub>Plugin store with GitHub sources</sub></td>
  </tr>
</table>

## Quick start

Development and testing run in a container that shows OpenHU in a window on your desktop:

```bash
git clone https://github.com/average-web-dev/openhu.git
git clone -b openhu https://github.com/average-web-dev/aasdk.git aasdk-ref   # next to it
cd openhu
docker/dev.sh run     # first run builds the image, aasdk and OpenHU (takes a while)
```

After code changes `docker/dev.sh run` again: it rebuilds only what changed and starts right away.

Plug in your phone (or set up wireless under *Einstellungen → Kabellos*). Bare-metal installation on a Raspberry Pi
or a Debian/Ubuntu system, and what wireless Android Auto and the car radio need: [docs/installation.md](docs/installation.md).

## Documentation

| | |
|---|---|
| [Installation](docs/installation.md) | container, bare metal, service, wireless and car radio requirements |
| [Configuration](docs/configuration.md) | every setting of `openhu.ini`, data files |
| [API](docs/api.md) | topics, methods and actions of the WebSocket API |
| [Plugins](docs/plugins.md) | manifest, QML module, store repositories |
| [Troubleshooting](docs/troubleshooting.md) | USB, wireless, car radio, logs |
| [Project status](docs/STATUS.md) | phases, decisions, what is tested |

## How it fits together

```
 Phone ──USB / WiFi (aasdk)──► Android Auto services ──► EventBus ◄──► WebSocket API ◄──► plugin processes
                                     │                      ▲  ▲
                                     ▼                      │  └── actions: keys, steering wheel, CAN, API
                              QML shell (Qt Quick) ─────────┘
 BlueZ / NetworkManager (D-Bus) ──► wireless Android Auto, car radio      pactl ──► volume, audio output
```

| Path | |
|---|---|
| `src/autoapp/Shell/` | QML shell, settings, Android Auto surface |
| `src/autoapp/Service/` | Android Auto channels (video, audio, input, navigation/media/phone status, Bluetooth) |
| `src/autoapp/Wireless/`, `src/autoapp/Bluetooth/` | hotspot, wireless handshake, Bluetooth manager, car radio |
| `src/autoapp/Api/`, `Core/`, `Input/`, `Plugins/` | API server, event bus, actions, plugin manager and store |
| `docker/` | development container |

aasdk comes from the fork [average-web-dev/aasdk](https://github.com/average-web-dev/aasdk) (branch `openhu`):
upstream opencardev/aasdk plus navigation state and phone status messages and faster USB device detection.

## Credits and license

OpenHU is a fork of [OpenAuto](https://github.com/opencardev/openauto) by f1x.studio (Michal Szwaj) and the
OpenCarDev team (original README: [docs/upstream-openauto-README.md](docs/upstream-openauto-README.md)) and uses
[aasdk](https://github.com/opencardev/aasdk). Thanks to them – none of this would exist without their work.

**GPL-3.0**, see [LICENSE](LICENSE).

<sub>Android Auto is a trademark of Google LLC. This project is not affiliated with or endorsed by Google.</sub>
