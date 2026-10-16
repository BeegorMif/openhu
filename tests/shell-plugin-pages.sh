#!/usr/bin/env bash
# Starts the installed OpenHU headless with two plugins and checks through the API that the page and overlay
# of one plugin survive changes of the other plugin and of its own dock entry, and are unloaded/loaded again
# when the plugin itself is disabled/enabled (openhu#19). The QML of the plugin logs when it is created and
# destroyed. An update of the other plugin from the store shows its new QML files at once (openhu#105).
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
mkdir -p "$plugins/watched/qml"
# the store offers "other" 1.0.1
echo "file $work/repo" > "$work/.openhu/sources.list"

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
# "other": a page with a second QML file that logs the version
other() {  # directory version
  mkdir -p "$1/qml"
  printf '{"id": "other", "name": "Other", "version": "%s", "api": 2, "ui": {"page": "qml/Main.qml", "dock": true}}\n' "$2" \
    > "$1/manifest.json"
  printf 'import QtQuick\nItem { View {} }\n' > "$1/qml/Main.qml"
  printf 'import QtQuick\nItem { Component.onCompleted: console.log("TEST other %s") }\n' "$2" > "$1/qml/View.qml"
}
other "$plugins/other" 1.0.0
mkdir -p "$work/repo/dist" "$work/package"
other "$work/package/other" 1.0.1
tar -czf "$work/repo/dist/other.tar.gz" -C "$work/package" other
printf '{"name": "Test", "api": 2, "plugins": [{"id": "other", "name": "Other", "version": "1.0.1", "api": 2, "arch": ["all"], "archive": "dist/other.tar.gz", "sha256": "%s"}]}\n' \
  "$(sha256sum "$work/repo/dist/other.tar.gz" | cut -d" " -f1)" > "$work/repo/index.json"

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
    # an update of "other" from the store: its new QML at once, the page of "watched" stays
    step(ws, "ui.navigate", (2, 1, 1, 1), page="plugin:other")
    step(ws, "store.refresh", (2, 1, 1, 1))
    step(ws, "store.install", (2, 1, 1, 1), id="other")
    deadline = time.time() + 15
    while "TEST other 1.0.1" not in open(os.environ["LOG"]).read() and time.time() < deadline:
        time.sleep(0.5)
    text = open(os.environ["LOG"]).read()
    print("other:", [line.split("TEST other ")[1] for line in text.splitlines() if "TEST other " in line], flush=True)
    if "TEST other 1.0.0" not in text or "TEST other 1.0.1" not in text:
        raise SystemExit("expected the page of other 1.0.0, then of 1.0.1")
PY

kill -TERM "$pid"
wait "$pid" || true

if [ "$status" -ne 0 ]; then
  cat "$log"
  echo "Plugin pages were recreated, not reloaded or not updated"
  exit 1
fi
echo "Plugin pages survive changes of other plugins and show updates at once"
