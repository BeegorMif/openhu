# OpenHU – Projektstand

Stand: 11.10.2026. Lebendes Dokument: wird bei jedem Phasenabschluss aktualisiert.
Benutzer-Doku: `README.md` und `docs/` (Installation, Konfiguration, API, Plugins, Fehlersuche).

## Ziel

OpenAuto (opencardev) zu einem erweiterbaren Head-Unit-System ausbauen, angelehnt an Hudiy:
Android Auto im Hauptfenster, API für Fahrzeug-/Mediendaten, Plugins mit Store, CAN-Bus,
Lenkradtasten, Radio, Rückfahrkamera. Projektname **OpenHU** (nur nach außen umbenannt, interne
Namespaces `f1x::openauto` und Binary `autoapp` bleiben, damit Upstream-Änderungen mergebar bleiben).

## Repositories und Branches

| Repo (lokal) | Branch | Inhalt |
|---|---|---|
| `openauto/` (GitHub `average-web-dev/openhu`) | `main` (Stand), `upstream-main` (alter Fork-Stand) | OpenHU |
| `aasdk-ref/` (GitHub-Fork `average-web-dev/aasdk`, Upstream `opencardev/aasdk` als Remote `upstream`) | `openhu` | aasdk + Navigation-State/Position, PhoneStatus |
| `openhu-plugins/` (GitHub `average-web-dev/openhu-plugins`) | `main` | offizielle Plugins + Store-Index |

Alle drei Repos sind gepusht (OpenHU bis Phase 6 + Doku, aasdk mit USB-Filter). aasdk hat eine eigene
SemVer-Version (`project(aasdk VERSION ...)`), der Workflow `openhu.yml` im aasdk-Fork baut `libaasdk`/`libaasdk-dev`
für amd64 und arm64 und veröffentlicht sie beim Tag `openhu-v<version>` als GitHub-Release. Die OpenHU-CI
(`.github/workflows/ci.yml`) holt das neueste Release, das zu `OPENHU_AASDK_VERSION` passt
(`scripts/fetch-aasdk.sh`), baut OpenHU für amd64 und arm64 (nativer arm-Runner), installiert die Pakete testweise in einem
frischen `debian:trixie-slim` und lädt OpenHU- und aasdk-`.deb` als Artefakte `openhu-amd64`/`openhu-arm64` hoch.
Sicherheitsnetz (vor allem für die Qt-6-Migration): Unit-Tests mit QtTest in `tests/` (EventBus, Konfiguration,
Aktionen/Keymap, API inkl. WebSocket, Plugin-Store mit lokalem Repo), `qmllint` über die Shell-QML und
`tests/shell-smoke.sh`, das die installierte Shell headless startet, alle Seiten per API zeigt und bei QML-Warnungen
fehlschlägt. Lokal: `docker/dev.sh test`. Der Code außer `main()` liegt dafür in der Bibliothek `openhu_core`.

Qt 6.8 (Branch `qt6`, Issue #5): Video über einen eigenen FFmpeg-Decoder (`Projection/FfmpegVideoOutput`, eine
AA-Nachricht = ein `AVPacket`, neuester Frame an den `QVideoSink` des `VideoOutput` in `AaPage.qml`, Decoder über
`Video.Decoder`), GStreamer entfällt. Audio über `QAudioSink`/`QAudioSource`. Plugin-API 2 (`api` im Manifest),
Plugins anderer API werden weder installiert noch geladen. Debians Qt 6 hat kein eglfs: der Dienst ohne Desktop
kommt mit #9 (Kiosk-Compositor `cage`).

## Phasen

