# OpenAuto API

WebSocket + JSON, default `ws://127.0.0.1:5520` (`openauto.ini`: `[Api] Port=5520`, `Listen=127.0.0.1`;
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
| `input.action` | `action`, `phase` (`click`, `press`, `release`) – every triggered action |

Plugins publish their own topics with `bus.publish` (e.g. `vehicle.speed`).

## Methods

| Method | Params | Result |
|---|---|---|
| `system.ping` | – | `{pong:true}` |
| `system.methods` | – | list of methods |
| `bus.publish` | `topic`, `data` (object) | `{}` |
| `bus.get` | `topic` | last value |
| `bus.topics` | – | topics that have a value |
| `volume.get` | – | `{volume, muted}` |
| `volume.set` | `value` (0–100) | `{volume, muted}` |
| `volume.change` | `delta` (e.g. `5`, `-5`) | `{volume, muted}` |
| `volume.mute` | `muted` (optional, toggles without) | `{volume, muted}` |
| `ui.navigate` | `page` (`home`, `aa`) | `{}` |
| `ui.toast` | `text`, `duration` (ms, default 3000) | `{}` |
| `daynight.set` | `night` (bool) | `{}` |
| `input.action` | `action`, `phase` (`click` default, `press`, `release`) | `{handled}` |
| `input.actions` | – | built-in actions with description |
| `input.keymap` | – | active keyboard mapping |

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
| `ui.home`, `ui.aa` | shell pages |
| `ui.back` | back inside Android Auto while it is shown, otherwise to the home page |

Any other name is published as `input.action` only, so plugins can define actions (e.g. `camera.toggle`).
For long presses send `press` and `release` separately.

## Keyboard

`~/.openauto/keymap.json` (in the container: `data/keymap.json`) overrides or extends the defaults.
Keys use Qt names, an empty action removes a key:

```json
{"N": "media.next", "F5": "camera.toggle", "Volume Up": "volume.up", "H": ""}
```

Defaults: arrows/Enter = D-pad, Esc = back, H = AA home, P/O = answer/end call, X/C/B = play/pause/toggle,
V/N = previous/next, M = voice, 1/2 = rotary, F/J/G = navigation/media/phone, +/- and media/volume keys.
