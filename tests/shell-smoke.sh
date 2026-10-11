#!/usr/bin/env bash
# Starts the installed OpenHU headless (offscreen, software rendering) in a temporary HOME, shows every
# shell page once through the API (python3-websockets), stops it with SIGTERM and fails on QML
# errors/warnings or a failed shutdown.
#
#   tests/shell-smoke.sh [autoapp] [seconds before the pages]
set -uo pipefail

autoapp="${1:-autoapp}"
seconds="${2:-8}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
log="$work/autoapp.log"

cd "$work"
# no plugin sources: the store page would download the official index otherwise
mkdir -p "$work/.openhu"
echo "# shell smoke test: no sources" > "$work/.openhu/sources.list"
HOME="$work" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$autoapp" > "$log" 2>&1 &
pid=$!

sleep "$seconds"
status=0
if ! kill -0 "$pid" 2>/dev/null; then
  wait "$pid"
  echo "autoapp ended by itself (exit $?):"
  cat "$log"
  exit 1
fi

# Pages that are not visible at startup (settings, store) only report their QML problems when shown
python3 - <<'PY' || status=1
import json, time
from websockets.sync.client import connect

with connect("ws://127.0.0.1:5520") as ws:
    for i, page in enumerate(["settings", "store", "aa", "home"], 1):
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

kill -TERM "$pid"
for _ in $(seq 1 50); do
  kill -0 "$pid" 2>/dev/null || break
  sleep 0.2
done
if kill -0 "$pid" 2>/dev/null; then
  kill -KILL "$pid"
  echo "autoapp did not stop on SIGTERM"
  status=1
else
  wait "$pid" || { echo "autoapp exit code $? after SIGTERM"; status=1; }
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
[ "$status" -eq 0 ] && echo "Shell started and stopped cleanly"
exit "$status"
