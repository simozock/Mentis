import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// The name of a control in a tooltip (ui-mobile.md, "Tooltip"), as the desktop's: 12 px darkText on
// darkSurface, radius 4, padding 4 x 8, 4 px under its parent's visible part when the parent is in
// the upper half of the window, over it otherwise. `inset` is the parent's empty band above and
// below its visible part (a button's touch target). The side is chosen at each showing, so a parent
// in a scrolled page gets the right one.
ToolTip {
    id: tip
    property real inset: 0
    property bool below: true
    onAboutToShow: below = parent.mapToItem(null, 0, 0).y < parent.Window.height / 2
    y: below ? parent.height - inset + Theme.grid : inset - implicitHeight - Theme.grid
    x: (parent.width - implicitWidth) / 2
    padding: 0
    topPadding: Theme.grid
    bottomPadding: Theme.grid
    leftPadding: 2 * Theme.grid
    rightPadding: 2 * Theme.grid
    closePolicy: Popup.NoAutoClose
    contentItem: Label {
        text: tip.text
        color: Theme.darkText
        font.pixelSize: Theme.captionSize
    }
    background: Rectangle {
        color: Theme.darkSurface
        radius: Theme.controlRadius
    }
}
