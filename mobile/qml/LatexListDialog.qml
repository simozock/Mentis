import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-132: the desktop's LatexListDialog on the phone, opened over the formula modal by "Symbols
// and formulas…". The modals' search field (not focused when it opens: the keyboard would cover the
// grid), the category select (a side panel select with its choices menu), a grid of entries (the
// drawn formula, the code, the name) as tall as the room above the keyboard. A tap on an entry
// closes the list and hands its code to `picked`; the X or Back close it without inserting.
ModalDialog {
    id: dialog
    objectName: "latexListDialog"
    // The page y where the keyboard starts (MapPage.keyboardTop).
    property real keyboardTop: parent ? parent.height : 0
    // The system's fixed-pitch family of the codes (MobileMap.fixedFontFamily, as the code field).
    property string fixedFamily
    signal picked(string code)

    title: qsTr("Symbols and formulas")
    closePolicy: Popup.CloseOnEscape
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 760)
    // No buttons (a tap on an entry inserts it, the X closes): no divider, the 24 px padding below.
    footer: Item { implicitHeight: 6 * Theme.grid }

    function openList() {
        search.text = ""
        list.category = -1
        grid.contentY = 0
        dialog.open()
    }
    // LatexCatalogue::insert: the selection [start, end) of value replaced by code, {text, caret}.
    function insertInto(value, start, end, code) { return list.insert(value, start, end, code) }

    onClosed: {
        Qt.inputMethod.commit()
        search.focus = false
        Qt.inputMethod.hide()
    }

    LatexList {
        id: list
        query: search.text
    }
    StyleChoiceMenu {
        id: categoryMenu
        parent: Overlay.overlay
    }

    Column {
        id: column
        width: dialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
        spacing: Theme.grid

        // The modals' field (ui-components.md, "Modali").
        TextField {
            id: search
            objectName: "latexListSearch"
            width: parent.width
            height: Theme.touchTarget
            placeholderText: qsTr("Search, for example integral, arrow or %1").arg("\\frac")
            color: Theme.darkText
            placeholderTextColor: Theme.darkTextMuted
            font.pixelSize: Theme.bodySize
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 2 * Theme.grid
            rightPadding: 2 * Theme.grid
            selectByMouse: true
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            background: Item {
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: Theme.fieldHeight
                    radius: Theme.controlRadius
                    color: Theme.darkSunken
                    border.width: search.activeFocus ? 1 : 0
                    border.color: Theme.accent
                }
            }
        }
        StyleSelectRow {
            objectName: "latexListCategory"
            width: parent.width
            label: qsTr("Category")
            menu: categoryMenu
            choices: list.categories()
            value: list.category
            onPicked: value => {
                list.category = value
                grid.contentY = 0
            }
        }
        GridView {
            id: grid
            objectName: "latexListGrid"
            readonly property int columns: Math.max(1, Math.floor(width / (36 * Theme.grid)))
            width: parent.width
            // Four rows at most, fewer when the keyboard leaves less room (the chrome is about 34
            // grid units above and below, the field and the select 2 x 48 px).
            height: Math.max(cellHeight, Math.min(4 * cellHeight, Math.min(dialog.parent ? dialog.parent.height : 0, dialog.keyboardTop)
                                                   - dialog.y - 34 * Theme.grid - 2 * Theme.touchTarget - 2 * Theme.grid))
            cellWidth: Math.floor(width / columns)
            cellHeight: 2 + 6 + 48 + 2 + 2 * (Theme.captionSize + 4) + 6 + 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            model: list
            delegate: AbstractButton {
                id: cell
                required property int index
                required property string code
                required property string preview
                required property string name
                width: grid.cellWidth
                height: grid.cellHeight
                focusPolicy: Qt.NoFocus
                Accessible.name: cell.name + ": " + cell.code
                onClicked: {
                    dialog.close()
                    dialog.picked(cell.code)
                }
                background: Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.controlRadius
                    color: cell.down ? Theme.darkRaised : Theme.darkSunken
                }
                contentItem: Item {
                    LatexFormula {
                        id: formula
                        x: 2 * Theme.grid
                        y: 2 + 6
                        width: parent.width - 4 * Theme.grid
                        height: 48
                        code: cell.preview
                        color: Theme.darkText
                    }
                    Label {
                        id: codeLabel
                        anchors.top: formula.bottom
                        anchors.topMargin: 2
                        x: 2 * Theme.grid
                        width: parent.width - 4 * Theme.grid
                        height: Theme.captionSize + 4
                        text: cell.code
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: Theme.darkText
                        font.family: dialog.fixedFamily
                        font.pixelSize: Theme.captionSize
                    }
                    Label {
                        anchors.top: codeLabel.bottom
                        x: 2 * Theme.grid
                        width: parent.width - 4 * Theme.grid
                        height: Theme.captionSize + 4
                        text: cell.name
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: Theme.darkTextMuted
                        font.pixelSize: Theme.captionSize
                    }
                }
            }
        }
        Label {
            objectName: "latexListEmpty"
            width: parent.width
            visible: list.count === 0
            text: qsTr("No results")
            horizontalAlignment: Text.AlignHCenter
            color: Theme.darkTextMuted
            font.pixelSize: Theme.bodySize
        }
    }
}
