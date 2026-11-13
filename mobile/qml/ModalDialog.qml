import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Mentis.Mobile

// The desktop's ModalDialog in Qt Quick (docs/design/ui-components.md, "Modali"): scrim
// rgba(15,23,42,0.24), a surface of colour surface with radius 8 and padding 24, title 20 px weight
// 600, 16 px between title, content and buttons, a 1 px divider over the buttons, which sit at the
// right 8 px apart. The light shadow is four rounded rectangles with alpha 10/8/6/4, 2 px lower.
// Put the DarkButtons in `buttons`; each one calls accept() or reject().
Dialog {
    id: dialog
    default property alias content: body.data
    property alias buttons: buttonRow.data
    modal: true
    // The header and the footer sit outside the padding: they bring the 24 px above and below.
    padding: 6 * Theme.grid
    topPadding: 0
    bottomPadding: 0
    spacing: 4 * Theme.grid

    Overlay.modal: Rectangle { color: Theme.scrim }

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

    header: Label {
        text: dialog.title
        visible: text !== ""
        color: Theme.darkText
        font.pixelSize: Theme.subtitleSize
        font.bold: true
        wrapMode: Text.Wrap
        leftPadding: 6 * Theme.grid
        rightPadding: 6 * Theme.grid
        topPadding: 6 * Theme.grid
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
