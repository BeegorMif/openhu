import QtQuick
import OpenHU 1.0
import QtQuick.Layouts
import "."

// Current track (media.metadata / media.status): from Android Auto or a local source such as the radio plugin
// ("source": "radio", radio 2.3).
Rectangle {
    id: card
    radius: Theme.radius
    color: Theme.surface
    clip: true

    // the source that plays (or started last): its metadata also after a switch, not that of the last publisher
    BusTopic { id: current; topic: "media.current" }
    readonly property var metadata: current.data.metadata || ({})
    readonly property var playback: current.data.status || ({})

    readonly property bool hasTrack: !!card.metadata.title && card.playback.state !== undefined && card.playback.state !== "stopped"

    // Position advances locally between the (sparse) status updates while playing.
    property real position: card.playback.position || 0
    onPlaybackChanged: card.position = card.playback.position || 0
    Timer {
        interval: 1000
        repeat: true
        running: card.visible && card.playback.state === "playing"
        onTriggered: card.position = Math.min(card.position + 1, card.metadata.duration || card.position + 1)
    }

    Image {
        anchors.fill: parent
        source: card.metadata.cover || ""
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
                anchors.margins: card.metadata.cover ? 0 : parent.width * 0.25
                source: card.metadata.cover || "qrc:/mp3-hot.png"
                fillMode: card.metadata.cover ? Image.PreserveAspectCrop : Image.PreserveAspectFit
                opacity: card.metadata.cover ? 1 : 0.5
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing / 3

            Text {
                Layout.fillWidth: true
                text: card.metadata.title || ""
                color: Theme.text
                font.pixelSize: Theme.fontLarge
                font.bold: true
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: [card.metadata.artist, card.metadata.album].filter(function(part) { return part }).join(" · ")
                color: Theme.textDim
                font.pixelSize: Theme.fontNormal
                elide: Text.ElideRight
            }
            Text {
                text: (card.playback.state === "paused" ? qsTrId("home.media.paused") : qsTrId("home.media.playing")) + (card.playback.app ? " · " + card.playback.app : "")
                color: Theme.textDim
                font.pixelSize: Theme.fontSmall
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: Theme.spacing / 2
                Layout.preferredHeight: Math.max(3, Theme.spacing / 4)
                radius: height / 2
                color: Theme.outline
                visible: (card.metadata.duration || 0) > 0

                Rectangle {
                    width: parent.width * Math.min(1, card.position / Math.max(1, card.metadata.duration))
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
            text: qsTrId("home.media.nothing_playing")
            color: Theme.textDim
            font.pixelSize: Theme.fontNormal
        }
    }
}
