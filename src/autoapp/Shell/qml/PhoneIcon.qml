import QtQuick
import OpenHU 1.0

// Phone outline, crossed out when no phone is connected.
Canvas {
    id: icon
    width: Theme.fontNormal * 0.8
    height: Theme.fontNormal * 1.2

    property bool crossed: false
    property color color: Theme.textDim

    onCrossedChanged: requestPaint()
    onColorChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        var w = width, h = height, line = Math.max(1.5, w / 8)
        ctx.reset()
        ctx.strokeStyle = icon.color
        ctx.fillStyle = icon.color
        ctx.lineWidth = line

        // body
        var r = w / 5, x = line / 2, y = line / 2, bw = w - line, bh = h - line
        ctx.beginPath()
        ctx.moveTo(x + r, y)
        ctx.arcTo(x + bw, y, x + bw, y + bh, r)
        ctx.arcTo(x + bw, y + bh, x, y + bh, r)
        ctx.arcTo(x, y + bh, x, y, r)
        ctx.arcTo(x, y, x + bw, y, r)
        ctx.closePath()
        ctx.stroke()

        // home button
        ctx.beginPath()
        ctx.arc(w / 2, h - line * 2.6, line * 0.7, 0, 2 * Math.PI)
        ctx.fill()

        if (icon.crossed) {
            ctx.beginPath()
            ctx.moveTo(-line, h + line)
            ctx.lineTo(w + line, -line)
            ctx.lineWidth = line * 1.2
            ctx.stroke()
        }
    }
}
