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
  "api": 1,
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
