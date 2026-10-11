import QtQuick
import OpenHU 1.0

Item {
    id: button
    width: Theme.dockWidth - Theme.spacing
    height: width

    property alias icon: image.source
    property string label
    property bool active: false
    property bool badge: false
    signal clicked()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: button.active ? Theme.surfaceHigh : (mouse.pressed ? Theme.outline : "transparent")
    }

    Image {
        id: image
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: parent.height * 0.14
        width: parent.width * 0.42
        height: width
        fillMode: Image.PreserveAspectFit
        smooth: true
        opacity: button.active ? 1 : 0.75
    }

    Rectangle {
        visible: button.badge
        width: Theme.spacing * 0.6
        height: width
        radius: width / 2
        color: Theme.success
        anchors.right: image.right
        anchors.top: image.top
    }

    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: parent.height * 0.1
        width: parent.width - 4
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        text: button.label
        color: button.active ? Theme.primary : Theme.textDim
        font.pixelSize: Theme.fontSmall * 0.85
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: button.clicked()
    }
}
