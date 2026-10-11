import QtQuick
import OpenHU 1.0
import "."

Rectangle {
    id: bar
    color: Theme.background

    signal volumeClicked()

    property date now: new Date()

    BusTopic { id: projection; topic: "projection.state" }
    BusTopic { id: phone; topic: "phone.status" }

    readonly property bool connected: projection.data.connected === true
    readonly property bool inCall: !!phone.data.callState && phone.data.callState !== "idle" && phone.data.callState !== "inactive"
    // signal_strength from the phone: 0-4 bars, larger values treated as percent
    readonly property int signalLevel: {
        var signal = phone.data.signal || 0
        return signal > 4 ? Math.min(4, Math.round(signal / 25)) : signal
    }

    Timer {
        interval: 1000
        running: true
        repeat: true
        onTriggered: bar.now = new Date()
    }

    // Volume (left)
    Row {
        id: volumeRow
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacing / 2

        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/volume-hot.png"
            width: Theme.fontNormal
            height: width
            fillMode: Image.PreserveAspectFit
            opacity: volume.muted ? 0.4 : 0.9
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: volume.muted ? "Stumm" : volume.volume + " %"
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
        }
    }

    MouseArea {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: volumeRow.width + Theme.spacing * 2
        onClicked: bar.volumeClicked()
    }

    Text {
        anchors.centerIn: parent
        text: Qt.formatTime(bar.now, "HH:mm")
        color: Theme.text
        font.pixelSize: Theme.fontNormal
        font.bold: true
    }

    // Phone status (right)
    Row {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacing / 2

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: bar.inCall
            text: {
                var call = (phone.data.calls || [])[0] || {}
                return call.name || call.number || ""
            }
            color: Theme.success
            font.pixelSize: Theme.fontSmall
        }
        // sound goes to the Bluetooth car radio (the phone has its own icon)
        BluetoothIcon {
            anchors.verticalCenter: parent.verticalCenter
            visible: carAudio.state.connected === true
        }
        SignalBars {
            anchors.verticalCenter: parent.verticalCenter
            visible: bar.connected && bar.signalLevel > 0
            level: bar.signalLevel
        }
        PhoneIcon {
            id: phoneIcon
            anchors.verticalCenter: parent.verticalCenter
            crossed: !bar.connected
            color: bar.inCall ? Theme.success : (bar.connected ? Theme.text : Theme.textDim)
        }
    }

    // Tap on the phone: reconnect (USB reset)
    MouseArea {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: phoneIcon.width + Theme.spacing * 2
        onClicked: actions.trigger("aa.reconnect")
    }
}
