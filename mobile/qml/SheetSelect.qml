import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// The sheet select, as the desktop's FloatingSelect (docs/design/ui-components.md, "Gruppi flottanti
// fissi"): 32 high in its group, text 14 px weight 600, arrow-down.svg 16 px 8 px from the right.
// Its list is dark as the desktop's floating lists (surface, divider border, radius 8; the current
// sheet on surfaceRaised, pressed divider), with 48 px rows for the finger (ui-mobile.md).
// FEAT-112: a long press on the select asks for the sheet menu (menuRequested); in the open list a
// long press on a row picks it up and dragging moves it (moveRequested), the drop place a 2 px
// accent line (ui-components.md, "Sheet select"); a tap still picks the sheet.
ComboBox {
    id: control
    padding: 0
    signal menuRequested()
    signal moveRequested(int from, int to)
    // The row being dragged and the gap where it falls (0..count, before that row); -1 = none.
    property int dragFrom: -1
    property int dropSlot: -1
    // 1 px of touch padding at each side (a group's controls are 2 px apart) + 8 inside the select.
    readonly property real sideInset: Theme.groupGap / 2
    leftPadding: sideInset + 2 * Theme.grid
    rightPadding: sideInset + 2 * Theme.grid + Theme.smallIconSize + 2 * Theme.grid
    implicitHeight: Theme.touchTarget
    implicitWidth: Math.max(Theme.touchTarget, contentItem.implicitWidth + leftPadding + rightPadding)
    font.pixelSize: Theme.bodySize
    font.bold: true

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: control.sideInset
            anchors.rightMargin: control.sideInset
            anchors.topMargin: (control.height - Theme.groupButtonSize) / 2
            anchors.bottomMargin: (control.height - Theme.groupButtonSize) / 2
            radius: Theme.groupButtonRadius
            color: control.down || selectArea.pressed || control.popup.visible ? Theme.pressedFill : control.hovered ? Theme.hoverFill : "transparent"
        }
    }
    // The press is the area's, not the ComboBox's: a tap opens the list, a long press the menu.
    MouseArea {
        id: selectArea
        objectName: "sheetSelectArea"
        anchors.fill: parent
        onClicked: control.popup.visible ? control.popup.close() : control.popup.open()
        onPressAndHold: { control.popup.close(); control.menuRequested() }
    }

    contentItem: Label {
        text: control.displayText
        color: Theme.text
        font: control.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }
    indicator: Image {
        x: control.width - control.sideInset - 2 * Theme.grid - width
        anchors.verticalCenter: parent.verticalCenter
        source: "qrc:/assets/hugeicons/arrow-down.svg"
        width: Theme.smallIconSize
        height: Theme.smallIconSize
        sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
    }

    delegate: ItemDelegate {
        id: row
        objectName: "sheetRow"
        required property int index
        required property var modelData
        width: ListView.view.width
        implicitHeight: Theme.touchTarget
        padding: 0
        leftPadding: 3 * Theme.grid
        rightPadding: 3 * Theme.grid
        highlighted: control.highlightedIndex === index
        contentItem: Label {
            text: row.modelData
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
            font.bold: row.index === control.currentIndex
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: rowArea.pressed || row.index === control.dragFrom ? Theme.darkDivider
                : row.index === control.currentIndex ? Theme.darkRaised : "transparent"
        }
        MouseArea {
            id: rowArea
            anchors.fill: parent
            preventStealing: control.dragFrom === row.index
            onClicked: {
                // Closing the list destroys this row (the list's model goes): keep what is needed.
                const select = control
                const index = row.index
                select.popup.close()
                select.activated(index)
            }
            onPressAndHold: mouse => {
                control.dragFrom = row.index
                control.dropSlot = row.index
            }
            onPositionChanged: mouse => {
                if (control.dragFrom !== row.index) return
                const list = row.ListView.view
                const point = rowArea.mapToItem(list, mouse.x, mouse.y)
                list.edgeY = point.y
                control.dropSlot = Math.max(0, Math.min(list.count, Math.round((point.y + list.contentY) / Theme.touchTarget)))
            }
            onReleased: {
                if (control.dragFrom !== row.index) return
                const from = control.dragFrom
                const slot = control.dropSlot
                control.endDrag(row.ListView.view)
                // The gap before the row or after it is the start place.
                const to = slot > from ? slot - 1 : slot
                if (to !== from) control.moveRequested(from, to)
            }
            onCanceled: if (control.dragFrom === row.index) control.endDrag(row.ListView.view)
        }
    }

    function endDrag(list) {
        control.dragFrom = -1
        control.dropSlot = -1
        if (list) list.edgeY = -1
    }

    popup: Popup {
        // Under the select (at the top left on the phone, FEAT-112), 4 px under its visible part.
        y: control.height - 2 * Theme.grid + Theme.grid
        width: Math.max(control.width, 160)
        implicitHeight: Math.min(contentItem.implicitHeight + 2 * padding, 8 * Theme.touchTarget)
        padding: Theme.grid
        onClosed: control.endDrag(null)
        contentItem: ListView {
            id: sheetList
            objectName: "sheetList"
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            interactive: control.dragFrom < 0
            // The finger's y in the list while a row is dragged; near an edge the list scrolls.
            property real edgeY: -1
            Timer {
                interval: 16
                repeat: true
                running: sheetList.edgeY >= 0 && (sheetList.edgeY < Theme.touchTarget / 2 || sheetList.edgeY > sheetList.height - Theme.touchTarget / 2)
                onTriggered: {
                    const step = sheetList.edgeY < Theme.touchTarget / 2 ? -8 : 8
                    sheetList.contentY = Math.max(0, Math.min(sheetList.contentHeight - sheetList.height, sheetList.contentY + step))
                    control.dropSlot = Math.max(0, Math.min(sheetList.count, Math.round((sheetList.edgeY + sheetList.contentY) / Theme.touchTarget)))
                }
            }
            // The drop place: a 2 px accent line, radius 1, across the row width.
            Rectangle {
                objectName: "sheetDropLine"
                visible: control.dropSlot >= 0
                parent: sheetList.contentItem
                x: 0
                width: sheetList.width
                y: control.dropSlot * Theme.touchTarget - height / 2
                height: 2
                radius: 1
                color: Theme.accent
                z: 1
            }
        }
        background: Rectangle {
            radius: Theme.surfaceRadius
            color: Theme.darkSurface
            border.width: 1
            border.color: Theme.darkDivider
        }
    }
}
