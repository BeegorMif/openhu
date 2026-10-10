import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import "."

Window {
    id: root
    visible: true
    title: "OpenAuto"
    color: Theme.background

    // "home" | "aa"; plugins add their own pages later
    property string page: "home"

    onWidthChanged: Theme.scale = Math.min(width / 1280, height / 720)
    onHeightChanged: Theme.scale = Math.min(width / 1280, height / 720)

    // Show Android Auto as soon as the phone starts projecting, fall back to home when it stops.
    Connections {
        target: aaBridge
        function onVideoActiveChanged() {
            if (aaBridge.videoActive)
                root.page = "aa"
            else if (root.page === "aa")
                root.page = "home"
        }
    }

    // Requests from the API (ui.navigate, ui.toast)
    Connections {
        target: shell
        function onNavigateRequested(page) { root.page = page }
        function onToastRequested(text, durationMs) { toast.show(text, durationMs) }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Dock {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.dockWidth
            currentPage: root.page
            onPageSelected: root.page = name
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StatusBar {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.statusHeight
                onVolumeClicked: volumePanel.show()
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                HomePage {
                    anchors.fill: parent
                    visible: root.page === "home"
                    onOpenAndroidAuto: root.page = "aa"
                }

                AaPage {
                    anchors.fill: parent
                    visible: root.page === "aa"
                }
            }
        }
    }

    // Overlays above everything, including Android Auto
    VolumePanel {
        id: volumePanel
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.statusHeight + Theme.spacing
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing * 2
    }
}
