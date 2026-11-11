import QtQuick
import QtQuick.Controls

// Switch for a boolean setting (backend.values[key]).
SettingRow {
    property string key
    // where the value lives: the shell's settings, or a plugin's config store (PluginSettings.qml)
    property var backend: settings

    Switch {
        checked: !!backend.values[key]
        onToggled: {
            const error = backend.set(key, checked)
            if (error) {
                shell.toast(error, 4000)
                checked = Qt.binding(() => !!backend.values[key])
            }
        }
    }
}
