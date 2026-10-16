import QtQuick
import QtQuick.Window
import QtQuick.VirtualKeyboard
import QtQuick.VirtualKeyboard.Settings

// On-screen keyboard (Qt Virtual Keyboard) at the bottom, slides in when a text field gets focus. Loaded by
// Main.qml only where the input method is active (autoapp.cpp), so the shell works without the module too.
InputPanel {
    id: panel

    // Settings -> General -> On-screen keyboard (off with a real keyboard)
    property bool allowed: true
    // language of OpenHU (general.language): the layout follows it, no language key on the keyboard
    property string language: "de"
    onLanguageChanged: applyLanguage()
    function applyLanguage() {
        // <language>_<LANGUAGE> (de_DE, fr_FR; English: en_US), else another layout of the language (sv_SE).
        // availableLocales can still be empty here: then the first one.
        const available = VirtualKeyboardSettings.availableLocales
        const preferred = language === "en" ? "en_US" : language + "_" + language.toUpperCase()
        const locale = available.length === 0 || available.indexOf(preferred) >= 0 ? preferred
                     : available.find(name => name.startsWith(language + "_")) || preferred
        VirtualKeyboardSettings.activeLocales = [locale]
        VirtualKeyboardSettings.locale = locale
    }
    readonly property bool shown: active && allowed
    // what the shell above has to leave free (read by Main.qml)
    readonly property real occupied: shown ? height : 0

    // The height follows the width (about 0.31 of it): at most 40 % of the screen, so the page keeps room for the
    // field above; the keys stay large enough (about 75 px at 1280x720). Centred.
    // (sized by the window: the Loader in Main.qml has no size of its own, it would stretch the panel otherwise)
    width: Math.min(Window.width, Window.height * 0.4 / 0.31)
    x: (Window.width - width) / 2
    y: shown ? Window.height - height : Window.height
    visible: y < Window.height

    Behavior on y {
        NumberAnimation { duration: 150; easing.type: Easing.OutQuad }
    }

    Component.onCompleted: {
        // Only one layout is active, so the language (globe) key would only be greyed out: hide it (the other
        // keys of its row take its width). The hide key stays. Long press on a key would list hidden functions, but
        // changing the language is not available with one layout, so nothing is listed.
        VirtualKeyboardSettings.visibleFunctionKeys = QtVirtualKeyboard.KeyboardFunctionKeys.Hide
        applyLanguage()
    }
}
