#!/usr/bin/env bash
# Starts a virtual "car display" (Xephyr) as a window on the host desktop
# and OpenHU inside it - like on a head unit without a window manager.
set -euo pipefail

RES="${OPENHU_RESOLUTION:-1280x720}"
VDISPLAY=":99"

Xephyr "$VDISPLAY" -screen "$RES" -title "OpenHU ($RES)" -nolisten tcp -no-host-grab -ac &
XEPHYR_PID=$!
trap 'kill $XEPHYR_PID 2>/dev/null || true' EXIT
# docker stop: SIGTERM an OpenHU weiterreichen, damit es z. B. den WLAN-Hotspot sauber beendet
trap 'kill -TERM ${APP_PID:-} 2>/dev/null || true' TERM INT

# wait until Xephyr is ready
for _ in $(seq 50); do
  [ -S /tmp/.X11-unix/X99 ] && break
  sleep 0.1
done

DISPLAY="$VDISPLAY" autoapp "$@" &
APP_PID=$!
# wait returns on the signal, the second wait waits for OpenHU to end
wait $APP_PID || true
wait $APP_PID 2>/dev/null || true
