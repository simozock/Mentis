import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-122 stage D: the desktop's Baseline popover (FEAT-082) as a dark modal at the top of the page.
// New snapshot: the name field filled with "Baseline N" and Save (disabled without tasks). Then the
// sheet's snapshots, newest first, under None: a tap compares with one (✓) or, on the compared one,
// stops; each row has the pen (the Rename modal; the phone's font has no ✎) and ×. Under the compared one, "N tasks removed" opens
// their names. While comparing, the desktop's legend (the snapshot, Later, Earlier, New): on the phone
// it sits here, the chart's Name header shows only the snapshot's name. Taking, renaming and removing
// are one Undo step each (MobileGantt); the comparison is the view's, not saved. Texts are the desktop's.
ModalDialog {
    id: dialog
    objectName: "baselinesDialog"
    required property MobileGantt gantt
    function text(source) { return qsTranslate("GanttBaselineMenu", source) }

    title: qsTranslate("MainWindow", "Baseline")
    closePolicy: Popup.CloseOnEscape
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 400)
    height: Math.min(parent ? parent.height - 8 * Theme.grid : 0, implicitHeight)

    property bool removedOpen: false
    function openBaselines() {
        nameField.text = dialog.gantt.nextBaselineName
        dialog.removedOpen = false
        body.contentY = 0
        dialog.open()
    }
    onClosed: Qt.inputMethod.hide()
    Connections {
        target: dialog.gantt
        // A new list (taken, renamed, removed): the next default name, as the desktop's popover.
        function onBaselinesChanged() { if (!nameField.activeFocus) nameField.text = dialog.gantt.nextBaselineName }
    }
    readonly property bool canCreate: !dialog.gantt.empty
    function create() {
        if (!dialog.canCreate || nameField.text.trim() === "") return
        dialog.gantt.addBaseline(nameField.text)
        nameField.focus = false
    }

    Flickable {
        id: body
        width: dialog.availableWidth
        height: Math.max(0, Math.min(content.height, (dialog.parent ? dialog.parent.height - 8 * Theme.grid : 600)
                                                     - (dialog.header ? dialog.header.implicitHeight : 0)
                                                     - (dialog.footer ? dialog.footer.implicitHeight : 0) - 2 * dialog.spacing))
        contentHeight: content.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: content
            width: body.width

            Label {
                width: parent.width
                height: Theme.touchTarget
                verticalAlignment: Text.AlignVCenter
                text: dialog.text("New snapshot")
                color: Theme.darkText
                font.pixelSize: 13
                font.weight: Font.DemiBold
            }
            Item {
                width: parent.width
                height: Theme.touchTarget
                NameField {
                    id: nameField
                    objectName: "baselineName"
                    anchors.left: parent.left
                    anchors.right: saveButton.left
                    anchors.rightMargin: 2 * Theme.grid
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: dialog.canCreate
                    Accessible.name: dialog.text("Snapshot name")
                    onAccepted: dialog.create()
                }
                DarkButton {
                    id: saveButton
                    objectName: "baselineSave"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    primary: true
                    enabled: dialog.canCreate && nameField.text.trim() !== ""
                    text: dialog.text("Save")
                    onClicked: dialog.create()
                }
            }
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.darkDivider
            }
            Label {
                objectName: "baselineEmpty"
                visible: dialog.gantt.baselines.length === 0
                width: parent.width
                height: Theme.touchTarget
                verticalAlignment: Text.AlignVCenter
                text: dialog.text("No snapshots yet")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.bodySize
            }
            ChoiceRow {
                objectName: "baselineNone"
                visible: dialog.gantt.baselines.length > 0
                name: dialog.text("None")
                checked: dialog.gantt.comparedBaseline === ""
                onClicked: dialog.gantt.compareBaseline("")
            }
            Repeater {
                model: dialog.gantt.baselines
                Column {
                    id: entry
                    required property var modelData
                    required property int index
                    readonly property bool compared: modelData.id === dialog.gantt.comparedBaseline
                    width: content.width
                    Item {
                        width: parent.width
                        height: Theme.touchTarget
                        ChoiceRow {
                            objectName: "baselineRow" + entry.index
                            anchors.left: parent.left
                            anchors.right: renameButton.left
                            name: entry.modelData.name
                            detail: entry.modelData.taken
                            checked: entry.compared
                            onClicked: dialog.gantt.compareBaseline(entry.compared ? "" : entry.modelData.id)
                        }
                        GlyphButton {
                            id: renameButton
                            objectName: "baselineRename" + entry.index
                            anchors.right: removeButton.left
                            icon.source: "qrc:/assets/hugeicons/pen.svg"  // the map's Rename
                            Accessible.name: dialog.text("Rename")
                            onClicked: renameDialog.openFor(entry.modelData.id, entry.modelData.name)
                        }
                        GlyphButton {
                            id: removeButton
                            objectName: "baselineRemove" + entry.index
                            anchors.right: parent.right
                            glyph: "×"
                            Accessible.name: dialog.text("Delete snapshot")
                            onClicked: dialog.gantt.removeBaseline(entry.modelData.id)
                        }
                    }
                    // Under the compared snapshot, the tasks removed since; a tap shows their names.
                    ChoiceRow {
                        objectName: "baselineRemoved"
                        visible: entry.compared && dialog.gantt.removedTasks.length > 0
                        name: (dialog.removedOpen ? "▾ " : "▸ ")
                              + qsTranslate("GanttBaselineMenu", "%n task(s) removed", "", dialog.gantt.removedTasks.length)
                        muted: true
                        onClicked: dialog.removedOpen = !dialog.removedOpen
                    }
                    Repeater {
                        model: entry.compared && dialog.removedOpen ? dialog.gantt.removedTasks : []
                        Label {
                            required property string modelData
                            width: content.width
                            leftPadding: 8 * Theme.grid
                            topPadding: Theme.grid
                            bottomPadding: Theme.grid
                            elide: Text.ElideRight
                            text: modelData
                            color: Theme.darkTextMuted
                            font.pixelSize: Theme.bodySize
                        }
                    }
                }
            }
            // The desktop's legend over the chart, while comparing.
            Flow {
                objectName: "baselineLegend"
                visible: dialog.gantt.comparedBaseline !== ""
                width: parent.width
                topPadding: 2 * Theme.grid
                spacing: 4 * Theme.grid
                LegendItem { sample: "#9aa2ac"; text: dialog.gantt.comparedName }
                LegendItem { sample: Theme.later; text: qsTranslate("GanttView", "Later") }
                LegendItem { sample: Theme.earlier; text: qsTranslate("GanttView", "Earlier") }
                Rectangle {
                    height: 16
                    width: newLabel.implicitWidth + 12
                    radius: 8
                    color: Theme.accent
                    Label {
                        id: newLabel
                        anchors.centerIn: parent
                        text: qsTranslate("GanttCanvas", "New")
                        color: "#ffffff"
                        font.pixelSize: 11
                    }
                }
            }
        }
    }
    buttons: [
        DarkButton { objectName: "baselinesDone"; text: qsTranslate("ColorSheet", "Done"); primary: true; onClicked: dialog.close() }
    ]

    // Rename: Enter or Rename applies it (an empty name keeps the old one), the X and Back drop it.
    ModalDialog {
        id: renameDialog
        objectName: "baselineRenameDialog"
        property string baselineId
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 320)
        title: dialog.text("Rename")
        function openFor(id, name) {
            renameDialog.baselineId = id
            renameField.text = name
            renameDialog.open()
            renameField.forceActiveFocus()
            renameField.selectAll()
        }
        onAccepted: if (renameField.text.trim() !== "") dialog.gantt.renameBaseline(baselineId, renameField.text)
        onClosed: Qt.inputMethod.hide()
        NameField {
            id: renameField
            objectName: "baselineRenameField"
            width: renameDialog.availableWidth
            Accessible.name: dialog.text("Snapshot name")
            onAccepted: renameDialog.accept()
        }
        buttons: [
            DarkButton { objectName: "baselineRenameApply"; text: dialog.text("Rename"); primary: true; onClicked: renameDialog.accept() }
        ]
    }

    component NameField: TextField {
        id: field
        height: Theme.touchTarget
        color: Theme.darkText
        placeholderTextColor: Theme.darkTextMuted
        font.pixelSize: Theme.bodySize
        verticalAlignment: TextInput.AlignVCenter
        leftPadding: 2 * Theme.grid
        rightPadding: 2 * Theme.grid
        selectByMouse: true
        maximumLength: 80  // GanttBaseline::kMaxName
        opacity: enabled ? 1 : 0.4
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

    // A row of the list: ✓ when checked, the name, the date taken muted at the right.
    component ChoiceRow: AbstractButton {
        id: choice
        property string name
        property string detail
        property bool muted: false
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        checkable: false
        focusPolicy: Qt.NoFocus
        Accessible.name: name
        contentItem: Item {
            Label {
                id: check
                anchors.left: parent.left
                anchors.leftMargin: 2 * Theme.grid
                anchors.verticalCenter: parent.verticalCenter
                width: 5 * Theme.grid
                text: choice.checked ? "✓" : ""
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            Label {
                anchors.left: check.right
                anchors.right: taken.left
                anchors.rightMargin: 2 * Theme.grid
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: choice.name
                color: choice.muted ? Theme.darkTextMuted : Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            Label {
                id: taken
                anchors.right: parent.right
                anchors.rightMargin: 2 * Theme.grid
                anchors.verticalCenter: parent.verticalCenter
                text: choice.detail
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: choice.down ? Theme.darkDivider : choice.checked ? Theme.darkRaised : "transparent"
        }
    }

    component GlyphButton: AbstractButton {
        id: glyphButton
        property string glyph
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        contentItem: Item {
            Label {
                anchors.fill: parent
                visible: glyphButton.glyph !== ""
                text: glyphButton.glyph
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.darkTextMuted
                font.pixelSize: Theme.largeSize
            }
            IconImage {
                anchors.centerIn: parent
                visible: glyphButton.glyph === ""
                width: Theme.smallIconSize
                height: Theme.smallIconSize
                source: glyphButton.icon.source
                color: Theme.darkTextMuted
                sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
            }
        }
        background: Rectangle {
            anchors.centerIn: parent
            width: Theme.buttonHeight
            height: Theme.buttonHeight
            radius: Theme.controlRadius
            color: glyphButton.down ? Theme.darkDivider : "transparent"
        }
    }

    component LegendItem: Row {
        property color sample
        property string text
        height: 16
        spacing: 6
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 4
            radius: 2
            color: parent.sample
        }
        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: parent.text
            color: Theme.darkTextMuted
            font.pixelSize: Theme.captionSize
        }
    }
}
