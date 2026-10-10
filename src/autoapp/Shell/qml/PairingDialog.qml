import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import OpenHU 1.0
import "."

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
        height: content.implicitHeight + Theme.spacing * 3
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.outline

        ColumnLayout {
            id: content
            anchors.fill: parent
            anchors.margins: Theme.spacing * 1.5
            spacing: Theme.spacing

            Text {
                Layout.fillWidth: true
                text: "Bluetooth-Kopplung"
                color: Theme.text
                font.pixelSize: Theme.fontLarge
            }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: bluetooth.state.pairingPasskey
                      ? "„" + bluetooth.state.pairingDevice + "“ möchte sich koppeln. Zeigt das Gerät denselben Code?"
                      : "„" + bluetooth.state.pairingDevice + "“ möchte sich koppeln."
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: !!bluetooth.state.pairingPasskey
                text: bluetooth.state.pairingPasskey || ""
                color: Theme.text
                font.pixelSize: Theme.fontHuge * 0.6
                font.letterSpacing: Theme.spacing / 2
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: Theme.spacing / 2
                Button {
                    text: "Ablehnen"
                    onClicked: bluetooth.confirmPairing(false)
                }
                Button {
                    text: "Koppeln"
                    highlighted: true
                    onClicked: bluetooth.confirmPairing(true)
                }
            }
        }
    }
}
