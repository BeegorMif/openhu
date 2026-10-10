import QtQuick 2.15
import OpenHU 1.0
import "."

Rectangle {
    id: dock
    color: Theme.surface

    property string currentPage
    signal pageSelected(string name)

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
            icon: "qrc:/aausb-hot.png"
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

        DockButton {
            icon: "qrc:/list-hot.png"
            label: "Apps"
            active: dock.currentPage === "apps" || dock.currentPage === "store"
            onClicked: dock.pageSelected("apps")
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
            onClicked: shell.openSettings()
        }
        DockButton {
            icon: "qrc:/power-hot.png"
            label: "Beenden"
            onClicked: shell.quit()
        }
    }
}
