import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-122 B2: the desktop's calendar (DatePicker) in a dark modal, for the Gantt's Start and End:
// the month and year with ‹ ›, the weekdays' two letters (12 px darkTextMuted), six weeks from the
// locale's first day (names in the interface language, MobileGanttTask::calendar), then Today. The
// chosen day on accent, today in bold, the other months' days darkTextMuted. Day cells 48 x 40,
// 48 x 48 targets (the modal's sides 8 px, so 7 x 48 fit a 360 dp phone). A day picks and closes.
ModalDialog {
    id: sheet
    objectName: "datePickerSheet"
    property string chosen: ""   // ISO
    property string month: ""    // any day of the month shown, ISO
    property var callback: null
    required property MobileGanttTask task  // the calendar's names and days
    readonly property var shown: sheet.task.calendar(sheet.month)
    readonly property real cell: Math.min(Theme.touchTarget, Math.floor(availableWidth / 7))

    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(4 * Theme.grid, (parent.height - height) / 2) : 0
    width: Math.min(parent ? parent.width - 2 * Theme.grid : 0, 400)
    leftPadding: 2 * Theme.grid
    rightPadding: 2 * Theme.grid

    // title: "Start" or "End"; date: the ISO day shown, empty for today's month.
    function openFor(title, date, picked) {
        sheet.title = title
        sheet.chosen = date
        sheet.month = date !== "" ? date : sheet.task.today()
        sheet.callback = picked
        sheet.open()
    }
    function pick(iso) {
        const picked = sheet.callback
        sheet.close()
        if (picked) picked(iso)
    }

    Column {
        width: sheet.availableWidth
        Item {
            width: parent.width
            height: Theme.touchTarget
            Label {
                objectName: "dateMonthTitle"
                anchors.left: parent.left
                anchors.leftMargin: 2 * Theme.grid
                anchors.verticalCenter: parent.verticalCenter
                text: sheet.shown.title
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                font.weight: Font.DemiBold
            }
            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                MonthButton { objectName: "datePrevious"; turn: 90; Accessible.name: qsTr("Previous month"); onClicked: sheet.month = sheet.task.addMonths(sheet.month, -1) }
                MonthButton { objectName: "dateNext"; turn: -90; Accessible.name: qsTr("Next month"); onClicked: sheet.month = sheet.task.addMonths(sheet.month, 1) }
            }
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            Repeater {
                model: sheet.shown.weekdays
                Label {
                    required property string modelData
                    width: sheet.cell
                    height: 6 * Theme.grid
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    color: Theme.darkTextMuted
                    font.pixelSize: Theme.captionSize
                }
            }
        }
        Grid {
            anchors.horizontalCenter: parent.horizontalCenter
            columns: 7
            Repeater {
                model: sheet.shown.days
                AbstractButton {
                    id: day
                    required property var modelData
                    objectName: "day_" + modelData.iso
                    readonly property bool picked: modelData.iso === sheet.chosen
                    width: sheet.cell
                    height: Theme.touchTarget
                    focusPolicy: Qt.NoFocus
                    Accessible.name: modelData.iso
                    onClicked: sheet.pick(modelData.iso)
                    background: Rectangle {
                        anchors.centerIn: parent
                        width: sheet.cell - 2 * 2
                        height: 10 * Theme.grid
                        radius: Theme.controlRadius
                        color: day.picked ? Theme.accent : day.down ? Theme.darkDivider : "transparent"
                    }
                    contentItem: Label {
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: day.modelData.day
                        color: day.picked ? "#ffffff" : day.modelData.inMonth ? Theme.darkText : Theme.darkTextMuted
                        font.pixelSize: Theme.bodySize
                        font.bold: day.modelData.today || day.picked
                    }
                }
            }
        }
        Item { width: 1; height: Theme.grid }
    }
    buttons: [
        DarkButton { objectName: "dateToday"; text: qsTranslate("DatePicker", "Today"); onClicked: sheet.pick(sheet.task.today()) }
    ]

    component MonthButton: AbstractButton {
        id: button
        property int turn: 0
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        contentItem: Item {
            IconImage {
                anchors.centerIn: parent
                width: Theme.iconSize
                height: Theme.iconSize
                rotation: button.turn
                source: "qrc:/assets/hugeicons/arrow-down.svg"
                color: Theme.darkText
                sourceSize: Qt.size(Theme.iconSize * Screen.devicePixelRatio, Theme.iconSize * Screen.devicePixelRatio)
            }
        }
        background: Rectangle {
            anchors.centerIn: parent
            width: Theme.buttonHeight
            height: Theme.buttonHeight
            radius: Theme.controlRadius
            color: button.down ? Theme.darkDivider : "transparent"
        }
    }
}
