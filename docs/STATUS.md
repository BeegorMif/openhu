# OpenHU – Projektstand

Stand: 10.10.2026. Lebendes Dokument: wird bei jedem Phasenabschluss aktualisiert.

## Ziel

OpenAuto (opencardev) zu einem erweiterbaren Head-Unit-System ausbauen, angelehnt an Hudiy:
Android Auto im Hauptfenster, API für Fahrzeug-/Mediendaten, Plugins mit Store, CAN-Bus,
Lenkradtasten, Radio, Rückfahrkamera. Projektname **OpenHU** (nur nach außen umbenannt, interne
Namespaces `f1x::openauto` und Binary `autoapp` bleiben, damit Upstream-Änderungen mergebar bleiben).

## Repositories und Branches

| Repo (lokal) | Branch | Inhalt |
|---|---|---|
| `openauto/` (GitHub `average-web-dev/openhu`) | `main` (Stand), `upstream-main` (alter Fork-Stand) | OpenHU |
| `aasdk-ref/` (Upstream `opencardev/aasdk`) | `feature/oa-extensions` | aasdk + Navigation-State/Position, PhoneStatus |
| `openhu-plugins/` (GitHub `average-web-dev/openhu-plugins`) | `main` | offizielle Plugins + Store-Index |

`openhu` und `openhu-plugins` sind gepusht. Die aasdk-Änderung liegt zusätzlich als Patch in
`aasdk-patches/`; die CI (`.github/workflows/ci.yml`) wendet ihn auf aasdk-Commit `9bf6adf` an
(kein eigener aasdk-Fork nötig). Die alten opencardev-Workflows (APT-Publish, Release) sind entfernt.

## Phasen

| Phase | Inhalt | Status |
|---|---|---|
| 0 | Build-Grundlage: aasdk lokal, QML/WebSockets, Dev-Workflow im Container | erledigt |
| 1 | QML-Shell, Android Auto im Hauptfenster (AaBridge/AaSurface, automatische Ränder) | erledigt, mit Handy getestet |
| 2 | AA-Daten (nav/media/phone) + WebSocket-API, Lautstärke, Nachtmodus | erledigt, mit Handy getestet |
| 3 | Aktionen (Tastatur, Lenkrad, API) → AA-Tasten / Shell | erledigt (`N`, `B`, `M`, Widgets → Maps/Spotify getestet) |
| 4 | Plugin-System + Store (apt-artige Quellen, sha256, Install/Update/Remove) | erledigt |
| 5 | Plugins CAN, Lenkradtasten, Rückfahrkamera, Radio | erledigt; Kamera mit Webcam getestet, CAN/Radio nur Unit-Tests |
| 7 | Einstellungen neu (QML), Legacy-UI und toten Code entfernen | **als Nächstes** |
| 6 | Plugin: Bluetooth-Audio an das Autoradio (A2DP-Source, AVRCP, HFP) | offen |
| – | aasdk-README modernisieren mit KI-Hinweis | offen |
| – | arm64-Build für den Raspberry Pi | ganz am Schluss |

Plan im Detail: `~/.claude/plans/ich-h-tte-gene-folgenden-ethereal-lighthouse.md`.

## Wichtige Entscheidungen

- QML-Shell statt Widgets, alte UI bis Phase 7 per `[General] UI=legacy` erreichbar.
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
openauto/docker/dev.sh image      # Build-Image (erstes Mal / neue Abhängigkeiten)
openauto/docker/dev.sh build      # inkrementell, --aasdk nach aasdk-Änderungen
openauto/docker/run.sh            # Start im Xephyr-Fenster
openauto/docker/dev.sh reconnect  # Handy per USB-Reset neu verbinden
```

Konfiguration `docker/config/openhu.ini`, Plugin-Daten `docker/data/`, Plugin-Repo als Store-Quelle
`file /opt/openhu-plugins`.

## Bekannte Punkte / Erkenntnisse

- Status-Services müssen in `start()` `channel_->receive()` aufrufen, sonst beantwortet OpenHU die
  Kanalöffnung nicht und das Handy sendet kein Video (war die Ursache für das schwarze Bild).
- Im Image darf nur eine aasdk liegen (`dev.sh`/`Dockerfile.run` entfernen die alte), sonst bindet
  `ldconfig` die alte Bibliothek.
- QML-Engine cached Plugin-Dateien: vor dem Austausch werden Seiten freigegeben und der Cache getrimmt;
  Icons bekommen den Änderungszeitpunkt als URL-Parameter.
- Das Handy liefert über Android Auto keine Signalstärke (immer 0) und keinen Akkustand → Phase 6 (HFP).
- Kabelloses Android Auto (`btservice`) läuft im Container nicht (eigener systemd-Dienst), ungetestet.
- `vcan0`-Test des CAN-Plugins braucht `sudo` auf dem Host (siehe Plugin-README).

## Toter Code (wird in Phase 7 entfernt)

- Nie benutzt: Services Radio, VendorExtension, MediaBrowser, GenericNotification, WifiProjection;
  `OMXVideoOutput`; `tests/` (nicht gebaut, veraltet); `buildenv/`, `Dockerfile.rootfs`.
- Legacy-UI/Crankshaft: `MainWindow`, `SettingsWindow`, Dialoge (~23 000 Zeilen inkl. .ui),
  Crankshaft-Teile in `autoapp.cpp`, `Configuration` (CS-Parameter, Mp3, alte GUI-Optionen),
  `helpers/`, `packaging/opt/crankshaft`, `RecentAddressesList`, `/tmp`-Signaldateien.
