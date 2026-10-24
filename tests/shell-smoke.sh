#!/usr/bin/env bash
# Starts the installed OpenHU headless (offscreen, software rendering) in a temporary HOME, shows every
# shell page once through the API (python3-websockets), stops it with SIGTERM and fails on QML
# errors/warnings, a failed shutdown or missing systemd notifications (READY=1, watchdog pings).
#
#   tests/shell-smoke.sh [openhu] [seconds before the pages]
set -uo pipefail

openhu="${1:-openhu}"
seconds="${2:-8}"

work="$(mktemp -d)"
listener=""
trap '[ -n "$listener" ] && kill "$listener" 2>/dev/null; rm -rf "$work"' EXIT
log="$work/openhu.log"

cd "$work"
# no plugin sources: the store page would download the official index otherwise
mkdir -p "$work/.openhu"
echo "# shell smoke test: no sources" > "$work/.openhu/sources.list"

# systemd's side of the notify socket (Type=notify, WatchdogSec=2 s on OpenHU OS: 10 s, #129), one line per message
python3 - "$work/notify" > "$work/notify.log" <<'PY' &
import socket, sys
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
s.bind(sys.argv[1])
while True:
    print(s.recv(512).decode().replace("\n", " "), flush=True)
PY
listener=$!
for _ in $(seq 1 25); do
  [ -S "$work/notify" ] && break
  sleep 0.2
done

HOME="$work" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software NOTIFY_SOCKET="$work/notify" WATCHDOG_USEC=2000000 \
  "$openhu" > "$log" 2>&1 &
pid=$!

sleep "$seconds"
status=0
if ! kill -0 "$pid" 2>/dev/null; then
  wait "$pid"
  echo "OpenHU ended by itself (exit $?):"
  cat "$log"
  exit 1
fi

# Pages that are not visible at startup (settings, store) only report their QML problems when shown
python3 - <<'PY' || status=1
import json, time
from websockets.sync.client import connect

with connect("ws://127.0.0.1:5520") as ws:
    # every settings category once (one QML file each); missing hardware falls back to General
    categories = ["general", "aa", "audio", "wifi", "bluetooth", "network", "keys", "system", "info"]
    for i, page in enumerate([f"settings/{c}" for c in categories] + ["store", "aa", "home"], 1):
        ws.send(json.dumps({"type": "request", "id": i, "method": "ui.navigate", "params": {"page": page}}))
        reply = json.loads(ws.recv(timeout=5))
        if "error" in reply:
            raise SystemExit(f"ui.navigate {page}: {reply['error']}")
        print(f"page {page}", flush=True)
        time.sleep(1.5)
    ws.send(json.dumps({"type": "request", "id": 99, "method": "ui.toast", "params": {"text": "smoke test"}}))
    ws.recv(timeout=5)
    time.sleep(1)
PY

# A second OpenHU must end right away (instance lock), without touching USB, the API port or the display
second="$(HOME="$work" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software timeout 20 "$openhu" 2>&1)"
second_status=$?
if [ "$second_status" -ne 1 ] || ! grep -q "OpenHU is already running (pid $pid)" <<< "$second"; then
  echo "A second OpenHU did not end with the instance lock (exit $second_status):"
  echo "$second"
  status=1
fi

kill -TERM "$pid"
for _ in $(seq 1 50); do
  kill -0 "$pid" 2>/dev/null || break
  sleep 0.2
done
if kill -0 "$pid" 2>/dev/null; then
  kill -KILL "$pid"
  echo "OpenHU did not stop on SIGTERM"
  status=1
else
  wait "$pid" || { echo "OpenHU exit code $? after SIGTERM"; status=1; }
fi

cat "$log"
echo

# QML reports problems as "<url>:<line>:<column>: <message>" or with these words
problems="$(grep -E 'qrc:/.*\.qml:[0-9]+|file:///.*\.qml:[0-9]+|ReferenceError|TypeError|is not a type|Cannot assign|Failed to load the QML shell|module ".*" is not installed' "$log")"
if [ -n "$problems" ]; then
  echo "QML problems at startup:"
  echo "$problems"
  status=1
fi
if ! grep -q "\[AutoApp\] Display" "$log"; then
  echo "The shell window was not shown"
  status=1
fi
# READY=1 once the shell is up, then a watchdog ping every second from the GUI thread (about 25 s of pages)
if ! head -n 1 "$work/notify.log" | grep -qx "READY=1 WATCHDOG_USEC=2000000"; then
  echo "OpenHU did not report READY=1 to systemd first:"
  cat "$work/notify.log"
  status=1
fi
pings="$(grep -cx "WATCHDOG=1" "$work/notify.log")"
if [ "$pings" -lt 10 ]; then
  echo "Only $pings watchdog pings"
  status=1
fi
[ "$status" -eq 0 ] && echo "Shell started and stopped cleanly ($pings watchdog pings)"
exit "$status"
