pragma Singleton
import QtQuick

QtObject {
    // Set by Main.qml from the window size; all sizes are designed for 1280x720.
    property real scale: 1

    readonly property color background: "#0f1317"
    readonly property color surface: "#192027"
    readonly property color surfaceHigh: "#232c35"
    readonly property color outline: "#2f3a45"
    readonly property color primary: "#4f9cf9"
    readonly property color primaryText: "#08111c"
    readonly property color text: "#e8eef4"
    readonly property color textDim: "#93a1ae"
    readonly property color success: "#4cc38a"

    readonly property int dockWidth: Math.round(104 * scale)
    readonly property int statusHeight: Math.round(52 * scale)
    readonly property int radius: Math.round(18 * scale)
    readonly property int spacing: Math.round(16 * scale)

    readonly property int fontSmall: Math.round(15 * scale)
    readonly property int fontNormal: Math.round(19 * scale)
    readonly property int fontLarge: Math.round(28 * scale)
    readonly property int fontHuge: Math.round(96 * scale)
}
