import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-112: a long press on the sheet select, as the desktop's right click on it: Rename sheet…,
// Duplicate sheet, Delete sheet (disabled with one sheet), for the active sheet. The sheet list's
// look (dark surface, divider border, radius 8, 48 px rows, ui-mobile.md), under the select.
Popup {
    id: menu
    objectName: "sheetMenu"
    required property MobileMap map
    signal renameRequested()
    signal deleteRequested()

    padding: Theme.grid
    width: 220
    implicitHeight: column.implicitHeight + 2 * padding
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.surfaceRadius
        color: Theme.darkSurface
        border.width: 1
        border.color: Theme.darkDivider
    }

    component MenuRow: ItemDelegate {
        id: row
        width: column.width
        implicitHeight: Theme.touchTarget
        padding: 0
        leftPadding: 3 * Theme.grid
        rightPadding: 3 * Theme.grid
        contentItem: Label {
            text: row.text
            color: Theme.darkText
            opacity: row.enabled ? 1 : 0.4
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: row.down ? Theme.darkDivider : "transparent"
        }
    }

    contentItem: Column {
        id: column
        MenuRow {
            objectName: "renameSheetButton"
            text: qsTr("Rename sheet…")
            onClicked: { menu.close(); menu.renameRequested() }
        }
        MenuRow {
            objectName: "duplicateSheetButton"
            text: qsTr("Duplicate sheet")
            onClicked: { menu.close(); menu.map.duplicateSheet() }
        }
        MenuRow {
            objectName: "deleteSheetButton"
            text: qsTr("Delete sheet")
            enabled: menu.map.sheets.length > 1
            onClicked: { menu.close(); menu.deleteRequested() }
        }
    }
}
