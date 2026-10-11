# OpenHU API

WebSocket + JSON, default `ws://127.0.0.1:5520` (`openhu.ini`: `[Api] Port=5520`, `Listen=127.0.0.1`;
`Listen=0.0.0.0` for access from the network).

## Messages

| Direction | Message |
|---|---|
| → | `{"type":"subscribe","topics":["nav.*","media.metadata"]}` – `*` = everything; the current value of each matching topic is sent right away |
| → | `{"type":"unsubscribe","topics":["nav.*"]}` |
| → | `{"type":"request","id":1,"method":"volume.set","params":{"value":40}}` |
| ← | `{"type":"response","id":1,"result":{...}}` or `{"type":"response","id":1,"error":"..."}` |
| ← | `{"type":"event","topic":"nav.turn","data":{...}}` |

Example: `websocat ws://127.0.0.1:5520` and then `{"type":"subscribe","topics":["*"]}`.

## Topics (events)

| Topic | Data |
|---|---|
| `projection.state` | `connected` (bool) – Android Auto session running |
| `projection.video` | `active` (bool) – projection video playing |
| `projection.transport` | `transport` (`usb`, `wifi`) – how the current session is connected |
| `nav.status` | `state`: `active`, `inactive`, `rerouting`, `unavailable` |
| `nav.turn` | `road`, `maneuver` (e.g. `turn_normal_left`, `roundabout_enter`, `destination`), `side` (`left`/`right`/`unspecified`), `roundaboutExit`, `turnAngle`, `image` (data URL, if the phone sends one), `lanes` (`[{directions:[...], highlighted}]`), `cue` |
| `nav.distance` | to the next maneuver: `meters`, `seconds`, `display` (as shown by the phone), `unit` (`meters`, `kilometers`, `miles`, …) |
| `nav.destination` | `address`, `meters`, `display`, `unit`, `seconds`, `eta` |
| `nav.road` | `name` – current road |
| `media.metadata` | `title`, `artist`, `album`, `playlist`, `duration` (s), `cover` (data URL), `source` |
| `media.status` | `state` (`playing`, `paused`, `stopped`), `app`, `position` (s), `shuffle`, `repeat`, `repeatOne`, `source` |
| `phone.status` | `callState` (`idle`, `incoming`, `in_call`, `on_hold`, …), `calls` (`[{state, duration, number, name, numberType, thumbnail}]`), `signal` |
| `audio.volume` | `volume` (0–100), `muted` |
| `system.daynight` | `night` (bool) – also switches Android Auto to night mode |
| `input.action` | `action`, `phase` (`click`, `press`, `release`) – every triggered action (one-shot event, no stored value) |
| `ui.page` | `page` – page shown in the shell (`home`, `aa`, `settings`, `plugin:<id>`) |
| `projection.exit` | – one-shot: "exit" inside Android Auto, the shell shows its home page |
| `plugins.state` | `plugins` – installed plugins after (re)loading |
| `store.state` | `busy`, `status`, `error` – progress of store operations |
| `wireless.state` | `enabled`, `running`, `hotspot` (`off`, `starting`, `on`, `error`), `hotspotMode` (`always`, `demand`), `hotspotError`, `wifiOff`, `interface`, `channel`, `ssid`, `bluetooth` (`off`, `on`, `error`), `bluetoothError`, `phase` (`idle`, `waiting`, `hotspot`, `handshake`, `joining`, `connected`, `error`), `phaseText`, `device` |
| `bluetooth.pairing` | `active`, `device`, `passkey` – a device asks to pair (confirmed in the shell or with `bluetooth.confirm`) |
| `bluetooth.state` | `available`, `error`, `adapter`, `pairing` (visible), `pairingDevice`, `pairingPasskey`, `pairingWith` (pairing started by OpenHU), `searching` |
| `caraudio.state` | `enabled`, `device`, `name`, `paired`, `connected`, `sink`, `error`, `status` – sound to the Bluetooth car radio |

Plugins publish their own topics with `bus.publish` (e.g. `vehicle.speed`).

API clients on the wireless hotspot network are refused unless "API auch im WLAN-Hotspot" is switched on
(the hotspot is meant for the phone; connect API devices over Ethernet).

## Methods

