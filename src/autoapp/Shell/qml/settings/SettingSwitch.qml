import QtQuick 2.15
import QtQuick.Controls 2.15

// Switch for a boolean setting (settings.values[key]).
SettingRow {
    property string key

    Switch {
        checked: !!settings.values[key]
        onToggled: settings.set(key, checked)
    }
}
