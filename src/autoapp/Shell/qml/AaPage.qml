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

    // debug video overlay (Settings → Debug, #120): what the decoder does
    BusTopic { id: debugState; topic: "debug.state" }
    BusTopic { id: videoStats; topic: "video.stats" }
    Rectangle {
        visible: debugState.data.videoOverlay === true && aaBridge.videoActive
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing / 2
        width: statsText.implicitWidth + Theme.spacing
        height: statsText.implicitHeight + Theme.spacing / 2
        color: Qt.rgba(0, 0, 0, 0.6)
        radius: Theme.spacing / 3
        Text {
            id: statsText
            anchors.centerIn: parent
            // untranslated: technical values
            text: (videoStats.data.codec || "?") + " " + (videoStats.data.width || 0) + "x" + (videoStats.data.height || 0)
                  + " · " + (videoStats.data.fps || 0) + " fps · " + (videoStats.data.decodeMs || 0) + " ms\n"
                  + (videoStats.data.decoder || "?") + (videoStats.data.hardware ? "" : " (software)")
            color: "white"
            font.pixelSize: Theme.fontSmall
            font.family: "monospace"
        }
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
            width: Math.min(implicitWidth, page.width - 2 * Theme.spacing)
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            text: aaBridge.videoError !== "" ? aaBridge.videoError : qsTrId("aa.waiting_android_auto")
            color: Theme.textDim
            font.pixelSize: Theme.fontLarge
        }
    }
}
