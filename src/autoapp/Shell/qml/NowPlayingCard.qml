import QtQuick
import OpenHU 1.0
import QtQuick.Layouts
import "."

// Current track from Android Auto (media.metadata / media.status).
Rectangle {
    id: card
    radius: Theme.radius
    color: Theme.surface
    clip: true

    BusTopic { id: metadata; topic: "media.metadata" }
    BusTopic { id: playback; topic: "media.status" }

    readonly property bool hasTrack: !!metadata.data.title && playback.data.state !== undefined && playback.data.state !== "stopped"

    // Position advances locally between the (sparse) status updates while playing.
    property real position: playback.data.position || 0
    Connections {
        target: playback
        function onDataChanged() { card.position = playback.data.position || 0 }
    }
    Timer {
        interval: 1000
        repeat: true
        running: card.visible && playback.data.state === "playing"
        onTriggered: card.position = Math.min(card.position + 1, metadata.data.duration || card.position + 1)
    }

    Image {
        anchors.fill: parent
        source: metadata.data.cover || ""
        fillMode: Image.PreserveAspectCrop
        opacity: 0.18
        visible: card.hasTrack
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacing * 1.5
        spacing: Theme.spacing * 1.5
        visible: card.hasTrack

        Rectangle {
            Layout.preferredWidth: Math.min(parent.height, Theme.fontHuge * 1.6)
            Layout.preferredHeight: Layout.preferredWidth
            radius: Theme.radius / 2
            color: Theme.surfaceHigh
            clip: true

            Image {
                anchors.fill: parent
                anchors.margins: metadata.data.cover ? 0 : parent.width * 0.25
                source: metadata.data.cover || "qrc:/mp3-hot.png"
                fillMode: metadata.data.cover ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                opacity: metadata.data.cover ? 1 : 0.5
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing / 3

            Text {
                Layout.fillWidth: true
                text: metadata.data.title || ""
                color: Theme.text
                font.pixelSize: Theme.fontLarge
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: [metadata.data.artist, metadata.data.album].filter(function(part) { return part }).join(" · ")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                elide: Text.ElideRight
            }
            Text {
                text: (playback.data.state === "paused" ? "Pausiert" : "Wiedergabe") + (playback.data.app ? " · " + playback.data.app : "")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacing / 2
                Layout.preferredHeight: Math.max(3, Theme.spacing / 4)
                radius: height / 2
                color: Theme.outline
                visible: (metadata.data.duration || 0) > 0

                Rectangle {
                    width: parent.width * Math.min(1, card.position / Math.max(1, metadata.data.duration))
                    height: parent.height
                    radius: height / 2
                    color: Theme.primary
                }
            }
        }
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing / 2
        visible: !card.hasTrack

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "qrc:/mp3-hot.png"
            width: Theme.fontHuge * 0.5
            height: width
            fillMode: Image.PreserveAspectFit
            opacity: 0.5
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Keine Wiedergabe"
            color: Theme.textDim
            font.pixelSize: Theme.fontNormal
        }
    }
}
