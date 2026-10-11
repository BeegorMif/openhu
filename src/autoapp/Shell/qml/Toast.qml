import QtQuick
import OpenHU 1.0

// Short message at the bottom of the screen (API: ui.toast).
Rectangle {
    id: toast
    width: Math.min(label.implicitWidth + Theme.spacing * 3, parent ? parent.width * 0.8 : 600)
    height: label.implicitHeight + Theme.spacing * 1.5
    radius: height / 2
    color: Theme.surfaceHigh
    border.color: Theme.outline
    opacity: 0
    visible: opacity > 0

    function show(text, duration) {
        label.text = text
        hideTimer.interval = duration > 0 ? duration : 3000
        opacity = 1
        hideTimer.restart()
    }

    Behavior on opacity { NumberAnimation { duration: 180 } }

    Timer {
        id: hideTimer
        onTriggered: toast.opacity = 0
    }

    Text {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, toast.width - Theme.spacing * 3)
        elide: Text.ElideRight
        color: Theme.text
        font.pixelSize: Theme.fontNormal
    }
}
