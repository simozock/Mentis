import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-113: the desktop's Outline search (FEAT-061) on the phone. A dark panel at the top of the
// map, the sheet list's look: the Find field, Search options (the desktop's menu) and Close; under
// them the found topics in tree order, grouped by sheet with Current file, the found part
// highlighted as on the desktop. A tap on a result shows the topic (MobileMap.reveal). The options
// live while the app runs, as on the desktop.
// FEAT-114: openPicker makes it the desktop's TopicLinkDialog: the selected topic is not offered,
// the switch "Also add the link to the chosen topic" and (with a link already) Remove link sit
// under the field, and a tap on a result links to it (MobileMap.setTopicLink, one Undo step).
Popup {
    id: panel
    objectName: "searchPanel"
    required property MobileMap map
    // A result tapped: the panel is closed, the topic shown.
    signal revealed()

    // FEAT-114: choosing the target of the selected topic's link (openPicker), with its back link.
    property bool picking: false
    property bool alsoBack: false
    property string source

    property bool allSheets: true
    property bool wholeWords: false
    property bool matchCase: false
    property int fields: 1 | 2 | 4 | 8  // NodeSearch::Field: Title, Note, Label, Link
    property var results: []
    // The page y where the keyboard starts (MapPage.keyboardTop): the panel ends 16 px above it.
    property real keyboardTop: parent ? parent.height : 0
    readonly property real maxHeight: Math.max(0, Math.min(parent ? parent.height : 0, keyboardTop) - y - 4 * Theme.grid)

    padding: Theme.grid
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    focus: true

    function openSearch() {
        picking = false
        open()
        findField.selectAll()
        findField.forceActiveFocus()
        Qt.inputMethod.show()
        refresh()
    }
    function openPicker() {
        if (map.selectedId === "") return
        picking = true
        alsoBack = false
        source = map.selectedId
        findField.text = ""
        open()
        findField.forceActiveFocus()
        Qt.inputMethod.show()
        refresh()
    }
    function refresh() {
        let found = visible ? map.search(findField.text, allSheets, wholeWords, matchCase, fields) : []
        if (picking) {
            // The topic itself is not offered; a sheet's name only over results that remain.
            found = found.filter(row => row.header || row.id !== source)
            found = found.filter((row, i) => !row.header || (i + 1 < found.length && !found[i + 1].header))
        }
        results = found
    }
    function reveal(sheet, id) {
        const linking = picking
        close()
        if (linking) map.setTopicLink(sheet, id, alsoBack)
        else if (map.reveal(sheet, id)) revealed()
    }

    onClosed: {
        Qt.inputMethod.commit()
        findField.focus = false
        Qt.inputMethod.hide()
        optionsMenu.close()
    }
    onAllSheetsChanged: refresh()
    onWholeWordsChanged: refresh()
    onMatchCaseChanged: refresh()
    onFieldsChanged: refresh()

    // Results follow typing without searching at every key (the desktop's 120 ms).
    Timer {
        id: searchDelay
        interval: 120
        onTriggered: panel.refresh()
    }

    background: Rectangle {
        radius: Theme.surfaceRadius
        color: Theme.darkSurface
        border.width: 1
        border.color: Theme.darkDivider
    }

    // A 48 px target with a light 20 px icon, as the dark lists' controls.
    component PanelButton: AbstractButton {
        id: button
        focusPolicy: Qt.NoFocus
        implicitWidth: Theme.touchTarget
        implicitHeight: Theme.touchTarget
        Accessible.name: text
        background: Rectangle {
            anchors.centerIn: parent
            width: Theme.groupButtonSize
            height: Theme.groupButtonSize
            radius: Theme.groupButtonRadius
            color: button.down ? Theme.darkDivider : "transparent"
        }
        contentItem: Item {
            IconImage {
                anchors.centerIn: parent
                color: Theme.darkText
                source: button.icon.source
                width: Theme.iconSize
                height: Theme.iconSize
                sourceSize: Qt.size(Theme.iconSize * Screen.devicePixelRatio, Theme.iconSize * Screen.devicePixelRatio)
            }
        }
    }

    contentItem: Column {
        spacing: 0
        Row {
            id: findRow
            width: parent.width
            TextField {
                id: findField
                objectName: "findField"
                width: findRow.width - optionsButton.width - closeButton.width
                height: Theme.touchTarget
                placeholderText: panel.picking ? qsTr("Find the topic to link") : qsTr("Find")
                placeholderTextColor: Theme.darkTextMuted
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                verticalAlignment: TextInput.AlignVCenter
                leftPadding: 2 * Theme.grid
                rightPadding: 2 * Theme.grid
                selectByMouse: true
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: searchDelay.restart()
                // Enter: the first result, as the desktop.
                onAccepted: {
                    const first = panel.results.find(row => !row.header)
                    if (first) panel.reveal(first.sheet, first.id)
                }
                background: Item {
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: Theme.fieldHeight
                        radius: Theme.controlRadius
                        color: Theme.darkSunken
                        border.width: findField.activeFocus ? 1 : 0
                        border.color: Theme.accent
                    }
                }
            }
            PanelButton {
                id: optionsButton
                objectName: "searchOptionsButton"
                icon.source: "qrc:/assets/hugeicons/settings-general.svg"
                text: qsTr("Search options")
                onClicked: {
                    optionsMenu.x = panel.width - optionsMenu.width
                    optionsMenu.y = findRow.height + Theme.grid
                    optionsMenu.open()
                }
            }
            PanelButton {
                id: closeButton
                objectName: "closeSearchButton"
                icon.source: "qrc:/assets/hugeicons/window-close.svg"
                text: qsTr("Close")
                onClicked: panel.close()
            }
        }
        DarkSwitch {
            id: backSwitch
            objectName: "alsoBackSwitch"
            visible: panel.picking
            width: parent.width
            leftPadding: 2 * Theme.grid
            rightPadding: 2 * Theme.grid
            text: qsTr("Also add the link to the chosen topic")
            checked: panel.alsoBack
            onToggled: panel.alsoBack = checked
        }
        ItemDelegate {
            id: removeLink
            objectName: "removeTopicLinkButton"
            visible: panel.picking && panel.map.selectedTopicLink !== ""
            width: parent.width
            implicitHeight: Theme.touchTarget
            padding: 0
            leftPadding: 2 * Theme.grid
            contentItem: Label {
                text: qsTr("Remove link")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.controlRadius
                color: removeLink.down ? Theme.darkDivider : "transparent"
            }
            onClicked: { panel.close(); panel.map.removeTopicLink() }
        }
        Label {
            objectName: "noResultsLabel"
            visible: findField.text.trim() !== "" && panel.results.length === 0 && !searchDelay.running
            width: parent.width
            height: Theme.touchTarget
            leftPadding: 2 * Theme.grid
            text: qsTr("No topics found")
            color: Theme.darkTextMuted
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
        }
        ListView {
            id: resultList
            objectName: "searchResults"
            width: parent.width
            // Down to the keyboard (or the page's bottom), at most the rows' height.
            height: Math.min(contentHeight, Math.max(0, panel.maxHeight - 2 * panel.padding - findRow.height
                                                     - (backSwitch.visible ? backSwitch.height : 0)
                                                     - (removeLink.visible ? removeLink.height : 0)))
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.results
            delegate: ItemDelegate {
                id: row
                required property var modelData
                objectName: modelData.header ? "searchSheetRow" : "searchResultRow"
                width: resultList.width
                enabled: !modelData.header
                padding: 0
                leftPadding: 2 * Theme.grid
                rightPadding: 2 * Theme.grid
                topPadding: modelData.header ? 2 * Theme.grid : 3 * Theme.grid
                bottomPadding: modelData.header ? Theme.grid : 3 * Theme.grid
                implicitHeight: Math.max(modelData.header ? 8 * Theme.grid : Theme.touchTarget,
                                         rowText.implicitHeight + topPadding + bottomPadding)
                contentItem: Label {
                    id: rowText
                    // Rich text for the highlight (MobileMap.search escapes the rest).
                    textFormat: row.modelData.header ? Text.PlainText : Text.RichText
                    text: row.modelData.header ? row.modelData.text : row.modelData.html
                    color: row.modelData.header ? Theme.darkTextMuted : Theme.darkText
                    font.pixelSize: row.modelData.header ? Theme.captionSize : Theme.bodySize
                    font.bold: row.modelData.header
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere  // a long link breaks too
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: row.down ? Theme.darkDivider : "transparent"
                }
                onClicked: panel.reveal(modelData.sheet, modelData.id)
            }
        }
    }

    // The desktop's options menu (NavigationPanel), the sheet menu's look; a chosen option is bold
    // on darkRaised, as the marker menu's value.
    Popup {
        id: optionsMenu
        objectName: "searchOptionsMenu"
        padding: Theme.grid
        width: 220
        height: Math.min(implicitHeight, panel.maxHeight - y)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: Theme.surfaceRadius
            color: Theme.darkSurface
            border.width: 1
            border.color: Theme.darkDivider
        }

        component OptionRow: ItemDelegate {
            id: option
            property bool on: false
            width: optionColumn.width
            implicitHeight: Theme.touchTarget
            padding: 0
            leftPadding: 3 * Theme.grid
            rightPadding: 3 * Theme.grid
            Accessible.checkable: true
            Accessible.checked: on
            contentItem: Label {
                text: option.text
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                font.bold: option.on
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.controlRadius
                color: option.down ? Theme.darkDivider : option.on ? Theme.darkRaised : "transparent"
            }
        }
        component Divider: Rectangle {
            width: optionColumn.width
            height: 2 * Theme.grid + 1
            color: "transparent"
            Rectangle {
                anchors.centerIn: parent
                width: parent.width
                height: 1
                color: Theme.darkDivider
            }
        }
        function toggleField(bit) { panel.fields = panel.fields ^ bit }

        contentItem: Flickable {
            implicitHeight: optionColumn.implicitHeight
            contentHeight: optionColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: optionColumn
                width: parent.width
                OptionRow {
                    objectName: "currentMapOption"
                    text: qsTr("Current map")
                    on: !panel.allSheets
                    onClicked: panel.allSheets = false
                }
                OptionRow {
                    objectName: "currentFileOption"
                    text: qsTr("Current file")
                    on: panel.allSheets
                    onClicked: panel.allSheets = true
                }
                Divider {}
                OptionRow {
                    objectName: "wholeWordsOption"
                    text: qsTr("Whole words")
                    on: panel.wholeWords
                    onClicked: panel.wholeWords = !panel.wholeWords
                }
                OptionRow {
                    objectName: "matchCaseOption"
                    text: qsTr("Match case")
                    on: panel.matchCase
                    onClicked: panel.matchCase = !panel.matchCase
                }
                Divider {}
                OptionRow {
                    objectName: "topicOption"
                    text: qsTr("Topic")
                    on: (panel.fields & 1) !== 0
                    onClicked: optionsMenu.toggleField(1)
                }
                OptionRow {
                    objectName: "noteOption"
                    text: qsTr("Note")
                    on: (panel.fields & 2) !== 0
                    onClicked: optionsMenu.toggleField(2)
                }
                OptionRow {
                    objectName: "tagOption"
                    text: qsTr("Tag")
                    on: (panel.fields & 4) !== 0
                    onClicked: optionsMenu.toggleField(4)
                }
                OptionRow {
                    objectName: "linkOption"
                    text: qsTr("Link")
                    on: (panel.fields & 8) !== 0
                    onClicked: optionsMenu.toggleField(8)
                }
            }
        }
    }
}
