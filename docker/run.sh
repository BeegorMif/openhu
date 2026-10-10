#!/usr/bin/env bash
# Startet OpenHU im Container mit Bildschirm, Ton und USB.
# Auflösung des virtuellen Autodisplays: OPENHU_RESOLUTION=800x480 ./run.sh
set -euo pipefail
cd "$(dirname "$0")"

# Root im Container darf auf den X-Server (XWayland)
xhost +SI:localuser:root >/dev/null

PULSE_SOCK="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/pulse/native"
HOST_DISPLAY="${DISPLAY:-:0}"
HOST_XSOCK="/tmp/.X11-unix/X${HOST_DISPLAY#:}"

# Plugins, Store-Quellen, Cache und Logs bleiben über Neustarts erhalten.
# Der Container schreibt als root, danach gehören die Dateien wieder dem eigenen Benutzer.
mkdir -p data
trap 'docker run --rm -v "$PWD/data:/data" --entrypoint chown openhu:run -R "$(id -u):$(id -g)" /data' EXIT

# Lokales Plugin-Repo (Store-Quelle "file /opt/openhu-plugins") für die Plugin-Entwicklung,
# Standard: neben diesem Repository
PLUGIN_REPO_DIR="${PLUGIN_REPO_DIR:-$(dirname "$(dirname "$PWD")")/openhu-plugins}"
PLUGIN_REPO=()
[ -d "$PLUGIN_REPO_DIR" ] && PLUGIN_REPO=(-v "$PLUGIN_REPO_DIR:/opt/openhu-plugins:ro")

# Kameras (Rückfahrkamera-Plugin) durchreichen, falls vorhanden
VIDEO_DEVICES=()
for dev in /dev/video*; do
  [ -e "$dev" ] && VIDEO_DEVICES+=(--device "$dev")
done

# --network host: nötig, damit udev-Hotplug-Events (Handy wechselt in den AOA-Modus) ankommen.
#                 Außerdem sind so die OpenHU-API (Port 5520) und CAN-Interfaces (vcan0/can0) direkt erreichbar.
# Nur den Host-X-Socket einhängen, damit Xephyrs eigener Socket im Container bleibt.
docker run --rm -it --privileged \
  --name openhu \
  --network host \
  -e DISPLAY="${HOST_DISPLAY}" \
  -e OPENHU_RESOLUTION="${OPENHU_RESOLUTION:-1280x720}" \
  -e QT_QPA_PLATFORM=xcb \
  -v /etc/localtime:/etc/localtime:ro \
  -v "${HOST_XSOCK}:${HOST_XSOCK}" \
  -e PULSE_SERVER=unix:/run/pulse/native \
  -v "${PULSE_SOCK}:/run/pulse/native" \
  -v /dev/bus/usb:/dev/bus/usb \
  -v /run/udev:/run/udev:ro \
  --device /dev/snd \
  --device /dev/dri \
  "${VIDEO_DEVICES[@]}" \
  "${PLUGIN_REPO[@]}" \
  -v "$PWD/config:/config" \
  -v "$PWD/data:/root/.openhu" \
  openhu:run "$@"
