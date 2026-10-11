import QtQuick
import OpenHU 1.0
import QtQuick.Controls

// Volume overlay: appears on every volume change (keys, API, steering wheel) and on tap in the status bar.
Rectangle {
    id: panel
    width: Theme.dockWidth * 5
    height: Theme.dockWidth * 1.1
    radius: Theme.radius
    color: Theme.surfaceHigh
    border.color: Theme.outline
    opacity: 0
    visible: opacity > 0

    function show() {
        opacity = 1
        hideTimer.restart()
    }

    Behavior on opacity { NumberAnimation { duration: 180 } }

    Timer {
        id: hideTimer
        interval: 2500
        onTriggered: if (!slider.pressed) panel.opacity = 0
    }

    Connections {
        target: volume
        function onChanged() { panel.show() }
    }

    Row {
        anchors.centerIn: parent
        spacing: Theme.spacing

        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: "qrc:/volume-hot.png"
            width: Theme.fontLarge
            height: width
            fillMode: Image.PreserveAspectFit
            opacity: volume.muted ? 0.4 : 1

            MouseArea {
                anchors.fill: parent
                anchors.margins: -Theme.spacing
                onClicked: { volume.toggleMute(); panel.show() }
            }
        }

        Slider {
            id: slider
            anchors.verticalCenter: parent.verticalCenter
            width: panel.width - Theme.fontLarge * 4.5
            from: 0
            to: 100
            stepSize: 1
            value: volume.volume
            onMoved: { volume.volume = value; panel.show() }
            onPressedChanged: if (!pressed) hideTimer.restart()
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.fontLarge * 2
            text: volume.muted ? "–" : volume.volume
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            horizontalAlignment: Text.AlignRight
        }
    }
}
