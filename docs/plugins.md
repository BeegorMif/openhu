# OpenAuto plugins

A plugin is a folder `~/.openauto/plugins/<id>/` (container: `./data/plugins/<id>/`) with a `manifest.json`.
It can bring a QML page, an overlay and a background process in any language that talks to the
[API](api.md). Plugins are installed from the store (Apps → Store) or copied by hand.

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
  "shared": ["oa.py"]
}
```

| Key | |
|---|---|
| `id` | folder name, letters, digits, `.`, `-`, `_` |
| `version` | `1.2.3`, compared numerically for updates |
| `arch` | `all`, `amd64`, `arm64`, `armhf` |
| `ui.page` | QML page, shown as `plugin:<id>` (Apps page, `ui.navigate`) |
| `ui.dock` | page as dock entry (users can change it, `plugin.setDock`) |
| `ui.overlay` | QML above the whole shell, shown with `ui.overlay.show` / `ui.overlay.hide` |
| `ui.icon` | SVG/PNG for dock and Apps page |
| `process.exec` | started with the plugin dir as working directory; `.py`/`.sh` without executable bit run with python3/bash |
| `process.restart` | `on-failure` (default), `always`, `never`; crash loops back off up to 60 s |
| `permissions` | shown in the store before installing (informational) |
| `shared` | files from the repository's `common/` copied to `lib/` when the archive is built |

Processes get `OA_API_URL`, `OA_PLUGIN_ID`, `OA_PLUGIN_DIR` and `OA_DATA_DIR` (persistent data,
`~/.openauto/plugin-data/<id>`, kept on updates). Output goes to `~/.openauto/logs/<id>.log`.
Enabled/disabled and dock state are kept in `~/.openauto/plugins.json`.

## QML

`import OpenAuto 1.0` provides:

- `Theme` – colors, sizes and fonts of the shell
- `BusTopic { id: radio; topic: "radio.state" }` – current value of a topic as `radio.data`
- context objects `oa.request(method, params)` (returns `{result}` or `{error}`), `oa.publish(topic, data)`,
  `oa.emitEvent(topic, data)`, `eventBus` (signal `event(topic, data)`), `plugins`, `actions`, `volume`

Typical pattern: the process owns the state and publishes it (`radio.state`), the page shows it with
`BusTopic` and sends commands as one-shot events (`oa.emitEvent("radio.command", {cmd: "tune", value: 99.5})`).

## Repositories (store sources)

`~/.openauto/sources.list`, one source per line, the first one wins on duplicate ids:

```
github average-web-dev/openauto-plugins main
github someone/my-plugins main
url    https://example.org/oa-repo/
file   /media/usb/oa-repo
```

A repository contains `repo.json`, `plugins/<id>/…`, optional `common/`, and the generated
`index.json` and `dist/<id>-<version>.tar.gz`. `tools/oa-repo-index` (in openauto-plugins) builds
them reproducibly; the store verifies each download against the sha256 in `index.json`.
