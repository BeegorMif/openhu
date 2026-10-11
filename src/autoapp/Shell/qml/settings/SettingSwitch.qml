import QtQuick
import QtQuick.Controls

// Switch for a boolean setting (settings.values[key]).
SettingRow {
    property string key

    Switch {
        checked: !!settings.values[key]
        onToggled: settings.set(key, checked)
    }
}
