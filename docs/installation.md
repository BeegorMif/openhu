# Installation

> [!NOTE]
> OpenHU is developed and tested in a container on x86_64 (Debian 13 "trixie" image). Running it directly on a
> Raspberry Pi is the goal, but **not tested yet**, and there are no ready-made arm64 packages (planned). The
> steps below for bare metal follow from how the container works; please report what does not fit.

- [Requirements](#requirements)
- [Development in a container](#development-in-a-container) – the tested way
- [Bare metal (Raspberry Pi, Debian, Ubuntu)](#bare-metal-raspberry-pi-debian-ubuntu)
- [Wireless Android Auto](#wireless-android-auto)
- [Bluetooth car radio](#bluetooth-car-radio)

## Requirements

| | |
|---|---|
| Computer | Raspberry Pi 4/5 (arm64) or an x86_64 PC |
| OS | Debian 13 "trixie" or Raspberry Pi OS based on it (Qt 6.8); Debian 12 / bookworm is not supported (Qt 6.4) |
| Display | X11 or Wayland, touch recommended (without a desktop: see below) |
| Audio | PulseAudio or PipeWire with `pipewire-pulse` (volume control uses `pactl`) |
| Phone | Android with Android Auto; wireless needs Android 11+ and 5 GHz WiFi in most phones |
| Wireless AA | Bluetooth adapter (BlueZ), WiFi card with access point mode managed by NetworkManager |
| Car radio | Bluetooth radio with A2DP (and ideally AVRCP for buttons/title) |

## Development in a container

The container builds OpenHU and runs it in a window on your desktop (Xephyr), with USB, sound, cameras and the
host's Bluetooth/NetworkManager. Expected layout:

```
workspace/
├── openhu/          this repository (any folder name)
├── aasdk-ref/       aasdk, branch openhu of https://github.com/average-web-dev/aasdk
└── openhu-plugins/  optional, store source "file /opt/openhu-plugins" for local plugin development
```

```bash
docker/dev.sh run            # build incrementally (aasdk + OpenHU), then OpenHU in a window
docker/dev.sh run --no-build # start without building (OPENHU_RESOLUTION=1920x1080 for another size)
docker/dev.sh build          # only build
docker/dev.sh test           # build, unit tests, qmllint, start the shell headless (same checks as CI)
docker/dev.sh package        # .deb -> docker/output/
docker/dev.sh shell          # shell in the build environment
docker/dev.sh reconnect      # USB reset of the phone while running
docker/dev.sh image          # rebuild the image with current Debian packages
docker/dev.sh clean          # delete build volumes and ccache, the next build starts from scratch
```

The image `openhu:dev` only holds the dependencies and is rebuilt automatically after changes to the `Dockerfile`.
aasdk (`../aasdk-ref`, other path with `AASDK_DIR=...`) and OpenHU are built in Docker volumes with Ninja and ccache,
OpenHU runs straight from its build directory. `docker/run.sh` is short for `docker/dev.sh run`.

`run` needs an X11 display (`DISPLAY`), the PulseAudio/PipeWire socket of your user and runs the container
`--privileged` with the host network: USB hotplug, the hotspot and Bluetooth (D-Bus; Docker's AppArmor profile
blocks D-Bus otherwise) need it. Settings are stored in `docker/config/openhu.ini`, plugins, key map and logs in
`docker/data/`.

## Bare metal (Raspberry Pi, Debian, Ubuntu)

### Build and install

OpenHU needs the `libaasdk` packages of the OpenHU fork, released at
[average-web-dev/aasdk](https://github.com/average-web-dev/aasdk/releases) (tags `openhu-v<version>`). The `.deb`
depends on `libaasdk (>= 1.0), libaasdk (<< 2)` (`OPENHU_AASDK_VERSION` in `CMakeLists.txt`): any release with the
same major version works. `--with-aasdk` downloads and installs the newest one; `--aasdk-path ../aasdk-ref` packages
a local source tree instead. Until the APT repository exists, `apt` cannot fetch aasdk by itself, so install the
`libaasdk` packages together with OpenHU.

```bash
sudo apt install build-essential cmake ninja-build pkg-config git ca-certificates \
    libboost-system-dev libboost-log-dev libprotobuf-dev protobuf-compiler libusb-1.0-0-dev libssl-dev \
    libgps-dev librtaudio-dev qt6-base-dev qt6-declarative-dev qt6-multimedia-dev qt6-websockets-dev \
    qt6-declarative-dev-tools libavcodec-dev libavutil-dev libswscale-dev qml6-module-qtquick \
    qml6-module-qtquick-window qml6-module-qtquick-layouts qml6-module-qtquick-controls qml6-module-qtmultimedia \
    dpkg-dev fakeroot file

git clone https://github.com/average-web-dev/openhu.git
cd openhu
./build.sh release --with-aasdk --package --output-dir ./packages   # installs libaasdk(-dev) with sudo
sudo apt install ./packages/libaasdk_*.deb ./packages/openhu_*.deb
```

The package pulls in the runtime libraries (Qt 6, FFmpeg for H.264, `pactl`) and recommends `bluez`,
`network-manager`, `iw` (wireless Android Auto, car radio) and `python3-websockets` (Python plugins).
It installs a udev rule for phones (`/lib/udev/rules.d/99-openhu.rules`).

### Start

**As a service without desktop** (starts with the system): Debian's Qt 6 has no eglfs platform plugin, which
the service used with Qt 5. Until it runs in a kiosk Wayland compositor (`cage`, issue #9) it needs a session with
X11 or Wayland; the commands stay the same:

```bash
sudo systemctl enable --now openhu
journalctl -u openhu -f
```

The service runs as root, keeps `openhu.ini` in `/var/lib/openhu` and its data in `/root/.openhu`. It uses the
audio server of user 1000 (`XDG_RUNTIME_DIR=/run/user/1000`); adjust the `Environment=` lines with
`sudo systemctl edit openhu` for another user or display setup.

**On a desktop** (X11/Wayland session): run it as your user from a folder for its configuration, e.g. from an
autostart entry:

```bash
mkdir -p ~/.config/openhu && cd ~/.config/openhu && autoapp
```

Phones need to be accessible for that user (the udev rule allows it).

## Wireless Android Auto

Switch it on under **Einstellungen → Kabellos**, then pair the phone once: **Koppeln** (OpenHU is visible for
2 minutes, confirm the code on both sides) or simply plug the phone in once with the cable – OpenHU pairs it over
Android Auto by itself. Afterwards the phone connects without cable whenever it is in range.

What has to be in place:

- **BlueZ** (`bluetoothd`) running, Bluetooth not blocked (`rfkill list`)
- **NetworkManager** manages the WiFi card (`nmcli device` shows it), the card supports access point mode
  (`iw list` → "Supported interface modes: AP"). WiFi switched off is shown on the settings page with a button
  to switch it on.
- **Country** set correctly (Einstellungen → Kabellos → Land): without it 5 GHz access points are not allowed.
  If 5 GHz does not work at all, use band "Auto" or "2,4 GHz" – many phones need 5 GHz for Android Auto, though.
- OpenHU creates the NetworkManager connection `openhu-hotspot` (IPv4 shared, DHCP for the phone) and only
  activates it while wireless Android Auto is on ("Bei Bedarf": only while a phone needs it).

With the phone on the cable USB wins by default ("Vorrang bei Kabel"); unplugged it switches to wireless.

## Bluetooth car radio

Under **Einstellungen → Audio → Autoradio (Bluetooth)**: put the radio into pairing mode, **Suchen**, then
**Verwenden** next to it. OpenHU pairs and connects it and makes it the audio output; it reconnects whenever the
radio is switched on. The radio's buttons control Android Auto, while a call rings play/pause answers it.

<img src="images/settings-audio.png" alt="Audio settings with the car radio section" width="640">

What has to be in place: PipeWire (`pipewire-pulse`, `wireplumber`, Bluetooth support, Debian:
`libspa-0.2-bluetooth`) or PulseAudio with `pulseaudio-module-bluetooth`, so the radio appears as an output
(`pactl list short sinks` shows `bluez_output…`).
