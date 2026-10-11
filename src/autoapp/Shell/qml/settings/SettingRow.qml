import QtQuick
import QtQuick.Layouts
import OpenHU 1.0

// One setting: title and description on the left, control on the right.
Rectangle {
    id: row
    width: parent ? parent.width : 0
    implicitHeight: Math.max(Theme.fontNormal * 3.6, texts.implicitHeight + Theme.spacing * 1.4)
    radius: Theme.radius / 2
    color: Theme.surface

    property string title
    property string description
    default property alias content: control.data

    ColumnLayout {
        id: texts
        anchors.left: parent.left
        anchors.right: control.left
        anchors.leftMargin: Theme.spacing
        anchors.rightMargin: Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        Text {
            Layout.fillWidth: true
            text: row.title
            color: Theme.text
            font.pixelSize: Theme.fontNormal
            elide: Text.ElideRight
        }
        Text {
            Layout.fillWidth: true
            visible: row.description !== ""
            text: row.description
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }
    }

    Item {
        id: control
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing / 2
        anchors.verticalCenter: parent.verticalCenter
        width: childrenRect.width
        height: childrenRect.height
    }
}
