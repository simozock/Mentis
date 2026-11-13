import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-116: the desktop's FormulaDialog (FEAT-064) on the phone. The code field holds the selected
// topic's formula (monospaced; Enter starts a new line, Ctrl+Enter confirms on a keyboard); the
// preview (FormulaPreview) draws it live on the formula's background, the error line says why it
// is not valid and Insert or Save stays disabled. Insert or Save sets it, Remove (when the topic has
// one) takes it off, Cancel leaves it; one Undo step each. The background colour is kept (no colour
// picker on the phone yet). At the top of the page, as tall as the space above the keyboard.
ModalDialog {
    id: dialog
    objectName: "formulaDialog"
    required property MobileMap map
    // The page y where the keyboard starts (MapPage.keyboardTop).
    property real keyboardTop: parent ? parent.height : 0
    // Set when it opens: after Insert the closing modal keeps saying Insert.
    property bool editing: false

    title: qsTr("LaTeX formula")
    // A tap outside does not close it: a formula being typed would be lost by a stray tap.
    closePolicy: Popup.CloseOnEscape
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 560)

    function openFormula() {
        if (dialog.map.selectedId === "") return
        codeField.text = dialog.map.selectedFormula
        dialog.editing = codeField.text !== ""
        flick.contentY = 0
        dialog.open()
        codeField.cursorPosition = codeField.length
        codeField.forceActiveFocus()
    }

    onAccepted: dialog.map.setFormula(codeField.text)
    onClosed: {
        Qt.inputMethod.commit()
        codeField.focus = false
        Qt.inputMethod.hide()
    }

    Flickable {
        id: flick
        width: dialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
        // The dialog's chrome (title, buttons) is about 30 px of grid above and below the content.
        height: Math.max(0, Math.min(column.height, Math.min(dialog.parent ? dialog.parent.height : 0, dialog.keyboardTop)
                                                    - dialog.y - 34 * Theme.grid))
        contentWidth: width
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: column
            width: flick.width
            spacing: Theme.grid
            Label {
                width: parent.width
                // The examples are code, not text: outside qsTr (lupdate also reads "\\f" as a form feed).
                text: qsTr("LaTeX code, for example %1").arg("\\frac{a}{b}, x^2, \\sqrt{x}, \\sum_{i=1}^{n} i")
                wrapMode: Text.Wrap
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            // The modals' field (ui-components.md, "Modali"), several lines as the desktop's.
            TextArea {
                id: codeField
                objectName: "formulaCodeField"
                width: parent.width
                height: Math.max(implicitHeight, 22 * Theme.grid)
                color: Theme.darkText
                font.family: dialog.map.fixedFontFamily()
                font.pixelSize: Theme.bodySize
                wrapMode: TextEdit.WrapAnywhere
                selectByMouse: true
                selectionColor: Theme.accent
                selectedTextColor: Theme.darkText
                leftPadding: 2 * Theme.grid
                rightPadding: 2 * Theme.grid
                topPadding: 2 * Theme.grid
                bottomPadding: 2 * Theme.grid
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                // The cursor stays in view while typing above the keyboard (as NoteDialog's).
                onCursorRectangleChanged: {
                    const top = y + cursorRectangle.y, bottom = top + cursorRectangle.height
                    if (top < flick.contentY) flick.contentY = top
                    else if (bottom > flick.contentY + flick.height) flick.contentY = bottom - flick.height
                }
                Keys.onPressed: event => {
                    if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) {
                        if (preview.valid) dialog.accept()
                        event.accepted = true
                    }
                }
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: Theme.darkSunken
                    border.width: codeField.activeFocus ? 1 : 0
                    border.color: Theme.accent
                }
            }
            Item { width: 1; height: Theme.grid }
            FormulaPreview {
                id: preview
                objectName: "formulaPreview"
                width: parent.width
                height: neededHeight
                code: codeField.text
                background: dialog.map.selectedFormulaBackground
                placeholder: qsTr("The formula appears here while you type")
            }
            // The desktop's error line, in the modals' error note look (ui-mobile.md, as WebLinkDialog).
            Label {
                objectName: "formulaError"
                width: parent.width
                visible: text !== ""
                text: preview.error
                wrapMode: Text.Wrap
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
        }
    }

    buttons: [
        DarkButton {
            objectName: "formulaRemoveButton"
            text: qsTr("Remove")
            visible: dialog.editing
            onClicked: {
                dialog.reject()
                dialog.map.removeFormula()
            }
        },
        DarkButton {
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        },
        DarkButton {
            objectName: "formulaInsertButton"
            text: dialog.editing ? qsTr("Save") : qsTr("Insert")
            primary: true
            enabled: preview.valid
            onClicked: dialog.accept()
        }
    ]
}
