import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OpenHU 1.0
import ".."

// Settings: categories on the left, content on the right. Every change is saved immediately.
Item {
    id: page

    property string category: "general"

    readonly property var categories: [
        { id: "general", label: "Allgemein" },
        { id: "aa", label: "Android Auto" },
        { id: "audio", label: "Audio" },
        { id: "bluetooth", label: "Kabellos" },
        { id: "keys", label: "Tasten" },
        { id: "apps", label: "Apps & Store" },
        { id: "network", label: "Netzwerk & API" },
        { id: "info", label: "Info" }
    ]

    // A new Android Auto connection applies the pending changes
    Connections {
        target: eventBus
        function onEvent(topic, data) {
            if (topic === "projection.state" && data.connected)
                settings.clearPending("reconnect")
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Categories
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.dockWidth * 2.2
            color: Theme.surface

            Column {
                anchors.fill: parent
                anchors.topMargin: Theme.spacing
                spacing: 2

                Repeater {
                    model: page.categories
                    Rectangle {
                        width: parent.width
                        height: Theme.fontNormal * 2.6
                        color: page.category === modelData.id ? Theme.surfaceHigh : (mouse.pressed ? Theme.outline : "transparent")

                        Rectangle {
                            visible: page.category === modelData.id
                            width: 4
                            height: parent.height * 0.6
                            anchors.verticalCenter: parent.verticalCenter
                            radius: 2
                            color: Theme.primary
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacing
                            text: modelData.label
                            color: page.category === modelData.id ? Theme.text : Theme.textDim
                            font.pixelSize: Theme.fontNormal
                        }
                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            onClicked: page.category = modelData.id
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Changes that need a reconnect or restart
            Rectangle {
                id: pendingBar
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.fontNormal * 3
                visible: settings.pending.length > 0
                color: Theme.surfaceHigh

                readonly property bool restart: settings.pending.indexOf("restart") >= 0

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing
                    anchors.rightMargin: Theme.spacing / 2

                    Text {
                        Layout.fillWidth: true
                        text: pendingBar.restart ? "Einige Änderungen gelten erst nach einem Neustart von OpenHU."
                                                    : "Änderungen gelten ab der nächsten Verbindung mit dem Telefon."
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                    Button {
                        text: pendingBar.restart ? "Neu starten" : "Neu verbinden"
                        highlighted: true
                        onClicked: {
                            if (pendingBar.restart) {
                                shell.restart()
                            } else {
                                actions.trigger("aa.reconnect")
                                settings.clearPending("reconnect")
                            }
                        }
                    }
                }
            }

            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: {
                    switch (page.category) {
                    case "aa": return androidAuto
                    case "audio": return audio
                    case "bluetooth": return wirelessCategory
                    case "keys": return keys
                    case "apps": return apps
                    case "network": return network
                    case "info": return info
                    default: return general
                    }
                }
            }
        }
    }

    // Scrollable column for a category
    component Section: Flickable {
        id: section
        default property alias content: column.data
        contentWidth: width
        contentHeight: column.implicitHeight + Theme.spacing * 2
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        // plain Column: rows with wrapped text get their width from here (no layout loop)
        Column {
            id: column
            x: Theme.spacing * 1.5
            y: Theme.spacing
            width: section.width - Theme.spacing * 3
            spacing: Theme.spacing / 2
        }
    }

    Component {
        id: general
        Section {
            SettingChoice {
                title: "Lenkrad"
                description: "Android Auto ordnet Bedienelemente zur Fahrerseite hin an."
                key: "general.driverSide"
                options: [{ value: "left", label: "Links" }, { value: "right", label: "Rechts" }]
            }
            SettingText {
                title: "Namenszusatz"
                description: "Das Telefon zeigt „OpenHU " + (settings.values["general.nameSuffix"] || "<Zusatz>")
                             + "“. Unterschiedliche Namen sind für das Telefon verschiedene Fahrzeuge mit eigenen Einstellungen."
                key: "general.nameSuffix"
                placeholder: "z. B. Golf"
            }
            SettingSwitch {
                title: "Mauszeiger anzeigen"
                description: "Für Bedienung mit Maus statt Touchscreen."
                key: "general.showCursor"
            }
        }
    }

    Component {
        id: androidAuto
        Section {
            SectionTitle { text: "Bild" }
            SettingChoice {
                title: "Auflösung"
                description: "Android Auto kennt nur feste Auflösungen. „Auto“ wählt die kleinste, die den Anzeigebereich abdeckt (aktuell "
                             + settings.values["aa.activeResolution"] + "); die Ränder passen sie an den Bereich an."
                key: "aa.resolution"
                options: [{ value: "auto", label: "Auto" }, { value: "800x480", label: "480p" },
                          { value: "1280x720", label: "720p" }, { value: "1920x1080", label: "1080p" },
                          { value: "2560x1440", label: "1440p" }]
            }
            SettingChoice {
                title: "Bildrate"
                key: "aa.fps"
                options: [{ value: 30, label: "30 fps" }, { value: 60, label: "60 fps" }]
            }
            SettingNumber {
                title: "Pixeldichte (DPI)"
                description: "Größer = größere Schrift und Symbole in Android Auto."
                key: "aa.dpi"
                from: 80
                to: 400
                stepSize: 10
            }
            SettingRow {
                title: "Ränder automatisch"
                description: "Ränder so berechnen, dass Android Auto den Bereich unverzerrt füllt."
                Switch {
                    checked: settings.values["aa.marginWidth"] === 0 && settings.values["aa.marginHeight"] === 0
                    onToggled: {
                        settings.set("aa.marginWidth", checked ? 0 : 20)
                        settings.set("aa.marginHeight", 0)
                    }
                }
            }
            SettingNumber {
                visible: settings.values["aa.marginWidth"] !== 0 || settings.values["aa.marginHeight"] !== 0
                title: "Rand links/rechts (Pixel)"
                key: "aa.marginWidth"
                to: 600
                stepSize: 2
            }
            SettingNumber {
                visible: settings.values["aa.marginWidth"] !== 0 || settings.values["aa.marginHeight"] !== 0
                title: "Rand oben/unten (Pixel)"
                key: "aa.marginHeight"
                to: 600
                stepSize: 2
            }

            SectionTitle { text: "Bedienung und Ton" }
            SettingSwitch {
                title: "Touchscreen"
                key: "aa.touchscreen"
            }
            SettingSwitch {
                title: "Musik und Medien"
                description: "Medienton von Android Auto über OpenHU ausgeben."
                key: "aa.audioMedia"
            }
            SettingSwitch {
                title: "Navigationsansagen"
                key: "aa.audioGuidance"
            }
            SettingSwitch {
                title: "Telefonate"
                key: "aa.audioTelephony"
            }

            SectionTitle { text: "Verbindung" }
            SettingRow {
                title: "Kabellos (WLAN)"
                description: settings.values["aa.wireless"] ? "Eingeschaltet" : "Ausgeschaltet"
                Button {
                    text: "Einrichten"
                    onClicked: page.category = "bluetooth"
                }
            }
            SettingRow {
                title: "Telefon neu verbinden"
                description: "Setzt das per USB verbundene Telefon zurück, als wäre es neu eingesteckt."
                Button {
                    text: "Neu verbinden"
                    onClicked: actions.trigger("aa.reconnect")
                }
            }
        }
    }

    Component {
        id: audio
        Section {
            SettingRow {
                title: "Lautstärke"
                description: volume.muted ? "Stumm" : volume.volume + " %"
                Row {
                    spacing: Theme.spacing / 2
                    Slider {
                        width: Theme.dockWidth * 3
                        from: 0
                        to: 100
                        stepSize: 1
                        value: volume.volume
                        onMoved: volume.volume = value
                    }
                    Switch {
                        text: "Stumm"
                        checked: volume.muted
                        onToggled: volume.muted = checked
                    }
                }
            }
            SettingChoice {
                title: "Audioausgabe"
                description: "RtAudio hat meist die geringere Verzögerung, Qt Multimedia ist die Alternative bei Problemen."
                key: "audio.backend"
                options: [{ value: "rtaudio", label: "RtAudio" }, { value: "qt", label: "Qt Multimedia" }]
            }

            SectionTitle { text: "Autoradio (Bluetooth)" }
            SettingSwitch {
                title: "Ton ans Autoradio"
                description: carAudio.state.device
                             ? carAudio.state.status
                             : "Ton per Bluetooth an das Radio; dessen Tasten steuern Android Auto (bei Anrufen: annehmen/auflegen), das Display zeigt den Titel. Zuerst ein Radio suchen."
                key: "carAudio.enabled"
            }
            SettingRow {
                visible: !!carAudio.state.device
                title: carAudio.state.name || carAudio.state.device
                description: carAudio.state.device + (carAudio.state.connected ? " · verbunden" : "")
                Button {
                    text: "Entfernen"
                    onClicked: {
                        carAudio.forgetDevice()
                        settings.refresh()
                    }
                }
            }
            SettingRow {
                title: bluetooth.state.searching ? "Suche läuft …" : carAudio.state.device ? "Anderes Radio" : "Radio suchen"
                description: "Das Radio in den Kopplungsmodus bringen (Bluetooth-Menü des Radios), dann suchen."
                Button {
                    text: bluetooth.state.searching ? "Stopp" : "Suchen"
                    highlighted: !bluetooth.state.searching && !carAudio.state.device
                    onClicked: bluetooth.state.searching ? bluetooth.stopSearch() : bluetooth.startSearch(30)
                }
            }
            Repeater {
                // audio devices: found by the search, or paired already (not the current radio, not phones)
                model: {
                    const audio = d => (d.icon || "").startsWith("audio") || (d.uuids || []).indexOf("0000110b-0000-1000-8000-00805f9b34fb") >= 0
                    const current = (carAudio.state.device || "").toUpperCase()
                    return bluetooth.discovered.concat(bluetooth.devices)
                        .filter(d => audio(d) && d.address.toUpperCase() !== current)
                }
                SettingRow {
                    title: modelData.name
                    description: modelData.address + (bluetooth.state.pairingWith === modelData.address.toUpperCase() ? " · wird gekoppelt …" : "")
                    Button {
                        text: "Verwenden"
                        // selecting ends the search, which rebuilds this list (and destroys this button)
                        onClicked: carAudio.selectDevice(modelData.address)
                    }
                }
            }
            SettingText {
                title: "Kopplungs-PIN"
                description: "Nur für ältere Radios, die eine PIN statt eines Codes zur Bestätigung verlangen."
                key: "carAudio.pin"
                placeholder: "0000"
            }
        }
    }

    function wirelessStatus(state) {
        if (state.bluetooth === "error")
            return state.bluetoothError
        if (state.hotspot === "error")
            return state.hotspotError
        if (state.hotspot === "starting")
            return "WLAN-Hotspot startet …"
        if (state.phaseText)
            return state.phaseText + (state.device ? " (" + state.device + ")" : "")
        if (state.hotspot === "off")
            return "Bereit · WLAN-Hotspot startet, sobald sich das Telefon meldet"
        return "Bereit · WLAN „" + state.ssid + "“ auf " + state.interface
    }

    Component {
        id: wirelessCategory
        Section {
            SettingSwitch {
                title: "Kabelloses Android Auto"
                description: "Das Telefon wird einmal per Bluetooth gekoppelt und verbindet sich danach automatisch über den WLAN-Hotspot von OpenHU."
                key: "aa.wireless"
            }
            SettingRow {
                visible: wireless.state.running
                title: "Status"
                description: page.wirelessStatus(wireless.state)
                Button {
                    visible: !!wireless.state.wifiOff
                    text: "WLAN einschalten"
                    onClicked: wireless.enableWifi()
                }
            }

            SectionTitle {
                visible: settings.values["aa.wireless"] && wireless.state.bluetooth !== "error"
                text: "Telefone"
            }
            SettingRow {
                visible: settings.values["aa.wireless"] && wireless.state.bluetooth !== "error"
                title: bluetooth.state.pairing ? "Sichtbar als „" + bluetooth.state.adapter + "“" : "Neues Telefon koppeln"
                description: bluetooth.state.pairing
                             ? "Auf dem Telefon unter Bluetooth „" + bluetooth.state.adapter + "“ auswählen und den Code hier bestätigen."
                             : "OpenHU ist dann 2 Minuten lang für Telefone sichtbar."
                Button {
                    text: bluetooth.state.pairing ? "Abbrechen" : "Koppeln"
                    highlighted: !bluetooth.state.pairing
                    onClicked: {
                        if (bluetooth.state.pairing) {
                            bluetooth.setPairing(false)
                        } else {
                            wireless.pair()
                            settings.refresh()
                        }
                    }
                }
            }
            Repeater {
                model: wireless.state.running ? bluetooth.devices.filter(d => d.icon === "phone") : []
                SettingRow {
                    title: modelData.name
                    description: modelData.address + (modelData.connected ? " · verbunden" : "")
                    Row {
                        spacing: Theme.spacing / 3
                        Button {
                            text: "Verbinden"
                            onClicked: wireless.connectDevice(modelData.address)
                        }
                        Button {
                            text: "Entfernen"
                            onClicked: bluetooth.removeDevice(modelData.address)
                        }
                    }
                }
            }

            SettingChoice {
                title: "Vorrang bei Kabel"
                description: settings.values["wireless.priority"] === "wifi"
                             ? "Das Telefon darf auch mit Kabel kabellos verbinden (das Kabel lädt dann nur)."
                             : "Mit Kabel läuft Android Auto über USB (stabiler, lädt). Kabellos übernimmt nach dem Abziehen."
                key: "wireless.priority"
                options: [{ value: "usb", label: "USB" }, { value: "wifi", label: "WLAN" }]
            }

            SectionTitle { text: "WLAN-Hotspot" }
            SettingChoice {
                title: "Hotspot"
                description: settings.values["wireless.hotspot"] === "demand"
                             ? "Startet erst, wenn sich ein gekoppeltes Telefon meldet (ein paar Sekunden länger), und geht nach 2 Minuten ohne kabellose Sitzung aus."
                             : "Läuft dauerhaft, solange kabelloses Android Auto eingeschaltet ist (schnellste Verbindung)."
                key: "wireless.hotspot"
                options: [{ value: "always", label: "Immer an" }, { value: "demand", label: "Bei Bedarf" }]
            }
            SettingText {
                title: "Name (SSID)"
                description: "Leer lassen für „" + wireless.state.ssid + "“."
                key: "wireless.ssid"
                placeholder: wireless.state.ssid
            }
            SettingText {
                title: "Passwort"
                description: "Muss nirgends eingetippt werden: Das Telefon bekommt es über Bluetooth."
                key: "wireless.password"
            }
            SettingChoice {
                title: "Frequenzband"
                description: "Android Auto läuft am besten mit 5 GHz. „Auto“ nimmt 5 GHz und weicht auf 2,4 GHz aus, wenn 5 GHz nicht startet. Beide gleichzeitig kann eine WLAN-Karte nicht."
                             + (wireless.state.channel > 0 ? " Aktuell Kanal " + wireless.state.channel + "." : "")
                key: "wireless.band"
                options: [{ value: "auto", label: "Auto" }, { value: "5", label: "5 GHz" }, { value: "2.4", label: "2,4 GHz" }]
            }
            SettingText {
                title: "Land"
                description: "Legt die erlaubten WLAN-Kanäle fest (für 5 GHz nötig)."
                key: "wireless.country"
                placeholder: "DE"
            }
            SettingRow {
                id: advancedRow
                property bool open: false
                title: "Erweiterte Einstellungen"
                description: "Kanal, Adresse, DHCP-Bereich"
                Button {
                    text: advancedRow.open ? "Ausblenden" : "Anzeigen"
                    onClicked: advancedRow.open = !advancedRow.open
                }
            }
            SettingText {
                visible: advancedRow.open
                title: "Kanal"
                description: settings.values["wireless.band"] === "auto"
                             ? "Mit „Auto“ wählt OpenHU selbst (36 bzw. 6). Für einen festen Kanal ein festes Band wählen."
                             : "Leer = Standard (" + (settings.values["wireless.band"] === "2.4" ? "6" : "36") + "). 5 GHz: 36–48 ohne Radarerkennung."
                key: "wireless.channel"
                placeholder: "automatisch"
            }
            SettingText {
                visible: advancedRow.open
                title: "Adresse (Gateway)"
                description: "OpenHU im Hotspot-Netz, mit Netzmaske. Das Telefon nutzt sie als Gateway und DNS."
                key: "wireless.address"
                placeholder: "10.42.0.1/24"
            }
            SettingText {
                visible: advancedRow.open
                title: "DHCP-Bereich"
                description: "Leer = automatisch."
                key: "wireless.dhcpRange"
                placeholder: "10.42.0.10-10.42.0.254"
            }


            SectionTitle { text: "Telefonate über Bluetooth" }
            SettingChoice {
                title: "Bluetooth-Adapter"
                description: "Telefon über das USB-Kabel per Bluetooth koppeln (bei kabellosem Android Auto immer aktiv)."
                key: "bluetooth.adapter"
                options: [{ value: "none", label: "Keiner" }, { value: "local", label: "Lokal" }]
            }
            SettingText {
                visible: settings.values["bluetooth.adapter"] === "local"
                title: "Adapter-Adresse"
                description: "MAC-Adresse, z. B. aus „bluetoothctl list“. Leer: erster Adapter."
                key: "bluetooth.address"
                placeholder: "00:11:22:33:44:55"
            }
        }
    }

    Component {
        id: keys
        KeymapSettings {}
    }

    Component {
        id: apps
        StorePage {}
    }

    Component {
        id: network
        Section {
            SettingNumber {
                title: "API-Port"
                description: "WebSocket-API für Plugins und eigene Programme."
                key: "api.port"
                from: 1024
                to: 65535
            }
            SettingSwitch {
                title: "API im Netzwerk freigeben"
                description: "Sonst nur auf diesem Gerät erreichbar. Die API hat keine Anmeldung – nur in vertrauenswürdigen Netzen einschalten."
                key: "api.network"
            }
            SettingSwitch {
                visible: !!settings.values["api.network"]
                title: "API auch im WLAN-Hotspot"
                description: "Der Hotspot ist für das Telefon gedacht; Geräte für die API besser per Ethernet anschließen."
                key: "api.hotspot"
            }
            SettingRow {
                title: "Adresse"
                description: settings.info.apiUrl
            }
        }
    }

    Component {
        id: info
        Section {
            SettingRow {
                title: "OpenHU"
                description: "Version " + settings.info.version + (settings.info.git ? " (" + settings.info.git + ")" : "") + " · Qt " + settings.info.qt
            }
            SettingRow {
                title: "Konfiguration"
                description: settings.info.configFile
            }
            SettingRow {
                title: "Fahrzeug-ID"
                description: settings.info.vehicleId + " (aus dem Namenszusatz)"
            }
            SettingRow {
                title: "Daten, Plugins und Logs"
                description: settings.info.dataDir
            }
            SettingRow {
                title: "OpenHU neu starten"
                Button {
                    text: "Neu starten"
                    onClicked: shell.restart()
                }
            }
            SettingRow {
                title: "OpenHU beenden"
                Button {
                    text: "Beenden"
                    onClicked: shell.quit()
                }
            }
            Text {
                width: parent ? parent.width : 0
                topPadding: Theme.spacing
                wrapMode: Text.WordWrap
                text: "Basiert auf OpenAuto (f1x.studio, OpenCarDev) und aasdk. Lizenz: GPL-3.0."
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
