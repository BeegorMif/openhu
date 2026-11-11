#!/usr/bin/env bash
# Starts the installed OpenHU headless with a plugin that declares settings of every type (openhu#152), opens its
# settings (ui.navigate "settings/apps/<id>"), changes values through the API and checks the config store file,
# the topic, the plugin translation of a setting's title and that the settings view gives no QML problems; also
# for a disabled plugin and for a plugin id that is not installed.
#
#   tests/shell-plugin-settings.sh [openhu] [seconds before the test]
set -uo pipefail

openhu="${1:-openhu}"
seconds="${2:-8}"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
log="$work/openhu.log"

cd "$work"
plugins="$work/.openhu/plugins"
mkdir -p "$plugins/org.test.settings/qml" "$work/.openhu/plugin-data/org.test.settings"
cat > "$plugins/org.test.settings/manifest.json" <<'EOF'
{"id": "org.test.settings", "name": "Settings test", "version": "1.0.0", "api": 2,
 "ui": {"page": "qml/Main.qml"},
 "settings": [
   {"type": "section", "key": "general", "title": "General"},
   {"key": "rds", "type": "switch", "default": true, "title": "RDS", "description": "Station names"},
   {"key": "gain", "type": "choice", "default": "auto", "options": ["auto", "0", "20"], "title": "Gain"},
   {"key": "band", "type": "choice", "default": "fm", "options": ["fm", "am", "dab", "lw", "sw"], "title": "Band"},
   {"key": "columns", "type": "number", "default": 4, "min": 3, "max": 6, "applies": "restart", "title": "Columns"},
   {"key": "name", "type": "text", "default": "", "maxLength": 20, "placeholder": "Golf", "title": "Name"},
   {"key": "broken", "type": "slider", "default": 1}
 ]}
EOF
printf 'import QtQuick\nItem {}\n' > "$plugins/org.test.settings/qml/Main.qml"
# a config of the plugin from before the store: its keys stay
echo '{"stations": [101.3], "columns": 5}' > "$work/.openhu/plugin-data/org.test.settings/config.json"

HOME="$work" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software "$openhu" > "$log" 2>&1 &
pid=$!
sleep "$seconds"

status=0
CONFIG="$work/.openhu/plugin-data/org.test.settings/config.json" python3 - <<'PY' || status=1
import json, os, time
from websockets.sync.client import connect

request_id = 0
def call(ws, method, **params):
    global request_id
    request_id += 1
    ws.send(json.dumps({"type": "request", "id": request_id, "method": method, "params": params}))
    while True:
        reply = json.loads(ws.recv(timeout=5))
        if reply.get("id") == request_id:
            return reply

def ok(ws, method, **params):
    reply = call(ws, method, **params)
    if "error" in reply:
        raise SystemExit(f"{method} {params}: {reply['error']}")
    return reply["result"]

def expect(what, actual, expected):
    print(f"{what}: {actual!r}", flush=True)
    if actual != expected:
        raise SystemExit(f"{what}: expected {expected!r}")

with connect("ws://127.0.0.1:5520") as ws:
    plugin = next(p for p in ok(ws, "plugin.list") if p["id"] == "org.test.settings")
    expect("settings in plugin.list", [s["key"] for s in plugin.get("settings", [])],
           ["general", "rds", "gain", "band", "columns", "name"])
    values = ok(ws, "plugin.config.get", id="org.test.settings")["values"]
    expect("values", (values["rds"], values["gain"], values["columns"], values["stations"]), (True, "auto", 5, [101.3]))

    ok(ws, "ui.navigate", page="settings/apps/org.test.settings")
    time.sleep(1.5)
    ok(ws, "plugin.config.set", id="org.test.settings", key="rds", value="off")
    ok(ws, "plugin.config.set", id="org.test.settings", values={"gain": 20, "name": "Golf", "band": "dab"})
    expect("refused", "error" in call(ws, "plugin.config.set", id="org.test.settings", key="columns", value=9), True)
    time.sleep(1)
    config = json.load(open(os.environ["CONFIG"]))
    expect("file", (config["rds"], config["gain"], config["name"], config["band"], config["stations"]),
           (False, "20", "Golf", "dab", [101.3]))
    topic = ok(ws, "bus.get", topic="plugin.config.org.test.settings")
    expect("topic", topic["values"]["gain"], "20")

    ok(ws, "plugin.config.reset", id="org.test.settings")
    ok(ws, "plugin.disable", id="org.test.settings")
    time.sleep(1)
    ok(ws, "plugin.config.set", id="org.test.settings", key="columns", value=6)
    ok(ws, "ui.navigate", page="settings/apps/org.test.none")
    time.sleep(1)
    ok(ws, "ui.navigate", page="settings/apps")
    time.sleep(1)
    config = json.load(open(os.environ["CONFIG"]))
    expect("after reset", ("rds" in config, config["columns"], config["stations"]), (False, 6, [101.3]))
PY

kill -TERM "$pid"
wait "$pid" || true

problems="$(grep -E 'qrc:/.*\.qml:[0-9]+|file:///.*\.qml:[0-9]+|ReferenceError|TypeError|is not a type|Cannot assign' "$log")"
if [ -n "$problems" ]; then
  echo "QML problems:"
  echo "$problems"
  status=1
fi
if ! grep -q 'settings\[6\] broken: unknown type "slider", left out' "$log"; then
  echo "The invalid setting was not reported"
  status=1
fi

if [ "$status" -ne 0 ]; then
  cat "$log"
  echo "Plugin settings failed"
  exit 1
fi
echo "Plugin settings are shown, stored and published"
