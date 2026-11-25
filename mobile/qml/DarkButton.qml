import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// A button of the dark surfaces, as the desktop's modals (docs/design/ui-components.md, "Modali"):
// 32 high, radius 4, padding 0 16. Secondary (e.g. "Today"): surfaceRaised, 1 px divider border, divider
// when pressed. Primary: accent, text weight 600 (Atkinson Hyperlegible draws it Bold), accent at
// 40% with muted text when disabled. Danger (FEAT-122 B2, the desktop's GanttPanelRemove): text and
// border danger. The touch target is 48 high (ui-mobile.md).
AbstractButton {
    id: control
    property bool primary: false
    property bool danger: false
    padding: 0
    implicitWidth: Math.max(Theme.touchTarget, label.implicitWidth + 8 * Theme.grid)
    implicitHeight: Theme.touchTarget

    background: Item {
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: Theme.buttonHeight
            radius: Theme.controlRadius
            color: control.primary ? (control.enabled ? Theme.accent : Theme.accentDisabled)
                                   : (control.down || control.hovered ? Theme.darkDivider : Theme.darkRaised)
            border.width: control.primary ? 0 : 1
            border.color: control.danger ? Theme.danger : Theme.darkDivider
        }
    }
    contentItem: Label {
        id: label
        text: control.text
        color: !control.enabled ? Theme.darkTextMuted : control.danger ? Theme.danger : Theme.darkText
        font.pixelSize: Theme.bodySize
        font.bold: control.primary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
