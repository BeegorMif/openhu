import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Bluetooth pairing request: the phone shows the same code, the user confirms on both sides.
Rectangle {
    id: dialog
    color: "#b0000000"
    visible: bluetooth.state.pairingDevice !== undefined && bluetooth.state.pairingDevice !== ""

    // swallow touches on the shell behind the dialog
    MouseArea { anchors.fill: parent }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing * 4, Theme.dockWidth * 6)
        height: content.height + Theme.spacing * 3
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.outline

        // Column, not ColumnLayout: the height of the box follows the wrapped text without a binding loop
        Column {
            id: content
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.spacing * 1.5
            spacing: Theme.spacing

            Text {
                width: parent.width
                text: qsTr("Bluetooth pairing")
                color: Theme.text
                font.pixelSize: Theme.fontLarge
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: bluetooth.state.pairingPasskey
                      ? qsTr("“%1” wants to pair. Does the device show the same code?").arg(bluetooth.state.pairingDevice)
                      : qsTr("“%1” wants to pair.").arg(bluetooth.state.pairingDevice)
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !!bluetooth.state.pairingPasskey
                text: bluetooth.state.pairingPasskey || ""
                color: Theme.text
                font.pixelSize: Theme.fontHuge * 0.6
                font.letterSpacing: Theme.spacing / 2
            }
            Row {
                anchors.right: parent.right
                spacing: Theme.spacing / 2
                Button {
                    text: qsTr("Decline")
                    onClicked: bluetooth.confirmPairing(false)
                }
                Button {
                    text: qsTr("Pair")
                    highlighted: true
                    onClicked: bluetooth.confirmPairing(true)
                }
            }
        }
    }
}
