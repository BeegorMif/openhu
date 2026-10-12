import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Settings → Audio: volume, output, sound to the Bluetooth car radio.
SettingsSection {
    id: audioSettings

    readonly property bool hasBluetooth: !!bluetooth.state.available

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
        visible: audioSettings.hasBluetooth
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
