import QtQuick
import QtQuick.Controls

// Settings → WiFi (only with a WiFi card): WiFi networks to go online through, the hotspot for wireless Android Auto.
SettingsSection {
    WifiNetworks {}

    SectionTitle { text: qsTrId("settings.wifi.hotspot_title") }
    SettingChoice {
        title: qsTrId("settings.wifi.hotspot_mode")
        description: settings.values["wireless.hotspot"] === "demand"
                     ? qsTrId("settings.wifi.hotspot_demand_description")
                     : qsTrId("settings.wifi.hotspot_always_description")
        key: "wireless.hotspot"
        options: [{ value: "always", label: qsTrId("settings.wifi.hotspot_always") }, { value: "demand", label: qsTrId("settings.wifi.hotspot_demand") }]
    }
    SettingText {
        title: qsTrId("settings.wifi.hotspot_name")
        description: qsTrId("settings.wifi.hotspot_name_description").arg(wireless.state.ssid)
        key: "wireless.ssid"
        placeholder: wireless.state.ssid
    }
    SettingText {
        title: qsTrId("settings.wifi.hotspot_password")
        description: qsTrId("settings.wifi.hotspot_password_description")
        key: "wireless.password"
        inputHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
    }
    SettingChoice {
        title: qsTrId("settings.wifi.band")
        description: qsTrId("settings.wifi.band_description")
                     + (wireless.state.channel > 0 ? " " + qsTrId("settings.wifi.current_channel").arg(wireless.state.channel) : "")
        key: "wireless.band"
        options: [{ value: "auto", label: qsTrId("settings.wifi.band_auto") }, { value: "5", label: "5 GHz" }, { value: "2.4", label: qsTrId("settings.wifi.band_2_4") }]
    }
    SettingText {
        title: qsTrId("settings.wifi.country")
        description: qsTrId("settings.wifi.country_description")
        key: "wireless.country"
        inputHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
        placeholder: "DE"
    }
    SettingRow {
        id: advancedRow
        property bool open: false
        title: qsTrId("settings.wifi.advanced")
        description: qsTrId("settings.wifi.advanced_description")
        Button {
            text: advancedRow.open ? qsTrId("settings.wifi.hide") : qsTrId("settings.wifi.show")
            onClicked: advancedRow.open = !advancedRow.open
        }
    }
    SettingText {
        visible: advancedRow.open
        title: qsTrId("settings.wifi.channel")
        description: settings.values["wireless.band"] === "auto"
                     ? qsTrId("settings.wifi.channel_auto_description")
                     : qsTrId("settings.wifi.channel_description").arg(settings.values["wireless.band"] === "2.4" ? "6" : "36")
        key: "wireless.channel"
        placeholder: qsTrId("settings.wifi.channel_automatic")
    }
    SettingText {
        visible: advancedRow.open
        title: qsTrId("settings.wifi.address")
        description: qsTrId("settings.wifi.address_description")
        key: "wireless.address"
        inputHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
        placeholder: "10.42.0.1/24"
    }
    SettingText {
        visible: advancedRow.open
        title: qsTrId("settings.wifi.dhcp_range")
        description: qsTrId("settings.wifi.dhcp_range_description")
        key: "wireless.dhcpRange"
        inputHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
        placeholder: "10.42.0.10-10.42.0.254"
    }
}
