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
            return "Update auf " + boot.running + " installiert."
        if (boot.state === "rollback")
            return "Update auf " + boot.failed + " fehlgeschlagen – die vorherige Version wurde wiederhergestellt."
        if (boot.state === "trial")
            return "Die neue Version wird gerade geprüft."
        return "Wird als Ganzes aktualisiert; startet eine neue Version nicht, kommt die alte automatisch zurück."
    }

    function updateTitle() {
        switch (page.updateInfo.state) {
        case "checking": return "Update wird vorbereitet …"
        case "installing": return "Update auf " + page.updateInfo.version + " wird installiert"
        case "done": return "Update auf " + page.updateInfo.version + " installiert"
        case "error": return "Update fehlgeschlagen"
        case "cancelled": return "Update abgebrochen"
        }
        if (page.updateInfo.busy)
            return "Suche nach Updates …"
        if (page.updateInfo.checked && page.updateInfo.checkError)
            return "Suche fehlgeschlagen"
        if (page.updateInfo.checked && page.updateInfo.available)
            return "Neue Version " + page.updateInfo.newest
        if (page.updateInfo.checked)
            return "OpenHU OS ist auf dem neuesten Stand"
        return "Nach Updates suchen"
    }

    function updateDescription() {
        switch (page.updateInfo.state) {
        case "installing":
            return page.updateInfo.percent + " % · " + (page.updateInfo.phase === "kernel" ? "Kernel" : "System")
                   + " · OpenHU läuft weiter, Android Auto bitte nicht verbinden"
        case "done": return "Aktiv nach dem Neustart. Startet sie nicht, kommt " + page.updateInfo.from + " automatisch zurück."
        case "error": return page.updateInfo.error || ""
        case "cancelled": return "Die laufende Version ist unverändert."
        }
        if (page.updateInfo.checked && page.updateInfo.checkError)
            return page.updateInfo.checkError
        if (page.updateInfo.checked && page.updateInfo.available)
            return "Etwa 500 MB Download." + (page.updateInfo.blocked ? " Nicht während Android Auto verbunden ist." : "")
        return "Online über GitHub-Releases (Internetverbindung nötig)."
    }

    function sshText() {
        if (page.sshInfo.error)
            return page.sshInfo.error
        let text = !page.sshInfo.enabled ? "Aus."
                 : page.sshInfo.running ? "An: ssh root@" + (page.sshInfo.addresses && page.sshInfo.addresses.length ? page.sshInfo.addresses.join(" / ") : "<Adresse>") + "."
                 : "An, startet gerade …"
        if (!page.sshInfo.enabled && !page.sshInfo.password && !page.sshInfo.keys)
            text += " Zuerst ein Root-Passwort setzen."
        return text + " Erreichbar in jedem Netz des Geräts, auch im WLAN-Hotspot für das Telefon."
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
            title: "Version " + system.osVersion
            description: page.bootText()
        }

        SectionTitle { text: "Update" }
        SettingRow {
            title: page.updateTitle()
            description: page.updateDescription()

            Row {
                spacing: Theme.spacing / 2
                Button {
                    visible: !page.installing && page.updateInfo.state !== "done" && !(page.updateInfo.checked && page.updateInfo.available)
                    enabled: !page.updateInfo.busy
                    text: page.updateInfo.checked ? "Erneut suchen" : "Suchen"
                    onClicked: system.checkForUpdate()
                }
                ConfirmButton {
                    visible: !page.installing && page.updateInfo.state !== "done" && !!page.updateInfo.checked && !!page.updateInfo.available
                    enabled: !page.updateInfo.blocked && !page.updateInfo.busy
                    label: "Installieren"
                    confirmText: "Wirklich installieren?"
                    onConfirmed: {
                        const error = system.installUpdate()
                        if (error)
                            shell.toast(error, 4000)
                    }
                }
                Button {
                    visible: page.installing
                    text: "Abbrechen"
                    onClicked: system.cancelUpdate()
                }
                ConfirmButton {
                    visible: page.updateInfo.state === "done"
                    label: "Neu starten"
                    confirmText: "Wirklich neu starten?"
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

        SectionTitle { text: "SSH-Zugang" }
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
            title: "Root-Passwort"
            description: page.sshInfo.password ? "Gesetzt. Ein neues ersetzt es." : "Noch nicht gesetzt (mindestens 8 Zeichen)."
            Row {
                spacing: Theme.spacing / 2
                TextField {
                    id: password
                    width: Theme.dockWidth * 3
                    // one field: visible while typing (no second field to catch typos), hidden afterwards
                    echoMode: TextInput.PasswordEchoOnEdit
                    placeholderText: "Neues Passwort"
                    onAccepted: saveButton.clicked()
                }
                Button {
                    id: saveButton
                    text: "Speichern"
                    enabled: !page.sshInfo.busy && password.text.length > 0
                    onClicked: {
                        const error = system.setSshPassword(password.text)
                        if (error) {
                            shell.toast(error, 4000)
                            return
                        }
                        // the row says "Gesetzt" once the helper stored it, errors come as a toast
                        password.text = ""
                    }
                }
            }
        }

        SectionTitle { text: "Gerät" }
        SettingRow {
            title: "Neu starten"
            description: "Startet das Gerät neu"
            ConfirmButton {
                label: "Neu starten"
                confirmText: "Wirklich neu starten?"
                onConfirmed: shell.reboot()
            }
        }
        SettingRow {
            title: "Ausschalten"
            description: "Fährt das Gerät herunter"
            ConfirmButton {
                label: "Ausschalten"
                confirmText: "Wirklich ausschalten?"
                onConfirmed: shell.powerOff()
            }
        }
    }
}
