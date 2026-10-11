import QtQuick
import OpenHU 1.0

// Mobile reception, level 0-4.
Row {
    id: bars
    spacing: Math.max(1, Theme.fontNormal / 10)

    property int level: 0

    Repeater {
        model: 4
        Rectangle {
            anchors.bottom: parent.bottom
            width: Math.max(2, Theme.fontNormal / 5)
            height: Theme.fontNormal * (0.35 + index * 0.22)
            radius: width / 3
            color: index < bars.level ? Theme.text : Theme.outline
        }
    }
}
