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
    // FEAT-118: an adding tool of the topic bar goes under the group's "+" when the bar does not fit.
    // `shown` says whether the selection wants it; `folded` is set by its FloatingGroup; `menuText`
    // names its row in the "+" menu.
    property bool adding: false
    property bool shown: true
    property bool folded: false
    property string menuText
    visible: shown && !(adding && folded)
    padding: 0
    // No keyboard focus: Space, Enter and Tab are the map's keys (KeyCommands), not a button's.
    focusPolicy: Qt.NoFocus
    implicitWidth: visualWidth + Theme.groupGap
    implicitHeight: Theme.touchTarget
    Accessible.name: text
    // The pen's hover (PenAsMouse) shows the hover fill and the tooltip, whatever the platform's default.
    hoverEnabled: true

    // The button's name in a tooltip, as the desktop's (user's request 2026-11-19): an icon button,
    // after 700 ms with the pen or the mouse over it (Qt's tooltip wake-up delay,
    // QStyle::SH_ToolTip_WakeUpDelay), or at once on a long press, which then does not press it
    // (AbstractButton skips clicked after pressAndHold); after the release it stays 1.5 s. A text
    // button shows its name already: a long press presses it, as before.
    // The tooltip is made the first time it is wanted: most buttons never show one.
    property bool tipHeld: false
    property NameTip tip: null
    readonly property bool tipWanted: iconOnly && text !== "" && (tipHeld || hovered)
    onTipWantedChanged: if (tipWanted && !tip) tip = tipComponent.createObject(control)
    onPressAndHold: {
        if (iconOnly) tipHeld = true
        else clicked()
    }
    onReleased: if (tipHeld) tipRelease.restart()
    onCanceled: tipHeld = false
    Timer {
        id: tipRelease
        interval: 1500
        onTriggered: control.tipHeld = false
    }
    Component {
        id: tipComponent
        NameTip {
            inset: (control.height - Theme.groupButtonSize) / 2
            visible: control.tipWanted && control.visible && (control.tipHeld || !control.down)
            delay: control.tipHeld ? 0 : 700
            text: control.text
        }
    }

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
