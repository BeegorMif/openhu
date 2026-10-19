import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Settings → Info: versions, files, missing hardware.
SettingsSection {
    id: info

    readonly property bool hasWifi: wireless.state.wifiAvailable !== false
    readonly property bool hasBluetooth: !!bluetooth.state.available

    SettingRow {
        title: "OpenHU"
        description: qsTrId("settings.info.version").arg(settings.info.version) + (settings.info.git ? " (" + settings.info.git + ")" : "") + " · Qt " + settings.info.qt
    }
    SettingRow {
        title: qsTrId("settings.info.configuration")
        description: settings.info.configFile
    }
    SettingRow {
        title: qsTrId("settings.info.vehicle_id")
        description: qsTrId("settings.info.name_suffix").arg(settings.info.vehicleId)
    }
    SettingRow {
        title: qsTrId("settings.info.data_plugins_logs")
        description: settings.info.dataDir
    }
    // On OpenHU OS: Settings -> System restarts and powers off the device (systemd would start OpenHU again
    // right after it quits)
    SettingRow {
        visible: !system.available
        title: qsTrId("settings.info.restart_openhu")
        Button {
            text: qsTrId("settings.info.restart")
            onClicked: shell.restart()
        }
    }
    SettingRow {
        visible: !system.available
        title: qsTrId("settings.info.quit_openhu")
        Button {
            text: qsTrId("settings.info.quit")
            onClicked: shell.quit()
        }
    }
    // settings for development and tests (#120)
    SettingSwitch {
        title: qsTrId("settings.info.debug_mode")
        description: qsTrId("settings.info.debug_mode_description")
        key: "debug.enabled"
    }
    // why some settings are missing
    SettingRow {
        visible: !info.hasWifi || !info.hasBluetooth
        title: qsTrId("settings.info.hardware")
        description: [!info.hasWifi ? qsTrId("settings.info.no_wifi_card_found") : "",
                      !info.hasBluetooth ? qsTrId("settings.info.no_bluetooth_adapter_found") : ""]
                     .filter(line => line).join(" ")
    }
    Text {
        width: parent ? parent.width : 0
        topPadding: Theme.spacing
        wrapMode: Text.WordWrap
        text: qsTrId("settings.info.based_openauto_f1x_studio")
        color: Theme.textDim
        font.pixelSize: Theme.fontSmall
    }
}