| Phase | Inhalt | Status |
|---|---|---|
| 0 | Build-Grundlage: aasdk lokal, QML/WebSockets, Dev-Workflow im Container | erledigt |
| 1 | QML-Shell, Android Auto im Hauptfenster (AaBridge/AaSurface, automatische Ränder) | erledigt, mit Handy getestet |
| 2 | AA-Daten (nav/media/phone) + WebSocket-API, Lautstärke, Nachtmodus | erledigt, mit Handy getestet |
| 3 | Aktionen (Tastatur, Lenkrad, API) → AA-Tasten / Shell | erledigt (`N`, `B`, `M`, Widgets → Maps/Spotify getestet) |
| 4 | Plugin-System + Store (apt-artige Quellen, sha256, Install/Update/Remove) | erledigt |
| 5 | Plugins CAN, Lenkradtasten, Rückfahrkamera, Radio | erledigt; Kamera mit Webcam getestet, CAN/Radio nur Unit-Tests |
| 7 | Einstellungen neu (QML), Legacy-UI und toten Code entfernen | erledigt (siehe unten) |
| 6a | Kabelloses Android Auto (BlueZ + NetworkManager-Hotspot, eingebaut) | erledigt, mit Fairphone 4 getestet |
| 6 | Bluetooth-Audio an das Autoradio (A2DP, AVRCP-Tasten/Titel, Anrufe per Taste), eingebaut | erledigt, mit Galaxy Buds+ getestet |
| – | aasdk-README modernisieren mit KI-Hinweis | erledigt (aasdk `c7b91ed`) |
| – | Doku: README neu (KI-Hinweis, Screenshots), Installation, Konfiguration, Fehlersuche, Plugin-Beispiel | erledigt |
| – | Bluetooth-Panel beim Tippen auf das BT-Symbol (Radio, Trennen, Einstellungen) | später |
| – | arm64-Build für den Raspberry Pi, aasdk als Paket, Test auf echtem Pi (eglfs) | ganz am Schluss |

Plan im Detail: `~/.claude/plans/ich-h-tte-gene-folgenden-ethereal-lighthouse.md`.

## Wichtige Entscheidungen

- QML-Shell statt Widgets; die alte Widgets-UI ist seit Phase 7 entfernt.
- Plugins hybrid: manifest + QML-Seite/Overlay + Prozess in beliebiger Sprache über die API.
- API: WebSocket + JSON, Port 5520, nur localhost (Doku `docs/api.md`, Plugins `docs/plugins.md`).
- Store-Pakete sind fertige Archive `dist/<id>-<version>.tar.gz` (statt Ordner aus dem Repo-Tarball),
  reproduzierbar gebaut von `tools/openhu-repo-index`.
- Lautstärke über `pactl` (PulseAudio und PipeWire) statt libpulse.
- Navigationskanal mit Typ ENUM (Pfeile zeichnet die Shell).
- Rückfahrkamera: Standard im Inhaltsbereich und im Dock, Vollbild optional.
- Aktionen (`input.action`) und Plugin-Befehle sind einmalige Events (`bus.emit`), Zustände
  werden mit letztem Wert gespeichert (`bus.publish`).

## Entwicklung

```
openauto/docker/dev.sh run        # inkrementell bauen (aasdk + OpenHU) und im Xephyr-Fenster starten
openauto/docker/dev.sh package    # .deb nach docker/output/
openauto/docker/dev.sh reconnect  # Handy per USB-Reset neu verbinden
openauto/docker/dev.sh clean      # Build-Volumes und ccache löschen
```

Konfiguration `docker/config/openhu.ini` (Fallback `openauto.ini`), Plugin-Daten `docker/data/`, Plugin-Repo als Store-Quelle
`file /opt/openhu-plugins`.

## Bekannte Punkte / Erkenntnisse

- Ohne `gstreamer1.0-x` (X-Video-Sinks, sonst nur Recommends von `gstreamer1.0-plugins-good`) dauert unter xcb
  jedes `QMediaPlayer` ~10 s (Wayland-Probe mit Timeout), der Verbindungsaufbau entsprechend länger.
