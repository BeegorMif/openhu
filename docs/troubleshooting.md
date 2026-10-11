# Troubleshooting

## Logs

| | |
|---|---|
| Service | `journalctl -u openhu -f` |
| Container | output of `docker/dev.sh run` (or `docker logs openhu`) |
| Plugins | `~/.openhu/logs/<id>.log` (container: `docker/data/logs/`) |
| Live data | `websocat ws://127.0.0.1:5520`, then `{"type":"subscribe","topics":["*"]}` ([api.md](api.md)) |
| Crashes | `coredumpctl list autoapp` (also catches crashes inside the container) |

## Android Auto over USB

**The phone is not detected / Android Auto does not start.** Tap the phone icon in the status bar (or
`docker/dev.sh reconnect`, API `projection.reconnect`): it resets the phone on USB as if it was plugged in again.
Check that the udev rule is installed (`/lib/udev/rules.d/99-openhu.rules`) when not running as root.

**Black picture, but Android Auto is connected.** The video is decoded with FFmpeg: the log shows
`[FfmpegVideoOutput] decoder …` at the start of a session and every 10 s fps, decoding time and errors. A hardware
decoder that shows no picture is replaced by software automatically; `Video.Decoder=software` in `openhu.ini`
forces it. When building yourself, make sure only one aasdk is
installed: builds before the `libaasdk` packages put it into `/usr/local`, and the compiler prefers the old headers in
`/usr/local/include`. Remove it with
`sudo rm -r /usr/local/lib/libaasdk* /usr/local/lib/libaap_protobuf* /usr/local/lib/cmake/aasdk /usr/local/lib/cmake/aap_protobuf /usr/local/include/aasdk /usr/local/include/aap_protobuf`.

**SSL errors right after a restart of OpenHU** (`SSL routines::unexpected message`). The phone was still in
accessory mode from the previous run. OpenHU resets such a phone at startup; if it still happens, unplug it once.

## Wireless Android Auto

The state is shown under **Einstellungen → Kabellos** and in the topic `wireless.state`.

| Message / symptom | Cause, fix |
|---|---|
| "WLAN ist ausgeschaltet." | WiFi radio off or blocked: button "WLAN einschalten" (`nmcli radio wifi on`), `rfkill list` |
| "Kein WLAN-Gerät gefunden." / "NetworkManager (nmcli) fehlt." | NetworkManager does not manage the card (`nmcli device`) or is not installed |
| "Hotspot startet nicht …" | card without access point mode on that band, or the country does not allow it: set the country, try band "Auto" or "2,4 GHz", a fixed channel 36–48 |
| "Bluetooth-Dienst (bluetoothd) nicht erreichbar." | BlueZ not running; in the container: `/run/dbus/system_bus_socket` not mounted or not `--privileged` (AppArmor blocks D-Bus) |
| Phone stays at "Wird mit Android Auto verbunden" | the phone does not get the WiFi data: check that OpenHU's Bluetooth profile is registered (log `[WirelessController] Bluetooth ready`); `btmon` shows whether the phone opens the RFCOMM channel |
| "Telefon meldet: STATUS_WIFI_NETWORK_UNAVAILABLE" | the phone does not see the hotspot: band (phones often want 5 GHz), country, distance |
| Phone says "Verbindung kann nicht hergestellt werden" when pairing | remove the pairing on both sides and pair again; on desktops with PipeWire OpenHU uses its hands-free profile (log `phone link via hands-free (system)`) |
| Wireless on although the phone is on the cable | "Vorrang bei Kabel" is "WLAN"; with "USB" the cable wins |

Useful tools: `nmcli connection show --active`, `journalctl -u NetworkManager` (DHCP of the phone),
`tcpdump -i <wifi> port 5000` (Android Auto connection), `btmon` (Bluetooth).

## Bluetooth car radio

| Message / symptom | Cause, fix |
|---|---|
| "Verbunden, aber keine Audioausgabe gefunden" | A2DP not available in the audio server: PipeWire needs its Bluetooth support (Debian `libspa-0.2-bluetooth`, `wireplumber`), PulseAudio `pulseaudio-module-bluetooth`; `pactl list short sinks` must list `bluez_output…` |
| Radio not found by the search | radio not in pairing mode, or it only shows up with its address (no name yet): search again |
| Old radio rejects pairing | it wants a PIN: set "Kopplungs-PIN" (often `0000` or `1234`) |
| Buttons do nothing | the radio needs AVRCP; the log shows `[CarAudio] radio button: …` for every press that arrives |

When OpenHU stops or the radio disconnects, the previous default output is restored.

## Settings and plugins

- Values on the settings page that are not accepted show a hint at the bottom of the screen.
- Plugin process crashing: its output is in `logs/<id>.log`; OpenHU restarts it with a growing delay (up to
  60 s), `plugin.list` shows whether it is running.
- Plugin page or icon does not update after an update: open the page again; OpenHU clears its QML cache when a
  plugin is replaced.
