import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// A control of a floating group (docs/design/ui-components.md, "Gruppi flottanti fissi"): with
// icon.source an icon button 32 x 32 with a 20 px icon, otherwise a text button 32 high, 14 px text,
// 8 px padding; radius 6, hover and pressed fills of ui-foundations.md. The touch target around it
// is 48 high and 1 px wider at each side, so the visible buttons of a group are 2 px apart as on the
// desktop (approved exception, ui-foundations.md). Disabled: drawn at 40%. Checked
// (a checkable button that is on): inverted, #1f2937 with a white icon (ui-components.md).
AbstractButton {
    id: control
    readonly property bool iconOnly: icon.source.toString() !== ""
    readonly property real visualWidth: iconOnly ? Theme.groupButtonSize : label.implicitWidth + 4 * Theme.grid
    padding: 0
    // No keyboard focus: Space, Enter and Tab are the map's keys (KeyCommands), not a button's.
    focusPolicy: Qt.NoFocus
    implicitWidth: visualWidth + Theme.groupGap
    implicitHeight: Theme.touchTarget
    Accessible.name: text

    background: Item {
        Rectangle {
            anchors.centerIn: parent
            width: control.visualWidth
            height: Theme.groupButtonSize
            radius: Theme.groupButtonRadius
            color: control.checked ? Theme.text : control.down ? Theme.pressedFill : control.hovered ? Theme.hoverFill : "transparent"
        }
    }
    contentItem: Item {
        opacity: control.enabled ? 1 : 0.4
        // IconImage tints the icon when checked; "transparent" leaves its own colours.
        IconImage {
            anchors.centerIn: parent
            visible: control.iconOnly
            color: control.checked ? Theme.surface : "transparent"
            source: control.icon.source
            width: Theme.iconSize
            height: Theme.iconSize
            sourceSize: Qt.size(Theme.iconSize * Screen.devicePixelRatio, Theme.iconSize * Screen.devicePixelRatio)
        }
        Label {
            id: label
            anchors.centerIn: parent
            visible: !control.iconOnly
            text: control.text
            color: control.checked ? Theme.surface : Theme.text
            font.pixelSize: Theme.bodySize
        }
    }
}
