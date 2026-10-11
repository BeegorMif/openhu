import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Segmented choice: options [{value, label}].
SettingRow {
    id: choice
    property string key
    property var options: []

    Row {
        spacing: Theme.spacing / 3
        Repeater {
            model: choice.options
            Button {
                text: modelData.label
                checkable: true
                checked: settings.values[choice.key] === modelData.value
                highlighted: checked
                onClicked: settings.set(choice.key, modelData.value)
            }
        }
    }
}
