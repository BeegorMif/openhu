# OpenHU API

WebSocket + JSON, default `ws://127.0.0.1:5520` (`openhu.ini`: `[Api] Port=5520`, `Listen=127.0.0.1`;
`Listen=0.0.0.0` for access from the network, `Listen=::` for IPv4 and IPv6).

## Messages

| Direction | Message |
|---|---|
| → | `{"type":"subscribe","topics":["nav.*","media.metadata"]}` – `*` = everything; the current value of each matching topic is sent right away |
| → | `{"type":"unsubscribe","topics":["nav.*"]}` |
| → | `{"type":"request","id":1,"method":"volume.set","params":{"value":40}}` |
| ← | `{"type":"response","id":1,"result":{...}}` or `{"type":"response","id":1,"error":"..."}` |
| ← | `{"type":"event","topic":"nav.turn","data":{...}}` |

Example: `websocat ws://127.0.0.1:5520` and then `{"type":"subscribe","topics":["*"]}`.

## Login (token)

Programs on the device itself (plugins, `openhu`, `websocat` on the head unit) need no login. Clients from the
network and browsers (every client that sends an `Origin` header, also on the device itself) have to send the API
token first. It is created at the first start (`openhu.ini`: `[Api] Token=`) and shown in Settings → Network
→ API token, which also makes a new one (clients logged in with the old one are disconnected). It cannot be read or
set through the API from the network.

The token goes with the handshake or as the first message:

| How | Example |
|---|---|
| Header | `Authorization: Bearer <token>` |
| URL | `ws://<head unit>:5520/?token=<token>` (browsers cannot set headers) |
| Message | → `{"type":"auth","token":"<token>"}` ← `{"type":"auth","authenticated":true}` |

Before the login every other message is answered with the error `authentication required`, and no events are
sent. A wrong token closes the connection (close code 1008), and so does no login within 10 seconds.

```sh
websocat -H "Authorization: Bearer $TOKEN" ws://192.168.1.50:5520
```

Browsers may connect from every origin by default (`[Api] AllowedOrigins=*`); a list such as
`http://192.168.1.10:8080,http://dashboard.local` allows only those, empty allows no browser at all. Browsers always
need the token and never get passwords (`secret` settings), not even on the device itself, so a web page cannot use
the API behind the user's back.

