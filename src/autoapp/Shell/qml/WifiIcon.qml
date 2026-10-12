import QtQuick
import OpenHU 1.0

// WiFi fan, shown in the status bar while OpenHU is in a known WiFi network; dim without internet.
Canvas {
    id: icon
    width: Theme.fontNormal * 1.1
    height: Theme.fontNormal * 0.9

    property color color: Theme.text

    onColorChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        var w = width, h = height, line = Math.max(1.5, w / 9)
        ctx.reset()
        ctx.strokeStyle = icon.color
        ctx.fillStyle = icon.color
        ctx.lineWidth = line
        ctx.lineCap = "round"
        // three arcs around the dot at the bottom centre
        var cx = w / 2, cy = h - line
        for (var i = 1; i <= 3; ++i) {
            var r = (h - line * 1.5) * i / 3
            ctx.beginPath()
            ctx.arc(cx, cy, r, -Math.PI * 0.75, -Math.PI * 0.25)
            ctx.stroke()
        }
        ctx.beginPath()
        ctx.arc(cx, cy, line * 0.9, 0, Math.PI * 2)
        ctx.fill()
    }
}
