// Settings → Network: the API; Ethernet, USB tethering and mobile data come here (average-web-dev/openhu-os#15).
SettingsSection {
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
