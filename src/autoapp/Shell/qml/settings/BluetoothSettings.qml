import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Settings → Bluetooth (only with a Bluetooth adapter): phones for wireless Android Auto, phone calls via Bluetooth.
// The car radio is under Audio.
SettingsSection {
    id: bt

    // pairing here is for wireless Android Auto, which needs WiFi as well
    readonly property bool hasWifi: wireless.state.wifiAvailable !== false

    SectionTitle { text: qsTrId("settings.bluetooth.phones") }
    SettingRow {
        visible: bt.hasWifi
        title: bluetooth.state.pairing ? qsTrId("settings.bluetooth.visible_as").arg(bluetooth.state.adapter) : qsTrId("settings.bluetooth.pair_new_phone")
        description: bluetooth.state.pairing
                     ? qsTrId("settings.bluetooth.choose_on_phone").arg(bluetooth.state.adapter)
                     : qsTrId("settings.bluetooth.visible_2_minutes")
        Button {
            text: bluetooth.state.pairing ? qsTrId("settings.bluetooth.cancel") : qsTrId("settings.bluetooth.pair")
            highlighted: !bluetooth.state.pairing
            onClicked: {
                if (bluetooth.state.pairing) {
                    bluetooth.setPairing(false)
                } else {
                    // switches wireless Android Auto on if needed
                    wireless.pair()
                    settings.refresh()
                }
            }
        }
    }
    Repeater {
        model: bluetooth.devices.filter(d => d.icon === "phone")
        SettingRow {
            required property var modelData
            title: modelData.name
            description: modelData.address + (modelData.connected ? " · " + qsTrId("settings.bluetooth.connected") : "")
            Row {
                spacing: Theme.spacing / 3
                Button {
                    // for wireless Android Auto
                    visible: !!wireless.state.running
                    text: qsTrId("settings.bluetooth.connect")
                    onClicked: wireless.connectDevice(modelData.address)
                }
                Button {
                    text: qsTrId("settings.bluetooth.remove")
                    onClicked: bluetooth.removeDevice(modelData.address)
                }
            }
        }
    }

    SectionTitle { text: qsTrId("settings.bluetooth.phone_calls") }
    SettingChoice {
        title: qsTrId("settings.bluetooth.adapter")
        description: qsTrId("settings.bluetooth.adapter_description")
        key: "bluetooth.adapter"
        options: [{ value: "none", label: qsTrId("settings.bluetooth.adapter_none") }, { value: "local", label: qsTrId("settings.bluetooth.adapter_local") }]
    }
    SettingText {
        visible: settings.values["bluetooth.adapter"] === "local"
        title: qsTrId("settings.bluetooth.adapter_address")
        description: qsTrId("settings.bluetooth.adapter_address_description")
        key: "bluetooth.address"
        placeholder: "00:11:22:33:44:55"
    }
}