- aasdk-USBHub verwirft Hotplug-Ereignisse, solange kein Promise wartet. Nach Sitzungsende meldet sich das Handy
  oft genau dann zurück (USB-Reset, AA am Handy beendet) oder bleibt im AOA-Modus ohne neues Ereignis; deshalb
  setzt `App::onAndroidAutoQuit()` den Hub zurück, die neue Registrierung meldet alle angesteckten Geräte.
- Status-Services müssen in `start()` `channel_->receive()` aufrufen, sonst beantwortet OpenHU die
  Kanalöffnung nicht und das Handy sendet kein Video (war die Ursache für das schwarze Bild).
- Im Container liegt aasdk nur im Volume unter `/opt/aasdk` (`CPATH`/`LD_LIBRARY_PATH` im Image), nicht in
  `/usr/local`, sonst bindet `ldconfig` womöglich eine alte Bibliothek.
- QML-Engine cached Plugin-Dateien: vor dem Austausch werden Seiten freigegeben und der Cache getrimmt;
  Icons bekommen den Änderungszeitpunkt als URL-Parameter.
- Das Handy liefert über Android Auto keine Signalstärke (immer 0) und keinen Akkustand.
- systemd-Dienst: `openhu.ini` wird im Arbeitsverzeichnis gelesen → `WorkingDirectory=/var/lib/openhu`
  (vorher landete die Datei unter `/`).
- `vcan0`-Test des CAN-Plugins braucht `sudo` auf dem Host (siehe Plugin-README).

## Phase 6 – Ergebnis (Autoradio)

- Fest eingebaut statt Plugin: `bt::BluetoothManager` (Adapter, Agent, Kopplung inkl. PIN für alte Radios,
  Suche, Geräte) wird von kabellosem AA und dem Radio gemeinsam genutzt; `bt::CarAudio` verbindet das Radio
  (auch erneut alle 20 s, Radio geht mit der Zündung an), setzt es per `pactl` als Standardausgabe und stellt
  beim Trennen/Beenden die vorherige wieder her. Sink-Namen: PipeWire neu `bluez_output.AA:BB:…`, alt mit `_`.
- AVRCP: eigener MPRIS-Player bei BlueZ (`Media1.RegisterPlayer`), Tasten → Aktionen; klingelt ein Anruf, nimmt
  Play/Pause an, im Gespräch legt es auf; Display zeigt Titel/Interpret bzw. den Anrufer.
- Lautstärke am Radio: AVRCP Absolute Volume über PipeWire → Standardausgabe → `audio.volume`.
- Anrufton geht per A2DP ans Radio, das Mikrofon ist am OpenHU-Gerät (Entscheidung: kein HFP-Gateway).
- Statusleiste: Bluetooth-Symbol, solange das Autoradio verbunden ist (das Handy hat sein eigenes Symbol).
- Getestet: Kopplung/Verbindung/Ausgabe, Wiederverbinden nach Neustart, Tasten und Ton (Buds+). Anruftasten und
  Titelanzeige auf einem Gerät mit Display noch ungetestet; kabelloses AA nach dem Umbau getestet.

## Kabelloses Android Auto – Ergebnis

- Eingebaut in OpenHU (`src/autoapp/Wireless/`), das separate `btservice` samt systemd-Dienst und Qt Bluetooth
  ist entfernt. Läuft auf Bare Metal und im Container gleich: BlueZ und NetworkManager über den System-D-Bus
  (Container: `/run/dbus/system_bus_socket` eingehängt, `--privileged` wegen AppArmor).
- Ablauf: BlueZ-Profil für den AA-RFCOMM-Kanal (UUID `4de17a00…`, **Kanal 8 fest**, sonst fehlt der
  RFCOMM-Eintrag im SDP und das Handy hängt bei „Wird mit Android Auto verbunden“) → Handy bekommt
  `WifiStartRequest` (10.42.0.1:5000) und auf `WifiInfoRequest` SSID/Passwort/BSSID (Sicherheitsmodus 8) →
  Handy tritt dem Hotspot `openhu-hotspot` bei (NM, IPv4 shared, 5 GHz Kanal 36, Land per `iw reg set`) →
  TCP auf Port 5000.
