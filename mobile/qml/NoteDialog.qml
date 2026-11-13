import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-097 B3: the selected topic's note (FEAT-059) in a dark modal as tall as the space above the
// on-screen keyboard (user's choice 2026-11-11). It opens to read: the keyboard comes with a tap on
// the text. The text is edited as plain text: the formatting already there is kept and typed text
// takes the format at the cursor. Done, Back or Esc write it (one Undo step; emptied = note removed);
// Cancel leaves the note as it was (as written when the app last went to the background).
// FEAT-106: Remove takes the note and its icon off (one Undo step), shown when the topic has a note.
ModalDialog {
    id: dialog
    objectName: "noteDialog"
    required property MobileMap map
    // The page y where the keyboard starts (MapPage.keyboardTop).
    property real keyboardTop: parent ? parent.height : 0
    // Set by Cancel: closing does not write.
    property bool discard: false

    title: qsTr("Note")
    // A tap outside does not close it: half a note would be lost or written by a stray tap.
    closePolicy: Popup.CloseOnEscape
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 480)
    height: Math.max(0, Math.min(parent ? parent.height : 0, keyboardTop) - y - 4 * Theme.grid)

    function openNote() {
        if (dialog.map.selectedId === "") return
        dialog.discard = false
        dialog.map.loadNote(noteText.textDocument)
        noteText.cursorPosition = 0
        flick.contentY = 0
        dialog.open()
    }

    // Android may end the app in the background: the note typed so far is written and saved first
    // (one Undo step; Cancel afterwards keeps it).
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive || !dialog.opened || dialog.discard) return
            Qt.inputMethod.commit()
            if (dialog.map.saveNote(noteText.textDocument)) dialog.map.save()
        }
    }

    onClosed: {
        Qt.inputMethod.commit()  // the word being composed belongs to the note
        noteText.focus = false
        Qt.inputMethod.hide()
        if (!dialog.discard) dialog.map.saveNote(noteText.textDocument)
    }

    Flickable {
        id: flick
        width: dialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
        height: dialog.availableHeight
        contentWidth: width
        contentHeight: noteText.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        TextEdit {
            id: noteText
            objectName: "noteText"
            width: flick.width
            height: Math.max(implicitHeight, flick.height)
            textFormat: TextEdit.RichText
            wrapMode: TextEdit.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
            selectionColor: Theme.accent
            selectedTextColor: Theme.darkText
            // The cursor stays in view while typing above the keyboard.
            onCursorRectangleChanged: {
                if (cursorRectangle.y < flick.contentY) flick.contentY = cursorRectangle.y
                else if (cursorRectangle.y + cursorRectangle.height > flick.contentY + flick.height)
                    flick.contentY = cursorRectangle.y + cursorRectangle.height - flick.height
            }
            Text {
                objectName: "notePlaceholder"
                visible: noteText.length === 0 && noteText.preeditText === ""
                text: qsTr("Write a note")
                color: Theme.darkTextMuted
                font: noteText.font
            }
        }
    }

    buttons: [
        DarkButton {
            objectName: "noteRemoveButton"
            text: qsTr("Remove")
            visible: dialog.map.selectedHasNote
            onClicked: {
                dialog.discard = true
                dialog.close()
                dialog.map.removeNote()
            }
        },
        DarkButton {
            objectName: "noteCancelButton"
            text: qsTr("Cancel")
            onClicked: { dialog.discard = true; dialog.close() }
        },
        DarkButton {
            objectName: "noteDoneButton"
            text: qsTr("Done")
            primary: true
            onClicked: dialog.close()
        }
    ]
}
