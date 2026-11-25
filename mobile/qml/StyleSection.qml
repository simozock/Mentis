import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-119: a section of the side panel (Shape, Text, Branch...), as the desktop's StylePage::section:
// a 48 px header row with the section's 13 px semibold name and a chevron, which folds and unfolds
// it when `foldable` (the bottom half panel); its rows under it. The open state is the panel's
// (SidePanel.openSections), so it lasts while the app runs.
Column {
    id: section
    required property string title
    property bool foldable: false
    property bool open: true
    signal toggled()
    // FEAT-122 B2: a text button at the header's right (the desktop's "+ Add" of the task panel).
    property string actionText: ""
    property alias actionButton: action
    signal actionClicked()
    default property alias rows: content.data
    width: parent ? parent.width : 0

    // The desktop's 1 px divider above every section (FEAT-072 B2).
    Rectangle { width: parent.width; height: 1; color: Theme.darkDivider }
    // The header row: the folding button and, over its right end, the action.
    Item {
        width: parent.width
        height: Theme.touchTarget
        AbstractButton {
            id: header
            objectName: "section" + section.title
            anchors.fill: parent
            enabled: section.foldable
            focusPolicy: Qt.NoFocus
            Accessible.name: section.title
            Accessible.checkable: section.foldable
            Accessible.checked: section.open
            onClicked: section.toggled()
            contentItem: Item {
                IconImage {
                    id: chevron
                    visible: section.foldable
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.smallIconSize
                    height: Theme.smallIconSize
                    rotation: section.open ? 0 : -90
                    source: "qrc:/assets/hugeicons/arrow-down.svg"
                    color: Theme.darkText
                    sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
                }
                Label {
                    anchors.left: chevron.visible ? chevron.right : parent.left
                    anchors.leftMargin: chevron.visible ? 2 * Theme.grid : 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: section.title
                    color: Theme.darkText
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }
            }
            background: Rectangle { color: header.down ? Theme.darkDivider : "transparent"; radius: Theme.controlRadius }
        }
        AbstractButton {
            id: action
            objectName: "sectionAction" + section.title
            visible: section.actionText !== ""
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.touchTarget
            width: Math.max(Theme.touchTarget, actionLabel.implicitWidth + 4 * Theme.grid)
            focusPolicy: Qt.NoFocus
            Accessible.name: section.actionText
            onClicked: section.actionClicked()
            contentItem: Label {
                id: actionLabel
                text: section.actionText
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                color: Theme.darkText
                opacity: action.enabled ? 1 : 0.4
                font.pixelSize: Theme.captionSize
            }
            background: Rectangle { color: action.down ? Theme.darkDivider : "transparent"; radius: Theme.controlRadius }
        }
    }
    Column {
        id: content
        width: parent.width
        visible: section.open || !section.foldable
        bottomPadding: 2 * Theme.grid
    }
}
