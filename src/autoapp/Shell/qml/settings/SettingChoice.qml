import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Segmented choice: options [{value, label}].
SettingRow {
    id: choice
    property string key
    // where the value lives: the shell's settings, or a plugin's config store (PluginSettings.qml)
    property var backend: settings
    property var options: []

    Row {
        spacing: Theme.spacing / 3
        Repeater {
            model: choice.options
            Button {
                text: modelData.label
                checkable: true
                checked: choice.backend.values[choice.key] === modelData.value
                highlighted: checked
                onClicked: choice.backend.set(choice.key, modelData.value)
            }
        }
    }
}
