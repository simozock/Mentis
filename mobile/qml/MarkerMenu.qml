import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-106: a tap on a topic's marker, as the desktop's right click on it (MainWindow::showMarkerMenu):
// the values of its series, the topic's one checked, then Remove marker. The list is dark as the
// sheet select's (surface, divider border, radius 8, 48 px rows for the finger, ui-mobile.md); an own
// marker has only Remove. A tap on a row applies it (one Undo step) and closes the menu.
Popup {
    id: menu
    objectName: "markerMenu"
    required property MobileMap map
    property string marker
    property var choices: []

    padding: Theme.grid
    width: 220
    implicitHeight: column.implicitHeight + 2 * padding
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // x, y: the item point below the marker (MapCanvas.markerRequested), in `parent`'s frame; the
    // menu stays inside it.
    function openAt(marker, x, y) {
        menu.marker = marker
        menu.choices = menu.map.markerChoices(marker)
        menu.x = Math.max(Theme.grid, Math.min(x - width / 2, parent.width - width - Theme.grid))
        // The rows' height from their count: the column is laid out only at the next polish.
        const menuHeight = (menu.choices.length + 1) * Theme.touchTarget + (menu.choices.length > 0 ? 1 : 0) + 2 * padding
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

    contentItem: Column {
        id: column
        Repeater {
            model: menu.choices
            ItemDelegate {
                id: row
                required property var modelData
                objectName: "markerChoice"
                width: column.width
                implicitHeight: Theme.touchTarget
                padding: 0
                leftPadding: 3 * Theme.grid
                rightPadding: 3 * Theme.grid
                contentItem: Row {
                    spacing: 2 * Theme.grid
                    MarkerIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.iconSize
                        height: Theme.iconSize
                        marker: row.modelData.marker
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData.label
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
                    menu.close()
                    if (!row.modelData.checked) menu.map.setMarker(row.modelData.marker)
                }
            }
        }
        Rectangle {
            visible: menu.choices.length > 0
            width: column.width
            height: 1
            color: Theme.darkDivider
        }
        ItemDelegate {
            id: removeRow
            objectName: "removeMarkerButton"
            width: column.width
            implicitHeight: Theme.touchTarget
            padding: 0
            leftPadding: 3 * Theme.grid
            rightPadding: 3 * Theme.grid
            contentItem: Label {
                text: qsTr("Remove marker")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.controlRadius
                color: removeRow.down ? Theme.darkDivider : "transparent"
            }
            onClicked: {
                menu.close()
                menu.map.removeMarker(menu.marker)
            }
        }
    }
}
