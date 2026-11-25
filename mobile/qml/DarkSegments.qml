import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// The desktop's SegmentedControl (ui-window.md) in the dark tones (ui-mobile.md, "Segmented
// controls"): track 32, radius 8, 1 px darkDivider border, padding 4; segments 24 high, radius 4,
// 12 px text (or a 16 px icon), the active one on darkRaised at weight 600; a 48 px touch target.
// segments: [{text, icon, enabled, objectName}]; current: the active index, -1 none (mixed values);
// checkedSet: for toggles, which segments are on.
Item {
    id: control
    property var segments: []
    property int current: -1
    // Toggles (bold, italic...): one flag per segment instead of one current index.
    property var checkedSet: undefined
    signal chosen(int index)
    implicitHeight: Theme.touchTarget
    implicitWidth: 200

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: Theme.groupButtonSize
        radius: Theme.surfaceRadius
        color: Theme.darkSurface
        border.width: 1
        border.color: Theme.darkDivider
    }
    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.grid
        anchors.rightMargin: Theme.grid
        Repeater {
            model: control.segments
            AbstractButton {
                id: segment
                required property int index
                required property var modelData
                readonly property bool active: control.checkedSet ? control.checkedSet[index] === true : index === control.current
                objectName: modelData.objectName || ""
                width: (control.width - 2 * Theme.grid) / Math.max(1, control.segments.length)
                height: control.height
                enabled: modelData.enabled !== false
                focusPolicy: Qt.NoFocus
                text: modelData.text || ""
                Accessible.name: text
                Accessible.checkable: true
                Accessible.checked: active
                onClicked: control.chosen(index)
                background: Rectangle {
                    anchors.centerIn: parent
                    width: parent.width
                    height: 6 * Theme.grid
                    radius: Theme.controlRadius
                    color: segment.active ? Theme.darkRaised : segment.down ? Theme.darkDivider : "transparent"
                }
                contentItem: Item {
                    opacity: segment.enabled ? 1 : 0.4
                    IconImage {
                        anchors.centerIn: parent
                        visible: !!segment.modelData.icon
                        source: segment.modelData.icon || ""
                        width: Theme.smallIconSize
                        height: Theme.smallIconSize
                        color: Theme.darkText
                        sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
                    }
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - 2 * Theme.grid
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                        visible: !segment.modelData.icon
                        text: segment.text
                        color: Theme.darkText
                        font.pixelSize: Theme.captionSize
                        font.weight: segment.active ? Font.DemiBold : Font.Normal
                    }
                }
            }
        }
    }
}
