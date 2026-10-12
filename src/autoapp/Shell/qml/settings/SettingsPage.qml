import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OpenHU 1.0

// Settings: categories on the left, the category on the right (one file each). Every change is saved immediately.
Item {
    id: page

    // the category shown; kept by Main.qml while OpenHU runs, set by ui.navigate "settings/<category>"
    property string category: "general"
    // the hardware: WiFi and Bluetooth only when there is an adapter (also plugged in later)
    readonly property bool hasWifi: wireless.state.wifiAvailable !== false
    readonly property bool hasBluetooth: !!bluetooth.state.available

    readonly property var categories: [
        { id: "general", label: qsTrId("settings.page.general"), file: "GeneralSettings.qml" },
        { id: "aa", label: "Android Auto", file: "AndroidAutoSettings.qml" },
        { id: "audio", label: qsTrId("settings.page.audio"), file: "AudioSettings.qml" }
    ].concat(hasWifi ? [{ id: "wifi", label: qsTrId("settings.page.wifi"), file: "WifiSettings.qml" }] : [],
             hasBluetooth ? [{ id: "bluetooth", label: qsTrId("settings.page.bluetooth"), file: "BluetoothSettings.qml" }] : [], [
        { id: "network", label: qsTrId("settings.page.network"), file: "NetworkSettings.qml" },
        { id: "keys", label: qsTrId("settings.page.keys"), file: "KeymapSettings.qml" },
        { id: "apps", label: qsTrId("settings.page.apps_store"), file: "../StorePage.qml" }
    ], system.available ? [{ id: "system", label: qsTrId("settings.page.system"), file: "SystemSettings.qml" }] : [],
             [{ id: "info", label: qsTrId("settings.page.info"), file: "InfoSettings.qml" }])
    // a category that is not there (no adapter, not on OpenHU OS): General
    readonly property var shown: categories.find(entry => entry.id === category) || categories[0]

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
                        color: page.shown.id === modelData.id ? Theme.selected : (mouse.pressed ? Theme.outline : "transparent")

                        Rectangle {
                            visible: page.shown.id === modelData.id
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
                            color: page.shown.id === modelData.id ? Theme.text : Theme.textDim
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
                source: page.shown.file
            }
        }
    }
}
