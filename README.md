# OpenHU

Open head unit for Raspberry Pi and other Linux devices: Android Auto inside a modern QML shell,
a WebSocket API and plugins installed from GitHub repositories.

OpenHU is based on [OpenAuto](https://github.com/opencardev/openauto) by f1x.studio and the OpenCarDev
team and uses [aasdk](https://github.com/opencardev/aasdk). The original README is in
[docs/upstream-openauto-README.md](docs/upstream-openauto-README.md).

## Features

- **Android Auto in the main window** (USB, wireless via the Bluetooth service): dock and status bar stay
  usable, margins are computed so the projection is not distorted, touch is mapped into the projection
- **Home screen** with the next navigation maneuver and the current track from the phone
- **WebSocket/JSON API** (`ws://127.0.0.1:5520`) with navigation, media, phone and volume data and control
  of the shell – see [docs/api.md](docs/api.md)
- **Actions** for keyboard, steering wheel, CAN and API (`media.next`, `volume.up`, `call.answer`, …),
  configurable key map
- **Plugins and store**: QML pages, overlays and background processes in any language; repositories
  configured like apt sources (`github user/repo`) – see [docs/plugins.md](docs/plugins.md).
  Official plugins: CAN bus, steering wheel keys, reverse camera, FM/AM radio
  ([openhu-plugins](https://github.com/average-web-dev/openhu-plugins))
- Reconnect the phone without unplugging (USB reset), night mode via API/CAN

## Development in a container

Expected layout: this repository, `aasdk-ref` (aasdk sources) and optionally `openhu-plugins` next to each other.

```
docker/dev.sh image      # build image (first time and after dependency changes)
docker/dev.sh build      # incremental build -> docker/output/*.deb -> image openhu:run
docker/run.sh            # OpenHU in a window (Xephyr), with sound, USB, cameras
docker/dev.sh reconnect  # phone USB reset while running
```

aasdk changes live in `aasdk-ref` (branch `feature/oa-extensions`) and as patches in `aasdk-patches/`
for CI, which applies them to the upstream commit in `.github/workflows/ci.yml`. After changing aasdk:
`git -C ../aasdk-ref format-patch 9bf6adf..HEAD -o $PWD/aasdk-patches`.

Configuration: `docker/config/openhu.ini` (`[General] UI=qml|legacy`, `[Api] Port`, …),
plugins and store data: `docker/data/`. The classic OpenAuto/Crankshaft interface is still available
with `UI=legacy`.

## License

GPL-3.0, see [LICENSE](LICENSE). Copyright of the original code: f1x.studio (Michal Szwaj) and the
OpenCarDev team.
