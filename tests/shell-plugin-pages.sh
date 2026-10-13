#!/usr/bin/env bash
# Starts the installed OpenHU headless with two plugins and checks through the API that the page and overlay
# of one plugin survive changes of the other plugin and of its own dock entry, and are unloaded/loaded again
# when the plugin itself is disabled/enabled (openhu#19). The QML of the plugin logs when it is created and
# destroyed.
#
#   tests/shell-plugin-pages.sh [openhu] [seconds before the test]
set -uo pipefail

openhu="${1:-openhu}"
seconds="${2:-8}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
log="$work/openhu.log"

cd "$work"
plugins="$work/.openhu/plugins"
mkdir -p "$plugins/watched/qml" "$plugins/other/qml"
echo "# shell plugin pages test: no sources" > "$work/.openhu/sources.list"

cat > "$plugins/watched/manifest.json" <<'EOF'
{"id": "watched", "name": "Watched", "version": "1.0.0", "api": 2,
 "ui": {"page": "qml/Main.qml", "overlay": "qml/Overlay.qml", "dock": true}}
EOF
cat > "$plugins/watched/qml/Main.qml" <<'EOF'
import QtQuick
Item {
    Component.onCompleted: console.log("TEST page created")
    Component.onDestruction: console.log("TEST page destroyed")
}
EOF
cat > "$plugins/watched/qml/Overlay.qml" <<'EOF'
import QtQuick
Item {
    Component.onCompleted: console.log("TEST overlay created")
    Component.onDestruction: console.log("TEST overlay destroyed")
}
EOF
cat > "$plugins/other/manifest.json" <<'EOF'
{"id": "other", "name": "Other", "version": "1.0.0", "api": 2, "ui": {"page": "qml/Main.qml", "dock": true}}
EOF
printf 'import QtQuick\nItem {}\n' > "$plugins/other/qml/Main.qml"

HOME="$work" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$openhu" > "$log" 2>&1 &
pid=$!
sleep "$seconds"

status=0
LOG="$log" python3 - <<'PY' || status=1
import json, os, time
from websockets.sync.client import connect

def counts():
    text = open(os.environ["LOG"]).read()
    return tuple(text.count(f"TEST {what}") for what in
                 ("page created", "page destroyed", "overlay created", "overlay destroyed"))

request_id = 0
def step(ws, method, expected, **params):
    global request_id
    request_id += 1
    ws.send(json.dumps({"type": "request", "id": request_id, "method": method, "params": params}))
    while True:
        reply = json.loads(ws.recv(timeout=5))
        if reply.get("id") == request_id:
            break
    if "error" in reply:
        raise SystemExit(f"{method} {params}: {reply['error']}")
    time.sleep(1.5)
    actual = counts()
    print(f"{method} {params}: page created/destroyed, overlay created/destroyed = {actual}", flush=True)
    if actual != expected:
        raise SystemExit(f"expected {expected}")

with connect("ws://127.0.0.1:5520") as ws:
    step(ws, "ui.navigate", (1, 0, 0, 0), page="plugin:watched")
    step(ws, "ui.overlay.show", (1, 0, 1, 0), id="watched")
    # another plugin changes: page and overlay of "watched" stay
    step(ws, "plugin.disable", (1, 0, 1, 0), id="other")
    step(ws, "plugin.enable", (1, 0, 1, 0), id="other")
    step(ws, "plugin.setDock", (1, 0, 1, 0), id="watched", dock=False)
    # the plugin itself
    step(ws, "plugin.disable", (1, 1, 1, 1), id="watched")
    step(ws, "plugin.enable", (2, 1, 1, 1), id="watched")
PY

kill -TERM "$pid"
wait "$pid" || true

if [ "$status" -ne 0 ]; then
  cat "$log"
  echo "Plugin pages were recreated or not reloaded"
  exit 1
fi
echo "Plugin pages survive changes of other plugins"
