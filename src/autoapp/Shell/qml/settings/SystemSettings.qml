import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// OpenHU OS: version, time zone and time, system update, ssh access, restart and power off of the device (only on
// OpenHU OS, see SystemController).
Flickable {
    id: page

    // not "update"/"ssh": inside child items "update" would be Item.update()
    readonly property var updateInfo: system.update
    readonly property var sshInfo: system.ssh
    readonly property var timeInfo: system.time
    // "Europe/Berlin", ... sorted by region, so the drop-down keeps the regions together
    readonly property var zones: system.timeZones()
    readonly property bool installing: updateInfo.state === "checking" || updateInfo.state === "installing"

    contentWidth: width
    contentHeight: column.implicitHeight + Theme.spacing * 2
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    Connections {
        target: system
        function onSshError(message) { shell.toast(message, 5000) }
        function onTimeError(message) { shell.toast(message, 5000) }
    }
    // synchronized changes on its own
    Component.onCompleted: system.refreshTime()

    function regionOf(zone) {
        const slash = zone.indexOf("/")
        return slash > 0 ? zone.slice(0, slash) : zone
    }
    function cityOf(zone) {
        const slash = zone.indexOf("/")
        return (slash > 0 ? zone.slice(slash + 1) : zone).replace(/_/g, " ")
    }
    // "Europe / Berlin", "America / Argentina/Salta", "UTC"
    function zoneLabel(zone) {
        const region = regionOf(zone)
        return region === zone ? zone : regionName(region) + " / " + cityOf(zone)
    }
    function regionName(region) {
        switch (region) {
        case "Africa": return qsTr("Africa")
        case "America": return qsTr("America")
        case "Antarctica": return qsTr("Antarctica")
        case "Arctic": return qsTr("Arctic")
        case "Asia": return qsTr("Asia")
        case "Atlantic": return qsTr("Atlantic Ocean")
        case "Australia": return qsTr("Australia")
        case "Europe": return qsTr("Europe")
        case "Indian": return qsTr("Indian Ocean")
        case "Pacific": return qsTr("Pacific Ocean")
        }
        return region
    }
    // "UTC+02:00"
    function offsetText(seconds) {
        const minutes = Math.abs(seconds) / 60
        const pad = n => String(Math.floor(n)).padStart(2, "0")
        return "UTC" + (seconds < 0 ? "−" : "+") + pad(minutes / 60) + ":" + pad(minutes % 60)
    }
    function zoneText() {
        if (page.timeInfo.error)
            return page.timeInfo.error
        if (!page.timeInfo.zone)
            return ""
        return page.timeInfo.zone === "UTC" ? "UTC"
             : page.cityOf(page.timeInfo.zone) + " (" + page.offsetText(page.timeInfo.offset || 0) + ")"
    }
    function ntpText() {
        if (!page.timeInfo.ntp)
            return qsTr("Off: set the date and time by hand below.")
        return page.timeInfo.synchronized ? qsTr("Synchronized with a time server.")
                                          : qsTr("Not synchronized yet – needs an internet connection.")
    }
    // the date and time pickers start at the current time
    function loadNow() {
        const now = new Date()
        year.currentIndex = now.getFullYear() - year.first
        month.currentIndex = now.getMonth()
        day.currentIndex = now.getDate() - 1
        hour.currentIndex = now.getHours()
        minute.currentIndex = now.getMinutes()
    }

    // a column of numbers to spin through (01, 02, ...), or of names
    component NumberTumbler: Tumbler {
        id: tumbler
        property int first: 0
        property int digits: 2
        property var names: null
        width: Theme.fontNormal * (digits + 1.6)
        height: Theme.fontNormal * 5
        visibleItemCount: 3
        delegate: Text {
            required property int index
            text: tumbler.names ? tumbler.names[index] : String(index + tumbler.first).padStart(tumbler.digits, "0")
            color: Theme.text
            opacity: 1.0 - Math.abs(Tumbler.displacement) / (tumbler.visibleItemCount / 2)
            font.pixelSize: Theme.fontNormal
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
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
        // without a network the hotspot may have the WiFi card: WiFi networks only after Search or Connect
        const hotspotOnCard = !!wifi.state.available && !!wifi.state.hotspotDevice && !(wifi.state.client || {}).ssid
        if (page.updateInfo.checked && page.updateInfo.checkError)
            return page.updateInfo.checkError
                   + (hotspotOnCard ? " " + qsTr("The WiFi card runs the hotspot: connect to a WiFi network under Network & API.") : "")
        if (page.updateInfo.checked && page.updateInfo.available)
            return qsTr("About 500 MB download.")
                   + (page.updateInfo.blocked ? " " + qsTr("Not while Android Auto is connected.") : "")
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

        SectionTitle { text: qsTr("Date and time") }
        SettingRow {
            title: qsTr("Time zone")
            description: page.zoneText()
            ComboBox {
                width: Theme.dockWidth * 4
                enabled: !page.timeInfo.busy && !page.timeInfo.error
                model: page.zones.map(page.zoneLabel)
                currentIndex: page.zones.indexOf(page.timeInfo.zone)
                onActivated: index => system.setTimeZone(page.zones[index])
            }
        }
        SettingRow {
            title: qsTr("Time from the internet")
            description: page.ntpText()
            Switch {
                checked: !!page.timeInfo.ntp
                enabled: !page.timeInfo.busy && !page.timeInfo.error
                onToggled: system.setNtp(checked)
            }
        }
        SettingRow {
            visible: !page.timeInfo.ntp && !page.timeInfo.error && page.timeInfo.zone !== undefined
            // after the tumblers are set up (an index set before is reset), also when visible from the start
            Component.onCompleted: Qt.callLater(page.loadNow)
            onVisibleChanged: if (visible) Qt.callLater(page.loadNow)
            title: qsTr("Date and time")
            description: qsTr("In the time zone above. Also sets the hardware clock.")
            Row {
                spacing: Theme.spacing / 4
                NumberTumbler {
                    id: day
                    first: 1
                    // always 31: a model that follows the month would reset the day (Set keeps it in the month)
                    model: 31
                }
                // the month by its name: day, month and year are clear in every language
                NumberTumbler {
                    id: month
                    width: Theme.fontNormal * 4
                    model: 12
                    names: [...Array(12).keys()].map(index => Qt.locale(settings.values["general.language"])
                                                                .standaloneMonthName(index, Locale.ShortFormat))
                }
                NumberTumbler {
                    id: year
                    first: 2024
                    digits: 4
                    model: 30
                }
                Item { width: Theme.spacing / 2; height: 1 }
                NumberTumbler {
                    id: hour
                    model: 24
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: ":"
                    color: Theme.text
                    font.pixelSize: Theme.fontNormal
                }
                NumberTumbler {
                    id: minute
                    model: 60
                }
                Button {
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: !page.timeInfo.busy
                    text: qsTr("Set")
                    onClicked: {
                        const y = year.currentIndex + year.first
                        const lastDay = new Date(y, month.currentIndex + 1, 0).getDate()
                        const time = new Date(y, month.currentIndex, Math.min(day.currentIndex + 1, lastDay),
                                              hour.currentIndex, minute.currentIndex)
                        const error = system.setTime(Math.floor(time.getTime() / 1000))
                        if (error)
                            shell.toast(error, 4000)
                    }
                }
            }
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
