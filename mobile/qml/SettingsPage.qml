import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Layouts
import Mentis.Mobile

// FEAT-125: the phone's Settings, opened from the Home; one section, Shortcuts: the desktop's list
// (FEAT-041) for the commands the phone runs with a hardware keyboard. Dark as the Home. A key field
// tapped records the next key; a key used by another command asks, as the desktop's page.
Page {
    id: page
    signal backRequested()
    readonly property bool covered: Overlay.overlay ? Overlay.overlay.children.length > 0 : false
    property string recordingId: ""
    property int recordingIndex: -1
    function text(source) { return qsTranslate("ShortcutsPage", source) }

    function startRecording(id, index) {
        recordingId = id
        recordingIndex = index
        recorder.forceActiveFocus()
    }
    // The desktop's search: the name or a key holds the text.
    function matches(command, query) {
        if (query === "") return true
        const texts = keys.revision >= 0 ? keys.keyTexts(command.id) : []
        return (command.name + " " + texts.join(" ")).toLowerCase().indexOf(query) >= 0
    }
    function stopRecording() { recordingId = ""; recordingIndex = -1 }
    function record(portable) {
        const id = recordingId
        const index = recordingIndex
        stopRecording()
        const other = keys.conflictName(id, portable)
        if (other === "") keys.setKey(id, index, portable)
        else conflictDialog.ask(id, index, portable, other)
    }

    KeyCommands { id: keys }

    background: Rectangle { color: Theme.darkSunken }

    header: Item {
        implicitHeight: Theme.touchTarget + 4 * Theme.grid
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 2 * Theme.grid
            anchors.rightMargin: 6 * Theme.grid
            spacing: 2 * Theme.grid
            AbstractButton {
                id: backButton
                objectName: "settingsBackButton"
                implicitWidth: Theme.touchTarget
                implicitHeight: Theme.touchTarget
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTranslate("HomePage", "Back")
                onClicked: page.backRequested()
                background: Rectangle {
                    anchors.centerIn: parent
                    width: Theme.groupButtonSize
                    height: Theme.groupButtonSize
                    radius: Theme.groupButtonRadius
                    color: backButton.down ? Theme.darkDivider : "transparent"
                }
                contentItem: Item {
                    IconImage {
                        anchors.centerIn: parent
                        width: Theme.iconSize
                        height: Theme.iconSize
                        source: "qrc:/assets/hugeicons/arrow-left.svg"
                        sourceSize: Qt.size(Theme.iconSize * Screen.devicePixelRatio, Theme.iconSize * Screen.devicePixelRatio)
                        color: Theme.darkText
                    }
                }
            }
            Label {
                text: qsTranslate("SettingsDialog", "Settings")
                color: Theme.darkText
                font.pixelSize: Theme.subtitleSize
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }

    // Android's Back closes the page; while a field records, the recorder takes it first.
    Shortcut {
        sequence: "Back"
        enabled: page.visible && !page.covered && page.recordingId === ""
        onActivated: page.backRequested()
    }

    // Takes the keys while a field records: every key, before the window's Shortcuts.
    Item {
        id: recorder
        objectName: "shortcutRecorder"
        Keys.onShortcutOverride: event => { event.accepted = page.recordingId !== "" }
        Keys.onPressed: event => {
            if (page.recordingId === "") return
            event.accepted = true
            if (event.key === Qt.Key_Escape || event.key === Qt.Key_Back) { page.stopRecording(); return }
            const portable = keys.recorded(event.key, event.modifiers)
            if (portable !== "") page.record(portable)  // "": a modifier alone, wait for the key
        }
        onActiveFocusChanged: if (!activeFocus) page.stopRecording()
    }

    // A key of a command: tapped, it records the next key; its X clears it.
    component KeyField: AbstractButton {
        id: field
        required property string commandId
        required property int keyIndex
        required property string keyText
        readonly property bool recording: page.recordingId === commandId && page.recordingIndex === keyIndex
        objectName: "shortcutField_" + commandId + "_" + keyIndex
        focusPolicy: Qt.NoFocus
        implicitHeight: Theme.touchTarget
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        Accessible.name: keyText
        onClicked: page.startRecording(commandId, keyIndex)
        background: Item {
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: Theme.fieldHeight
                radius: Theme.controlRadius
                color: Theme.darkSunken
                border.width: field.recording ? 1 : 0
                border.color: Theme.accent
            }
        }
        contentItem: Label {
            leftPadding: 2 * Theme.grid
            rightPadding: clear.visible ? clear.width : 2 * Theme.grid
            text: field.recording ? qsTr("Press a key…") : field.keyText
            color: field.recording ? Theme.darkTextMuted : Theme.darkText
            font.pixelSize: Theme.bodySize
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        AbstractButton {
            id: clear
            objectName: field.objectName + "Clear"
            visible: field.keyText !== "" && !field.recording
            anchors.right: parent.right
            width: Theme.touchTarget
            height: parent.height
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTranslate("ModalDialog", "Close")
            onClicked: { page.stopRecording(); keys.setKey(field.commandId, field.keyIndex, "") }
            contentItem: Item {
                IconImage {
                    anchors.centerIn: parent
                    width: Theme.smallIconSize
                    height: Theme.smallIconSize
                    sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
                    source: "qrc:/assets/hugeicons/window-close-light.svg"
                    color: clear.down ? Theme.darkText : Theme.darkTextMuted
                }
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: content.implicitHeight + 6 * Theme.grid
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollIndicator.vertical: ScrollIndicator {}

        Column {
            id: content
            x: 4 * Theme.grid
            width: flick.width - 8 * Theme.grid
            spacing: 2 * Theme.grid

            Label {
                text: qsTranslate("SettingsDialog", "Shortcuts")
                color: Theme.darkText
                font.pixelSize: Theme.largeSize
                font.bold: true
            }
            RowLayout {
                width: parent.width
                spacing: 2 * Theme.grid
                TextField {
                    id: search
                    objectName: "shortcutSearch"
                    Layout.fillWidth: true
                    implicitHeight: Theme.touchTarget
                    placeholderText: page.text("Search")
                    placeholderTextColor: Theme.darkTextMuted
                    color: Theme.darkText
                    font.pixelSize: Theme.bodySize
                    verticalAlignment: TextInput.AlignVCenter
                    leftPadding: 2 * Theme.grid
                    rightPadding: 2 * Theme.grid
                    background: Item {
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: Theme.fieldHeight
                            radius: Theme.controlRadius
                            color: Theme.darkSurface
                            border.width: search.activeFocus ? 1 : 0
                            border.color: Theme.accent
                        }
                    }
                }
                DarkButton {
                    objectName: "shortcutRestoreAll"
                    text: page.text("Restore all defaults")
                    onClicked: { page.stopRecording(); keys.resetAll() }
                }
            }
            Label {
                width: parent.width
                text: qsTr("Tap a field, then press the keys on a hardware keyboard. Changes apply immediately.")
                wrapMode: Text.Wrap
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }

            Repeater {
                model: keys.groups()
                Column {
                    id: group
                    required property var modelData
                    readonly property string query: search.text.trim().toLowerCase()
                    readonly property int shownRows: {
                        let count = 0
                        for (const command of modelData.commands) if (page.matches(command, query)) ++count
                        return count
                    }
                    width: parent.width
                    spacing: Theme.grid
                    visible: shownRows > 0
                    topPadding: 2 * Theme.grid
                    Label {
                        text: group.modelData.name
                        color: Theme.darkText
                        font.pixelSize: Theme.bodySize
                        font.bold: true
                    }
                    Rectangle {
                        width: parent.width
                        height: rowColumn.implicitHeight
                        radius: Theme.surfaceRadius
                        color: Theme.darkSurface
                        Column {
                            id: rowColumn
                            width: parent.width
                            Repeater {
                                model: group.modelData.commands
                                Column {
                                    id: row
                                    required property var modelData
                                    required property int index
                                    readonly property var texts: keys.revision >= 0 ? keys.keyTexts(modelData.id) : ["", ""]
                                    objectName: "shortcutRow_" + modelData.id
                                    width: rowColumn.width
                                    visible: page.matches(modelData, group.query)
                                    Rectangle {  // between two rows
                                        visible: row.index > 0
                                        width: parent.width
                                        height: 1
                                        color: Theme.darkDivider
                                    }
                                    Label {
                                        width: parent.width
                                        leftPadding: 3 * Theme.grid
                                        rightPadding: 3 * Theme.grid
                                        topPadding: 2 * Theme.grid
                                        text: row.modelData.name
                                        wrapMode: Text.Wrap
                                        color: Theme.darkText
                                        font.pixelSize: Theme.bodySize
                                    }
                                    RowLayout {
                                        x: 3 * Theme.grid
                                        width: parent.width - 6 * Theme.grid
                                        spacing: 2 * Theme.grid
                                        KeyField { commandId: row.modelData.id; keyIndex: 0; keyText: row.texts[0] }
                                        KeyField { commandId: row.modelData.id; keyIndex: 1; keyText: row.texts[1] }
                                        DarkButton {
                                            objectName: "shortcutRestore_" + row.modelData.id
                                            text: page.text("Restore")
                                            enabled: keys.revision >= 0 && !keys.isDefault(row.modelData.id)
                                            onClicked: { page.stopRecording(); keys.reset(row.modelData.id) }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // The desktop's modal for a key in use (ShortcutsPage::apply): Replace moves it here.
    ModalDialog {
        id: conflictDialog
        objectName: "shortcutConflictDialog"
        property string commandId
        property int keyIndex
        property string portable
        property string otherName
        title: page.text("Shortcut already in use")
        anchors.centerIn: parent
        width: Math.min(page.width - 8 * Theme.grid, 400)
        function ask(id, index, portable, other) {
            commandId = id
            keyIndex = index
            conflictDialog.portable = portable
            otherName = other
            open()
        }
        onAccepted: keys.setKey(commandId, keyIndex, portable)
        Label {
            width: conflictDialog.availableWidth
            text: page.text("%1 is already used by \"%2\".").arg(keys.shown(conflictDialog.portable)).arg(conflictDialog.otherName)
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton {
                objectName: "shortcutConflictReplace"
                text: page.text("Replace")
                primary: true
                onClicked: conflictDialog.accept()
            }
        ]
    }
}
