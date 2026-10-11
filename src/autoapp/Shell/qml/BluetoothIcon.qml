import QtQuick
import OpenHU 1.0

// Bluetooth rune, shown in the status bar while the car radio is connected.
Canvas {
    id: icon
    width: Theme.fontNormal * 0.75
    height: Theme.fontNormal * 1.2

    property color color: Theme.primary

    onColorChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        var w = width, h = height, line = Math.max(1.5, w / 7)
        ctx.reset()
        ctx.strokeStyle = icon.color
        ctx.lineWidth = line
        ctx.lineJoin = "round"
        ctx.lineCap = "round"

        var left = w * 0.18, right = w * 0.82, mid = w * 0.5
        var top = line, bottom = h - line
        ctx.beginPath()
        ctx.moveTo(left, h * 0.3)
        ctx.lineTo(right, h * 0.7)
        ctx.lineTo(mid, bottom)
        ctx.lineTo(mid, top)
        ctx.lineTo(right, h * 0.3)
        ctx.lineTo(left, h * 0.7)
        ctx.stroke()
    }
}
