import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OpenHU 1.0
import ".."

// Settings: categories on the left, content on the right. Every change is saved immediately.
Item {
    id: page

    property string category: "general"
    // the hardware: settings for WiFi and Bluetooth only when there is an adapter (also plugged in later)
    readonly property bool hasWifi: wireless.state.wifiAvailable !== false
    readonly property bool hasBluetooth: !!bluetooth.state.available

    readonly property var categories: [
        { id: "general", label: qsTr("General") },
        { id: "aa", label: "Android Auto" },
        { id: "audio", label: qsTr("Audio") },
    ].concat(hasWifi || hasBluetooth ? [{ id: "bluetooth", label: qsTr("Wireless") }] : [], [
        { id: "keys", label: qsTr("Keys") },
        { id: "apps", label: qsTr("Apps & Store") },
        { id: "network", label: qsTr("Network & API") }
    ]).concat(system.available ? [{ id: "system", label: qsTr("System") }] : [], [{ id: "info", label: qsTr("Info") }])

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
                        text: pendingBar.restart ? qsTr("Some changes only apply after restarting OpenHU.")
                                                    : qsTr("Changes apply from the next connection with the phone.")
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                    Button {
                        text: pendingBar.restart ? qsTr("Restart") : qsTr("Reconnect")
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
                    case "system": return systemCategory
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
            SettingDropdown {
                // Translations::languages(): every language with a translation, named in itself
                title: qsTr("Language")
                description: qsTr("Language of OpenHU and the on-screen keyboard.")
                key: "general.language"
                options: settings.info.languages
            }
            SettingChoice {
                title: qsTr("Steering wheel")
                description: qsTr("Android Auto arranges its controls towards the driver's side.")
                key: "general.driverSide"
                options: [{ value: "left", label: qsTr("Left") }, { value: "right", label: qsTr("Right") }]
            }
            SettingText {
                title: qsTr("Name suffix")
                description: qsTr("The phone shows “OpenHU %1”. Different names are different vehicles with their own settings for the phone.")
                             .arg(settings.values["general.nameSuffix"] || qsTr("<suffix>"))
                key: "general.nameSuffix"
                placeholder: qsTr("e.g. Golf")
            }
            SettingSwitch {
                title: qsTr("Show mouse cursor")
                description: qsTr("For using a mouse instead of the touchscreen.")
                key: "general.showCursor"
            }
            SettingSwitch {
                visible: onScreenKeyboardAvailable
                title: qsTr("On-screen keyboard")
                description: qsTr("Appears for text fields. Switch it off when a keyboard is connected.")
                key: "general.onScreenKeyboard"
            }
        }
    }

    Component {
        id: androidAuto
        Section {
            SectionTitle { text: qsTr("Picture") }
            SettingChoice {
                title: qsTr("Resolution")
                description: qsTr("Android Auto only knows fixed resolutions. “Auto” picks the smallest one that covers the display area (currently %1); the margins fit it to the area.")
                             .arg(settings.values["aa.activeResolution"])
                key: "aa.resolution"
                options: [{ value: "auto", label: qsTr("Auto") }, { value: "800x480", label: "480p" },
                          { value: "1280x720", label: "720p" }, { value: "1920x1080", label: "1080p" },
                          { value: "2560x1440", label: "1440p" }]
            }
            SettingChoice {
                title: qsTr("Frame rate")
                key: "aa.fps"
                options: [{ value: 30, label: "30 fps" }, { value: 60, label: "60 fps" }]
            }
            SettingNumber {
                title: qsTr("Pixel density (DPI)")
                description: qsTr("Larger = bigger text and icons in Android Auto.")
                key: "aa.dpi"
                from: 80
                to: 400
                stepSize: 10
            }
            SettingRow {
                title: qsTr("Automatic margins")
                description: qsTr("Compute the margins so Android Auto fills the area undistorted.")
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
                title: qsTr("Margin left/right (pixels)")
                key: "aa.marginWidth"
                to: 600
                stepSize: 2
            }
            SettingNumber {
                visible: settings.values["aa.marginWidth"] !== 0 || settings.values["aa.marginHeight"] !== 0
                title: qsTr("Margin top/bottom (pixels)")
                key: "aa.marginHeight"
                to: 600
                stepSize: 2
            }

            SectionTitle { text: qsTr("Controls and sound") }
            SettingSwitch {
                title: qsTr("Touchscreen")
                key: "aa.touchscreen"
            }
            SettingSwitch {
                title: qsTr("Music and media")
                description: qsTr("Play the media sound of Android Auto through OpenHU.")
                key: "aa.audioMedia"
            }
            SettingSwitch {
                title: qsTr("Navigation announcements")
                key: "aa.audioGuidance"
            }
            SettingSwitch {
                title: qsTr("Phone calls")
                key: "aa.audioTelephony"
            }

            SectionTitle { text: qsTr("Connection") }
            SettingRow {
                visible: page.hasWifi && page.hasBluetooth
                title: qsTr("Wireless (WiFi)")
                description: settings.values["aa.wireless"] ? qsTr("On") : qsTr("Off")
                Button {
                    text: qsTr("Set up")
                    onClicked: page.category = "bluetooth"
                }
            }
            SettingRow {
                title: qsTr("Reconnect phone")
                description: qsTr("Resets the phone connected by USB, as if it was plugged in again.")
                Button {
                    text: qsTr("Reconnect")
                    onClicked: actions.trigger("aa.reconnect")
                }
            }
        }
    }

    Component {
        id: audio
        Section {
            SettingRow {
                title: qsTr("Volume")
                description: volume.muted ? qsTr("Muted") : volume.volume + " %"
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
                        text: qsTr("Mute")
                        checked: volume.muted
                        onToggled: volume.muted = checked
                    }
                }
            }
            SettingChoice {
                title: qsTr("Audio output")
                description: qsTr("RtAudio mostly has the lower latency, Qt Multimedia is the alternative when there are problems.")
                key: "audio.backend"
                options: [{ value: "rtaudio", label: "RtAudio" }, { value: "qt", label: "Qt Multimedia" }]
            }

            Column {
                width: parent.width
                spacing: Theme.spacing / 2
                visible: page.hasBluetooth
                SectionTitle { text: qsTr("Car radio (Bluetooth)") }
                SettingSwitch {
                    title: qsTr("Sound to the car radio")
                    description: carAudio.state.device
                                 ? carAudio.state.status
                                 : qsTr("Sound via Bluetooth to the radio; its buttons control Android Auto (during calls: answer/hang up), its display shows the title. Search for a radio first.")
                    key: "carAudio.enabled"
                }
                SettingRow {
                    visible: !!carAudio.state.device
                    title: carAudio.state.name || carAudio.state.device
                    description: carAudio.state.device + (carAudio.state.connected ? " · " + qsTr("connected") : "")
                    Button {
                        text: qsTr("Remove")
                        onClicked: {
                            carAudio.forgetDevice()
                            settings.refresh()
                        }
                    }
                }
                SettingRow {
                    title: bluetooth.state.searching ? qsTr("Searching …") : carAudio.state.device ? qsTr("Other radio") : qsTr("Search for a radio")
                    description: qsTr("Put the radio into pairing mode (Bluetooth menu of the radio), then search.")
                    Button {
                        text: bluetooth.state.searching ? qsTr("Stop") : qsTr("Search")
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
                        description: modelData.address + (bluetooth.state.pairingWith === modelData.address.toUpperCase() ? " · " + qsTr("pairing …") : "")
                        Button {
                            text: qsTr("Use")
                            // selecting ends the search, which rebuilds this list (and destroys this button)
                            onClicked: carAudio.selectDevice(modelData.address)
                        }
                    }
                }
                SettingText {
                    title: qsTr("Pairing PIN")
                    description: qsTr("Only for older radios that ask for a PIN instead of confirming a code.")
                    key: "carAudio.pin"
                    inputHints: Qt.ImhDigitsOnly
                    placeholder: "0000"
                }
            }
        }
    }

    function wirelessStatus(state) {
        if (state.bluetooth === "error")
            return state.bluetoothError
        if (state.hotspot === "error")
            return state.hotspotError
        if (state.hotspot === "starting")
            return qsTr("WiFi hotspot is starting …")
        if (state.phaseText)
            return state.phaseText + (state.device ? " (" + state.device + ")" : "")
        if (state.hotspot === "off")
            return qsTr("Ready · the WiFi hotspot starts as soon as the phone shows up")
        return qsTr("Ready · WiFi “%1” on %2").arg(state.ssid).arg(state.interface)
    }

    Component {
        id: wirelessCategory
        Section {
            // instead of settings that cannot work: which adapter is missing
            SettingRow {
                visible: !page.hasWifi || !page.hasBluetooth
                title: qsTr("Wireless Android Auto")
                description: (!page.hasWifi ? qsTr("No WiFi adapter found.") + " " : "")
                             + (!page.hasBluetooth ? qsTr("No Bluetooth adapter found.") + " " : "")
                             + qsTr("Wireless Android Auto needs both.")
            }
            Column {
                width: parent.width
                spacing: Theme.spacing / 2
                visible: page.hasWifi && page.hasBluetooth
                SettingSwitch {
                    title: qsTr("Wireless Android Auto")
                    description: qsTr("The phone is paired once via Bluetooth and then connects automatically through the WiFi hotspot of OpenHU.")
                    key: "aa.wireless"
                }
                SettingRow {
                    visible: wireless.state.running
                    title: qsTr("Status")
                    description: page.wirelessStatus(wireless.state)
                    Button {
                        visible: !!wireless.state.wifiOff
                        text: qsTr("Switch WiFi on")
                        onClicked: wireless.enableWifi()
                    }
                }

                SectionTitle {
                    visible: settings.values["aa.wireless"] && wireless.state.bluetooth !== "error"
                    text: qsTr("Phones")
                }
                SettingRow {
                    visible: settings.values["aa.wireless"] && wireless.state.bluetooth !== "error"
                    title: bluetooth.state.pairing ? qsTr("Visible as “%1”").arg(bluetooth.state.adapter) : qsTr("Pair a new phone")
                    description: bluetooth.state.pairing
                                 ? qsTr("On the phone, choose “%1” under Bluetooth and confirm the code here.").arg(bluetooth.state.adapter)
                                 : qsTr("OpenHU is then visible to phones for 2 minutes.")
                    Button {
                        text: bluetooth.state.pairing ? qsTr("Cancel") : qsTr("Pair")
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
                        description: modelData.address + (modelData.connected ? " · " + qsTr("connected") : "")
                        Row {
                            spacing: Theme.spacing / 3
                            Button {
                                text: qsTr("Connect")
                                onClicked: wireless.connectDevice(modelData.address)
                            }
                            Button {
                                text: qsTr("Remove")
                                onClicked: bluetooth.removeDevice(modelData.address)
                            }
                        }
                    }
                }

                SettingChoice {
                    title: qsTr("Priority with cable")
                    description: settings.values["wireless.priority"] === "wifi"
                                 ? qsTr("The phone may connect wirelessly also with a cable (the cable only charges then).")
                                 : qsTr("With a cable Android Auto runs over USB (more stable, charges). Wireless takes over after unplugging.")
                    key: "wireless.priority"
                    options: [{ value: "usb", label: "USB" }, { value: "wifi", label: qsTr("WiFi") }]
                }

                SectionTitle { text: qsTr("WiFi hotspot") }
                SettingChoice {
                    title: qsTr("Hotspot")
                    description: settings.values["wireless.hotspot"] === "demand"
                                 ? qsTr("Starts only when a paired phone shows up (a few seconds longer) and goes off after 2 minutes without a wireless session.")
                                 : qsTr("Runs all the time while wireless Android Auto is on (fastest connection).")
                    key: "wireless.hotspot"
                    options: [{ value: "always", label: qsTr("Always on") }, { value: "demand", label: qsTr("On demand") }]
                }
                SettingText {
                    title: qsTr("Name (SSID)")
                    description: qsTr("Leave empty for “%1”.").arg(wireless.state.ssid)
                    key: "wireless.ssid"
                    placeholder: wireless.state.ssid
                }
                SettingText {
                    title: qsTr("Password")
                    description: qsTr("Never needs to be typed in: the phone gets it via Bluetooth.")
                    key: "wireless.password"
                    inputHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                }
                SettingChoice {
                    title: qsTr("Frequency band")
                    description: qsTr("Android Auto runs best with 5 GHz. “Auto” takes 5 GHz and falls back to 2.4 GHz when 5 GHz does not start. A WiFi card cannot do both at once.")
                                 + (wireless.state.channel > 0 ? " " + qsTr("Currently channel %1.").arg(wireless.state.channel) : "")
                    key: "wireless.band"
                    options: [{ value: "auto", label: qsTr("Auto") }, { value: "5", label: "5 GHz" }, { value: "2.4", label: qsTr("2.4 GHz") }]
                }
                SettingText {
                    title: qsTr("Country")
                    description: qsTr("Sets the allowed WiFi channels (needed for 5 GHz).")
                    key: "wireless.country"
                    inputHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
                    placeholder: "DE"
                }
                SettingRow {
                    id: advancedRow
                    property bool open: false
                    title: qsTr("Advanced settings")
                    description: qsTr("Channel, address, DHCP range")
                    Button {
                        text: advancedRow.open ? qsTr("Hide") : qsTr("Show")
                        onClicked: advancedRow.open = !advancedRow.open
                    }
                }
                SettingText {
                    visible: advancedRow.open
                    title: qsTr("Channel")
                    description: settings.values["wireless.band"] === "auto"
                                 ? qsTr("With “Auto” OpenHU chooses by itself (36 or 6). Choose a fixed band for a fixed channel.")
                                 : qsTr("Empty = default (%1). 5 GHz: 36–48 without radar detection.").arg(settings.values["wireless.band"] === "2.4" ? "6" : "36")
                    key: "wireless.channel"
                    placeholder: qsTr("automatic")
                }
                SettingText {
                    visible: advancedRow.open
                    title: qsTr("Address (gateway)")
                    description: qsTr("OpenHU in the hotspot network, with netmask. The phone uses it as gateway and DNS.")
                    key: "wireless.address"
                    inputHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                    placeholder: "10.42.0.1/24"
                }
                SettingText {
                    visible: advancedRow.open
                    title: qsTr("DHCP range")
                    description: qsTr("Empty = automatic.")
                    key: "wireless.dhcpRange"
                    inputHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                    placeholder: "10.42.0.10-10.42.0.254"
                }
            }
            Column {
                width: parent.width
                spacing: Theme.spacing / 2
                visible: page.hasBluetooth
                SectionTitle { text: qsTr("Phone calls via Bluetooth") }
                SettingChoice {
                    title: qsTr("Bluetooth adapter")
                    description: qsTr("Pair the phone via Bluetooth through the USB cable (always active with wireless Android Auto).")
                    key: "bluetooth.adapter"
                    options: [{ value: "none", label: qsTr("None") }, { value: "local", label: qsTr("Local") }]
                }
                SettingText {
                    visible: settings.values["bluetooth.adapter"] === "local"
                    title: qsTr("Adapter address")
                    description: qsTr("MAC address, e.g. from “bluetoothctl list”. Empty: first adapter.")
                    key: "bluetooth.address"
                    placeholder: "00:11:22:33:44:55"
                }
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
                title: qsTr("API port")
                description: qsTr("WebSocket API for plugins and own programs.")
                key: "api.port"
                from: 1024
                to: 65535
            }
            SettingSwitch {
                title: qsTr("Share the API in the network")
                description: qsTr("Otherwise only reachable on this device. The API has no login – only switch it on in trusted networks.")
                key: "api.network"
            }
            SettingSwitch {
                visible: !!settings.values["api.network"]
                title: qsTr("API also in the WiFi hotspot")
                description: qsTr("The hotspot is meant for the phone; better connect devices for the API via Ethernet.")
                key: "api.hotspot"
            }
            SettingRow {
                title: qsTr("Address")
                description: settings.info.apiUrl
            }
            WifiNetworks {}
        }
    }

    Component {
        id: systemCategory
        SystemSettings {}
    }

    Component {
        id: info
        Section {
            SettingRow {
                title: "OpenHU"
                description: qsTr("Version %1").arg(settings.info.version) + (settings.info.git ? " (" + settings.info.git + ")" : "") + " · Qt " + settings.info.qt
            }
            SettingRow {
                title: qsTr("Configuration")
                description: settings.info.configFile
            }
            SettingRow {
                title: qsTr("Vehicle ID")
                description: qsTr("%1 (from the name suffix)").arg(settings.info.vehicleId)
            }
            SettingRow {
                title: qsTr("Data, plugins and logs")
                description: settings.info.dataDir
            }
            // On OpenHU OS: Settings -> System restarts and powers off the device (systemd would start OpenHU again
            // right after it quits)
            SettingRow {
                visible: !system.available
                title: qsTr("Restart OpenHU")
                Button {
                    text: qsTr("Restart")
                    onClicked: shell.restart()
                }
            }
            SettingRow {
                visible: !system.available
                title: qsTr("Quit OpenHU")
                Button {
                    text: qsTr("Quit")
                    onClicked: shell.quit()
                }
            }
            // why some settings are missing
            SettingRow {
                visible: !page.hasWifi || !page.hasBluetooth
                title: qsTr("Hardware")
                description: [!page.hasWifi ? qsTr("No WiFi card found: wireless Android Auto and WiFi networks are not available.") : "",
                              !page.hasBluetooth ? qsTr("No Bluetooth adapter found: wireless Android Auto, the car radio and phone calls via Bluetooth are not available.") : ""]
                             .filter(line => line).join(" ")
            }
            Text {
                width: parent ? parent.width : 0
                topPadding: Theme.spacing
                wrapMode: Text.WordWrap
                text: qsTr("Based on OpenAuto (f1x.studio, OpenCarDev) and aasdk. License: GPL-3.0.")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
