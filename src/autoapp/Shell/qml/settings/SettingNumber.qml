import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Number setting, applied when the value changes.
SettingRow {
    id: number
    property string key
    // where the value lives: the shell's settings, or a plugin's config store (PluginSettings.qml)
    property var backend: settings
    property int from: 0
    property int to: 1000
    property int stepSize: 1

    SpinBox {
        from: number.from
        to: number.to
        stepSize: number.stepSize
        editable: true
        inputMethodHints: Qt.ImhDigitsOnly
        value: number.backend.values[number.key] || 0
        onValueModified: {
            const error = number.backend.set(number.key, value)
            if (error) {
                shell.toast(error, 4000)
                value = Qt.binding(() => number.backend.values[number.key] || 0)
            }
        }
        // plain numbers (no thousands separator for ports)
        textFromValue: function(value) { return value.toString() }
        valueFromText: function(text) { return parseInt(text) || 0 }
        width: Theme.dockWidth * 2.2
    }
}
