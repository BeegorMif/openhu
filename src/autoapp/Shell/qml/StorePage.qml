import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OpenHU 1.0

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

                TabButton { text: "Verfügbar" }
                TabButton { text: "Installiert" }
                TabButton {
                    text: {
                        var count = 0
                        var all = store.packages
                        for (var i = 0; i < all.length; ++i)
                            if (all[i].updateAvailable) ++count
                        return count > 0 ? "Updates (" + count + ")" : "Updates"
                    }
                }
                TabButton { text: "Quellen" }
            }

            Button {
                text: "Aktualisieren"
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
                text: "Alle aktualisieren"
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
                            text: (modelData.name || modelData.id).charAt(0).toUpperCase()
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
                            text: (modelData.name || modelData.id) + "  " + (modelData.installed && modelData.updateAvailable
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
                            text: modelData.description || ""
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: {
                                var parts = []
                                parts.push(modelData.source ? (modelData.official ? "Offiziell" : "Nicht offiziell · " + modelData.source)
                                                            : "Lokal installiert")
                                if (modelData.permissions && modelData.permissions.length)
                                    parts.push("Rechte: " + modelData.permissions.join(", "))
                                if (!modelData.compatible)
                                    parts.push("nicht für dieses Gerät oder diese OpenHU-Version")
                                return parts.join(" · ")
                            }
                            color: modelData.official ? Theme.success : Theme.textDim
                            font.pixelSize: Theme.fontSmall * 0.9
                            elide: Text.ElideRight
                        }
                    }

                    Button {
                        visible: modelData.installed && page.pluginInfo(modelData.id).page !== undefined
                                 && page.pluginInfo(modelData.id).page !== "" && page.pluginInfo(modelData.id).enabled
                        text: "Öffnen"
                        onClicked: shell.navigate("plugin:" + modelData.id)
                    }
                    Switch {
                        visible: modelData.installed && !!page.pluginInfo(modelData.id).page
                        text: "Im Dock"
                        checked: !!page.pluginInfo(modelData.id).dock
                        onToggled: api.request("plugin.setDock", { id: modelData.id, dock: checked })
                    }
                    Switch {
                        visible: modelData.installed
                        text: "Aktiv"
                        checked: !!page.pluginInfo(modelData.id).enabled
                        onToggled: api.request(checked ? "plugin.enable" : "plugin.disable", { id: modelData.id })
                    }
                    Button {
                        visible: !modelData.installed || modelData.updateAvailable
                        enabled: !store.busy && modelData.compatible
                        text: modelData.installed ? "Aktualisieren" : "Installieren"
                        highlighted: true
                        onClicked: store.install(modelData.id)
                    }
                    Button {
                        visible: modelData.installed
                        enabled: !store.busy
                        text: "Entfernen"
                        flat: true
                        onClicked: store.remove(modelData.id)
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: parent.count === 0
                text: page.tab === 0 ? "Keine weiteren Plugins verfügbar"
                    : page.tab === 1 ? "Noch keine Plugins installiert"
                    : "Alles aktuell"
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
                            text: modelData.line + (modelData.official ? "  (offiziell)" : "")
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: modelData.packages + " Plugins"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontSmall
                        }
                        Button {
                            text: "Entfernen"
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
                    placeholderText: sourceType.currentText === "github" ? "benutzer/repository"
                                   : sourceType.currentText === "url" ? "https://server/pfad/" : "/media/usb/openhu-repo"
                }
                TextField {
                    id: sourceBranch
                    visible: sourceType.currentText === "github"
                    Layout.preferredWidth: Theme.dockWidth * 1.4
                    placeholderText: "main"
                }
                Button {
                    text: "Hinzufügen"
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