The connection is not encrypted (`ws://`): the token travels in clear text through the network, use the API only in
networks you trust.

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
| `system.daynight` | `night` (bool) – switches Android Auto to night mode while Settings → Android Auto → Night mode is "Automatic" |
| `system.nightMode` | `mode` (`auto`, `day`, `night`) – that setting (`General.NightMode`) |
| `input.action` | `action`, `phase` (`click`, `press`, `release`) – every triggered action (one-shot event, no stored value) |
| `ui.page` | `page` – page shown in the shell (`home`, `aa`, `settings`, `plugin:<id>`) |
| `ui.language` | `language` (`de`, `en`), `available` – language of the shell; texts of plugin processes can follow it |
| `projection.exit` | – one-shot: "exit" inside Android Auto, the shell shows its home page |
| `plugins.state` | `plugins` – installed plugins after (re)loading |
| `store.state` | `busy`, `status`, `error` – progress of store operations |
| `wireless.state` | `enabled`, `wifiAvailable` (a WiFi card is there), `wifiInterface`, `bluetoothAvailable` (a Bluetooth adapter is there; wireless Android Auto needs both and starts by itself when one is plugged in later), `running`, `hotspot` (`off`, `starting`, `on`, `error`), `hotspotMode` (`always`, `demand`), `hotspotError`, `hotspotRoom` (the hotspot leaves the card to WiFi networks: `search`, `client`, empty), `searchUntil` (ms since 1970), `wifiOff`, `interface`, `channel`, `ssid`, `bluetooth` (`off`, `on`, `error`), `bluetoothError`, `phase` (`idle`, `waiting`, `hotspot`, `handshake`, `joining`, `connected`, `error`), `phaseText`, `device` |
| `wifi.state` | `available` (a WiFi card is there), `ready` (the cards are known), `enabled`, `devices` (WiFi cards), `client` (`ssid`, `ip`, `device`, `uuid`) while OpenHU is in a known WiFi network, `internet`, `networks` (saved ones, `uuid`, `ssid`, `priority`, `secured`, `hidden`, `active`, `joined` = connected at least once; highest priority first), `found` (after `wifi.scan`: `ssid`, `signal`, `security`, `secured`, `known`), `scanning`, `busy`, `message`, `hotspotDevice` (the card runs OpenHU's hotspot) |
| `bluetooth.pairing` | `active`, `device`, `passkey` – a device asks to pair (confirmed in the shell or with `bluetooth.confirm`) |
| `bluetooth.state` | `available` (an adapter is there, also before it is used), `error`, `adapter`, `pairing` (visible), `pairingDevice`, `pairingPasskey`, `pairingWith` (pairing started by OpenHU), `searching` |
| `caraudio.state` | `enabled`, `device`, `name`, `paired`, `connected`, `sink`, `error`, `status` – sound to the Bluetooth car radio |
| `system.update` | OpenHU OS only: `state` (`idle`, `checking`, `installing`, `done`, `uptodate`, `error`, `cancelled`), `phase` (`root`, `kernel`), `percent`, `from`, `version`, `error`, `source` (`usb`, `online`), after a check `checked`, `current`, `newest`, `available`, `source`, `checkError`; `usb` while a USB stick with OpenHU OS is plugged in (`version`, `newer`, `path`); `offer` – what `system.update.install` installs (`version`, `source`: the newer of the checked version and the stick's); `busy`, `blocked` (Android Auto connected) |
| `settings.changed` | `key`, `value` (not for passwords), `pending` (`reconnect`, `restart`) – a setting was changed (settings page or `settings.set`) |
| `system.ssh` | OpenHU OS only: `enabled`, `running`, `password` (set), `keys`, `busy`, `addresses` |
| `system.time` | OpenHU OS only: `zone` (`Europe/Berlin`, `UTC`), `offset` (seconds east of UTC), `ntp` (time from the internet), `synchronized`, `time` (seconds since 1970), `busy` |

Plugins publish their own topics with `bus.publish` (e.g. `vehicle.speed`).

API clients on the wireless hotspot network are refused unless "API auch im WLAN-Hotspot" is switched on
(the hotspot is meant for the phone; connect API devices over Ethernet). If they are allowed, they need the token too.

## Methods

| Method | Params | Result |
|---|---|---|
| `system.ping` | – | `{pong:true}` |
| `system.methods` | – | list of methods |
| `bus.publish` | `topic`, `data` (object) | `{}` |
| `bus.emit` | `topic`, `data` (object) | `{}` – event without keeping a value (commands for plugin processes) |
| `bus.get` | `topic` | last value |
| `bus.topics` | – | topics that have a value |
| `settings.keys` | – | per key: `type` (`bool`, `int`, `string`, `choice`), `choices`, `min`/`max`, `maxLength`, `applies` (`now`, `reconnect`: next Android Auto connection, `restart`: of OpenHU), `secret`, `readOnly` |
| `settings.list` | – | `{values, pending}` – all settings (keys as in the settings page: `aa.fps`, `wireless.hotspot`, …); passwords and the API token (`secret`) only for clients on the device |
| `settings.get` | `key` | `{key, value}`; unknown key → error |
| `settings.set` | `key`, `value` | `{key, value, applies, pending}` – the same checks as the settings page, saved to `openhu.ini` right away; the value may be text (`"60"`, `"on"`, `"false"`) and is converted to the key's type; a wrong value → error with the reason |
| `volume.get` | – | `{volume, muted}` |
| `volume.set` | `value` (0–100) | `{volume, muted}` |
| `volume.change` | `delta` (e.g. `5`, `-5`) | `{volume, muted}` |
| `volume.mute` | `muted` (optional, toggles without) | `{volume, muted}` |
| `ui.navigate` | `page` (`home`, `aa`, `settings`, `settings/<category>`, `store` = `settings/apps`, `plugin:<id>`); categories: `general`, `aa`, `audio`, `wifi`, `bluetooth`, `network`, `keys`, `apps`, `system`, `info` (one that is not there, e.g. without its adapter, shows General) | `{}` |
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
| `wifi.state` | – | like the topic `wifi.state` |
| `wifi.scan` | – | `{queued}` – networks in range in `found`; on a card shared with an "Always on" hotspot the hotspot is off for 5 minutes (not during wireless Android Auto) |
| `wifi.add` | `ssid`, `password` (8–63 characters, empty for an open network), `hidden` | `{queued}` – saved as NetworkManager profile `openhu-wifi-<ssid>` and joined like `wifi.connect`; a wrong password removes it again |
| `wifi.connect` | `uuid` | `{queued}` – join it now; on a shared card the hotspot stays off while the connection lasts. Joined once, NetworkManager joins it by itself afterwards |
| `wifi.remove` | `uuid` | `{queued}` |
| `wifi.moveUp` | `uuid` | `{queued}` – one place higher in the order NetworkManager tries them |
| `bluetooth.state` | – | like the topic `bluetooth.state` |
| `bluetooth.devices` | – | paired devices (`address`, `name`, `connected`, `icon`, `uuids`) |
| `bluetooth.search` | `enabled` (default `true`), `seconds` (default 30) | `{discovered}` – devices found so far |
| `bluetooth.pair` | `address`, `pin` (optional, legacy pairing) | `{}` – OpenHU pairs and confirms by itself |
| `bluetooth.remove` | `address` | `{}` |
| `system.os` | – | `openhuOs` (bool), `version` (OpenHU OS version), `boot` (`state`: `normal`, `trial`, `updated`, `rollback`; `running`, `failed`) |
| `system.update.state` | – | like the topic `system.update` (OpenHU OS only, the others too) |
| `system.update.check` | – | `{queued}` – the USB sticks and the update server, result in `system.update` (`newest`, `available`, `source`) |
| `system.update.install` | – | `{queued}` – `offer` of `system.update`: the newest version found by the check or on a USB stick; refused while Android Auto is connected. Progress in `system.update`; after `done` reboot (`input.action` or the shell) |
| `system.update.cancel` | – | `{}` – the running version stays |
| `system.ssh.state` | – | like the topic `system.ssh` |
| `system.ssh.enable` | `enabled` (default `true`) | `{queued}` – needs a root password, which is set in the shell only (Settings → System) |
| `system.time.state` | – | like the topic `system.time` |
| `system.time.zones` | – | the time zones to choose from (`Africa/Abidjan`, …, `UTC`) |
| `system.time.setZone` | `zone` | `{queued}` – applies at once |
| `system.time.setNtp` | `enabled` (default `true`) | `{queued}` – time from the internet on or off |
| `system.time.set` | `time` (seconds since 1970, UTC) | `{queued}` – only while the time from the internet is off; also sets the hardware clock |
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

Edited on the settings page (Keys), stored as differences to the defaults in `~/.openhu/keymap.json`
(container: `docker/data/keymap.json`).
Keys use Qt names, an empty action removes a key:

```json
{"N": "media.next", "F5": "camera.toggle", "Volume Up": "volume.up", "H": ""}
```

Defaults: arrows/Enter = D-pad, Esc = back, H = AA home, P/O = answer/end call, X/C/B = play/pause/toggle,
V/N = previous/next, M = voice, 1/2 = rotary, F/J/G = navigation/media/phone, +/- and media/volume keys.
