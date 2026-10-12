import QtQuick

// Shown from the start of autoapp until the shell's first frame, in a window of its own. It continues the boot
// splash of OpenHU OS (Plymouth theme "openhu" in average-web-dev/openhu-os, splash/theme/openhu.script): same
// logo, size, position and dots, so the change from the splash to OpenHU is not visible. Colors of Theme.qml
// (the OpenHU module is not registered yet when this is loaded).
Rectangle {
    id: root
    color: "#0f1317"

    // 40 % of the screen width, at most the image's own size
    readonly property real logoWidth: Math.min(width * 0.4, logo.sourceSize.width)
    // the dot image of the splash is 48 px with a circle of 40 px in it
    readonly property real dotSize: Math.max(8, logo.height * 0.12)

    Image {
        id: logo
        source: "qrc:/startup-logo.png"
        width: root.logoWidth
        height: sourceSize.width > 0 ? sourceSize.height * width / sourceSize.width : 0
        x: (root.width - width) / 2
        y: (root.height - height) / 2 - height * 0.3
        smooth: true
        mipmap: true
    }

    Repeater {
        model: 3

        Rectangle {
            required property int index
            width: root.dotSize * 40 / 48
            height: width
            radius: width / 2
            color: "#4f9cf9"
            x: root.width / 2 + (index - 1) * root.dotSize * 1.8 - width / 2
            y: logo.y + logo.height * 1.6 + (root.dotSize - height) / 2
            opacity: 0.25

            // One dot after the other, a cycle of 1.5 s. Animators run in the render thread, so the dots keep
            // pulsing while the GUI thread starts the rest of OpenHU.
            SequentialAnimation {
                running: true
                loops: Animation.Infinite
                PauseAnimation { duration: index * 500 }
                OpacityAnimator { from: 0.25; to: 1; duration: 250; easing.type: Easing.InOutSine }
                OpacityAnimator { from: 1; to: 0.25; duration: 250; easing.type: Easing.InOutSine }
                PauseAnimation { duration: 1000 - index * 500 }
            }
        }
    }
}
