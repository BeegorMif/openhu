import QtQuick
import OpenHU 1.0
import "."

Rectangle {
    id: dock
    color: Theme.surface

    property string currentPage
    signal pageSelected(string name)

    BusTopic { id: projection; topic: "projection.state" }
    BusTopic { id: transport; topic: "projection.transport" }

    Column {
        anchors.top: parent.top
        anchors.topMargin: Theme.spacing
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.spacing / 2

        DockButton {
            icon: "qrc:/home-hot.png"
            label: "Start"
            active: dock.currentPage === "home"
            onClicked: dock.pageSelected("home")
        }
        DockButton {
            // connection type while connected, otherwise only the logo
            icon: projection.data.connected !== true ? "qrc:/aa-hot.png"
                  : transport.data.transport === "wifi" ? "qrc:/aawifi-hot.png" : "qrc:/aausb-hot.png"
            label: "Auto"
            active: dock.currentPage === "aa"
            badge: aaBridge.videoActive
            onClicked: dock.pageSelected("aa")
        }

        // Plugins with "ui": {"dock": true}
        Repeater {
            model: plugins.plugins
            DockButton {
                visible: modelData.enabled && modelData.dock && modelData.page !== ""
                icon: modelData.icon || "qrc:/list-hot.png"
                label: modelData.name
                active: dock.currentPage === "plugin:" + modelData.id
                onClicked: dock.pageSelected("plugin:" + modelData.id)
            }
        }

    }

    Column {
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: Theme.spacing / 2

        DockButton {
            icon: "qrc:/settings-hot.png"
            label: "Einstellungen"
            active: dock.currentPage === "settings"
            onClicked: dock.pageSelected("settings")
        }
    }
}
