import QtQuick
import Mentis.Mobile

// A floating group over the map, as the desktop's FloatingGroup (docs/design/ui-components.md,
// "Barra in alto"): white at alpha 214, 1 px border rgba(15,23,42,13), radius 10, 4 px round the
// 32 px buttons. Its children are GroupButtons (48 px high touch targets, 1 px at each side of the
// visible button): the frame is drawn 4 px inside the group, and the row is padded at its sides so
// the frame stays 4 px round the visible buttons. The light shadow is drawn outside the frame only: four
// 1 px rings, 1 px lower, whose alpha is the desktop's four layers summed where they overlap
// (4+3+2+1, 3+2+1, 2+1, 1). A few rectangles, no effect or offscreen buffer (AGENTS.md).
// Buttons wider than maximumWidth wrap into rows: the bottom row holds as many as fit, the rows
// above the rest, in reading order (the topic bar on a phone, user's request 2026-11-11).
Item {
    id: group
    default property alias content: row.data
    property real maximumWidth: Infinity
    // The row's padding at each side: 4 px of touch margin, 4 inside the frame, less the button's 1.
    readonly property real sidePadding: 2 * Theme.grid - Theme.groupGap / 2
    // The visible children split into rows, filled from the last child backwards so the bottom row
    // is the fullest; a child wider than the room still gets a row of its own.
    readonly property var lines: {
        const room = maximumWidth - 2 * sidePadding
        const result = []
        let line = []
        let width = 0
        for (let i = row.children.length - 1; i >= 0; --i) {
            const child = row.children[i]
            if (!child.visible) continue
            if (line.length > 0 && width + child.width > room) {
                result.unshift(line)
                line = []
                width = 0
            }
            line.unshift(child)
            width += child.width
        }
        if (line.length > 0) result.unshift(line)
        return result
    }
    readonly property int rows: Math.max(1, lines.length)
    implicitWidth: row.width
    implicitHeight: row.height

    onLinesChanged: place()
    Component.onCompleted: place()

    function place() {
        let y = 0
        let widest = 0
        for (const line of lines) {
            let x = sidePadding
            let height = 0
            for (const child of line) {
                child.x = x
                child.y = y
                x += child.width
                height = Math.max(height, child.height)
            }
            widest = Math.max(widest, x - sidePadding)
            y += height
        }
        row.width = widest + 2 * sidePadding
        row.height = y
    }

    Repeater {
        model: [10, 6, 3, 1]
        Rectangle {
            required property int index
            required property int modelData
            readonly property int grow: index + 1
            x: frame.x - grow
            y: frame.y - grow + 1
            width: frame.width + 2 * grow
            height: frame.height + 2 * grow
            radius: Theme.groupRadius + grow
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(Theme.shadow.r, Theme.shadow.g, Theme.shadow.b, modelData / 255)
        }
    }
    Rectangle {
        id: frame
        anchors.fill: parent
        anchors.margins: Theme.grid
        radius: Theme.groupRadius
        color: Theme.groupFill
        border.width: 1
        border.color: Theme.groupBorder
    }
    // FEAT-110: a press between the buttons or on a disabled one stays in the group: it reached the
    // map, which deselected the topic and hid the bar (user's report 2026-11-12).
    MouseArea {
        anchors.fill: parent
    }
    Item {
        id: row
    }
}
