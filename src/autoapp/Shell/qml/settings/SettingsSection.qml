import QtQuick
import QtQuick.Controls
import OpenHU 1.0

// Scrollable column of a settings category.
Flickable {
    id: section
    default property alias content: column.data
    contentWidth: width
    contentHeight: column.implicitHeight + Theme.spacing * 2
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    // plain Column: rows with wrapped text get their width from here (no layout loop)
    Column {
        id: column
        x: Theme.spacing * 1.5
        y: Theme.spacing
        width: section.width - Theme.spacing * 3
        spacing: Theme.spacing / 2
    }
}
