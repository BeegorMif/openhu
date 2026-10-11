import QtQuick
import QtQuick.Controls
import OpenHU 1.0
import QtQuick.Layouts
import "."

Item {
    id: home

    signal openAndroidAuto()

    property date now: new Date()

    BusTopic { id: projection; topic: "projection.state" }
    readonly property bool connected: projection.data.connected === true

    Timer {
        interval: 1000
        running: home.visible
        repeat: true
        onTriggered: home.now = new Date()
    }

    // Clock next to the widgets, in portrait above them
    GridLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 1.5
        columns: home.width >= home.height ? 2 : 1
        columnSpacing: Theme.spacing * 1.5
        rowSpacing: Theme.spacing * 1.5

        // Clock
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 45
            Layout.preferredHeight: 30
            spacing: 0

            Item { Layout.fillHeight: true }
            Text {
                text: Qt.formatTime(home.now, "HH:mm")
                color: Theme.text
                font.pixelSize: Theme.fontHuge
                font.weight: Font.Light
            }
            Text {
                text: home.now.toLocaleDateString(Qt.locale("de_DE"), "dddd, d. MMMM")
                color: Theme.textDim
                font.pixelSize: Theme.fontLarge
            }
            Item { Layout.fillHeight: true }
        }

        // Widgets (Android Auto connected) or connection hint
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 55
            Layout.preferredHeight: 70
            spacing: Theme.spacing

            NavigationCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: home.connected

                // Android Auto with the navigation app (Maps) in front
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        actions.trigger("aa.navigation")
                        home.openAndroidAuto()
                    }
                }
            }

            NowPlayingCard {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: home.connected

                // Android Auto with the last media app (e.g. Spotify) in front
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        actions.trigger("aa.media")
                        home.openAndroidAuto()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !home.connected
                radius: Theme.radius
                color: Theme.surface

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing * 1.5
                    spacing: Theme.spacing

                    Image {
                        source: "qrc:/aa-hot.png"
                        Layout.preferredWidth: Theme.fontHuge * 0.7
                        Layout.preferredHeight: Layout.preferredWidth
                        fillMode: Image.PreserveAspectFit
                    }
                    Text {
                        text: "Android Auto"
                        color: Theme.text
                        font.pixelSize: Theme.fontLarge
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Telefon per USB anschließen oder kabellos verbinden."
                        color: Theme.textDim
                        font.pixelSize: Theme.fontNormal
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text !== ""
                        wrapMode: Text.WordWrap
                        text: {
                            const state = wireless.state
                            if (bluetooth.state.pairing)
                                return "Sichtbar als „" + bluetooth.state.adapter + "“: am Telefon unter Bluetooth koppeln."
                            if (!state.running)
                                return ""
                            if (state.bluetooth === "error")
                                return state.bluetoothError
                            if (state.hotspot === "error")
                                return state.hotspotError
                            return state.phaseText || "Kabellos bereit"
                        }
                        color: Theme.text
                        font.pixelSize: Theme.fontSmall
                    }
                    // Ended on the phone: OpenHU waits, connect the last phone again or another paired one
                    Flow {
                        Layout.fillWidth: true
                        visible: wireless.state.running === true && wireless.state.endedByUser === true
                        spacing: Theme.spacing / 2
                        Repeater {
                            model: {
                                const last = wireless.state.lastDevice || ""
                                const phones = bluetooth.devices.filter(d => d.icon === "phone")
                                return phones.filter(d => d.address.toUpperCase() === last)
                                             .concat(phones.filter(d => d.address.toUpperCase() !== last))
                            }
                            Button {
                                readonly property bool last: modelData.address.toUpperCase() === wireless.state.lastDevice
                                text: (last ? "Wieder verbinden: " : "Verbinden: ") + modelData.name
                                highlighted: last
                                onClicked: wireless.connectDevice(modelData.address)
                            }
                        }
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
