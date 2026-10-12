import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// OpenHU OS: version, system update, ssh access, restart and power off of the device (only on OpenHU OS, see
// SystemController).
Flickable {
    id: page

    // not "update"/"ssh": inside child items "update" would be Item.update()
    readonly property var updateInfo: system.update
    readonly property var sshInfo: system.ssh
    readonly property bool installing: updateInfo.state === "checking" || updateInfo.state === "installing"

    contentWidth: width
    contentHeight: column.implicitHeight + Theme.spacing * 2
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    Connections {
        target: system
        function onSshError(message) { shell.toast(message, 5000) }
    }

    function bootText() {
        const boot = system.bootStatus
        if (boot.state === "updated")
            return qsTr("Update to %1 installed.").arg(boot.running)
        if (boot.state === "rollback")
            return qsTr("Update to %1 failed – the previous version was restored.").arg(boot.failed)
        if (boot.state === "trial")
            return qsTr("The new version is being checked.")
        return qsTr("Updated as a whole; if a new version does not start, the old one comes back automatically.")
    }

    function updateTitle() {
        switch (page.updateInfo.state) {
        case "checking": return qsTr("Preparing the update …")
        case "installing": return qsTr("Installing the update to %1").arg(page.updateInfo.version)
        case "done": return qsTr("Update to %1 installed").arg(page.updateInfo.version)
        case "error": return qsTr("Update failed")
        case "cancelled": return qsTr("Update cancelled")
        }
        if (page.updateInfo.busy)
            return qsTr("Searching for updates …")
        if (page.updateInfo.checked && page.updateInfo.checkError)
            return qsTr("Search failed")
        if (page.updateInfo.checked && page.updateInfo.available)
            return qsTr("New version %1").arg(page.updateInfo.newest)
        if (page.updateInfo.checked)
            return qsTr("OpenHU OS is up to date")
        return qsTr("Search for updates")
    }

    function updateDescription() {
        switch (page.updateInfo.state) {
        case "installing":
            return page.updateInfo.percent + " % · " + (page.updateInfo.phase === "kernel" ? qsTr("Kernel") : qsTr("System"))
                   + " · " + qsTr("OpenHU keeps running, please do not connect Android Auto")
        case "done": return qsTr("Active after the restart. If it does not start, %1 comes back automatically.").arg(page.updateInfo.from)
        case "error": return page.updateInfo.error || ""
        case "cancelled": return qsTr("The running version is unchanged.")
        }
        if (page.updateInfo.checked && page.updateInfo.checkError)
            return page.updateInfo.checkError
        if (page.updateInfo.checked && page.updateInfo.available)
            return qsTr("About 500 MB download.") + (page.updateInfo.blocked ? " " + qsTr("Not while Android Auto is connected.") : "")
        return qsTr("Online from GitHub releases (needs an internet connection).")
    }

    function sshText() {
        if (page.sshInfo.error)
            return page.sshInfo.error
        let text = !page.sshInfo.enabled ? qsTr("Off.")
                 : page.sshInfo.running ? qsTr("On: ssh root@%1.").arg(page.sshInfo.addresses && page.sshInfo.addresses.length ? page.sshInfo.addresses.join(" / ") : qsTr("<address>"))
                 : qsTr("On, starting …")
        if (!page.sshInfo.enabled && !page.sshInfo.password && !page.sshInfo.keys)
            text += " " + qsTr("Set a root password first.")
        return text + " " + qsTr("Reachable in every network of the device, also in the WiFi hotspot for the phone.")
    }

    // plain Column: rows with wrapped text get their width from here (no layout loop)
    Column {
        id: column
        x: Theme.spacing * 1.5
        y: Theme.spacing
        width: page.width - Theme.spacing * 3
        spacing: Theme.spacing / 2

        SectionTitle { text: "OpenHU OS" }
        SettingRow {
            title: qsTr("Version %1").arg(system.osVersion)
            description: page.bootText()
        }

        SectionTitle { text: qsTr("Update") }
        SettingRow {
            title: page.updateTitle()
            description: page.updateDescription()

            Row {
                spacing: Theme.spacing / 2
                Button {
                    visible: !page.installing && page.updateInfo.state !== "done" && !(page.updateInfo.checked && page.updateInfo.available)
                    enabled: !page.updateInfo.busy
                    text: page.updateInfo.checked ? qsTr("Search again") : qsTr("Search")
                    onClicked: system.checkForUpdate()
                }
                ConfirmButton {
                    visible: !page.installing && page.updateInfo.state !== "done" && !!page.updateInfo.checked && !!page.updateInfo.available
                    enabled: !page.updateInfo.blocked && !page.updateInfo.busy
                    label: qsTr("Install")
                    confirmText: qsTr("Really install?")
                    onConfirmed: {
                        const error = system.installUpdate()
                        if (error)
                            shell.toast(error, 4000)
                    }
                }
                Button {
                    visible: page.installing
                    text: qsTr("Cancel")
                    onClicked: system.cancelUpdate()
                }
                ConfirmButton {
                    visible: page.updateInfo.state === "done"
                    label: qsTr("Restart")
                    confirmText: qsTr("Really restart?")
                    onConfirmed: shell.reboot()
                }
            }
        }
        ProgressBar {
            visible: page.updateInfo.state === "installing"
            width: parent.width
            from: 0
            to: 100
            value: page.updateInfo.percent || 0
        }

        SectionTitle { text: qsTr("SSH access") }
        SettingRow {
            title: "SSH"
            description: page.sshText()
            Switch {
                checked: !!page.sshInfo.enabled
                enabled: !page.sshInfo.busy && !page.sshInfo.error && (!!page.sshInfo.enabled || !!page.sshInfo.password || !!page.sshInfo.keys)
                onToggled: system.setSshEnabled(checked)
            }
        }
        SettingRow {
            title: qsTr("Root password")
            description: page.sshInfo.password ? qsTr("Set. A new one replaces it.") : qsTr("Not set yet (at least 8 characters).")
            Row {
                spacing: Theme.spacing / 2
                TextField {
                    id: password
                    width: Theme.dockWidth * 3
                    // one field: visible while typing (no second field to catch typos), hidden afterwards
                    echoMode: TextInput.PasswordEchoOnEdit
                    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                    placeholderText: qsTr("New password")
                    onAccepted: saveButton.clicked()
                }
                Button {
                    id: saveButton
                    text: qsTr("Save")
                    enabled: !page.sshInfo.busy && password.text.length > 0
                    onClicked: {
                        const error = system.setSshPassword(password.text)
                        if (error) {
                            shell.toast(error, 4000)
                            return
                        }
                        // the row says "Set" once the helper stored it, errors come as a toast
                        password.text = ""
                        password.focus = false  // closes the on-screen keyboard
                    }
                }
            }
        }

        SectionTitle { text: qsTr("Device") }
        SettingRow {
            title: qsTr("Restart")
            description: qsTr("Restarts the device")
            ConfirmButton {
                label: qsTr("Restart")
                confirmText: qsTr("Really restart?")
                onConfirmed: shell.reboot()
            }
        }
        SettingRow {
            title: qsTr("Power off")
            description: qsTr("Shuts the device down")
            ConfirmButton {
                label: qsTr("Power off")
                confirmText: qsTr("Really power off?")
                onConfirmed: shell.powerOff()
            }
        }
    }
}