- Handy-Verbindung: Bietet das System schon Freisprechen (PipeWire-HFP), verbindet OpenHU das Handy normal
  (`Device1.Connect`); ein zusätzliches HSP-Profil daneben lässt das Handy die Verbindung ablehnen. Ohne HFP
  (Pi ohne PipeWire) eigenes HSP-Profil wie bei den WLAN-Dongles.
- Kopplung: Knopf „Koppeln“ (2 min sichtbar, Code-Bestätigung im Dialog, eigener BlueZ-Agent nur in dieser
  Zeit) oder über das USB-Kabel (AA-Bluetooth-Kanal meldet die Adresse, OpenHU akzeptiert genau dieses Handy
  und schickt den Code über AA zurück). Das Handy wartet darauf, dass OpenHU die Kopplung startet (`Pair()`);
  getestet: gekoppelt in < 2 s ohne Rückfrage.
- Einstellung „Vorrang bei Kabel“ (`Wireless.Priority`): USB (Standard) – mit Kabel keine WLAN-Daten, TCP
  abgewiesen, Einstecken beendet die WLAN-Sitzung; WLAN – Handy darf auch mit Kabel kabellos wechseln.
- Hotspot „Immer an“ / „Bei Bedarf“: bei Bedarf startet er, wenn ein gekoppeltes Handy den AA-Kanal öffnet
  (das Handy wartet, WLAN-Daten erst danach; gemessen 0,3 s), und geht nach 2 min ohne kabellose Sitzung aus
  (Abschalten getestet).
- Band Auto/5/2,4 GHz (Auto: 5 GHz, bei Fehler 2,4 GHz; eine Karte kann nicht beide gleichzeitig),
  erweitert: Kanal, Adresse/Gateway, DHCP-Bereich (NM `ipv4.shared-dhcp-range`, ab NM 1.42). Eigene DNS-Server
  gehen nicht: NM erlaubt `ipv4.dns` bei `method=shared` nicht.
- Startprobleme mit Handy am Kabel behoben: (1) ein Handy, das vom vorigen Lauf noch im Accessory-Modus hängt,
  wird beim Start per USB-Reset frisch verbunden (sonst SSL-Fehler „unexpected message“); (2) aasdk fragte
  jedes USB-Gerät per AOAP an und schlief 1 s pro Gerät („VMware-Workaround“) → 22 s bis zum Handy; jetzt
  Filter (Hubs/HID/Audio/Video übersprungen), Pause entfernt (aasdk-Fork) → Handy nach 1,7 s.
- Abstürze behoben: `SensorService` las GPS ohne gpsd-Verbindung (uninitialisiertes `gps_data_t`);
  beim Beenden `_Exit` nach dem Aufräumen statt `exit` (Qt-Abbau-Absturz in der QML-Engine).
- API aus dem Hotspot-Netz standardmäßig abgewiesen, Schalter „API auch im WLAN-Hotspot“ (ungetestet).
- Versteckte SSID getestet und verworfen: NM richtet den Hotspot versteckt ein, das Handy meldet aber
  `STATUS_WIFI_NETWORK_UNAVAILABLE` – Android Auto sucht nicht aktiv nach versteckten Netzen.
- Wichtiger Fix in `App::waitForDevice`: Der USB-Hub-Start lief inline auf dem App-Strand und blockierte ihn
  beim Programmstart 10–20 s (Hotplug-Enumeration aller USB-Geräte); das Handy gab nach 10 s ohne Daten auf.
  Jetzt eigener Worker-Aufruf → kabellos verbunden ca. 5 s nach dem Start.
