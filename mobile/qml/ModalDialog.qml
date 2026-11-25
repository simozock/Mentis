import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.impl
import Mentis.Mobile

// The desktop's ModalDialog in Qt Quick (docs/design/ui-components.md, "Modali"): scrim
// rgba(15,23,42,0.24), a surface of colour surface with radius 8 and padding 24, title 20 px weight
// 600, 16 px between title, content and buttons, a 1 px divider over the buttons, which sit at the
// right 8 px apart. The light shadow is four rounded rectangles with alpha 10/8/6/4, 2 px lower.
// Put the DarkButtons in `buttons`; each one calls accept() or reject(). closeButton: the desktop's
// X beside the title (16 px icon, here in a 48 px target), on by default: no modal has a "Cancel"
// button (user's request 2026-11-23). It emits closeClicked, then reject(). titleButtons:
// more 48 px targets at the left of the X (FEAT-127: the Resources modal's charts toggle).
// Ctrl+Enter on a keyboard (a tablet's) clicks the visible, enabled primary button, also from a
// multi-line field (user's request 2026-11-24, as the desktop).
Dialog {
    id: dialog
    default property alias content: body.data
    property alias buttons: buttonRow.data
    property bool closeButton: true
    // The divider and the buttons row; off for a modal closed only by its X or a choice. Not from the
    // buttons' visibility: a hidden footer hides them too, so it would never come back.
    property bool buttonsShown: buttonRow.children.length > 0
    signal closeClicked()
    // Asked before the X closes the modal: false keeps it open (FEAT-070: the sticker editor asks
    // whether to discard its changes first).
    property var canClose: null
    property alias titleButtons: titleExtras.data
    modal: true
    // The header and the footer sit outside the padding: they bring the 24 px above and below.
    padding: 6 * Theme.grid
    topPadding: 0
    bottomPadding: 0
    spacing: 4 * Theme.grid

    Overlay.modal: Rectangle { color: Theme.scrim }

    function primaryButton() {
        for (let i = buttonRow.children.length - 1; i >= 0; --i) {
            const button = buttonRow.children[i]
            if (button.primary && button.visible && button.enabled) return button
        }
        return null
    }
    // Two open modals make the keys ambiguous: then neither acts.
    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: dialog.opened
        onActivated: {
            const button = dialog.primaryButton()
            if (!button) return
            Qt.inputMethod.commit()  // the word being composed belongs to the field
            button.clicked()
        }
    }

    background: Item {
        Repeater {
            model: [10, 8, 6, 4]
            Rectangle {
                required property int index
                required property int modelData
                readonly property int grow: index + 1
                x: -grow
                y: -grow + 2
                width: parent.width + 2 * grow
                height: parent.height + 2 * grow
                radius: Theme.surfaceRadius + grow
                color: Qt.rgba(0, 0, 0, modelData / 255)
            }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.surfaceRadius
            color: Theme.darkSurface
        }
    }

    header: Item {
        visible: dialog.title !== ""
        implicitHeight: titleLabel.implicitHeight
        Label {
            id: titleLabel
            width: parent.width - (dialog.closeButton ? closeX.width + closeX.anchors.rightMargin : 0)
                   - (titleExtras.width > 0 ? titleExtras.width + titleExtras.anchors.rightMargin : 0)
            text: dialog.title
            color: Theme.darkText
            font.pixelSize: Theme.subtitleSize
            font.bold: true
            wrapMode: Text.Wrap
            leftPadding: 6 * Theme.grid
            rightPadding: dialog.closeButton ? 0 : 6 * Theme.grid
            topPadding: 6 * Theme.grid
        }
        Row {
            id: titleExtras
            anchors.right: dialog.closeButton ? closeX.left : parent.right
            anchors.rightMargin: dialog.closeButton ? 0 : 6 * Theme.grid - (Theme.touchTarget - 28) / 2
            y: 6 * Theme.grid + 14 - Theme.touchTarget / 2
        }
        // The 28 px square at the 24 px padding, centred on the title's first line; the 48 px target
        // reaches into the padding.
        AbstractButton {
            id: closeX
            objectName: dialog.objectName + "Close"
            visible: dialog.closeButton
            anchors.right: parent.right
            anchors.rightMargin: 6 * Theme.grid - (Theme.touchTarget - 28) / 2
            y: 6 * Theme.grid + 14 - height / 2
            width: Theme.touchTarget
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTranslate("ModalDialog", "Close")
            onClicked: {
                if (dialog.canClose && !dialog.canClose()) return
                dialog.closeClicked()
                dialog.reject()
            }
            contentItem: Item {
                IconImage {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    sourceSize: Qt.size(16, 16)
                    source: "qrc:/assets/hugeicons/window-close-light.svg"
                    color: Theme.darkText
                }
            }
            background: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    radius: Theme.controlRadius
                    color: closeX.down ? Theme.darkDivider : "transparent"
                }
            }
        }
    }

    // Item, not a layout: the content sets its width from dialog.availableWidth (a binding on the
    // parent's width loops through the dialog's implicit height).
    Item {
        id: body
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
    }

    footer: ColumnLayout {
        spacing: 0
        visible: dialog.buttonsShown
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 6 * Theme.grid
            Layout.rightMargin: 6 * Theme.grid
            implicitHeight: 1
            color: Theme.darkDivider
        }
        RowLayout {
            id: buttonRow
            spacing: 2 * Theme.grid
            Layout.alignment: Qt.AlignRight
            Layout.rightMargin: 6 * Theme.grid
            // 16 px over and 24 px under the visible buttons, less the 8 px of their touch target
            Layout.topMargin: 2 * Theme.grid
            Layout.bottomMargin: 4 * Theme.grid
        }
    }
}
