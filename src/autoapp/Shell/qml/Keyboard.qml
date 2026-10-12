import QtQuick
import QtQuick.Window
import QtQuick.VirtualKeyboard
import QtQuick.VirtualKeyboard.Settings
import OpenHU 1.0

// On-screen keyboard (Qt Virtual Keyboard) at the bottom, slides in when a text field gets focus. Loaded by
// Main.qml only where the input method is active (autoapp.cpp), so the shell works without the module too.
InputPanel {
    id: panel

    // Settings -> Allgemein -> Bildschirmtastatur (off with a real keyboard)
    property bool allowed: true
    readonly property bool shown: active && allowed
    // what the shell above has to leave free (read by Main.qml)
    readonly property real occupied: shown ? height : 0

    // Everything right of the dock, which stays usable; the height follows the width (about 0.31 of it).
    // (sized by the window: the Loader in Main.qml has no size of its own, it would stretch the panel otherwise)
    width: Window.width - Theme.dockWidth
    x: Theme.dockWidth
    y: shown ? Window.height - height : Window.height
    visible: y < Window.height

    Behavior on y {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }

    Component.onCompleted: {
        VirtualKeyboardSettings.activeLocales = ["de_DE", "en_US"]
        VirtualKeyboardSettings.locale = "de_DE"
    }
}