| Method | Params | Result |
|---|---|---|
| `system.ping` | – | `{pong:true}` |
| `system.methods` | – | list of methods |
| `bus.publish` | `topic`, `data` (object) | `{}` |
| `bus.emit` | `topic`, `data` (object) | `{}` – event without keeping a value (commands for plugin processes) |
| `bus.get` | `topic` | last value |
| `bus.topics` | – | topics that have a value |
| `volume.get` | – | `{volume, muted}` |
| `volume.set` | `value` (0–100) | `{volume, muted}` |
| `volume.change` | `delta` (e.g. `5`, `-5`) | `{volume, muted}` |
| `volume.mute` | `muted` (optional, toggles without) | `{volume, muted}` |
| `ui.navigate` | `page` (`home`, `aa`, `settings`, `store` = settings/apps, `plugin:<id>`) | `{}` |
| `ui.toast` | `text`, `duration` (ms, default 3000) | `{}` |
| `daynight.set` | `night` (bool) | `{}` |
| `projection.reconnect` | – | `{reset:[...]}` – USB reset of the phone, Android Auto reconnects |
| `input.action` | `action`, `phase` (`click` default, `press`, `release`) | `{handled}` |
| `input.actions` | – | built-in actions with description |
| `input.keymap` | – | active keyboard mapping |
| `plugin.list` | – | installed plugins (`id`, `name`, `version`, `enabled`, `running`, `dock`, `page`, `overlay`, …) |
| `plugin.enable` / `plugin.disable` | `id` | plugin |
| `plugin.restart` | `id` | plugin – restarts its process |
| `plugin.setDock` | `id`, `dock` (bool) | plugin – page as dock entry |
| `ui.overlay.show` / `ui.overlay.hide` | `id` | `{}` – plugin overlay above everything |
| `store.list` | – | packages of all sources with `installed`, `installedVersion`, `updateAvailable`, `source`, `official` |
| `store.refresh` | – | `{queued}` – reload `index.json` of all sources |
| `store.install` | `id` | `{queued}` – install or update |
| `store.update` | `id` (optional) | `{queued}` – one or all updates |
| `store.remove` | `id` | `{queued}` |
| `store.sources.list` | – | sources |
| `store.sources.add` | `type` (`github`/`url`/`file`), `location`, `branch` | sources |
| `store.sources.remove` | `index` | sources |
| `wireless.state` | – | like the topic `wireless.state` |
| `bluetooth.state` | – | like the topic `bluetooth.state` |
| `bluetooth.devices` | – | paired devices (`address`, `name`, `connected`, `icon`, `uuids`) |
| `bluetooth.search` | `enabled` (default `true`), `seconds` (default 30) | `{discovered}` – devices found so far |
| `bluetooth.pair` | `address`, `pin` (optional, legacy pairing) | `{}` – OpenHU pairs and confirms by itself |
| `bluetooth.remove` | `address` | `{}` |
| `caraudio.state` | – | like the topic `caraudio.state` |
| `caraudio.select` | `address` | car audio state – use this radio (pairs it), switches car audio on |
| `caraudio.forget` | – | car audio state – unpairs the radio, switches car audio off |
| `bluetooth.pairing` | `enabled` (default `true`) | `{pairing}` – visible for 2 minutes, wireless Android Auto must be on |
| `bluetooth.confirm` | `accept` (bool) | `{}` – answer to `bluetooth.pairing` |
| `bluetooth.connect` | `address` | `{}` – use this phone for wireless Android Auto and connect it |

Store operations run in the background; progress and errors arrive as `store.state` (`busy`, `status`, `error`).
Plugins: see [plugins.md](plugins.md).

## Actions

Steering wheel buttons, CAN, GPIO and keyboard all end up as actions (`input.action`).

| Action | Effect |
|---|---|
| `media.play_pause`, `media.play`, `media.pause`, `media.next`, `media.previous` | Android Auto media keys |
| `call.answer`, `call.end` | accept / end or reject a call |
| `voice` | voice assistant |
| `aa.home`, `aa.back`, `aa.navigation`, `aa.media`, `aa.phone` | Android Auto screens |
| `dpad.up`, `dpad.down`, `dpad.left`, `dpad.right`, `dpad.enter`, `rotary.left`, `rotary.right` | navigation inside Android Auto |
| `volume.up`, `volume.down` (±5 %), `volume.mute` | system volume |
| `ui.home`, `ui.aa`, `ui.settings` | shell pages |
| `ui.back` | back inside Android Auto while it is shown, otherwise to the home page |
| `aa.reconnect` | USB reset of the phone (also: tap on the phone icon in the status bar) |

Any other name is published as `input.action` only, so plugins can define actions (e.g. `camera.toggle`).
For long presses send `press` and `release` separately.

## Keyboard

Edited on the settings page (Tasten), stored as differences to the defaults in `~/.openhu/keymap.json`
(container: `docker/data/keymap.json`).
Keys use Qt names, an empty action removes a key:

```json
{"N": "media.next", "F5": "camera.toggle", "Volume Up": "volume.up", "H": ""}
```

Defaults: arrows/Enter = D-pad, Esc = back, H = AA home, P/O = answer/end call, X/C/B = play/pause/toggle,
V/N = previous/next, M = voice, 1/2 = rotary, F/J/G = navigation/media/phone, +/- and media/volume keys.
