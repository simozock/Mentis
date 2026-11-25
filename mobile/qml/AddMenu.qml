import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-118: the topic bar's "+" menu, the adding tools that do not fit on the bar; also the top
// right group's map menu (Find, Present, Outliner, Gantt; user's request 2026-11-13), opened under its arrow,
// and (FEAT-121) the outliner's row menu and "+" menu. The marker menu's
// look (ui-mobile.md: darkSurface, 1 px darkDivider, radius 8, padding 4, 48 px rows), each row the
// tool's 20 px icon and its 14 px name. It opens 4 px above the "+" (below the arrow) and scrolls when taller than the
// room. A tap on a row closes the menu, then does what the tool's button does.
Popup {
    id: menu
    objectName: "addMenu"
    property var tools: []
    // The rows' column, for the tests.
    readonly property Item rows: column

    padding: Theme.grid
    width: 220
    implicitHeight: column.implicitHeight + 2 * padding
    // Modal with a transparent overlay: a tap outside (also on the "+") only closes the menu, it
    // does not reach the map or the bar (FEAT-118, "without doing anything").
    modal: true
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // button: the "+" GroupButton, its visible 32 px square centred in its 48 px target. A tap on the
    // "+" while the menu is open is a tap outside; a click without a press (TalkBack) closes it here.
    function toggleAbove(button, tools) {
        toggle(button, tools, false)
    }
    // The top right group's map menu: 4 px under the button, its right edge on the button's.
    function toggleBelow(button, tools) {
        toggle(button, tools, true)
    }
    function toggle(button, tools, below) {
        if (menu.visible) {
            menu.close()
            return
        }
        menu.tools = tools
        const margin = 4 * Theme.grid
        const right = button.mapToItem(parent, (button.width + button.visualWidth) / 2, 0).x
        const squareTop = button.mapToItem(parent, 0, (button.height - Theme.groupButtonSize) / 2).y
        // The rows' height from their count: the column is laid out only at the next polish. At
        // least one row, also in a very short window.
        const wanted = tools.length * Theme.touchTarget + 2 * padding
        const minimum = Theme.touchTarget + 2 * padding
        if (below) {
            const top = squareTop + Theme.groupButtonSize + Theme.grid
            menu.height = Math.max(minimum, Math.min(wanted, parent.height - top - margin))
            menu.y = top
        } else {
            const bottom = squareTop - Theme.grid
            menu.height = Math.max(minimum, Math.min(wanted, bottom - Theme.grid))
            menu.y = bottom - menu.height
        }
        // Inside the page's margins; a page narrower than the menu keeps its left margin.
        menu.x = Math.max(margin, Math.min(right - width, parent.width - width - margin))
        menu.open()
    }

    background: Rectangle {
        radius: Theme.surfaceRadius
        color: Theme.darkSurface
        border.width: 1
        border.color: Theme.darkDivider
    }

    contentItem: Flickable {
        clip: true
        contentHeight: column.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: column
            width: parent.width
            Repeater {
                model: menu.tools
                ItemDelegate {
                    id: row
                    required property var modelData
                    objectName: modelData.objectName + "Row"
                    width: column.width
                    implicitHeight: Theme.touchTarget
                    enabled: modelData.enabled
                    padding: 0
                    leftPadding: 3 * Theme.grid
                    rightPadding: 3 * Theme.grid
                    Accessible.name: modelData.menuText
                    Accessible.checkable: modelData.checkable || modelData.checked
                    Accessible.checked: modelData.checked
                    contentItem: Row {
                        spacing: 2 * Theme.grid
                        opacity: row.enabled ? 1 : 0.4
                        IconImage {
                            id: rowIcon
                            anchors.verticalCenter: parent.verticalCenter
                            // FEAT-121: rows without an icon (the outliner row menu, as the desktop's
                            // context menu) start their text at the 12 px padding.
                            visible: row.modelData.icon.source.toString() !== ""
                            width: Theme.iconSize
                            height: Theme.iconSize
                            source: row.modelData.icon.source
                            color: Theme.darkText
                            sourceSize: Qt.size(Theme.iconSize * Screen.devicePixelRatio, Theme.iconSize * Screen.devicePixelRatio)
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            width: row.availableWidth - (rowIcon.visible ? Theme.iconSize + parent.spacing : 0)
                            elide: Text.ElideRight
                            text: row.modelData.menuText
                            color: Theme.darkText
                            font.pixelSize: Theme.bodySize
                            font.bold: row.modelData.checked
                        }
                    }
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: row.down ? Theme.darkDivider : row.modelData.checked ? Theme.darkRaised : "transparent"
                    }
                    onClicked: {
                        const tool = row.modelData
                        menu.close()
                        tool.clicked()
                    }
                }
            }
        }
    }
}
