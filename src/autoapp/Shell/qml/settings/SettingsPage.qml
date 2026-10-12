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
        { id: "general", label: qsTrId("settings.page.general") },
        { id: "aa", label: "Android Auto" },
        { id: "audio", label: qsTrId("settings.page.audio") },
    ].concat(hasWifi || hasBluetooth ? [{ id: "bluetooth", label: qsTrId("settings.page.wireless") }] : [], [
        { id: "keys", label: qsTrId("settings.page.keys") },
        { id: "apps", label: qsTrId("settings.page.apps_store") },
        { id: "network", label: qsTrId("settings.page.network_api") }
    ]).concat(system.available ? [{ id: "system", label: qsTrId("settings.page.system") }] : [], [{ id: "info", label: qsTrId("settings.page.info") }])

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
                        text: pendingBar.restart ? qsTrId("settings.page.some_changes_only_apply")
                                                    : qsTrId("settings.page.changes_apply_next_connection")
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                    }
                    Button {
                        text: pendingBar.restart ? qsTrId("settings.page.restart") : qsTrId("settings.page.reconnect")
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
                title: qsTrId("settings.general.language")
                description: qsTrId("settings.general.language_openhu_onscreen_keyboard")
                key: "general.language"
                options: settings.info.languages
            }
            SettingChoice {
                title: qsTrId("settings.general.steering_wheel")
                description: qsTrId("settings.general.android_auto_arranges_controls")
                key: "general.driverSide"
                options: [{ value: "left", label: qsTrId("settings.general.left") }, { value: "right", label: qsTrId("settings.general.right") }]
            }
            SettingText {
                title: qsTrId("settings.general.name_suffix")
                description: qsTrId("settings.general.phone_shows_openhu_different")
                             .arg(settings.values["general.nameSuffix"] || qsTrId("settings.general.suffix"))
                key: "general.nameSuffix"
                placeholder: qsTrId("settings.general.e_g_golf")
            }
            SettingSwitch {
                title: qsTrId("settings.general.show_mouse_cursor")
                description: qsTrId("settings.general.using_mouse_instead_touchscreen")
                key: "general.showCursor"
            }
            SettingSwitch {
                visible: onScreenKeyboardAvailable
                title: qsTrId("settings.general.onscreen_keyboard")
                description: qsTrId("settings.general.appears_text_fields_switch")
                key: "general.onScreenKeyboard"
            }
        }
    }

    Component {
        id: androidAuto
        Section {
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

            SectionTitle { text: qsTrId("settings.aa.connection") }
            SettingRow {
                visible: page.hasWifi && page.hasBluetooth
                title: qsTrId("settings.aa.wireless_wifi")
                description: settings.values["aa.wireless"] ? qsTrId("settings.aa.on") : qsTrId("settings.aa.off")
                Button {
                    text: qsTrId("settings.aa.set_up")
                    onClicked: page.category = "bluetooth"
                }
            }
            SettingRow {
                title: qsTrId("settings.aa.reconnect_phone")
                description: qsTrId("settings.aa.resets_phone_connected_usb")
                Button {
                    text: qsTrId("settings.aa.reconnect")
                    onClicked: actions.trigger("aa.reconnect")
                }
            }
        }
    }

    Component {
        id: audio
        Section {
            SettingRow {
                title: qsTrId("settings.audio.volume")
                description: volume.muted ? qsTrId("settings.audio.muted") : volume.volume + " %"
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
                        text: qsTrId("settings.audio.mute")
                        checked: volume.muted
                        onToggled: volume.muted = checked
                    }
                }
            }
            SettingChoice {
                title: qsTrId("settings.audio.audio_output")
                description: qsTrId("settings.audio.rtaudio_mostly_has_lower")
                key: "audio.backend"
                options: [{ value: "rtaudio", label: "RtAudio" }, { value: "qt", label: "Qt Multimedia" }]
            }

            Column {
                width: parent.width
                spacing: Theme.spacing / 2
                visible: page.hasBluetooth
                SectionTitle { text: qsTrId("settings.audio.car_radio_bluetooth") }
                SettingSwitch {
                    title: qsTrId("settings.audio.sound_car_radio")
                    description: carAudio.state.device
                                 ? carAudio.state.status
                                 : qsTrId("settings.audio.sound_via_bluetooth_radio")
                    key: "carAudio.enabled"
                }
                SettingRow {
                    visible: !!carAudio.state.device
                    title: carAudio.state.name || carAudio.state.device
                    description: carAudio.state.device + (carAudio.state.connected ? " · " + qsTrId("settings.audio.connected") : "")
                    Button {
                        text: qsTrId("settings.audio.remove")
                        onClicked: {
                            carAudio.forgetDevice()
                            settings.refresh()
                        }
                    }
                }
                SettingRow {
                    title: bluetooth.state.searching ? qsTrId("settings.audio.searching") : carAudio.state.device ? qsTrId("settings.audio.other_radio") : qsTrId("settings.audio.search_radio")
                    description: qsTrId("settings.audio.put_radio_into_pairing")
                    Button {
                        text: bluetooth.state.searching ? qsTrId("settings.audio.stop") : qsTrId("settings.audio.search")
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
                        description: modelData.address + (bluetooth.state.pairingWith === modelData.address.toUpperCase() ? " · " + qsTrId("settings.audio.pairing") : "")
                        Button {
                            text: qsTrId("settings.audio.use")
                            // selecting ends the search, which rebuilds this list (and destroys this button)
                            onClicked: carAudio.selectDevice(modelData.address)
                        }
                    }
                }
                SettingText {
                    title: qsTrId("settings.audio.pairing_pin")
                    description: qsTrId("settings.audio.only_older_radios_ask")
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
            return qsTrId("settings.page.wifi_hotspot_starting")
        if (state.phaseText)
            return state.phaseText + (state.device ? " (" + state.device + ")" : "")
        if (state.hotspot === "off")
            return qsTrId("settings.page.ready_wifi_hotspot_starts")
        return qsTrId("settings.page.ready_wifi").arg(state.ssid).arg(state.interface)
    }

    Component {
        id: wirelessCategory
        Section {
            // instead of settings that cannot work: which adapter is missing
            SettingRow {
                visible: !page.hasWifi || !page.hasBluetooth
                title: qsTrId("settings.wireless.wireless_android_auto")
                description: (!page.hasWifi ? qsTrId("settings.wireless.no_wifi_adapter_found") + " " : "")
                             + (!page.hasBluetooth ? qsTrId("settings.wireless.no_bluetooth_adapter_found") + " " : "")
                             + qsTrId("settings.wireless.wireless_android_auto_needs")
            }
            Column {
                width: parent.width
                spacing: Theme.spacing / 2
                visible: page.hasWifi && page.hasBluetooth
                SettingSwitch {
                    title: qsTrId("settings.wireless.wireless_android_auto")
                    description: qsTrId("settings.wireless.phone_paired_once_via")
                    key: "aa.wireless"
                }
                SettingRow {
                    visible: wireless.state.running
                    title: qsTrId("settings.wireless.status")
                    description: page.wirelessStatus(wireless.state)
                    Button {
                        visible: !!wireless.state.wifiOff
                        text: qsTrId("settings.wireless.switch_wifi")
                        onClicked: wireless.enableWifi()
                    }
                }

                SectionTitle {
                    visible: settings.values["aa.wireless"] && wireless.state.bluetooth !== "error"
                    text: qsTrId("settings.wireless.phones")
                }
                SettingRow {
                    visible: settings.values["aa.wireless"] && wireless.state.bluetooth !== "error"
                    title: bluetooth.state.pairing ? qsTrId("settings.wireless.visible").arg(bluetooth.state.adapter) : qsTrId("settings.wireless.pair_new_phone")
                    description: bluetooth.state.pairing
                                 ? qsTrId("settings.wireless.phone_choose_under_bluetooth").arg(bluetooth.state.adapter)
                                 : qsTrId("settings.wireless.visible_2_minutes")
                    Button {
                        text: bluetooth.state.pairing ? qsTrId("settings.wireless.cancel") : qsTrId("settings.wireless.pair")
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
                        description: modelData.address + (modelData.connected ? " · " + qsTrId("settings.wireless.connected") : "")
                        Row {
                            spacing: Theme.spacing / 3
                            Button {
                                text: qsTrId("settings.wireless.connect")
                                onClicked: wireless.connectDevice(modelData.address)
                            }
                            Button {
                                text: qsTrId("settings.wireless.remove")
                                onClicked: bluetooth.removeDevice(modelData.address)
                            }
                        }
                    }
                }

                SettingChoice {
                    title: qsTrId("settings.wireless.priority_cable")
                    description: settings.values["wireless.priority"] === "wifi"
                                 ? qsTrId("settings.wireless.phone_may_connect_wirelessly")
                                 : qsTrId("settings.wireless.cable_android_auto_runs")
                    key: "wireless.priority"
                    options: [{ value: "usb", label: "USB" }, { value: "wifi", label: qsTrId("settings.wireless.wifi") }]
                }

                SectionTitle { text: qsTrId("settings.wireless.wifi_hotspot") }
                SettingChoice {
                    title: qsTrId("settings.wireless.hotspot")
                    description: settings.values["wireless.hotspot"] === "demand"
                                 ? qsTrId("settings.wireless.starts_only_paired_phone")
                                 : qsTrId("settings.wireless.runs_all_time_wireless")
                    key: "wireless.hotspot"
                    options: [{ value: "always", label: qsTrId("settings.wireless.always") }, { value: "demand", label: qsTrId("settings.wireless.demand") }]
                }
                SettingText {
                    title: qsTrId("settings.wireless.name_ssid")
                    description: qsTrId("settings.wireless.leave_empty").arg(wireless.state.ssid)
                    key: "wireless.ssid"
                    placeholder: wireless.state.ssid
                }
                SettingText {
                    title: qsTrId("settings.wireless.password")
                    description: qsTrId("settings.wireless.never_needs_typed_phone")
                    key: "wireless.password"
                    inputHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                }
                SettingChoice {
                    title: qsTrId("settings.wireless.frequency_band")
                    description: qsTrId("settings.wireless.android_auto_runs_best")
                                 + (wireless.state.channel > 0 ? " " + qsTrId("settings.wireless.currently_channel").arg(wireless.state.channel) : "")
                    key: "wireless.band"
                    options: [{ value: "auto", label: qsTrId("settings.wireless.auto") }, { value: "5", label: "5 GHz" }, { value: "2.4", label: qsTrId("settings.wireless.2_4_ghz") }]
                }
                SettingText {
                    title: qsTrId("settings.wireless.country")
                    description: qsTrId("settings.wireless.sets_allowed_wifi_channels")
                    key: "wireless.country"
                    inputHints: Qt.ImhUppercaseOnly | Qt.ImhNoPredictiveText
                    placeholder: "DE"
                }
                SettingRow {
                    id: advancedRow
                    property bool open: false
                    title: qsTrId("settings.wireless.advanced_settings")
                    description: qsTrId("settings.wireless.channel_address_dhcp_range")
                    Button {
                        text: advancedRow.open ? qsTrId("settings.wireless.hide") : qsTrId("settings.wireless.show")
                        onClicked: advancedRow.open = !advancedRow.open
                    }
                }
                SettingText {
                    visible: advancedRow.open
                    title: qsTrId("settings.wireless.channel")
                    description: settings.values["wireless.band"] === "auto"
                                 ? qsTrId("settings.wireless.auto_openhu_chooses_itself")
                                 : qsTrId("settings.wireless.empty_default_5_ghz").arg(settings.values["wireless.band"] === "2.4" ? "6" : "36")
                    key: "wireless.channel"
                    placeholder: qsTrId("settings.wireless.automatic")
                }
                SettingText {
                    visible: advancedRow.open
                    title: qsTrId("settings.wireless.address_gateway")
                    description: qsTrId("settings.wireless.openhu_hotspot_network_netmask")
                    key: "wireless.address"
                    inputHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                    placeholder: "10.42.0.1/24"
                }
                SettingText {
                    visible: advancedRow.open
                    title: qsTrId("settings.wireless.dhcp_range")
                    description: qsTrId("settings.wireless.empty_automatic")
                    key: "wireless.dhcpRange"
                    inputHints: Qt.ImhPreferNumbers | Qt.ImhNoPredictiveText
                    placeholder: "10.42.0.10-10.42.0.254"
                }
            }
            Column {
                width: parent.width
                spacing: Theme.spacing / 2
                visible: page.hasBluetooth
                SectionTitle { text: qsTrId("settings.wireless.phone_calls_via_bluetooth") }
                SettingChoice {
                    title: qsTrId("settings.wireless.bluetooth_adapter")
                    description: qsTrId("settings.wireless.pair_phone_via_bluetooth")
                    key: "bluetooth.adapter"
                    options: [{ value: "none", label: qsTrId("settings.wireless.none") }, { value: "local", label: qsTrId("settings.wireless.local") }]
                }
                SettingText {
                    visible: settings.values["bluetooth.adapter"] === "local"
                    title: qsTrId("settings.wireless.adapter_address")
                    description: qsTrId("settings.wireless.mac_address_e_g")
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
            SettingRow {
                title: qsTrId("settings.network.address")
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
            // why some settings are missing
            SettingRow {
                visible: !page.hasWifi || !page.hasBluetooth
                title: qsTrId("settings.info.hardware")
                description: [!page.hasWifi ? qsTrId("settings.info.no_wifi_card_found") : "",
                              !page.hasBluetooth ? qsTrId("settings.info.no_bluetooth_adapter_found") : ""]
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
    }
}
