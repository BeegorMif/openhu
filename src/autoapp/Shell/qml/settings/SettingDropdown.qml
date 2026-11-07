import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Drop-down choice for longer lists: options [{value, label}].
SettingRow {
    id: dropdown
    property string key
    // where the value lives: the shell's settings, or a plugin's config store (PluginSettings.qml)
    property var backend: settings
    property var options: []

    ComboBox {
        width: Theme.dockWidth * 2.6
        model: dropdown.options
        textRole: "label"
        valueRole: "value"
        // not indexOfValue(): evaluated before the model is there, and not again afterwards
        currentIndex: dropdown.options.findIndex(option => option.value === dropdown.backend.values[dropdown.key])
        onActivated: dropdown.backend.set(dropdown.key, currentValue)
    }
}
