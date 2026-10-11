import QtQuick
import OpenHU 1.0
import QtQuick.Layouts
import "."
import "Maneuver.js" as Maneuver

// Next maneuver from Android Auto navigation (nav.turn / nav.distance / nav.destination).
Rectangle {
    id: card
    radius: Theme.radius
    color: Theme.surface

    BusTopic { id: navStatus; topic: "nav.status" }
    BusTopic { id: turn; topic: "nav.turn" }
    BusTopic { id: distance; topic: "nav.distance" }
    BusTopic { id: destination; topic: "nav.destination" }

    readonly property bool active: navStatus.data.state === "active" || navStatus.data.state === "rerouting"

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 1.5
        spacing: Theme.spacing * 1.5
        visible: card.active

        Item {
            Layout.preferredWidth: Theme.fontHuge * 1.1
            Layout.fillHeight: true

            Image {
                id: turnImage
                anchors.centerIn: parent
                width: parent.width
                height: width
                source: turn.data.image || ""
                visible: status === Image.Ready
                fillMode: Image.PreserveAspectFit
            }
            Text {
                anchors.centerIn: parent
                visible: !turnImage.visible
                text: Maneuver.symbol(turn.data.maneuver)
                color: Theme.primary
                font.pixelSize: Theme.fontHuge
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing / 3

            Text {
                text: navStatus.data.state === "rerouting" ? "Route wird neu berechnet …" : Maneuver.distance(distance.data)
                color: Theme.text
                font.pixelSize: Theme.fontLarge * 1.3
                font.bold: true
            }
            Text {
                Layout.fillWidth: true
                text: turn.data.road || turn.data.cue || ""
                color: Theme.text
                font.pixelSize: Theme.fontNormal
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                visible: text !== ""
                text: [destination.data.eta ? "Ankunft " + destination.data.eta : "",
                       Maneuver.distance(destination.data),
                       Maneuver.duration(destination.data.seconds)].filter(function(part) { return part }).join(" · ")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
                elide: Text.ElideRight
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing / 2
        visible: !card.active

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "↑"
            color: Theme.outline
            font.pixelSize: Theme.fontHuge * 0.6
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Keine aktive Navigation"
            color: Theme.textDim
            font.pixelSize: Theme.fontNormal
        }
    }
}
