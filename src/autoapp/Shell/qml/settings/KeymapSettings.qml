import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import OpenHU 1.0

// Key map: keyboard and steering wheel keys (as keyboard keys) -> actions.
Item {
    id: keymap

    property var entries: actions.keymapList()
    // built-in actions, actions of the official plugins and everything already assigned
    property var actionNames: {
        var names = Object.keys(api.request("input.actions").result || {})
        names = names.concat(["camera.toggle", "camera.show", "camera.hide", "radio.toggle", "radio.next", "radio.previous"])
        for (var i = 0; i < entries.length; ++i)
            names.push(entries[i].action)
        return names.filter(function(name, index) { return names.indexOf(name) === index }).sort()
    }
    property bool capturing: false
    property string captured: ""

    Connections {
        target: actions
        function onKeymapChanged() { keymap.entries = actions.keymapList() }
        function onKeyCaptured(key) {
            keymap.capturing = false
            keymap.captured = key
        }
    }

    Component.onDestruction: if (capturing) actions.captureKey(false)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 1.5
        spacing: Theme.spacing / 2

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing / 2

            Text {
                Layout.fillWidth: true
                text: keymap.capturing ? "Taste drücken …"
                    : keymap.captured ? "Taste „" + keymap.captured + "“: Aktion wählen"
                    : "Tastatur- und Lenkradtasten (über USB-Adapter als Tastatur) lösen Aktionen aus."
                color: keymap.capturing || keymap.captured ? Theme.primary : Theme.textDim
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            ComboBox {
                id: newAction
                visible: keymap.captured !== ""
                Layout.preferredWidth: Theme.dockWidth * 2.6
                model: keymap.actionNames
            }
            Button {
                visible: keymap.captured !== ""
                text: "Zuweisen"
                highlighted: true
                onClicked: {
                    actions.setKeyAction(keymap.captured, newAction.currentText)
                    keymap.captured = ""
                }
            }
            Button {
                text: keymap.capturing ? "Abbrechen" : "Taste hinzufügen"
                onClicked: {
                    keymap.captured = ""
                    keymap.capturing = !keymap.capturing
                    actions.captureKey(keymap.capturing)
                }
            }
            Button {
                text: "Standard"
                flat: true
                onClicked: actions.resetKeymap()
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: keymap.entries
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                width: ListView.view.width
                height: Theme.fontNormal * 2.8
                radius: Theme.radius / 3
                color: Theme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spacing
                    anchors.rightMargin: Theme.spacing / 2

                    Text {
                        Layout.preferredWidth: parent.width * 0.3
                        text: modelData.key
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                        elide: Text.ElideRight
                    }
                    ComboBox {
                        Layout.fillWidth: true
                        model: keymap.actionNames
                        currentIndex: keymap.actionNames.indexOf(modelData.action)
                        onActivated: actions.setKeyAction(modelData.key, currentText)
                    }
                    Text {
                        visible: modelData.custom
                        text: "geändert"
                        color: Theme.primary
                        font.pixelSize: Theme.fontSmall
                    }
                    Button {
                        text: "✕"
                        flat: true
                        onClicked: actions.removeKey(modelData.key)
                    }
                }
            }
        }
    }
}
