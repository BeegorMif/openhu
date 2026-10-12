import QtQuick
import QtQuick.Controls

// Button that asks once more: the first tap shows confirmText for a few seconds, the second one emits confirmed().
// With confirm: false it acts on the first tap.
Button {
    id: button

    property string label
    property string confirmText
    property bool confirm: true
    property bool armed: false

    signal confirmed()

    text: armed ? confirmText : label
    highlighted: armed
    onClicked: {
        if (!confirm || armed) {
            armed = false
            disarm.stop()
            confirmed()
        } else {
            armed = true
            disarm.restart()
        }
    }

    Timer {
        id: disarm
        interval: 4000
        onTriggered: button.armed = false
    }
}