- Endet eine kabellose Sitzung oder kommt nach dem Handshake 30 s keine TCP-Verbindung, trennt OpenHU
  Bluetooth und verbindet neu (das Handy versucht es sonst nicht noch einmal). Ausnahme: AA am Handy beendet
  (`ByeByeRequest` mit `USER_SELECTION`) – dann erst wieder, wenn das Handy selbst startet oder über
  „Verbinden“ (trennt Bluetooth kurz, sonst startet das Handy AA nicht neu).
- Hotspot wird bei Beenden, SIGTERM (`docker stop`, Entrypoint reicht es weiter) und Fenster-Schließen
  (`atexit`) heruntergefahren.

## Phase 7 – Ergebnis

- Legacy-UI entfernt (`MainWindow`, `SettingsWindow`, Dialoge), ebenso toter Code: Services Radio/
  VendorExtension/MediaBrowser/GenericNotification/WifiProjection, `OMXVideoOutput`, `RecentAddressesList`,
  `tests/`, `buildenv/`, `Dockerfile.rootfs`, `helpers/`, `packaging/opt/crankshaft`, Fix-Notizen,
  ungenutzte Icons (~32 500 Zeilen).
- `Configuration` neu und schlank (gleiche ini-Schlüssel, Datei `openhu.ini`), `QGuiApplication` statt
  `QApplication`, CMake ohne OMX/taglib/blkid/Widgets/Crankshaft-Installation, udev-Regel ohne Crankshaft.
- Einstellungsseite in QML (`Shell/qml/settings/`) mit Kategorien Allgemein, Android Auto, Audio, Bluetooth,
  Tasten, Apps & Store, Netzwerk & API, Info. Jede Änderung wird sofort gespeichert; eine Leiste zeigt an,
  wenn eine Neuverbindung (USB-Reset per Knopf) oder ein Neustart nötig ist.
- Apps-Menü ist jetzt die Kategorie „Apps & Store“ (Öffnen, Im Dock, Aktiv, Installieren/Updates/Quellen).
- Neustart im selben Prozess (`execv`, gleiche PID, funktioniert im Container und unter systemd). Vorher
  werden Plugin-Prozesse und `pactl subscribe` beendet; der Wireless-Acceptor (Port 5000) ist
  close-on-exec, sonst erben Kindprozesse den Socket und der neue Prozess bekommt „Address already in use“.
- Tastenbelegung bearbeitbar (aufnehmen, Aktion zuweisen, entfernen, zurücksetzen; nur Abweichungen landen in
  `~/.openhu/keymap.json`). In fokussierten Textfeldern gehen alle Tasten außer Medien/Lautstärke ans Feld.
- Android Auto:
  - Fahrerseite wird gemeldet (war fest „rechts“).
  - **Namenszusatz** (`[General] NameSuffix`): Das Handy zeigt „OpenHU <Zusatz>“. Die Fahrzeug-ID ist ein
    Hash des Zusatzes, damit mehrere Head-Units am Handy getrennt konfiguriert werden; derselbe Name bleibt
    dasselbe Auto (ohne Zusatz die bisherige ID).
  - **Automatische Auflösung** (`[Video] AutoResolution`, Standard an): kleinste AA-Stufe, die die AA-Fläche
    abdeckt (480p/720p/1080p, im Hochformat 720x1280/1080x1920), dazu automatische Ränder. AA kennt nur
    feste Stufen; die Wahl greift bei der nächsten Verbindung.
  - „Beenden“ in Android Auto → Startseite (`projection.exit`), Nachtmodus nur noch über API.
- Getestet im Container: alle Kategorien ohne QML-Warnungen, Werte speichern/laden, Tasten anlernen,
  automatische Auflösung bei 800x480/1280x720/1920x1080/720x1280, Hochformat-Layout, Neustart mit
  laufendem Plugin. Noch mit Handy zu testen: geänderte Fahrerseite/Namenszusatz/Auflösung.
