import QtQuick
import OpenHU 1.0
import QtQuick.Window
import QtQuick.Layouts
import "."
import "settings"

Window {
    id: root
    visible: true
    title: "OpenHU"
    color: Theme.background

    // "home" | "aa" | "settings" | "plugin:<id>"
    property string page: "home"
    property string settingsCategory: "general"

    function navigate(name) {
        // Apps and store live in the settings
        if (name === "apps" || name === "store") {
            settingsCategory = "apps"
            name = "settings"
        }
        page = name
    }
    onPageChanged: shell.setCurrentPage(page)
    Component.onCompleted: {
        shell.setCurrentPage(page)
        updatePluginPages()
    }

    // Plugin pages and overlays, one row per plugin. Only the rows of plugins whose page changed are touched:
    // plugins.plugins also changes for the dock, running state, other plugins, ... and recreating a page would
    // e.g. take the camera away from a camera page while it is shown.
    ListModel { id: pluginPages }
    function updatePluginPages() {
        var pages = plugins.plugins.map(p => ({ pluginId: p.id, name: p.name, version: p.version, page: p.page,
                                                overlay: p.overlay, pluginEnabled: p.enabled }))
        for (var i = pluginPages.count - 1; i >= 0; --i) {
            var row = pluginPages.get(i)
            if (!pages.some(p => p.pluginId === row.pluginId))
                pluginPages.remove(i)
        }
        for (var j = 0; j < pages.length; ++j) {
            var index = -1
            for (var k = 0; k < pluginPages.count; ++k) {
                if (pluginPages.get(k).pluginId === pages[j].pluginId) {
                    index = k
                    break
                }
            }
            if (index < 0) {
                pluginPages.append(pages[j])
                continue
            }
            for (var key in pages[j]) {
                if (pluginPages.get(index)[key] !== pages[j][key])
                    pluginPages.setProperty(index, key, pages[j][key])
            }
        }
    }
    Connections {
        target: plugins
        function onPluginsChanged() { root.updatePluginPages() }
    }

    // Sizes are designed for 1280x720 (720x1280 in portrait)
    function updateScale() {
        Theme.scale = width >= height ? Math.min(width / 1280, height / 720) : Math.min(width / 720, height / 1280)
    }
    onWidthChanged: updateScale()
    onHeightChanged: updateScale()

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

    // "Exit" inside Android Auto
    Connections {
        target: eventBus
        function onEvent(topic, data) {
            if (topic === "projection.exit" && root.page === "aa")
                root.page = "home"
        }
    }

    // Requests from the API (ui.navigate, ui.toast)
    Connections {
        target: shell
        function onNavigateRequested(page) { root.navigate(page) }
        function onToastRequested(text, durationMs) { toast.show(text, durationMs) }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Dock {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.dockWidth
            currentPage: root.page
            onPageSelected: root.navigate(name)
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

                Loader {
                    anchors.fill: parent
                    active: root.page === "settings"
                    sourceComponent: SettingsPage {
                        category: root.settingsCategory
                        onCategoryChanged: root.settingsCategory = category
                    }
                }

                // Plugin pages, loaded when first shown and kept alive afterwards
                Repeater {
                    model: pluginPages

                    Loader {
                        id: pluginPage
                        required property string pluginId
                        required property string name
                        required property string page
                        required property bool pluginEnabled
                        anchors.fill: parent
                        visible: root.page === "plugin:" + pluginId
                        property bool used: false
                        onVisibleChanged: if (visible) used = true
                        active: pluginEnabled && page !== "" && (visible || used)
                        source: page

                        Text {
                            anchors.centerIn: parent
                            visible: pluginPage.status === Loader.Error
                            text: pluginPage.name + ": Seite kann nicht geladen werden (Log: data/logs)"
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
        model: pluginPages

        Loader {
            id: pluginOverlay
            required property string pluginId
            required property string overlay
            anchors.fill: parent
            property bool shown: plugins.overlayVisible(pluginId)
            active: shown && overlay !== ""
            source: overlay

            Connections {
                target: plugins
                function onOverlayChanged(id, visible) {
                    if (id === pluginOverlay.pluginId)
                        pluginOverlay.shown = visible
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

    PairingDialog {
        anchors.fill: parent
    }

    Toast {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing * 2
    }
}
