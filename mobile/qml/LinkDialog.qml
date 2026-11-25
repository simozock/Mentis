import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-117: the desktop's LinkDialog on the phone, the selected topic's links on two tabs.
// Web page (FEAT-108): the address field holds the topic's link; only http, https and mailto
// addresses are allowed ("https://" added, MobileMap::webAddress), otherwise Insert is disabled and
// the desktop's note says why; Insert sets the link. Topic (FEAT-114): the found topics by title,
// grouped by sheet, without the topic itself; the switch adds the link back; a tap on a result links
// to it. Remove (when the visible tab's link exists) takes that link off. One Undo step each; the
// other tab's link is left as it is. At the top of the page, above the on-screen keyboard.
ModalDialog {
    id: dialog
    objectName: "linkDialog"
    required property MobileMap map
    // The page y where the keyboard starts (MapPage.keyboardTop): the results end above it.
    property real keyboardTop: parent ? parent.height : 0
    property bool topicTab: false
    property bool alsoBack: false
    property string source
    property var results: []
    readonly property bool valid: dialog.map.webAddress(addressField.text) !== ""
    readonly property bool hasLink: topicTab ? dialog.map.selectedTopicLink !== "" : dialog.map.selectedWebLink !== ""

    title: qsTr("Insert a link")
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 480)

    // The Topic tab when the topic has only a topic link, otherwise Web page (the desktop's rule).
    function openLink() {
        if (dialog.map.selectedId === "") return
        source = dialog.map.selectedId
        addressField.text = dialog.map.selectedWebLink
        findField.text = ""
        alsoBack = false
        results = []
        dialog.open()
        addressField.selectAll()
        showTab(dialog.map.selectedTopicLink !== "" && dialog.map.selectedWebLink === "")
    }
    function showTab(topic) {
        topicTab = topic
        ;(topic ? findField : addressField).forceActiveFocus()
    }
    function refresh() {
        let found = opened ? dialog.map.search(findField.text, true, false, false, 1) : []  // NodeSearch::Title
        // The topic itself is not offered; a sheet's name only over results that remain.
        found = found.filter(row => row.header || row.id !== source)
        results = found.filter((row, i) => !row.header || (i + 1 < found.length && !found[i + 1].header))
    }
    function link(sheet, id) {
        const back = alsoBack
        dialog.close()
        dialog.map.setTopicLink(sheet, id, back)
    }

    onAccepted: dialog.map.setWebLink(addressField.text)
    onClosed: {
        Qt.inputMethod.commit()
        addressField.focus = false
        findField.focus = false
        Qt.inputMethod.hide()
    }

    // Results follow typing without searching at every key (the desktop's 120 ms).
    Timer {
        id: searchDelay
        interval: 120
        onTriggered: dialog.refresh()
    }

    // The modals' field (ui-components.md, "Modali").
    component LinkField: TextField {
        id: field
        height: Theme.touchTarget
        color: Theme.darkText
        placeholderTextColor: Theme.darkTextMuted
        font.pixelSize: Theme.bodySize
        verticalAlignment: TextInput.AlignVCenter
        leftPadding: 2 * Theme.grid
        rightPadding: 2 * Theme.grid
        selectByMouse: true
        background: Item {
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: Theme.fieldHeight
                radius: Theme.controlRadius
                color: Theme.darkSunken
                border.width: field.activeFocus ? 1 : 0
                border.color: Theme.accent
            }
        }
    }

    Column {
        id: page
        width: dialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
        spacing: Theme.grid

        // The desktop's SegmentedControl in the dark tones (ui-mobile.md), as the stickers modal's.
        Item {
            id: segments
            width: parent.width
            height: Theme.touchTarget
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
                    model: [qsTr("Web page"), qsTr("Topic")]
                    AbstractButton {
                        id: segment
                        required property int index
                        required property string modelData
                        readonly property bool active: (index === 1) === dialog.topicTab
                        objectName: index === 0 ? "webSegment" : "topicSegment"
                        width: (segments.width - 2 * Theme.grid) / 2
                        height: segments.height
                        text: modelData
                        focusPolicy: Qt.NoFocus
                        onClicked: dialog.showTab(index === 1)
                        background: Rectangle {
                            anchors.centerIn: parent
                            width: parent.width
                            height: 6 * Theme.grid
                            radius: Theme.controlRadius
                            color: segment.active ? Theme.darkRaised : segment.down || segment.hovered ? Theme.darkDivider : "transparent"
                        }
                        contentItem: Label {
                            text: segment.text
                            color: Theme.darkText
                            font.pixelSize: Theme.captionSize
                            font.bold: segment.active
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
        }

        // Web page.
        LinkField {
            id: addressField
            objectName: "webLinkField"
            visible: !dialog.topicTab
            width: parent.width
            placeholderText: "https://"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            onAccepted: if (dialog.valid) dialog.accept()
        }
        // QLabel#ModalError of the desktop: muted, caption size.
        Label {
            objectName: "webLinkError"
            width: parent.width
            visible: !dialog.topicTab && !dialog.valid && addressField.text.trim() !== ""
            text: qsTr("Only http, https and mailto addresses are allowed.")
            wrapMode: Text.Wrap
            color: Theme.darkTextMuted
            font.pixelSize: Theme.captionSize
        }

        // Topic.
        LinkField {
            id: findField
            objectName: "findTopicField"
            visible: dialog.topicTab
            width: parent.width
            placeholderText: qsTr("Find the topic to link")
            inputMethodHints: Qt.ImhNoPredictiveText
            onTextEdited: searchDelay.restart()
            // Enter: the first result, as the desktop's search.
            onAccepted: {
                const first = dialog.results.find(row => !row.header)
                if (first) dialog.link(first.sheet, first.id)
            }
        }
        DarkSwitch {
            id: backSwitch
            objectName: "alsoBackSwitch"
            visible: dialog.topicTab
            width: parent.width
            leftPadding: 2 * Theme.grid  // the label lines up with the field's text
            rightPadding: 2 * Theme.grid
            text: qsTr("Also add the link to the chosen topic")
            checked: dialog.alsoBack
            onToggled: dialog.alsoBack = checked
        }
        Label {
            objectName: "noResultsLabel"
            visible: dialog.topicTab && findField.text.trim() !== "" && dialog.results.length === 0 && !searchDelay.running
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
            objectName: "topicResults"
            visible: dialog.topicTab && count > 0
            width: parent.width
            // Down to the keyboard (or the page's bottom), less the buttons, at most the rows' height.
            height: Math.min(contentHeight, Math.max(Theme.touchTarget, dialog.keyboardTop - dialog.y - y - 40 * Theme.grid))
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: dialog.results
            delegate: ItemDelegate {
                id: row
                required property var modelData
                objectName: modelData.header ? "linkSheetRow" : "linkResultRow"
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
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: row.down ? Theme.darkDivider : "transparent"
                }
                onClicked: dialog.link(modelData.sheet, modelData.id)
            }
        }
    }

    buttons: [
        DarkButton {
            objectName: "linkRemoveButton"
            text: qsTr("Remove")
            visible: dialog.hasLink
            onClicked: {
                const topic = dialog.topicTab
                dialog.reject()
                if (topic) dialog.map.removeTopicLink()
                else dialog.map.removeWebLink()
            }
        },
        DarkButton {
            objectName: "webLinkInsertButton"
            text: qsTr("Insert")
            visible: !dialog.topicTab  // on Topic a tap on a result links
            primary: true
            enabled: dialog.valid
            onClicked: dialog.accept()
        }
    ]
}
