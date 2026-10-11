import QtQuick
import QtMultimedia
import OpenHU 1.0

// Android Auto inside the shell. The phone renders its UI into contentRect of the video frame
// (frame minus margins, chosen by AaBridge to match this page's aspect ratio); the video is
// scaled so exactly that part fills the page.
Rectangle {
    id: page
    color: "black"
    clip: true

    Binding {
        target: aaBridge
        property: "surfaceVisible"
        value: page.visible && aaBridge.videoActive
    }

    // Report the available size once the layout settled (margins are announced on the next connection).
    Timer {
        id: viewportTimer
        interval: 300
        onTriggered: if (page.width > 0 && page.height > 0) aaBridge.setViewportSize(page.width, page.height)
    }
    onWidthChanged: viewportTimer.restart()
    onHeightChanged: viewportTimer.restart()

    // The decoder (FfmpegVideoOutput) sends its frames to this sink
    VideoOutput {
        id: video
        readonly property real sx: page.width / aaBridge.contentRect.width
        readonly property real sy: page.height / aaBridge.contentRect.height

        Component.onCompleted: aaBridge.videoSink = video.videoSink
        fillMode: VideoOutput.Stretch
        x: -aaBridge.contentRect.x * sx
        y: -aaBridge.contentRect.y * sy
        width: aaBridge.videoSize.width * sx
        height: aaBridge.videoSize.height * sy
        visible: aaBridge.videoActive
    }

    AaSurface {
        anchors.fill: parent
        enabled: aaBridge.videoActive
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing
        visible: !aaBridge.videoActive

        Image {
            anchors.horizontalCenter: parent.horizontalCenter
            source: "qrc:/aa-hot.png"
            width: Theme.fontHuge
            height: width
            fillMode: Image.PreserveAspectFit
            opacity: 0.6
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Warte auf Android Auto …"
            color: Theme.textDim
            font.pixelSize: Theme.fontLarge
        }
    }
}
