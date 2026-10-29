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
    // Display control plugin publishes this kept topic so extra dim applies on every page.
    BusTopic { id: extraDim; topic: "ui.extraDim" }

    // The page on screen. While the phone restarts its video stream (after the reverse camera or "Exit") the
    // Android Auto page would show its last picture and then the phone's black frames and fade-in: the page shown
    // before stays until the first picture of the new stream is there, at most 2 s (#88)
    property string previousPage: "home"
    property bool pictureWaitOver: false
    readonly property string shownPage: page === "aa" && aaBridge.pictureWaiting && !pictureWaitOver
                                        ? previousPage : page
    property bool hideStatusBar: false


    // fromApi: ui.navigate (e.g. a plugin leaving its page), otherwise chosen by the user
    function navigate(name, fromApi) {
        if (fromApi)
            name = shell.pageOnNavigate(page, name)
        else
            shell.pageChosenByUser()
        // a category of the settings: "settings/wifi"; apps and store live in the settings
        if (name.startsWith("settings/")) {
            settingsCategory = name.slice(9)
            name = "settings"
        } else if (name === "apps" || name === "store") {
            settingsCategory = "apps"
            name = "settings"
        }
        page = name
    }
    onPageChanged: {
        shell.setCurrentPage(page)
        pictureWaitOver = false
    }
    onShownPageChanged: if (shownPage !== "aa") previousPage = shownPage

    Timer {
        interval: 2000
        running: root.page === "aa" && aaBridge.pictureWaiting && !root.pictureWaitOver
        onTriggered: root.pictureWaitOver = true
    }
    Component.onCompleted: {
        shell.setCurrentPage(page)
        updatePluginPages()
        root.hideStatusBar = settings.values["general.hideStatusBar"] === true
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

    // Show Android Auto as soon as the phone starts projecting, fall back to home when it stops. A plugin page (the
    // reverse camera) stays: Android Auto takes the screen when the plugin leaves its page (#87).
    Connections {
        target: aaBridge
        function onVideoActiveChanged() {
            root.page = shell.pageOnVideo(root.page, aaBridge.videoActive)
        }
    }

    // "Exit" inside Android Auto
    Connections {
        target: eventBus
        function onEvent(topic, data) {
            if (topic !== "projection.exit")
                return
            shell.pageChosenByUser()
            if (root.page === "aa")
                root.page = "home"
        }
    }

    // Requests from the API (ui.navigate, ui.toast)
    Connections {
        target: shell
        function onNavigateRequested(page) { root.navigate(page, true) }
        function onToastRequested(text, durationMs) { toast.show(text, durationMs) }
    }

    // A USB stick with a newer OpenHU OS: tap opens Settings -> System with the Install button
    Connections {
        target: system
        function onUsbUpdateFound(version) {
            toast.show(qsTrId("shell.usb_update_found").arg(version), 8000, "settings/system")
        }
    }
    Connections {
        target: settings

        function onValuesChanged() {
            root.hideStatusBar = settings.values["general.hideStatusBar"] === true
        }
    }
    // Height of the on-screen keyboard while it is shown: the page area gets smaller, so the pages can scroll the
    // focused field into view (revealFocus)
    // Keyboard.qml is loaded by file name, so the shell starts without the module; qmllint only sees a QObject
    // qmllint disable missing-property
    readonly property real keyboardHeight: keyboard.item ? keyboard.item.occupied : 0
    // qmllint enable missing-property

    // Scrolls the Flickable around the focused text field so the field is above the keyboard
    function revealFocus() {
        const item = root.activeFocusItem
        if (!item || keyboardHeight === 0)
            return
        let flickable = item.parent
        while (flickable && !(flickable instanceof Flickable))
            flickable = flickable.parent
        if (!flickable)
            return
        const top = item.mapToItem(flickable.contentItem, 0, 0).y
        const margin = Theme.spacing
        if (top + item.height + margin > flickable.contentY + flickable.height)
            flickable.contentY = Math.min(top + item.height + margin - flickable.height,
                                          Math.max(0, flickable.contentHeight - flickable.height))
        else if (top - margin < flickable.contentY)
            flickable.contentY = Math.max(0, top - margin)
    }
    onActiveFocusItemChanged: revealTimer.restart()
    onKeyboardHeightChanged: revealTimer.restart()
    // after the layout followed the new height
    Timer {
        id: revealTimer
        interval: 50
        onTriggered: root.revealFocus()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Dock {
            Layout.fillHeight: true
            Layout.preferredWidth: Theme.dockWidth
            currentPage: root.page
            onPageSelected: name => root.navigate(name)
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StatusBar {
                Layout.fillWidth: true
                Layout.preferredHeight: root.hideStatusBar ? 0 : Theme.statusHeight
                visible: !root.hideStatusBar
                onVolumeClicked: volumePanel.show()
            }

            // the pages: smaller while the on-screen keyboard is shown (dock and status bar stay)
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.bottomMargin: root.keyboardHeight

                HomePage {
                    anchors.fill: parent
                    visible: root.shownPage === "home"
                    onOpenAndroidAuto: root.navigate("aa")
                }

                AaPage {
                    anchors.fill: parent
                    visible: root.shownPage === "aa"
                }

                Loader {
                    anchors.fill: parent
                    active: root.shownPage === "settings"
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
                        visible: root.shownPage === "plugin:" + pluginId
                        property bool used: false
                        onVisibleChanged: if (visible) used = true
                        // created while already on screen (plugin updated or enabled again): no visibleChanged
                        Component.onCompleted: if (visible) used = true
                        active: pluginEnabled && page !== "" && (visible || used)
                        source: page

                        Text {
                            anchors.centerIn: parent
                            visible: pluginPage.status === Loader.Error
                            text: qsTrId("shell.page_cannot_loaded_log").arg(pluginPage.name)
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

    // Tap outside the focused text field (and the keyboard above this): the field loses focus, the keyboard
    // closes. The tap still reaches what is below (a button, another field, which then takes the focus).
    MouseArea {
        anchors.fill: parent
        enabled: root.keyboardHeight > 0
        onPressed: mouse => {
            const field = root.activeFocusItem
            if (field && !field.contains(field.mapFromItem(null, mouse.x, mouse.y)))
                field.focus = false
            mouse.accepted = false
        }
    }

    // no size: a Loader with a size stretches the panel to it (Keyboard.qml sizes itself by the window)
    Loader {
        id: keyboard
        active: onScreenKeyboardAvailable
        source: "Keyboard.qml"
        // qmllint disable missing-property
        onLoaded: {
            item.allowed = Qt.binding(() => settings.values["general.onScreenKeyboard"] !== false)
            item.language = Qt.binding(() => settings.values["general.language"])
        }
        // qmllint enable missing-property
    }

    Toast {
        id: toast
        onOpened: page => root.navigate(page)
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing * 2 + root.keyboardHeight
    }

    // A visual-only black layer: it dims plugin pages, the dock, status bar and dialogs without
    // intercepting touch input. Kept at the end so it covers every shell surface.
    Rectangle {
        anchors.fill: parent
        z: 1000
        visible: extraDim.data.enabled === true
        color: "#80000000"
        enabled: false
    }
}
