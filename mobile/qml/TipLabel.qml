import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// A label of the side panel that shows its own text in a tooltip (user's request 2026-11-22, as an
// icon GroupButton): after 700 ms with the pen or the mouse over the text, or at once on a long
// press; after the release it stays 1.5 s. The panel's TipArea finds the label under the pen and sets
// `pointed` or calls hold(), also in a disabled row (a disabled item gets no presses). An enabled
// `button` around the label holds it itself, from its pressAndHold when holdsAt() says the press is
// on the text, and then skips its own long press: one timer decides, so a release never both shows
// the tip and presses the row.
// The tooltip (NameTip) is made the first time it is wanted.
Label {
    id: label
    readonly property bool tipLabel: true
    property bool pointed: false
    property bool held: false
    property Item button: null
    readonly property bool heldByButton: button !== null && button.enabled
    property NameTip tip: null
    // The painted text, not the empty rest of a label stretched to its row.
    readonly property real textWidth: Math.min(implicitWidth, width)
    readonly property bool tipWanted: text !== "" && (held || pointed)
    onTipWantedChanged: if (tipWanted && !tip) tip = tipComponent.createObject(label)

    // `x`, `y` in `item`: on the painted text.
    function at(item, x, y) {
        const point = item.mapToItem(label, x, y)
        return text !== "" && point.x >= 0 && point.x <= textWidth && point.y >= 0 && point.y <= height
    }
    // As at(), only for a label under a TipArea (SidePanel.tipArea), which shows the tip.
    function holdsAt(item, x, y) {
        for (let ancestor = parent; ancestor; ancestor = ancestor.parent)
            if (ancestor.tipArea !== undefined) return at(item, x, y)
        return false
    }
    function hold() { held = true }
    function release() { if (held) releaseTimer.restart() }
    function cancel() { held = false }

    Timer {
        id: releaseTimer
        interval: 1500
        onTriggered: label.held = false
    }
    Component {
        id: tipComponent
        NameTip {
            visible: label.tipWanted && label.visible
            delay: label.held ? 0 : 700  // Qt's tooltip wake-up delay, as GroupButton
            text: label.text
            // A label centred in a taller row: the tip goes by its text, not by the row.
            inset: label.verticalAlignment === Text.AlignVCenter ? Math.max(0, (label.height - label.contentHeight) / 2) : 0
            x: (label.textWidth - implicitWidth) / 2
        }
    }
}
