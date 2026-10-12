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
    readonly property var cards: [{ value: "", label: qsTrId("settings.wifi.automatic") }]
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
            text += (text ? " · " : "") + (wifi.state.internet ? qsTrId("settings.wifi.internet") : qsTrId("settings.wifi.no_internet"))
            if (networks.room === "client")
                text += " · " + qsTrId("settings.wifi.hotspot_off_connected")
            return text
        }
        if (networks.room === "search")
            return qsTrId("settings.wifi.hotspot_off_search_until")
                   .arg(Qt.formatTime(new Date(wireless.state.searchUntil), "HH:mm"))
        if (networks.hotspotOnCard)
            return qsTrId("settings.wifi.wifi_card_runs_hotspot")
        return qsTrId("settings.wifi.joins_known_network_range")
    }

    Connections {
        target: wifi
        function onError(message) { shell.toast(message, 5000) }
    }

    SectionTitle { text: qsTrId("settings.wifi.wifi_networks") }

    SettingRow {
        visible: !networks.hasCard
        title: qsTrId("settings.wifi.connect_wifi_networks")
        description: qsTrId("settings.wifi.no_wifi_adapter_found")
    }

    SettingSwitch {
        visible: networks.hasCard
        title: qsTrId("settings.wifi.connect_wifi_networks")
        description: qsTrId("settings.wifi.openhu_goes_online_through")
        key: "wireless.client"
    }

    // with a second card: one for the hotspot, one for WiFi networks, both at once
    SettingDropdown {
        visible: networks.clientOn && networks.cards.length > 2
        title: qsTrId("settings.wifi.wifi_card_hotspot")
        description: qsTrId("settings.wifi.automatic_first_one")
        key: "wireless.interface"
        options: networks.cards
    }
    SettingDropdown {
        visible: networks.clientOn && networks.cards.length > 2
        title: qsTrId("settings.wifi.wifi_card_wifi_networks")
        description: qsTrId("settings.wifi.automatic_whichever_free_card")
        key: "wireless.clientInterface"
        options: networks.cards
    }

    SettingRow {
        visible: networks.clientOn
        title: networks.client.ssid ? qsTrId("settings.wifi.connected_to").arg(networks.client.ssid) : qsTrId("settings.wifi.not_connected")
        description: networks.statusText()
    }

    Repeater {
        model: networks.clientOn ? (wifi.state.networks || []) : []
        SettingRow {
            required property var modelData
            required property int index
            title: modelData.ssid
            description: [modelData.active ? qsTrId("settings.wifi.connected") : modelData.joined ? "" : qsTrId("settings.wifi.not_connected_yet"),
                          modelData.secured ? "" : qsTrId("settings.wifi.open_network"),
                          modelData.hidden ? qsTrId("settings.wifi.hidden") : ""].filter(part => part).join(" · ")
            Row {
                spacing: Theme.spacing / 3
                Button {
                    visible: !modelData.active
                    enabled: !wifi.state.busy
                    text: qsTrId("settings.wifi.connect")
                    onClicked: networks.report(wireless.connectNetwork(modelData.uuid))
                }
                Button {
                    // NetworkManager tries the networks from the top
                    visible: index > 0
                    text: qsTrId("settings.wifi.up")
                    onClicked: wifi.moveUp(modelData.uuid)
                }
                Button {
                    text: qsTrId("settings.wifi.remove")
                    onClicked: wifi.remove(modelData.uuid)
                }
            }
        }
    }

    SettingRow {
        visible: networks.clientOn
        title: wifi.state.scanning ? qsTrId("settings.wifi.searching") : qsTrId("settings.wifi.add_network")
        description: wifi.state.message || qsTrId("settings.wifi.search_networks_range_add")
        Row {
            spacing: Theme.spacing / 3
            Button {
                text: qsTrId("settings.wifi.search")
                enabled: !wifi.state.scanning && !wifi.state.busy
                onClicked: networks.report(wireless.searchNetworks())
            }
            Button {
                text: qsTrId("settings.wifi.hidden_network")
                onClicked: networks.startAdding("", true)
            }
        }
    }

    Repeater {
        model: networks.clientOn ? (wifi.state.found || []).filter(network => !network.known) : []
        SettingRow {
            required property var modelData
            title: modelData.ssid
            description: modelData.signal + " %" + (modelData.secured ? " · " + modelData.security : " · " + qsTrId("settings.wifi.open_network"))
            Button {
                text: qsTrId("settings.wifi.add")
                onClicked: networks.startAdding(modelData.ssid, modelData.secured)
            }
        }
    }

    SettingRow {
        visible: networks.clientOn && networks.addingOpen
        title: networks.adding ? qsTrId("settings.wifi.add_title").arg(networks.adding) : qsTrId("settings.wifi.hidden_network")
        description: networks.addingSecured ? qsTrId("settings.wifi.wpa_password_8_63") : qsTrId("settings.wifi.open_network_no_password")
        Row {
            spacing: Theme.spacing / 3
            TextField {
                id: name
                visible: networks.adding === ""
                width: Theme.dockWidth * 2.4
                placeholderText: qsTrId("settings.wifi.name_ssid")
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onAccepted: password.forceActiveFocus()
            }
            TextField {
                id: password
                visible: networks.addingSecured
                width: Theme.dockWidth * 2.4
                placeholderText: qsTrId("settings.wifi.password")
                echoMode: TextInput.PasswordEchoOnEdit
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onAccepted: networks.save()
            }
            Button {
                text: qsTrId("settings.wifi.save")
                highlighted: true
                enabled: !wifi.state.busy && name.text.trim() !== "" && (!networks.addingSecured || password.text.length >= 8)
                onClicked: networks.save()
            }
            Button {
                text: qsTrId("settings.wifi.cancel")
                flat: true
                onClicked: networks.addingOpen = false
            }
        }
    }
}
