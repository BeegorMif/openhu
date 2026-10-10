import QtQuick 2.15
import OpenHU 1.0
import QtQuick.Window 2.15
import QtQuick.Layouts 1.15
import "."

Window {
    id: root
    visible: true
    title: "OpenHU"
    color: Theme.background

    // "home" | "aa" | "apps" | "store" | "plugin:<id>"
    property string page: "home"
    onPageChanged: shell.setCurrentPage(page)
    Component.onCompleted: shell.setCurrentPage(page)

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

                AppsPage {
                    anchors.fill: parent
                    visible: root.page === "apps"
                    onOpenPage: root.page = name
                }

                Loader {
                    anchors.fill: parent
                    active: root.page === "store"
                    source: "StorePage.qml"
                }

                // Plugin pages, loaded when first shown and kept alive afterwards
                Repeater {
                    model: plugins.plugins

                    Loader {
                        id: pluginPage
                        anchors.fill: parent
                        visible: root.page === "plugin:" + modelData.id
                        property bool used: false
                        onVisibleChanged: if (visible) used = true
                        active: modelData.enabled && modelData.page !== "" && (visible || used)
                        source: modelData.page

                        Text {
                            anchors.centerIn: parent
                            visible: pluginPage.status === Loader.Error
                            text: modelData.name + ": Seite kann nicht geladen werden (Log: data/logs)"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontNormal
                        }
                    }
                }
            }
        }
    }

    // Plugin overlays (e.g. reverse camera) above the whole shell, shown via plugins.setOverlayVisible / ui.overlay.show
    Repeater {
        model: plugins.plugins

        Loader {
            anchors.fill: parent
            property bool shown: plugins.overlayVisible(modelData.id)
            active: shown && modelData.overlay !== ""
            source: modelData.overlay

            Connections {
                target: plugins
                function onOverlayChanged(id, visible) {
                    if (id === modelData.id)
                        shown = visible
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
