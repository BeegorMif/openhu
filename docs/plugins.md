# OpenHU plugins

A plugin is a folder `~/.openhu/plugins/<id>/` (container: `docker/data/plugins/<id>/`) with a `manifest.json`.
It can bring a QML page, an overlay and a background process in any language that talks to the
[API](api.md). Plugins are installed from the store (Einstellungen → Apps & Store) or copied by hand.

## manifest.json

```json
{
  "id": "org.example.radio",
  "name": "Radio",
  "version": "1.2.0",
  "api": 2,
  "description": "Shown in the store",
  "arch": ["all"],
  "ui": {"page": "qml/Main.qml", "overlay": "qml/Overlay.qml", "icon": "icon.svg", "dock": true},
  "process": {"exec": "bin/radio.py", "args": [], "restart": "on-failure"},
  "permissions": ["audio", "usb"],
  "shared": ["openhu.py"]
}
```

| Key | |
|---|---|
| `id` | folder name, letters, digits, `.`, `-`, `_` |
| `version` | `1.2.3`, compared numerically for updates |
| `api` | plugin API the plugin is made for: `2` = OpenHU with Qt 6 (QML pages for Qt 6). Plugins of another API are not installed and, if copied by hand, not loaded |
| `arch` | `all`, `amd64`, `arm64`, `armhf` |
| `ui.page` | QML page, shown as `plugin:<id>` (dock, "Öffnen" in the store, `ui.navigate`) |
| `ui.dock` | page as dock entry (users can change it, `plugin.setDock`) |
| `ui.overlay` | QML above the whole shell, shown with `ui.overlay.show` / `ui.overlay.hide` |
| `ui.icon` | SVG/PNG for the dock |
| `process.exec` | started with the plugin dir as working directory; `.py`/`.sh` without executable bit run with python3/bash |
| `process.restart` | `on-failure` (default), `always`, `never`; crash loops back off up to 60 s |
| `permissions` | shown in the store before installing (informational) |
| `shared` | files from the repository's `common/` copied to `lib/` when the archive is built |

Processes get `OPENHU_API_URL`, `OPENHU_PLUGIN_ID`, `OPENHU_PLUGIN_DIR` and `OPENHU_DATA_DIR` (persistent data,
`~/.openhu/plugin-data/<id>`, kept on updates). Output goes to `~/.openhu/logs/<id>.log`.
Enabled/disabled and dock state are kept in `~/.openhu/plugins.json`.

## QML

`import OpenHU 1.0` provides:

- `Theme` – colors, sizes and fonts of the shell
- `BusTopic { id: radio; topic: "radio.state" }` – current value of a topic as `radio.data`
- context objects `api.request(method, params)` (returns `{result}` or `{error}`), `api.publish(topic, data)`,
  `api.emitEvent(topic, data)`, `eventBus` (signal `event(topic, data)`), `plugins`, `actions`, `volume`

Typical pattern: the process owns the state and publishes it (`radio.state`), the page shows it with
`BusTopic` and sends commands as one-shot events (`api.emitEvent("radio.command", {cmd: "tune", value: 99.5})`).

## Example

A plugin that shows the speed from the CAN plugin and keeps the top speed – process, page, manifest:

```
plugins/org.example.speed/
├── manifest.json   {"id": "org.example.speed", "name": "Speed", "version": "1.0.0", "api": 2, "arch": ["all"],
│                    "ui": {"page": "qml/Main.qml", "dock": true},
│                    "process": {"exec": "bin/speed.py"}, "shared": ["openhu.py"]}
├── bin/speed.py
└── qml/Main.qml
```

`bin/speed.py` – `openhu.py` (from `common/` of openhu-plugins) is copied to `lib/` by `shared`:

```python
import asyncio, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
from openhu import OpenHU, setup_logging

api = OpenHU()
fastest = 0

async def on_speed(topic, data):
    global fastest
    fastest = max(fastest, data.get("kmh", 0))
    await api.publish("speed.state", kmh=data.get("kmh", 0), fastest=fastest)

async def on_command(topic, data):
    global fastest
    if data.get("cmd") == "reset":
        fastest = 0

api.on("vehicle.speed", on_speed)
api.on("speed.command", on_command)
setup_logging()
asyncio.run(api.run())
```

`qml/Main.qml`:

```qml
import QtQuick
import QtQuick.Controls
import OpenHU 1.0

Rectangle {
    color: Theme.background
    BusTopic { id: speed; topic: "speed.state" }

    Column {
        anchors.centerIn: parent
        Text { text: (speed.data.kmh || 0) + " km/h"; color: Theme.text; font.pixelSize: Theme.fontHuge }
        Text { text: "max. " + (speed.data.fastest || 0); color: Theme.textDim; font.pixelSize: Theme.fontNormal }
        Button { text: "Reset"; onClicked: api.emitEvent("speed.command", {cmd: "reset"}) }
    }
}
```

## Repositories (store sources)

`~/.openhu/sources.list`, one source per line, the first one wins on duplicate ids:

```
github average-web-dev/openhu-plugins main
github someone/my-plugins main
url    https://example.org/openhu-repo/
file   /media/usb/openhu-repo
```

A repository contains `repo.json`, `plugins/<id>/…`, optional `common/`, and the generated
`index.json` and `dist/<id>-<version>.tar.gz`. `tools/openhu-repo-index` (in openhu-plugins) builds
them reproducibly; the store verifies each download against the sha256 in `index.json`.
