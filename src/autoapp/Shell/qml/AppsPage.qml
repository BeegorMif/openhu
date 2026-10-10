import QtQuick 2.15
import OpenAuto 1.0
import "."

// Installed plugin apps and the store.
Item {
    id: apps

    signal openPage(string name)

    readonly property var tiles: {
        var list = [{ id: "", name: "Store", icon: "qrc:/list-hot.png", page: "store" }]
        var installed = plugins.plugins
        for (var i = 0; i < installed.length; ++i) {
            var plugin = installed[i]
            if (plugin.enabled && plugin.page !== "")
                list.push({ id: plugin.id, name: plugin.name, icon: plugin.icon, page: "plugin:" + plugin.id })
        }
        return list
    }

    GridView {
        id: grid
        anchors.fill: parent
        anchors.margins: Theme.spacing * 1.5
        cellWidth: Theme.dockWidth * 1.6
        cellHeight: Theme.dockWidth * 1.6
        clip: true
        model: apps.tiles

        delegate: Item {
            width: grid.cellWidth
            height: grid.cellHeight

            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.spacing / 2
                radius: Theme.radius
                color: tileMouse.pressed ? Theme.surfaceHigh : Theme.surface

                Image {
                    id: tileIcon
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: parent.height * 0.18
                    width: parent.width * 0.38
                    height: width
                    source: modelData.icon || ""
                    fillMode: Image.PreserveAspectFit
                    visible: status === Image.Ready
                }
                Text {
                    anchors.centerIn: tileIcon
                    visible: !tileIcon.visible
                    text: modelData.name.charAt(0).toUpperCase()
                    color: Theme.primary
                    font.pixelSize: Theme.fontHuge * 0.45
                    font.bold: true
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: parent.height * 0.12
                    width: parent.width - Theme.spacing
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: modelData.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSmall
                }

                MouseArea {
                    id: tileMouse
                    anchors.fill: parent
                    onClicked: apps.openPage(modelData.page)
                }
            }
        }
    }
}
