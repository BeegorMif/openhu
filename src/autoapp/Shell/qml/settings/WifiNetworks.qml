import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Known WiFi networks (WifiClient, QML "wifi"; WirelessController makes room on the card): OpenHU goes online through
// them for updates, remote management and plugins. Joined by itself after a first connection by hand.
Column {
    id: networks
    width: parent ? parent.width : 0
    spacing: Theme.spacing / 2
    // once the cards are known: the settings, or why there are none
    visible: !!wifi.state.ready

    readonly property bool hasCard: !!wifi.state.available
    readonly property bool clientOn: hasCard && !!settings.values["wireless.client"]
    readonly property var client: wifi.state.client || {}
    readonly property string room: wireless.state.hotspotRoom || ""
    readonly property bool hotspotOnCard: !!wifi.state.hotspotDevice
    // [{value, label}] of the WiFi cards, "" = automatic
    readonly property var cards: [{ value: "", label: qsTr("Automatic") }]
                                 .concat((wifi.state.devices || []).map(card => ({ value: card, label: card })))
    // the network the password row is for; "" while adding a hidden one by its name
    property string adding: ""
    property bool addingOpen: false
    property bool addingSecured: true

    function startAdding(ssid, secured) {
        adding = ssid
        addingSecured = secured
        addingOpen = true
        password.text = ""
        name.text = ssid
        if (ssid === "")
            name.forceActiveFocus()
        else if (secured)
            password.forceActiveFocus()
    }
    function report(error) {
        if (error)
            shell.toast(error, 4000)
        return !error
    }
    function save() {
        if (!report(wireless.addNetwork(name.text.trim(), addingSecured ? password.text : "", adding === "")))
            return
        addingOpen = false
        password.text = ""
        password.focus = false
        name.focus = false
    }
    function statusText() {
        if (networks.client.ssid) {
            let text = networks.client.ip || ""
            text += (text ? " · " : "") + (wifi.state.internet ? qsTr("internet") : qsTr("no internet"))
            if (networks.room === "client")
                text += " · " + qsTr("the hotspot is off while connected")
            return text
        }
        if (networks.room === "search")
            return qsTr("The hotspot is off for the search until %1.")
                   .arg(Qt.formatTime(new Date(wireless.state.searchUntil), "HH:mm"))
        if (networks.hotspotOnCard)
            return qsTr("The WiFi card runs the hotspot for wireless Android Auto. Search or connect to use a WiFi network; the hotspot comes back when it is lost.")
        return qsTr("Joins a known network in range by itself whenever the hotspot does not need the WiFi card.")
    }

    Connections {
        target: wifi
        function onError(message) { shell.toast(message, 5000) }
    }

    SectionTitle { text: qsTr("WiFi networks") }

    SettingRow {
        visible: !networks.hasCard
        title: qsTr("Connect to WiFi networks")
        description: qsTr("No WiFi adapter found.")
    }

    SettingSwitch {
        visible: networks.hasCard
        title: qsTr("Connect to WiFi networks")
        description: qsTr("OpenHU goes online through saved WiFi networks (home, workshop, a phone's hotspot) for updates, remote access and plugins, whenever the hotspot does not need the WiFi card.")
        key: "wireless.client"
    }

    // with a second card: one for the hotspot, one for WiFi networks, both at once
    SettingDropdown {
        visible: networks.clientOn && networks.cards.length > 2
        title: qsTr("WiFi card for the hotspot")
        description: qsTr("Automatic: the first one.")
        key: "wireless.interface"
        options: networks.cards
    }
    SettingDropdown {
        visible: networks.clientOn && networks.cards.length > 2
        title: qsTr("WiFi card for WiFi networks")
        description: qsTr("Automatic: whichever is free. A card of its own keeps the hotspot running meanwhile.")
        key: "wireless.clientInterface"
        options: networks.cards
    }

    SettingRow {
        visible: networks.clientOn
        title: networks.client.ssid ? qsTr("Connected to “%1”").arg(networks.client.ssid) : qsTr("Not connected")
        description: networks.statusText()
    }

    Repeater {
        model: networks.clientOn ? (wifi.state.networks || []) : []
        SettingRow {
            required property var modelData
            required property int index
            title: modelData.ssid
            description: [modelData.active ? qsTr("connected") : modelData.joined ? "" : qsTr("not connected yet"),
                          modelData.secured ? "" : qsTr("open network"),
                          modelData.hidden ? qsTr("hidden") : ""].filter(part => part).join(" · ")
            Row {
                spacing: Theme.spacing / 3
                Button {
                    visible: !modelData.active
                    enabled: !wifi.state.busy
                    text: qsTr("Connect")
                    onClicked: networks.report(wireless.connectNetwork(modelData.uuid))
                }
                Button {
                    // NetworkManager tries the networks from the top
                    visible: index > 0
                    text: qsTr("Up")
                    onClicked: wifi.moveUp(modelData.uuid)
                }
                Button {
                    text: qsTr("Remove")
                    onClicked: wifi.remove(modelData.uuid)
                }
            }
        }
    }

    SettingRow {
        visible: networks.clientOn
        title: wifi.state.scanning ? qsTr("Searching …") : qsTr("Add a network")
        description: wifi.state.message || qsTr("Search for networks in range, or add a hidden one by its name.")
        Row {
            spacing: Theme.spacing / 3
            Button {
                text: qsTr("Search")
                enabled: !wifi.state.scanning && !wifi.state.busy
                onClicked: networks.report(wireless.searchNetworks())
            }
            Button {
                text: qsTr("Hidden network")
                onClicked: networks.startAdding("", true)
            }
        }
    }

    Repeater {
        model: networks.clientOn ? (wifi.state.found || []).filter(network => !network.known) : []
        SettingRow {
            required property var modelData
            title: modelData.ssid
            description: modelData.signal + " %" + (modelData.secured ? " · " + modelData.security : " · " + qsTr("open network"))
            Button {
                text: qsTr("Add")
                onClicked: networks.startAdding(modelData.ssid, modelData.secured)
            }
        }
    }

    SettingRow {
        visible: networks.clientOn && networks.addingOpen
        title: networks.adding ? qsTr("Add “%1”").arg(networks.adding) : qsTr("Hidden network")
        description: networks.addingSecured ? qsTr("WPA password (8 to 63 characters).") : qsTr("Open network, no password.")
        Row {
            spacing: Theme.spacing / 3
            TextField {
                id: name
                visible: networks.adding === ""
                width: Theme.dockWidth * 2.4
                placeholderText: qsTr("Name (SSID)")
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onAccepted: password.forceActiveFocus()
            }
            TextField {
                id: password
                visible: networks.addingSecured
                width: Theme.dockWidth * 2.4
                placeholderText: qsTr("Password")
                echoMode: TextInput.PasswordEchoOnEdit
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onAccepted: networks.save()
            }
            Button {
                text: qsTr("Save")
                highlighted: true
                enabled: !wifi.state.busy && name.text.trim() !== "" && (!networks.addingSecured || password.text.length >= 8)
                onClicked: networks.save()
            }
            Button {
                text: qsTr("Cancel")
                flat: true
                onClicked: networks.addingOpen = false
            }
        }
    }
}
