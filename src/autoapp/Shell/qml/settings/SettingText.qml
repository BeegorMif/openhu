import QtQuick 2.15
import QtQuick.Controls 2.15
import OpenHU 1.0

// Text setting, applied with Enter or when the field loses focus.
SettingRow {
    id: textSetting
    property string key
    property string placeholder

    TextField {
        width: Theme.dockWidth * 3
        placeholderText: textSetting.placeholder
        text: settings.values[textSetting.key] || ""
        onEditingFinished: if (text !== (settings.values[textSetting.key] || "")) settings.set(textSetting.key, text)
    }
}
