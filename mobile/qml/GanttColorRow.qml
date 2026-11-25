import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-122 C2: a Gantt colour row (the desktop's ColorField without opacity, then its × back to the
// default): the 14 px label, the 28 px square (radius 4, 1 px darkDivider; "—" when the tasks differ)
// in a 48 px target opening the colour sheet, and a 48 px × column kept at the right, shown when
// `resettable`.
Item {
    id: row
    property string label
    property string sheetTitle: label
    property var colorSheet
    property var color: undefined   // undefined or invalid: none shown ("—")
    property bool resettable: false
    property string resetName
    signal picked(color value)
    signal reset()
    readonly property bool shown: color !== undefined && color !== null && color.valid !== false && color.a > 0
    width: parent ? parent.width : 0
    height: Theme.touchTarget
    opacity: enabled ? 1 : 0.4

    TipLabel {
        anchors.left: parent.left
        anchors.right: swatch.left
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: row.label
        color: Theme.darkText
        font.pixelSize: Theme.bodySize
    }
    AbstractButton {
        id: swatch
        objectName: row.objectName + "Swatch"
        anchors.right: cross.left
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        Accessible.name: row.label
        onClicked: row.colorSheet.openWith(row.sheetTitle, row.shown ? row.color : "", chosen => row.picked(chosen))
        background: Item {}
        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                width: 28
                height: 28
                radius: Theme.controlRadius
                color: row.shown ? row.color : Theme.darkSunken
                border.width: 1
                border.color: swatch.down ? Theme.darkText : Theme.darkDivider
                Label {
                    anchors.centerIn: parent
                    visible: !row.shown
                    text: "—"
                    color: Theme.darkTextMuted
                    font.pixelSize: Theme.bodySize
                }
            }
        }
    }
    AbstractButton {
        id: cross
        objectName: row.objectName + "Reset"
        anchors.right: parent.right
        width: Theme.touchTarget
        height: Theme.touchTarget
        enabled: row.resettable
        opacity: row.resettable ? 1 : 0
        focusPolicy: Qt.NoFocus
        Accessible.name: row.resetName
        onClicked: row.reset()
        contentItem: Label {
            text: "×"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.darkTextMuted
            font.pixelSize: Theme.largeSize
        }
    }
}
