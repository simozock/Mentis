import QtQuick
import Mentis.Mobile

// The pointer side of TipLabel: an item over `root` (above its controls) whose handlers watch the
// pen, the mouse and long presses without taking them (a PointHandler's passive grab, a
// non-blocking hover), so the controls under it work as before. A disabled row gets no presses, so
// the label under the point is looked up from here: on a long press, and a moment after the hover
// stops moving (a walk of the panel's items, not one at every move).
Item {
    id: area
    property Item root: parent
    property Item pointed: null
    property Item held: null

    function labelAt(item, x, y) {
        for (let i = item.children.length - 1; i >= 0; --i) {
            const child = item.children[i]
            if (child === area || !child.visible || child.opacity === 0) continue
            if (child.tipLabel === true) {
                if (child.at(area, x, y)) return child
                continue
            }
            if (child.clip && !child.contains(area.mapToItem(child, x, y))) continue
            const found = labelAt(child, x, y)
            if (found) return found
        }
        return null
    }
    function point(label) {
        if (pointed === label) return
        if (pointed) pointed.pointed = false
        pointed = label
        if (label) label.pointed = true
    }

    HoverHandler {
        id: hover
        onHoveredChanged: if (!hovered) area.point(null)
        onPointChanged: if (hovered) lookup.restart()
    }
    Timer {
        id: lookup
        interval: 50
        onTriggered: area.point(hover.hovered && !tap.pressed ? area.labelAt(area.root, hover.point.position.x, hover.point.position.y) : null)
    }
    // A PointHandler only watches (a passive grab): a TapHandler here kept the press from the rows.
    // The long press is its own: Qt's press and hold interval, ended by a drag (the panel's scroll).
    PointHandler {
        id: tap
        readonly property bool pressed: active
        property point pressedAt
        onActiveChanged: {
            if (active) {
                area.point(null)
                pressedAt = point.position
                holdTimer.restart()
            } else {
                holdTimer.stop()
                if (area.held) {
                    area.held.release()
                    area.held = null
                }
            }
        }
        onPointChanged: if (active && holdTimer.running
                            && Math.hypot(point.position.x - pressedAt.x, point.position.y - pressedAt.y) > Qt.styleHints.startDragDistance)
            holdTimer.stop()
        onCanceled: {
            holdTimer.stop()
            if (area.held) {
                area.held.cancel()
                area.held = null
            }
        }
    }
    Timer {
        id: holdTimer
        interval: Qt.styleHints.mousePressAndHoldInterval
        onTriggered: {
            const label = area.labelAt(area.root, tap.pressedAt.x, tap.pressedAt.y)
            if (!label || label.heldByButton) return
            area.held = label
            label.hold()
        }
    }
}
