import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-108: the desktop's WebLinkDialog (FEAT-027) on the phone. The address field holds the
// selected topic's link; only http, https and mailto addresses are allowed ("https://" added,
// MobileMap::webAddress), otherwise Insert is disabled and the desktop's note says why. Insert sets
// the link, Remove (when the topic has one) takes it off, Cancel leaves it; one Undo step each.
// At the top of the page, so the on-screen keyboard does not cover it.
ModalDialog {
    id: dialog
    objectName: "webLinkDialog"
    required property MobileMap map
    readonly property bool valid: dialog.map.webAddress(addressField.text) !== ""

    title: qsTr("Link to a web page")
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 400)

    function openLink() {
        if (dialog.map.selectedId === "") return
        addressField.text = dialog.map.selectedWebLink
        dialog.open()
        addressField.selectAll()
        addressField.forceActiveFocus()
    }

    onAccepted: dialog.map.setWebLink(addressField.text)
    onClosed: {
        Qt.inputMethod.commit()
        addressField.focus = false
        Qt.inputMethod.hide()
    }

    Column {
        width: dialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
        spacing: Theme.grid
        // The modals' field (ui-components.md, "Modali").
        TextField {
            id: addressField
            objectName: "webLinkField"
            width: parent.width
            height: Theme.touchTarget
            color: Theme.darkText
            placeholderText: "https://"
            placeholderTextColor: Theme.darkTextMuted
            font.pixelSize: Theme.bodySize
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 2 * Theme.grid
            rightPadding: 2 * Theme.grid
            selectByMouse: true
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onAccepted: if (dialog.valid) dialog.accept()
            background: Item {
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: Theme.fieldHeight
                    radius: Theme.controlRadius
                    color: Theme.darkSunken
                    border.width: addressField.activeFocus ? 1 : 0
                    border.color: Theme.accent
                }
            }
        }
        // QLabel#ModalError of the desktop: muted, caption size.
        Label {
            objectName: "webLinkError"
            width: parent.width
            visible: !dialog.valid && addressField.text.trim() !== ""
            text: qsTr("Only http, https and mailto addresses are allowed.")
            wrapMode: Text.Wrap
            color: Theme.darkTextMuted
            font.pixelSize: Theme.captionSize
        }
    }

    buttons: [
        DarkButton {
            objectName: "webLinkRemoveButton"
            text: qsTr("Remove")
            visible: dialog.map.selectedWebLink !== ""
            onClicked: {
                dialog.reject()
                dialog.map.removeWebLink()
            }
        },
        DarkButton {
            text: qsTr("Cancel")
            onClicked: dialog.reject()
        },
        DarkButton {
            objectName: "webLinkInsertButton"
            text: qsTr("Insert")
            primary: true
            enabled: dialog.valid
            onClicked: dialog.accept()
        }
    ]
}
