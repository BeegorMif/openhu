import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OpenHU 1.0
import "../PluginText.js" as PluginText

// Settings of a plugin (Settings → Apps → Installed → Settings, ui.navigate "settings/apps/<id>", openhu#152): the
// "settings" of its manifest, drawn with the shell's own setting rows. The values live in the plugin's config store
// (topic plugin.config.<id>, plugin.config.set), also while the plugin is disabled.
Item {
    id: view

    property string pluginId
    readonly property var plugin: plugins.plugins.find(p => p.id === pluginId) || null
    readonly property var schema: plugin && plugin.settings ? plugin.settings : []

    signal back()

    // the plugin's config store, used by the rows instead of the shell's settings
    QtObject {
        id: pluginStore
        property var values: ({})

        function refresh() {
            values = eventBus.last("plugin.config." + view.pluginId).values || {}
        }
        // empty or the reason (shown as a toast by the rows)
        function set(key, value) {
            const reply = api.request("plugin.config.set", { id: view.pluginId, key: key, value: value })
            return reply.error || ""
        }
    }
    onPluginIdChanged: pluginStore.refresh()
    Component.onCompleted: pluginStore.refresh()
    Connections {
        target: eventBus
        function onEvent(topic, data) {
            if (topic === "plugin.config." + view.pluginId)
                pluginStore.values = data.values || {}
        }
    }

    // text of the plugin's translation ("<id>.settings.<key>[.<suffix>]"), else the manifest's English text
    function pluginText(key, suffix, fallback) {
        const id = view.pluginId + ".settings." + key + (suffix ? "." + suffix : "")
        const translated = qsTrId(id)
        return translated !== id ? translated : (fallback || "")
    }
    function title(entry) {
        return pluginText(entry.key, "", entry.title) || entry.key
    }
    function description(entry) {
        const text = pluginText(entry.key, "description", entry.description)
        return entry.applies === "restart" ? (text ? text + " · " : "") + qsTrId("settings.plugin.restarts_plugin") : text
    }
    function options(entry) {
        return (entry.options || []).map(option => ({ value: option, label: pluginText(entry.key, option, option) }))
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacing
            spacing: Theme.spacing

            Button {
                text: qsTrId("settings.plugin.back")
                flat: true
                onClicked: view.back()
            }
            Text {
                Layout.fillWidth: true
                text: view.plugin ? PluginText.name(view.plugin) : view.pluginId
                color: Theme.text
                font.pixelSize: Theme.fontLarge
                font.bold: true
                elide: Text.ElideRight
            }
            ConfirmButton {
                visible: view.schema.length > 0
                label: qsTrId("settings.plugin.reset")
                confirmText: qsTrId("settings.plugin.reset_confirm")
                onConfirmed: {
                    const reply = api.request("plugin.config.reset", { id: view.pluginId })
                    if (reply.error)
                        shell.toast(reply.error, 4000)
                }
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacing * 1.5
            Layout.rightMargin: Theme.spacing * 1.5
            visible: text !== ""
            text: !view.plugin ? qsTrId("settings.plugin.not_installed")
                : !view.plugin.enabled ? qsTrId("settings.plugin.disabled_hint") : ""
            color: Theme.textDim
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
        }

        SettingsSection {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Repeater {
                model: view.schema

                // one row: only the loader of its type is active
                Column {
                    id: row
                    required property var modelData
                    readonly property string type: modelData.type
                    width: parent ? parent.width : 0

                    Loader {
                        active: row.type === "section"
                        width: row.width
                        sourceComponent: SectionTitle { text: view.title(row.modelData) }
                    }
                    Loader {
                        active: row.type === "switch"
                        width: row.width
                        sourceComponent: SettingSwitch {
                            backend: pluginStore
                            key: row.modelData.key
                            title: view.title(row.modelData)
                            description: view.description(row.modelData)
                        }
                    }
                    Loader {
                        active: row.type === "number"
                        width: row.width
                        sourceComponent: SettingNumber {
                            backend: pluginStore
                            key: row.modelData.key
                            title: view.title(row.modelData)
                            description: view.description(row.modelData)
                            from: row.modelData.min
                            to: row.modelData.max
                            stepSize: row.modelData.step || 1
                        }
                    }
                    Loader {
                        active: row.type === "text"
                        width: row.width
                        sourceComponent: SettingText {
                            backend: pluginStore
                            key: row.modelData.key
                            title: view.title(row.modelData)
                            description: view.description(row.modelData)
                            placeholder: row.modelData.placeholder || ""
                        }
                    }
                    // a few options as buttons, more in a drop-down
                    Loader {
                        active: row.type === "choice" && (row.modelData.options || []).length <= 4
                        width: row.width
                        sourceComponent: SettingChoice {
                            backend: pluginStore
                            key: row.modelData.key
                            title: view.title(row.modelData)
                            description: view.description(row.modelData)
                            options: view.options(row.modelData)
                        }
                    }
                    Loader {
                        active: row.type === "choice" && (row.modelData.options || []).length > 4
                        width: row.width
                        sourceComponent: SettingDropdown {
                            backend: pluginStore
                            key: row.modelData.key
                            title: view.title(row.modelData)
                            description: view.description(row.modelData)
                            options: view.options(row.modelData)
                        }
                    }
                }
            }
        }
    }
}
