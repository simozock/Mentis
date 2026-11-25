import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-122 stage F: the desktop's column choice (FEAT-078, the table header's menu and its drag) as a
// dark modal. A switch per column: No. first (disabled without the map's numbering), then Start, End,
// Duration, %, Priority, People in their order, each with a grip at the left: dragged up or down it
// moves the column (the row dimmed, a 2 px accent line where it goes). Name is always shown. The choice is the
// app's (the desktop's settings), not the file's, and no Undo step.
ModalDialog {
    id: dialog
    objectName: "columnsDialog"
    required property MobileGantt gantt

    title: qsTr("Columns")
    closePolicy: Popup.CloseOnEscape
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 400)

    // The movable column being dragged and where it would go (indexes without No.).
    property int dragFrom: -1
    property int dragTo: -1
    readonly property int movableCount: Math.max(0, dialog.gantt.columns.length - 1)

    Item {
        width: dialog.availableWidth
        height: list.height
        Column {
            id: list
            width: parent.width
            Repeater {
                model: dialog.gantt.columns
                Item {
                    id: entry
                    required property var modelData
                    required property int index
                    readonly property int movableIndex: modelData.movable ? index - 1 : -1  // No. is first
                    readonly property bool dragged: grip.active
                    width: list.width
                    height: Theme.touchTarget
                    // Dimmed in place (not moved under the finger: the handler measures in this row).
                    opacity: dragged ? 0.5 : 1
                    Item {
                        id: gripArea
                        objectName: "columnGrip" + entry.index
                        width: Theme.touchTarget
                        height: parent.height
                        IconImage {
                            anchors.centerIn: parent
                            visible: entry.modelData.movable
                            width: Theme.smallIconSize
                            height: Theme.smallIconSize
                            source: "qrc:/assets/hugeicons/menu.svg"
                            color: Theme.darkTextMuted
                            sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
                        }
                        DragHandler {
                            id: grip
                            enabled: entry.modelData.movable
                            target: null
                            xAxis.enabled: false
                            onActiveChanged: {
                                if (active) {
                                    dialog.dragFrom = entry.movableIndex
                                    dialog.dragTo = entry.movableIndex
                                } else {
                                    const from = dialog.dragFrom, to = dialog.dragTo
                                    dialog.dragFrom = -1
                                    dialog.dragTo = -1
                                    dialog.gantt.moveColumn(from, to)
                                }
                            }
                            onTranslationChanged: if (active) dialog.dragTo = Math.max(0, Math.min(dialog.movableCount - 1,
                                                      entry.movableIndex + Math.round(translation.y / Theme.touchTarget)))
                        }
                    }
                    DarkSwitch {
                        objectName: "columnSwitch" + entry.index
                        anchors.left: gripArea.right
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modelData.title
                        checked: entry.modelData.shown
                        enabled: entry.modelData.enabled
                        opacity: enabled ? 1 : 0.4
                        onToggled: dialog.gantt.setColumnShown(entry.modelData.field, checked)
                    }
                }
            }
        }
        // Where the dragged column goes: before the row it is over, or after it when moving down.
        Rectangle {
            objectName: "columnDropLine"
            visible: dialog.dragFrom >= 0 && dialog.dragTo !== dialog.dragFrom
            x: Theme.touchTarget
            width: parent.width - x
            height: 2
            radius: 1
            color: Theme.accent
            y: (1 + dialog.dragTo + (dialog.dragTo > dialog.dragFrom ? 1 : 0)) * Theme.touchTarget - 1
        }
    }
    buttons: [
        DarkButton { objectName: "columnsDone"; text: qsTranslate("ColorSheet", "Done"); primary: true; onClicked: dialog.close() }
    ]
}
