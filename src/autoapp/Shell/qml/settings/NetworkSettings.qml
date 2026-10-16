import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Settings → Network: the Ethernet port (OpenHU OS, average-web-dev/openhu-os#15) and the API; USB tethering and
// mobile data come here too.
SettingsSection {
    id: page

    readonly property var ethernet: system.ethernet
    readonly property bool server: ethernet.mode === "server"

    Connections {
        target: system
        function onEthernetError(message) { shell.toast(message, 5000) }
    }
    // cables and devices come and go
    Timer {
        running: page.visible && system.available
        interval: 5000
        repeat: true
        triggeredOnStart: true
        onTriggered: system.refreshEthernet()
    }

    function modeText() {
        if (page.ethernet.error)
            return page.ethernet.error
        if (page.server)
            return qsTrId("settings.network.ethernet_server_description").arg(page.ethernet.address || "")
        if (page.ethernet.serverAllowed === false)
            return qsTrId("settings.network.ethernet_other_dhcp_server")
        return qsTrId("settings.network.ethernet_client_description")
    }
    function portText(port) {
        switch (port.state) {
        case "connected":
            return port.address ? qsTrId("settings.network.ethernet_connected_address").arg(port.address)
                                : qsTrId("settings.network.ethernet_connected")
        case "connecting": return qsTrId("settings.network.ethernet_waiting_address")
        case "no-cable": return qsTrId("settings.network.ethernet_no_cable")
        }
        return qsTrId("settings.network.ethernet_not_connected")
    }
    function clientsText() {
        const clients = page.ethernet.clients || []
        if (!clients.length)
            return qsTrId("settings.network.ethernet_no_devices")
        return clients.map(c => c.name ? c.name + " (" + c.ip + ")" : c.ip).join("\n")
    }

    SectionTitle {
        visible: system.available
        text: qsTrId("settings.network.ethernet")
    }
    SettingRow {
        visible: system.available
        title: qsTrId("settings.network.ethernet_mode")
        description: page.modeText()
        Row {
            spacing: Theme.spacing / 3
            Repeater {
                model: [{value: "client", label: qsTrId("settings.network.dhcp_client")},
                        {value: "server", label: qsTrId("settings.network.dhcp_server")}]
                Button {
                    text: modelData.label
                    checkable: true
                    checked: page.ethernet.mode === modelData.value
                    highlighted: checked
                    // never two DHCP servers on one network: not while another one gave the port an address
                    enabled: !page.ethernet.busy && !page.ethernet.error
                             && (modelData.value !== "server" || page.server || page.ethernet.serverAllowed !== false)
                    onClicked: {
                        checked = Qt.binding(() => page.ethernet.mode === modelData.value)
                        if (page.ethernet.mode === modelData.value)
                            return
                        const error = system.setEthernetMode(modelData.value)
                        if (error)
                            shell.toast(error, 4000)
                    }
                }
            }
        }
    }
    Repeater {
        model: system.available ? (page.ethernet.devices || []) : []
        SettingRow {
            required property var modelData
            title: qsTrId("settings.network.ethernet_port").arg(modelData.interface)
            description: page.portText(modelData)
        }
    }
    SettingRow {
        visible: system.available && page.server
        title: qsTrId("settings.network.ethernet_devices")
        description: page.clientsText()
    }
    // devices reach the API only when it listens on the network (with the token)
    SettingRow {
        visible: system.available && page.server && !settings.values["api.network"]
        title: qsTrId("settings.network.ethernet_api_hint")
        description: qsTrId("settings.network.ethernet_api_hint_description")
    }

    SectionTitle { text: qsTrId("settings.network.api") }
    SettingNumber {
        title: qsTrId("settings.network.api_port")
        description: qsTrId("settings.network.websocket_api_plugins_own")
        key: "api.port"
        from: 1024
        to: 65535
    }
    SettingSwitch {
        title: qsTrId("settings.network.share_api_network")
        description: qsTrId("settings.network.otherwise_only_reachable_device")
        key: "api.network"
    }
    SettingSwitch {
        visible: !!settings.values["api.network"]
        title: qsTrId("settings.network.api_also_wifi_hotspot")
        description: qsTrId("settings.network.hotspot_meant_phone_better")
        key: "api.hotspot"
    }
    // clients from the network and browsers log in with it (docs/api.md)
    SettingRow {
        visible: !!settings.values["api.network"]
        title: qsTrId("settings.network.api_token")
        // the token on a line of its own, so it is not broken at a "-"
        description: qsTrId("settings.network.api_token_description") + "\n" + String(settings.values["api.token"] || "")
        ConfirmButton {
            label: qsTrId("settings.network.new_token")
            confirmText: qsTrId("settings.network.really_new_token")
            onConfirmed: settings.regenerateApiToken()
        }
    }
    SettingRow {
        title: qsTrId("settings.network.address")
        description: settings.info.apiUrl
    }
}
