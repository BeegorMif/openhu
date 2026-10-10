import QtQuick 2.15
import OpenHU 1.0
import QtQuick.Layouts 1.15
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

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 1.5
        spacing: Theme.spacing * 1.5

        // Clock
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 45
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
                        source: "qrc:/aausb-hot.png"
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
                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
