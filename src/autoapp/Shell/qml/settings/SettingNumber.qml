import QtQuick 2.15
import QtQuick.Controls 2.15
import OpenHU 1.0

// Number setting, applied when the value changes.
SettingRow {
    id: number
    property string key
    property int from: 0
    property int to: 1000
    property int stepSize: 1

    SpinBox {
        from: number.from
        to: number.to
        stepSize: number.stepSize
        editable: true
        value: settings.values[number.key] || 0
        onValueModified: settings.set(number.key, value)
        // plain numbers (no thousands separator for ports)
        textFromValue: function(value) { return value.toString() }
        valueFromText: function(text) { return parseInt(text) || 0 }
        width: Theme.dockWidth * 2.2
    }
}
