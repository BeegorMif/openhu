import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OpenHU 1.0
import "PluginText.js" as PluginText

// Plugin store: packages from the sources in ~/.openhu/sources.list
Item {
    id: page

    property int tab: 0  // 0 available, 1 installed, 2 updates, 3 sources

    readonly property var filtered: {
        var all = store.packages
        var result = []
        for (var i = 0; i < all.length; ++i) {
            var p = all[i]
            if ((tab === 0 && !p.installed) || (tab === 1 && p.installed) || (tab === 2 && p.updateAvailable))
                result.push(p)
        }
        return result
    }

    BusTopic { id: projection; topic: "projection.state" }

    // plugins that provide to Android Auto change only while no phone is connected (#123)
    function locked(id) {
        return projection.data.connected === true && !!page.pluginInfo(id).androidAuto
    }

    function androidAutoText(section) {
        if (!section)
            return ""
        return (section.sensors || []).concat(section.services || []).join(", ")
    }

    // installed plugins that require this one and run (#104); they stop with it
    function dependentsOf(id) {
        return plugins.plugins.filter(p => p.enabled && p.requires && p.requires[id] !== undefined)
                              .map(p => PluginText.name(p))
    }

    // the plugin names of a "requires" map or a "recommends" list
    function namesOf(ids) {
        if (!ids)
            return ""
        var list = Array.isArray(ids) ? ids : Object.keys(ids)
        return list.map(id => { var p = page.pluginInfo(id); return p.id ? PluginText.name(p) : id }).join(", ")
    }

    function missingText(missing) {
        return (missing || []).map(m => (page.pluginInfo(m.id).id ? PluginText.name(page.pluginInfo(m.id)) : m.id)
                                         + (m.reason === "disabled" ? " (" + qsTrId("store.requirement_disabled") + ")"
                                            : m.version ? " " + m.version : "")).join(", ")
    }

    // asks before something that also stops other plugins
    Dialog {
        id: confirmDialog
        property string pluginId
        property string kind  // "disable" or "remove"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: Math.min(parent.width * 0.8, Theme.dockWidth * 8)
        property alias text: confirmText.text
        Text {
            id: confirmText
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }
        onAccepted: page.change(pluginId, kind)
    }

    function change(id, kind) {
        if (kind === "remove")
            store.remove(id)
        else
            api.request("plugin.disable", { id: id, force: true })
    }

    function confirmIfRequired(id, kind) {
        var dependents = page.dependentsOf(id)
        if (dependents.length === 0) {
            page.change(id, kind)
            return
        }
        confirmDialog.text = (kind === "remove" ? qsTrId("store.remove_also_stops") : qsTrId("store.disable_also_stops"))
                             .arg(dependents.join(", "))
        confirmDialog.pluginId = id
        confirmDialog.kind = kind
        confirmDialog.open()
    }

    function pluginInfo(id) {
        var list = plugins.plugins
        for (var i = 0; i < list.length; ++i)
            if (list[i].id === id)
                return list[i]
        return {}
    }

    Component.onCompleted: if (store.packages.length === 0) store.refresh()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 1.5
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            TabBar {
                id: tabs
                Layout.fillWidth: true
                currentIndex: page.tab
                onCurrentIndexChanged: page.tab = currentIndex
                background: Rectangle { color: "transparent" }

                TabButton { text: qsTrId("store.available") }
                TabButton { text: qsTrId("store.installed") }
                TabButton {
                    text: {
                        var count = 0
                        var all = store.packages
                        for (var i = 0; i < all.length; ++i)
                            if (all[i].updateAvailable) ++count
                        return count > 0 ? qsTrId("store.updates_count").arg(count) : qsTrId("store.updates")
                    }
                }
                TabButton { text: qsTrId("store.sources") }
            }

            Button {
                text: qsTrId("store.refresh")
                enabled: !store.busy
                onClicked: store.refresh()
            }
        }

        // Status line
        RowLayout {
            Layout.fillWidth: true
            visible: store.status !== ""
            BusyIndicator {
                running: store.busy
                visible: store.busy
                Layout.preferredWidth: Theme.fontLarge
                Layout.preferredHeight: Theme.fontLarge
            }
            Text {
                Layout.fillWidth: true
                text: store.status
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
            Button {
                visible: page.tab === 2 && page.filtered.length > 1
                text: qsTrId("store.update_all")
                enabled: !store.busy
                onClicked: store.updateAll()
            }
        }

        // Packages
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.tab < 3
            clip: true
            spacing: Theme.spacing / 2
            model: page.filtered

            delegate: Rectangle {
                width: ListView.view.width
                height: row.implicitHeight + Theme.spacing * 1.5
                radius: Theme.radius
                color: Theme.surface

                RowLayout {
                    id: row
                    anchors.fill: parent
                    anchors.margins: Theme.spacing * 0.75
                    spacing: Theme.spacing

                    Rectangle {
                        Layout.preferredWidth: Theme.fontHuge * 0.6
                        Layout.preferredHeight: Layout.preferredWidth
                        radius: Theme.radius / 2
                        color: Theme.surfaceHigh

                        Text {
                            anchors.centerIn: parent
                            text: PluginText.name(modelData).charAt(0).toUpperCase()
                            color: Theme.primary
                            font.pixelSize: Theme.fontLarge
                            font.bold: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2

                        Text {
                            Layout.fillWidth: true
                            text: PluginText.name(modelData) + "  " + (modelData.installed && modelData.updateAvailable
                                    ? modelData.installedVersion + " → " + modelData.version
                                    : (modelData.installedVersion || modelData.version || ""))
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                            font.bold: true
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: text !== ""
                            text: PluginText.description(modelData)
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                // first, the rest of the line is cut off
                                if (modelData.installed && page.locked(modelData.id))
                                    return qsTrId("store.disconnect_phone").arg(PluginText.name(modelData))
                                var missing = modelData.installed && page.pluginInfo(modelData.id).enabled
                                              ? page.pluginInfo(modelData.id).missing : []
                                if (missing && missing.length)
                                    return qsTrId("store.not_started_requires").arg(page.missingText(missing))
                                var parts = []
                                parts.push(modelData.source ? (modelData.official ? qsTrId("store.official") : qsTrId("store.not_official") + " · " + modelData.source)
                                                            : qsTrId("store.installed_locally"))
                                if (modelData.permissions && modelData.permissions.length)
                                    parts.push(qsTrId("store.permissions").arg(modelData.permissions.join(", ")))
                                // an index from before the section: what the installed plugin declares
                                var androidAuto = page.androidAutoText(modelData.androidAuto || page.pluginInfo(modelData.id).androidAuto)
                                if (androidAuto)
                                    parts.push(qsTrId("store.provides_android_auto").arg(androidAuto))
                                var requires = page.namesOf(modelData.requires || page.pluginInfo(modelData.id).requires)
                                if (requires)
                                    parts.push(qsTrId("store.requires").arg(requires))
                                var recommends = page.namesOf(modelData.recommends || page.pluginInfo(modelData.id).recommends)
                                if (recommends)
                                    parts.push(qsTrId("store.works_with").arg(recommends))
                                if (!modelData.compatible)
                                    parts.push(qsTrId("store.not_device_openhu_version"))
                                return parts.join(" · ")
                            }
                            color: modelData.installed && (page.locked(modelData.id)
                                     || (page.pluginInfo(modelData.id).enabled && (page.pluginInfo(modelData.id).missing || []).length))
                                 ? Theme.text
                                 : modelData.official ? Theme.success : Theme.textDim
                            font.pixelSize: Theme.fontSmall * 0.9
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        visible: modelData.installed && page.pluginInfo(modelData.id).page !== undefined
                                 && page.pluginInfo(modelData.id).page !== "" && page.pluginInfo(modelData.id).enabled
                        text: qsTrId("store.open")
                        onClicked: shell.navigate("plugin:" + modelData.id)
                    }
                    Switch {
                        visible: modelData.installed && !!page.pluginInfo(modelData.id).page
                        text: qsTrId("store.dock")
                        checked: !!page.pluginInfo(modelData.id).dock
                        onToggled: api.request("plugin.setDock", { id: modelData.id, dock: checked })
                    }
                    Switch {
                        visible: modelData.installed
                        enabled: !page.locked(modelData.id)
                        text: qsTrId("store.active")
                        checked: !!page.pluginInfo(modelData.id).enabled
                        onToggled: {
                            if (checked) {
                                api.request("plugin.enable", { id: modelData.id })
                                return
                            }
                            const id = modelData.id
                            // stays on until confirmed (the plugin list resets it)
                            checked = Qt.binding(() => !!page.pluginInfo(id).enabled)
                            page.confirmIfRequired(id, "disable")
                        }
                    }
                    Button {
                        visible: !modelData.installed || modelData.updateAvailable
                        enabled: !store.busy && modelData.compatible && !(modelData.installed && page.locked(modelData.id))
                        text: modelData.installed ? qsTrId("store.update") : qsTrId("store.install")
                        highlighted: true
                        onClicked: store.install(modelData.id)
                    }
                    Button {
                        visible: modelData.installed
                        enabled: !store.busy && !page.locked(modelData.id)
                        text: qsTrId("store.remove")
                        flat: true
                        onClicked: {
                            const id = modelData.id
                            page.confirmIfRequired(id, "remove")
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: parent.count === 0
                text: page.tab === 0 ? qsTrId("store.no_more_plugins_available")
                    : page.tab === 1 ? qsTrId("store.no_plugins_installed_yet")
                    : qsTrId("store.everything_up_date")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
            }
        }

        // Sources
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: page.tab === 3
            spacing: Theme.spacing / 2

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: Theme.spacing / 2
                model: store.sources

                delegate: Rectangle {
                    width: ListView.view.width
                    height: Theme.fontNormal * 3
                    radius: Theme.radius
                    color: Theme.surface

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing
                        anchors.rightMargin: Theme.spacing / 2

                        Text {
                            Layout.fillWidth: true
                            text: modelData.line + (modelData.official ? "  (" + qsTrId("store.official_source") + ")" : "")
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: modelData.packages === 1 ? qsTrId("store.one_plugin") : qsTrId("store.n_plugins").arg(modelData.packages)
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                        }
                        Button {
                            text: qsTrId("store.remove")
                            flat: true
                            enabled: !store.busy
                            onClicked: {
                                var error = store.removeSource(modelData.index)
                                addError.text = error
                            }
                        }
                    }
                }
            }

            // Add a source: GitHub user/repo, web address or folder
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacing / 2

                ComboBox {
                    id: sourceType
                    model: ["github", "url", "file"]
                }
                TextField {
                    id: sourceLocation
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    placeholderText: sourceType.currentText === "github" ? qsTrId("store.user_repository")
                                   : sourceType.currentText === "url" ? qsTrId("store.https_server_path") : "/media/usb/openhu-repo"
                }
                TextField {
                    id: sourceBranch
                    visible: sourceType.currentText === "github"
                    Layout.preferredWidth: Theme.dockWidth * 1.4
                    inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                    placeholderText: "main"
                }
                Button {
                    text: qsTrId("store.add")
                    highlighted: true
                    enabled: sourceLocation.text !== "" && !store.busy
                    onClicked: {
                        var error = store.addSource(sourceType.currentText, sourceLocation.text, sourceBranch.text)
                        addError.text = error
                        if (!error) {
                            sourceLocation.text = ""
                            sourceBranch.text = ""
                        }
                    }
                }
            }
            Text {
                id: addError
                visible: text !== ""
                color: "#ff8a80"
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
