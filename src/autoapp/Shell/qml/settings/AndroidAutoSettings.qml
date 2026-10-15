import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Settings → Android Auto: picture, night mode, controls and sound, connection, wireless Android Auto.
SettingsSection {
    id: aa

    // wireless Android Auto needs both adapters (otherwise a line which one is missing)
    readonly property bool hasWifi: wireless.state.wifiAvailable !== false
    readonly property bool hasBluetooth: !!bluetooth.state.available

    function wirelessStatus(state) {
        if (state.bluetooth === "error")
            return state.bluetoothError
        if (state.hotspot === "error")
            return state.hotspotError
        if (state.hotspot === "starting")
            return qsTrId("settings.aa.hotspot_starting")
        if (state.phaseText)
            return state.phaseText + (state.device ? " (" + state.device + ")" : "")
        if (state.hotspot === "off")
            return qsTrId("settings.aa.ready_hotspot_on_demand")
        return qsTrId("settings.aa.ready_wifi").arg(state.ssid).arg(state.interface)
    }

    SectionTitle { text: qsTrId("settings.aa.picture") }
    SettingChoice {
        title: qsTrId("settings.aa.resolution")
        description: qsTrId("settings.aa.android_auto_only_knows")
                     .arg(settings.values["aa.activeResolution"])
        key: "aa.resolution"
        options: [{ value: "auto", label: qsTrId("settings.aa.auto") }, { value: "800x480", label: "480p" },
                  { value: "1280x720", label: "720p" }, { value: "1920x1080", label: "1080p" },
                  { value: "2560x1440", label: "1440p" }]
    }
    SettingChoice {
        title: qsTrId("settings.aa.frame_rate")
        key: "aa.fps"
        options: [{ value: 30, label: "30 fps" }, { value: 60, label: "60 fps" }]
    }
    SettingNumber {
        title: qsTrId("settings.aa.pixel_density_dpi")
        description: qsTrId("settings.aa.larger_bigger_text_icons")
        key: "aa.dpi"
        from: 80
        to: 400
        stepSize: 10
    }
    SettingRow {
        title: qsTrId("settings.aa.automatic_margins")
        description: qsTrId("settings.aa.compute_margins_so_android")
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
        title: qsTrId("settings.aa.margin_left_right_pixels")
        key: "aa.marginWidth"
        to: 600
        stepSize: 2
    }
    SettingNumber {
        visible: settings.values["aa.marginWidth"] !== 0 || settings.values["aa.marginHeight"] !== 0
        title: qsTrId("settings.aa.margin_top_bottom_pixels")
        key: "aa.marginHeight"
        to: 600
        stepSize: 2
    }
    // only Android Auto has a night mode; with a "Display" category it moves there (#9)
    SettingChoice {
        title: qsTrId("settings.aa.night_mode")
        description: qsTrId("settings.aa.night_mode_description")
        key: "aa.nightMode"
        options: [{ value: "auto", label: qsTrId("settings.aa.night_auto") }, { value: "day", label: qsTrId("settings.aa.night_day") },
                  { value: "night", label: qsTrId("settings.aa.night_night") }]
    }

    SectionTitle { text: qsTrId("settings.aa.controls_sound") }
    SettingSwitch {
        title: qsTrId("settings.aa.touchscreen")
        key: "aa.touchscreen"
    }
    SettingSwitch {
        title: qsTrId("settings.aa.music_media")
        description: qsTrId("settings.aa.play_media_sound_android")
        key: "aa.audioMedia"
    }
    SettingSwitch {
        title: qsTrId("settings.aa.navigation_announcements")
        key: "aa.audioGuidance"
    }
    SettingSwitch {
        title: qsTrId("settings.aa.phone_calls")
        key: "aa.audioTelephony"
    }
    SettingSwitch {
        title: qsTrId("settings.aa.radio")
        description: qsTrId("settings.aa.radio_description")
        key: "aa.radio"
    }

    SectionTitle { text: qsTrId("settings.aa.connection") }
    SettingRow {
        title: qsTrId("settings.aa.reconnect_phone")
        description: qsTrId("settings.aa.resets_phone_connected_usb")
        Button {
            text: qsTrId("settings.aa.reconnect")
            onClicked: actions.trigger("aa.reconnect")
        }
    }

    SectionTitle { text: qsTrId("settings.aa.wireless_android_auto") }
    // instead of settings that cannot work: which adapter is missing
    SettingRow {
        visible: !aa.hasWifi || !aa.hasBluetooth
        title: qsTrId("settings.aa.wireless_android_auto")
        description: (!aa.hasWifi ? qsTrId("settings.aa.no_wifi_adapter_found") + " " : "")
                     + (!aa.hasBluetooth ? qsTrId("settings.aa.no_bluetooth_adapter_found") + " " : "")
                     + qsTrId("settings.aa.wireless_needs_both")
    }
    Column {
        width: parent.width
        spacing: Theme.spacing / 2
        visible: aa.hasWifi && aa.hasBluetooth
        SettingSwitch {
            title: qsTrId("settings.aa.wireless_android_auto")
            description: qsTrId("settings.aa.wireless_description")
            key: "aa.wireless"
        }
        SettingRow {
            visible: wireless.state.running
            title: qsTrId("settings.aa.wireless_status")
            description: aa.wirelessStatus(wireless.state)
            Button {
                visible: !!wireless.state.wifiOff
                text: qsTrId("settings.aa.switch_wifi_on")
                onClicked: wireless.enableWifi()
            }
        }
        SettingChoice {
            title: qsTrId("settings.aa.priority_cable")
            description: settings.values["wireless.priority"] === "wifi"
                         ? qsTrId("settings.aa.priority_wifi_description")
                         : qsTrId("settings.aa.priority_usb_description")
            key: "wireless.priority"
            options: [{ value: "usb", label: "USB" }, { value: "wifi", label: qsTrId("settings.aa.priority_wifi") }]
        }
    }
}
