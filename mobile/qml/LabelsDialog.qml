import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-114: the desktop's LabelEditor (FEAT-060) on the phone, a dark modal at the top of the page
// (above the on-screen keyboard): the Auto sort switch (Labels::autoSort, an app setting), the field
// with the topic's tags separated by commas, and the document's tags as pills that add themselves,
// filtered by the word being typed (Labels::suggestions). Done, Back or Esc write the tags (one Undo
// step, as the desktop's Enter, Esc or click outside); Cancel leaves them as they were.
ModalDialog {
    id: dialog
    objectName: "labelsDialog"
    required property MobileMap map
    property bool discard: false
    property var allLabels: []
    readonly property var shownSuggestions: dialog.map.labelSuggestions(labelsField.text, dialog.allLabels)

    title: qsTr("Labels")
    closePolicy: Popup.CloseOnEscape
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 400)

    function openLabels() {
        if (dialog.map.selectedId === "") return
        dialog.discard = false
        dialog.allLabels = dialog.map.documentLabels()
        const labels = dialog.map.selectedLabels
        labelsField.text = labels.length > 0 ? labels.join(", ") + ", " : ""
        dialog.open()
        labelsField.cursorPosition = labelsField.length
        labelsField.forceActiveFocus()
    }

    function addSuggestion(label) {
        labelsField.text = dialog.map.labelsWithAdded(labelsField.text, label)
        labelsField.cursorPosition = labelsField.length
        labelsField.forceActiveFocus()
    }

    onClosed: {
        Qt.inputMethod.commit()  // the word being composed belongs to the field
        labelsField.focus = false
        Qt.inputMethod.hide()
        if (!dialog.discard) dialog.map.setLabels(labelsField.text)
    }

    Column {
        width: dialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
        spacing: 2 * Theme.grid

        DarkSwitch {
            id: autoSort
            objectName: "labelsAutoSort"
            width: parent.width
            text: qsTr("Auto sort")
            checked: dialog.map.labelsAutoSort
            onToggled: dialog.map.labelsAutoSort = checked
        }

        // The modals' field (ui-components.md, "Modali").
        TextField {
            id: labelsField
            objectName: "labelsField"
            width: parent.width
            height: Theme.touchTarget
            color: Theme.darkText
            placeholderText: qsTr("Labels, separated by commas")
            placeholderTextColor: Theme.darkTextMuted
            font.pixelSize: Theme.bodySize
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 2 * Theme.grid
            rightPadding: 2 * Theme.grid
            selectByMouse: true
            onAccepted: dialog.close()
            background: Item {
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: Theme.fieldHeight
                    radius: Theme.controlRadius
                    color: Theme.darkSunken
                    border.width: labelsField.activeFocus ? 1 : 0
                    border.color: Theme.accent
                }
            }
        }

        Label {
            visible: dialog.shownSuggestions.length > 0
            text: qsTr("Labels in this document")
            color: Theme.darkTextMuted
            font.pixelSize: Theme.captionSize
        }

        // The desktop's suggestion pills (24 px, radius 12, 1 px darkDivider border on darkSunken,
        // 10 px padding, 6 px apart), each in a 48 px target; at most four rows in view, the rest
        // scrolled (the desktop shows six and finds the others by typing).
        Flickable {
            width: parent.width
            height: Math.min(suggestionFlow.height, 4 * Theme.touchTarget)
            visible: dialog.shownSuggestions.length > 0
            contentWidth: width
            contentHeight: suggestionFlow.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Flow {
                id: suggestionFlow
                objectName: "labelSuggestions"
                width: parent.width
                Repeater {
                    model: dialog.shownSuggestions
                    AbstractButton {
                        id: suggestion
                        required property string modelData
                        focusPolicy: Qt.NoFocus
                        readonly property real pillWidth: Math.min(suggestionText.implicitWidth + 20, dialog.availableWidth)
                        width: Math.max(Theme.touchTarget, pillWidth + 6)
                        height: Theme.touchTarget
                        Accessible.name: modelData
                        onClicked: dialog.addSuggestion(modelData)
                        background: Rectangle {
                            y: (suggestion.height - height) / 2
                            width: suggestion.pillWidth
                            height: 24
                            radius: 12
                            color: suggestion.down ? Theme.darkRaised : Theme.darkSunken
                            border.width: 1
                            border.color: Theme.darkDivider
                        }
                        contentItem: Item {
                            Label {
                                id: suggestionText
                                x: 10
                                width: suggestion.pillWidth - 20
                                anchors.verticalCenter: parent.verticalCenter
                                text: suggestion.modelData
                                elide: Text.ElideRight
                                color: Theme.darkText
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }

    buttons: [
        DarkButton {
            objectName: "labelsCancelButton"
            text: qsTr("Cancel")
            onClicked: { dialog.discard = true; dialog.close() }
        },
        DarkButton {
            objectName: "labelsDoneButton"
            text: qsTr("Done")
            primary: true
            onClicked: dialog.close()
        }
    ]
}
