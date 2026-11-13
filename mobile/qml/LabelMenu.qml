import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-114: a long press on a tag pill (user's choice 2026-11-12; a tap highlights its topics):
// Edit tags, which opens the Labels modal. The marker menu's look (ui-mobile.md, "Marker menu").
Popup {
    id: menu
    objectName: "labelMenu"

    signal editRequested()

    padding: Theme.grid
    width: 220
    implicitHeight: editRow.implicitHeight + 2 * padding
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // x, y: the item point below the pill (MapCanvas.labelMenuRequested), in `parent`'s frame; the
    // menu stays inside it.
    function openAt(x, y) {
        menu.x = Math.max(Theme.grid, Math.min(x - width / 2, parent.width - width - Theme.grid))
        const menuHeight = Theme.touchTarget + 2 * padding
        const below = y + Theme.grid
        menu.y = below + menuHeight <= parent.height - Theme.grid ? below : Math.max(Theme.grid, y - menuHeight - 6 * Theme.grid)
        menu.open()
    }

    background: Rectangle {
        radius: Theme.surfaceRadius
        color: Theme.darkSurface
        border.width: 1
        border.color: Theme.darkDivider
    }

    contentItem: ItemDelegate {
        id: editRow
        objectName: "editLabelsButton"
        implicitHeight: Theme.touchTarget
        padding: 0
        leftPadding: 3 * Theme.grid
        rightPadding: 3 * Theme.grid
        contentItem: Label {
            text: qsTr("Edit tags")
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: editRow.down ? Theme.darkDivider : "transparent"
        }
        onClicked: {
            menu.close()
            menu.editRequested()
        }
    }
}
