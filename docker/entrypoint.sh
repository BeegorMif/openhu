#!/usr/bin/env bash
# Startet ein virtuelles "Autodisplay" (Xephyr) als ein Fenster auf dem Host-Desktop
# und darin OpenHU - wie auf einem Head-Unit ohne Fenstermanager.
set -euo pipefail

RES="${OPENHU_RESOLUTION:-1280x720}"
VDISPLAY=":99"

Xephyr "$VDISPLAY" -screen "$RES" -title "OpenHU ($RES)" -nolisten tcp -no-host-grab -ac &
XEPHYR_PID=$!
trap 'kill $XEPHYR_PID 2>/dev/null || true' EXIT

# warten, bis Xephyr bereit ist
for _ in $(seq 50); do
  [ -S /tmp/.X11-unix/X99 ] && break
  sleep 0.1
done

DISPLAY="$VDISPLAY" autoapp "$@"
