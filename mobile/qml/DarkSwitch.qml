import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-114: the desktop's ToggleSwitch in the dark tones with its label, in a 48 px target: the
// label at the left (14 px darkText, wrapped), the switch at the right (track 36 x 20, radius 10,
// off darkRaised, on #878c92 as FEAT-072; a 16 px darkText knob). A tap toggles it (toggled()).
AbstractButton {
    id: control
    checkable: true
    focusPolicy: Qt.NoFocus
    implicitHeight: Math.max(Theme.touchTarget, switchLabel.implicitHeight + 2 * Theme.grid)
    Accessible.name: text
    contentItem: Item {
        Label {
            id: switchLabel
            anchors.left: parent.left
            anchors.right: track.left
            anchors.rightMargin: 3 * Theme.grid
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        Rectangle {
            id: track
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            height: 20
            radius: 10
            color: control.checked ? "#878c92" : Theme.darkRaised
            Rectangle {
                width: 16
                height: 16
                radius: 8
                y: 2
                x: control.checked ? parent.width - width - 2 : 2
                color: Theme.darkText
            }
        }
    }
}
