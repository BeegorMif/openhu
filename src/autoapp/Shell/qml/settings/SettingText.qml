import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Text setting, applied with Enter or when the field loses focus.
SettingRow {
    id: textSetting
    property string key
    // where the value lives: the shell's settings, or a plugin's config store (PluginSettings.qml)
    property var backend: settings
    property string placeholder
    // for the on-screen keyboard: no suggestions or capitals by default (names, addresses, codes)
    property int inputHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase

    TextField {
        width: Theme.dockWidth * 3
        placeholderText: textSetting.placeholder
        inputMethodHints: textSetting.inputHints
        // Enter: done, the on-screen keyboard closes
        onAccepted: focus = false
        text: String(textSetting.backend.values[textSetting.key] || "")
        onEditingFinished: {
            if (text === String(textSetting.backend.values[textSetting.key] || ""))
                return
            const error = textSetting.backend.set(textSetting.key, text)
            if (error) {
                shell.toast(error, 4000)
                text = Qt.binding(() => String(textSetting.backend.values[textSetting.key] || ""))
            }
        }
    }
}
