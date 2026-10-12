import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Text setting, applied with Enter or when the field loses focus.
SettingRow {
    id: textSetting
    property string key
    property string placeholder
    // for the on-screen keyboard: no suggestions or capitals by default (names, addresses, codes)
    property int inputHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase

    TextField {
        width: Theme.dockWidth * 3
        placeholderText: textSetting.placeholder
        inputMethodHints: textSetting.inputHints
        // Enter: done, the on-screen keyboard closes
        onAccepted: focus = false
        text: String(settings.values[textSetting.key] || "")
        onEditingFinished: {
            if (text === String(settings.values[textSetting.key] || ""))
                return
            const error = settings.set(textSetting.key, text)
            if (error) {
                shell.toast(error, 4000)
                text = Qt.binding(() => String(settings.values[textSetting.key] || ""))
            }
        }
    }
}
