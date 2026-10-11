import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Text setting, applied with Enter or when the field loses focus.
SettingRow {
    id: textSetting
    property string key
    property string placeholder

    TextField {
        width: Theme.dockWidth * 3
        placeholderText: textSetting.placeholder
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
