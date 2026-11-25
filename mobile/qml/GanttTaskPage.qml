import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-122 B2: the side panel's Task page while the Gantt is shown, as the desktop's GanttTaskPanel
// (FEAT-078): Task (name, Milestone, Name on the bar), Dates (Start, Duration, End or Add date),
// Details (Progress, Priority), Predecessors and Successors (each link's task, its type and lag),
// then Delete. With several tasks selected the edits apply to all of them, a value they do not share
// shows "—", the name and the links are not shown. Only the fields of the task's kind: a milestone
// has no duration nor end, a summary only its name and its switch. Every edit is one Undo step
// (MobileGanttTask). Texts are the desktop panel's.
Column {
    id: page
    required property MobileGanttTask task
    required property var menu              // the panel's StyleChoiceMenu
    required property DatePickerSheet datePicker
    required property var colorSheet        // FEAT-122 C2: the Style section's Colour
    required property MobileGanttStyle ganttStyle  // the Gantt page's lists and pictures
    property bool foldable: false
    property var openSections: ({})
    signal sectionToggled(string name)
    signal resourcesRequested()  // FEAT-122 C1: "Add people…" with no people in the document
    // FEAT-002 (user, 2026-11-23): the Name as it is typed, for the Gantt's row and bar; an empty id
    // when the typing ends (kept or not).
    signal nameTyped(string id, string name)
    function sectionOpen(name) { return openSections[name] !== false }
    readonly property var fields: task.fields
    function text(source) { return qsTranslate("GanttTaskPanel", source) }
    // FEAT-122 D: a value against the compared snapshot, red when later or longer, green when earlier.
    function changeColor(change) { return change > 0 ? Theme.later : change < 0 ? Theme.earlier : Theme.darkText }

    // FEAT-002: the whole name replaced by a key typed on the Gantt (MapPage); the typing goes on in
    // the field. False with no single task (the field disabled).
    function typeName(text) {
        if (!nameField.enabled) return false
        nameField.forceActiveFocus()
        nameField.remove(0, nameField.length)
        nameField.insert(0, text)
        nameField.cursorPosition = nameField.length
        Qt.inputMethod.show()
        return true
    }
    // A task just added by Enter (MapPage): its Name typed, the whole of it selected, as the desktop
    // table opens the new row's name.
    function editName() {
        if (!nameField.enabled) return false
        nameField.forceActiveFocus()
        nameField.selectAll()
        Qt.inputMethod.show()
        return true
    }
    // FEAT-124: the desktop's format keys (FEAT-095) on the name field's selection.
    KeyCommands { id: nameKeys }
    component NameFormatKey: Shortcut {
        required property string command
        required property int style
        sequences: nameKeys.revision >= 0 ? nameKeys.sequences(command) : []  // FEAT-125: follows Settings
        enabled: nameField.activeFocus
        onActivated: page.task.toggleNameFormat(nameField.textDocument, nameField.editingId,
                                                nameField.selectionStart, nameField.selectionEnd, style)
    }
    NameFormatKey { command: "bold"; style: 0 }
    NameFormatKey { command: "italic"; style: 1 }
    NameFormatKey { command: "underline"; style: 2 }
    NameFormatKey { command: "strikeOut"; style: 3 }

    Label {
        objectName: "ganttNoTask"
        visible: !page.fields.task
        width: parent.width
        topPadding: 4 * Theme.grid
        horizontalAlignment: Text.AlignHCenter
        text: page.text("No task selected")
        color: Theme.darkTextMuted
        font.pixelSize: Theme.bodySize
    }

    Column {
        width: parent.width
        visible: page.fields.task === true

        StyleSection {
            title: page.fields.title !== undefined ? page.fields.title : ""
            foldable: page.foldable
            open: page.sectionOpen("GanttTask")
            onToggled: page.sectionToggled("GanttTask")
            Item {
                width: parent.width
                height: Theme.touchTarget
                TipLabel {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.text("Name")
                    color: Theme.darkText
                    font.pixelSize: Theme.bodySize
                }
                // FEAT-124: the name with its formats (bold, italic, strikethrough, underline,
                // highlight), the Markdown shortcuts and the format keys, as the desktop's field. One
                // line in the look of the page's text fields.
                Rectangle {
                    id: nameBox
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.round(parent.width * 0.6)
                    height: Theme.fieldHeight
                    radius: Theme.controlRadius
                    color: Theme.darkSunken
                    opacity: nameField.enabled ? 1 : 0.4
                    border.width: 1
                    border.color: nameField.activeFocus ? Theme.accent : Theme.darkDivider
                }
                Flickable {
                    id: nameFlick
                    anchors.fill: nameBox
                    anchors.leftMargin: 2 * Theme.grid
                    anchors.rightMargin: 2 * Theme.grid
                    anchors.topMargin: (nameBox.height - Theme.touchTarget) / 2
                    anchors.bottomMargin: (nameBox.height - Theme.touchTarget) / 2
                    contentWidth: Math.max(width, nameField.contentWidth + 1)
                    contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    interactive: false
                    clip: true
                    TextEdit {
                        id: nameField
                        objectName: "ganttTaskName"
                        width: nameFlick.contentWidth
                        height: nameFlick.height
                        enabled: page.fields.nameEnabled === true
                        textFormat: TextEdit.PlainText  // the formats are the document's (GanttNames)
                        wrapMode: TextEdit.NoWrap
                        verticalAlignment: TextEdit.AlignVCenter
                        color: Theme.darkText
                        font.pixelSize: Theme.bodySize
                        selectionColor: Theme.accent
                        selectedTextColor: Theme.darkText
                        EnterKey.type: Qt.EnterKeyDone
                        Accessible.name: page.text("Name")
                        // The task being renamed, kept from the typing's start: a tap on another row while
                        // typing confirms the name on this one (as the desktop's commitPendingEdit).
                        property string editingId: ""
                        function reload() {
                            if (page.fields.nameEnabled) page.task.loadName(nameField.textDocument, page.fields.id)
                            else nameField.clear()  // several tasks: no name (theirs differ)
                        }
                        Component.onCompleted: reload()
                        onActiveFocusChanged: {
                            if (activeFocus) { editingId = page.fields.id; return }
                            Qt.inputMethod.commit()
                            // An empty name is refused and the old one comes back.
                            page.task.renameFrom(nameField.textDocument, editingId, "", 0)
                            page.nameTyped("", "")
                            reload()
                        }
                        // Loads come with no focus (reload) or right before it goes (Esc): only typing here.
                        onTextChanged: if (activeFocus && editingId !== "") page.nameTyped(editingId, text)
                        Keys.onReturnPressed: focus = false
                        Keys.onEnterPressed: focus = false
                        // Esc keeps the old name, as on the map (FEAT-002).
                        Keys.onEscapePressed: { Qt.inputMethod.reset(); reload(); focus = false }
                        onCursorRectangleChanged: {
                            if (cursorRectangle.x < nameFlick.contentX) nameFlick.contentX = cursorRectangle.x
                            else if (cursorRectangle.x + 1 > nameFlick.contentX + nameFlick.width)
                                nameFlick.contentX = cursorRectangle.x + 1 - nameFlick.width
                        }
                        Connections {
                            target: page.task
                            function onChanged() {
                                if (!nameField.activeFocus) nameField.reload()
                                else if (page.fields.id !== nameField.editingId) nameField.focus = false
                            }
                        }
                        // FEAT-133: the braces of a formula closed while typing.
                        BraceTyping {
                            document: nameField.textDocument
                            active: nameField.activeFocus
                        }
                        MarkdownTyping {
                            document: nameField.textDocument
                            active: nameField.activeFocus && page.task.nameFormattable(nameField.editingId)
                            boldWeight: page.task.nameBoldWeight(nameField.editingId)
                            onApplied: Qt.inputMethod.update(Qt.ImQueryInput)
                        }
                    }
                }
            }
            DarkSwitch {
                objectName: "ganttTaskMilestone"
                width: parent.width
                visible: page.fields.milestoneShown === true
                text: page.text("Milestone")
                checked: page.fields.milestone === true
                onToggled: page.task.setMilestone(checked)
            }
            DarkSwitch {
                objectName: "ganttTaskLabel"
                width: parent.width
                text: page.text("Name on the bar")
                checked: page.fields.nameOnBar === true
                onToggled: page.task.setNameOnBar(checked)
            }
        }

        // FEAT-080: the task's own style (the desktop's Style section): Colour (the topic's branch colour),
        // Shape (tasks of one kind only), Fill, Border, Border colour, each with a × back to the default
        // or the sheet's.
        StyleSection {
            objectName: "ganttOwnStyle"
            title: page.text("Style")
            foldable: page.foldable
            open: page.sectionOpen("GanttStyle")
            onToggled: page.sectionToggled("GanttStyle")
            GanttColorRow {
                objectName: "ganttOwnColor"
                label: page.text("Colour")
                colorSheet: page.colorSheet
                color: page.fields.color
                resettable: page.fields.colorManual === true
                resetName: page.text("Default colour")
                onPicked: color => page.task.setColor(color)
                onReset: page.task.clearColor()
            }
            OwnStyleRow {
                objectName: "ganttOwnShape"
                visible: page.fields.styleKind >= 0
                part: 0
                label: page.text("Shape")
                resetName: page.text("Default shape")
                list: page.fields.styleKind === 1 ? MobileGanttStyle.SummaryShape
                    : page.fields.styleKind === 2 ? MobileGanttStyle.MilestoneShape : MobileGanttStyle.BarShape
                value: page.fields.ownShape
                resettable: page.fields.ownShapeSet === true
            }
            OwnStyleRow {
                objectName: "ganttOwnFill"
                part: 1
                label: page.text("Fill")
                resetName: page.text("Default fill")
                list: MobileGanttStyle.Fill
                value: page.fields.ownFill
                resettable: page.fields.ownFillSet === true
            }
            OwnStyleRow {
                objectName: "ganttOwnBorder"
                part: 2
                label: page.text("Border")
                resetName: page.text("Default border")
                list: MobileGanttStyle.Border
                value: page.fields.ownBorder
                resettable: page.fields.ownBorderSet === true
            }
            GanttColorRow {
                objectName: "ganttOwnBorderColor"
                label: page.text("Border colour")
                colorSheet: page.colorSheet
                color: page.fields.borderColor
                resettable: page.fields.borderColorSet === true
                resetName: page.text("Default border colour")
                onPicked: color => page.task.setBorderColor(color)
                onReset: page.task.clearBorderColor()
            }
        }

        StyleSection {
            visible: page.fields.datesShown === true
            title: page.text("Dates")
            foldable: page.foldable
            open: page.sectionOpen("GanttDates")
            onToggled: page.sectionToggled("GanttDates")
            DateRow {
                objectName: "ganttTaskStart"
                visible: page.fields.startShown === true
                label: page.text("Start")
                iso: page.fields.start !== undefined ? page.fields.start : ""
                shownText: page.fields.startText !== undefined ? page.fields.startText : ""
                clearable: page.fields.clearShown === true
                textColor: page.changeColor(page.fields.startChange)
                onPicked: date => page.task.setStart(date)
                onCleared: page.task.clearStart()
            }
            Item {
                width: parent.width
                height: Theme.touchTarget
                visible: page.fields.addShown === true
                TipLabel {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.text("Start")
                    color: Theme.darkText
                    font.pixelSize: Theme.bodySize
                }
                DarkButton {
                    objectName: "ganttTaskAddDate"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.text("Add date")
                    onClicked: page.task.addStart()
                }
            }
            StyleStepper {
                objectName: "ganttTaskDuration"
                visible: page.fields.lengthShown === true
                label: page.text("Duration (days)")
                value: page.fields.duration >= 0 ? page.fields.duration : undefined
                minimum: page.task.minDuration
                maximum: page.task.maxDuration
                valueColor: page.changeColor(page.fields.durationChange)
                onStepped: delta => page.task.stepDuration(delta)
                onTyped: number => page.task.setDuration(number)
            }
            DateRow {
                objectName: "ganttTaskEnd"
                visible: page.fields.lengthShown === true
                enabled: page.fields.endEnabled === true
                label: page.text("End")
                iso: page.fields.end !== undefined ? page.fields.end : ""
                shownText: page.fields.endEnabled ? page.fields.endText : "—"
                textColor: page.changeColor(page.fields.endChange)
                onPicked: date => page.task.setEnd(date)
            }
            // The snapshot's dates, in place of the desktop's cell tooltips; "New" for a task added since.
            Label {
                objectName: "ganttTaskBaseline"
                visible: page.fields.baselineShown === true
                width: parent.width
                bottomPadding: 2 * Theme.grid
                wrapMode: Text.Wrap
                text: page.fields.baselineText !== undefined ? page.fields.baselineText : ""
                color: page.fields.isNew === true ? Theme.accent : Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
        }

        StyleSection {
            visible: page.fields.detailsShown === true
            title: page.text("Details")
            foldable: page.foldable
            open: page.sectionOpen("GanttDetails")
            onToggled: page.sectionToggled("GanttDetails")
            StyleStepper {
                objectName: "ganttTaskProgress"
                label: page.text("Progress (%)")
                enabled: page.fields.progressEnabled === true
                value: page.fields.progress >= 0 ? page.fields.progress : undefined
                step: page.fields.progressStep !== undefined ? page.fields.progressStep : 1
                maximum: 100
                onStepped: delta => page.task.stepProgress(delta)
                onTyped: number => page.task.setProgress(number)
            }
            StyleSelectRow {
                objectName: "ganttTaskPriority"
                label: page.text("Priority")
                menu: page.menu
                choices: page.task.priorityNames.map((name, index) => ({ value: index, name: name, group: "" }))
                value: page.fields.priority >= 0 ? page.fields.priority : undefined
                mixed: page.fields.priority < 0
                onPicked: value => page.task.setPriority(value)
            }
        }

        // FEAT-081/083: the people assigned, each a card with its role, × and share (%); "+ Add" opens the
        // people not on every task, or "No people yet" and "Add people…" (the Resources modal).
        StyleSection {
            id: peopleSection
            objectName: "ganttPeopleSection"
            visible: page.fields.peopleShown === true
            title: page.text("Resources")
            foldable: page.foldable
            open: page.sectionOpen("GanttResources")
            onToggled: page.sectionToggled("GanttResources")
            actionText: page.text("+ Add")
            actionButton.enabled: page.fields.everyoneAssigned !== true
            actionButton.opacity: page.fields.everyoneAssigned === true ? 0.4 : 1
            onActionClicked: {
                if (page.fields.hasResources !== true)
                    personChoices.choices = [{ value: "", name: page.text("No people yet"), group: "", enabled: false },
                                             { value: "+", name: page.text("Add people…"), group: "" }]
                else
                    personChoices.choices = page.task.personCandidates().map(person => ({ value: person.id, name: person.name, group: "" }))
                page.menu.openFor(personChoices)
            }
            StyleSelectRow {
                id: personChoices
                visible: false
                parent: peopleSection.actionButton
                width: parent.width
                height: parent.height
                onPicked: value => {
                    if (value === "+") page.resourcesRequested()
                    else if (value !== "") {
                        if (!page.sectionOpen("GanttResources")) page.sectionToggled("GanttResources")  // adding opens it
                        page.task.assignPerson(value)
                    }
                }
            }
            Label {
                visible: page.task.people.length === 0
                height: Theme.touchTarget
                verticalAlignment: Text.AlignVCenter
                text: page.text("None")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.bodySize
            }
            Column {
                width: peopleSection.width
                spacing: 2 * Theme.grid
                Repeater {
                    model: page.task.people
                    Rectangle {
                        id: personCard
                        required property var modelData
                        objectName: "ganttPerson_" + modelData.id
                        width: peopleSection.width
                        height: personColumn.implicitHeight
                        radius: 6
                        color: Theme.darkSunken
                        Column {
                            id: personColumn
                            width: parent.width
                            leftPadding: 2 * Theme.grid
                            Item {
                                width: parent.width - parent.leftPadding
                                height: Theme.touchTarget
                                Column {
                                    anchors.left: parent.left
                                    anchors.right: personRemove.left
                                    anchors.verticalCenter: parent.verticalCenter
                                    // On only some of the tasks: muted, as the desktop's row.
                                    opacity: personCard.modelData.partial ? 0.5 : 1
                                    Label {
                                        width: parent.width
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                        text: personCard.modelData.name
                                        color: Theme.darkText
                                        font.pixelSize: Theme.bodySize
                                    }
                                    Label {
                                        width: parent.width
                                        visible: text !== ""
                                        elide: Text.ElideRight
                                        textFormat: Text.PlainText
                                        text: personCard.modelData.partial
                                              ? [personCard.modelData.role, page.text("Only on some of the tasks")].filter(part => part !== "").join(" · ")
                                              : personCard.modelData.role
                                        color: Theme.darkTextMuted
                                        font.pixelSize: Theme.captionSize
                                    }
                                }
                                CrossButton {
                                    id: personRemove
                                    objectName: "ganttPersonRemove"
                                    anchors.right: parent.right
                                    Accessible.name: page.text("Remove from the task")
                                    onClicked: page.task.unassignPerson(personCard.modelData.id)
                                }
                            }
                            StyleStepper {
                                objectName: "ganttPersonPercent"
                                width: parent.width - parent.leftPadding - 2 * Theme.grid
                                label: page.text("Share of the task (%)")
                                value: personCard.modelData.percent > 0 ? personCard.modelData.percent : undefined
                                step: page.task.percentStep
                                minimum: page.task.minPercent
                                maximum: 100
                                onStepped: delta => page.task.stepPersonPercent(personCard.modelData.id, delta)
                                onTyped: number => page.task.setPersonPercent(personCard.modelData.id, number)
                            }
                        }
                    }
                }
            }
            Item { width: 1; height: Theme.grid }
        }

        LinkSection {
            predecessors: true
            title: page.text("Predecessors")
            links: page.task.predecessors
        }
        LinkSection {
            predecessors: false
            title: page.text("Successors")
            links: page.task.successors
        }

        Item { width: 1; height: 2 * Theme.grid }
        // At the bottom, red and as wide as the panel, as the desktop's; asked first.
        DarkButton {
            objectName: "ganttTaskDelete"
            width: parent.width
            danger: true
            text: page.text("Delete")
            onClicked: deleteDialog.open()
        }
        Item { width: 1; height: 3 * Theme.grid }
    }

    ModalDialog {
        id: deleteDialog
        objectName: "ganttDeleteDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 400)
        title: page.text("Delete task")
        onAccepted: page.task.removeTasks()
        Label {
            width: deleteDialog.availableWidth
            wrapMode: Text.Wrap
            text: page.fields.several ? qsTranslate("GanttTaskPanel", "Delete %n task(s) and their topics on the map?", "", page.fields.count)
                                      : page.text("Delete \"%1\" and its topic on the map?").arg(page.fields.name)
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton { objectName: "ganttDeleteConfirm"; text: page.text("Delete"); primary: true; onClicked: deleteDialog.accept() }
        ]
    }

    // A date field: the label, the date as a field (a tap opens the calendar), × removes the date.
    component DateRow: Item {
        id: row
        property string label
        property string iso
        property string shownText
        property bool clearable: false
        property color textColor: Theme.darkText
        signal picked(string date)
        signal cleared()
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        opacity: enabled ? 1 : 0.4
        TipLabel {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        AbstractButton {
            id: dateButton
            objectName: row.objectName + "Field"
            anchors.right: clear.left  // the × column kept on every date row, so Start and End line up
            anchors.verticalCenter: parent.verticalCenter
            width: 30 * Theme.grid
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            Accessible.name: row.label
            onClicked: page.datePicker.openFor(row.label, row.iso, date => row.picked(date))
            contentItem: Label {
                leftPadding: 2 * Theme.grid
                verticalAlignment: Text.AlignVCenter
                text: row.shownText
                color: row.textColor
                font.pixelSize: Theme.bodySize
            }
            background: Item {
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: Theme.fieldHeight
                    radius: Theme.controlRadius
                    color: dateButton.down ? Theme.darkDivider : Theme.darkSunken
                    border.width: 1
                    border.color: Theme.darkDivider
                }
            }
        }
        CrossButton {
            id: clear
            objectName: row.objectName + "Clear"
            opacity: row.clearable ? 1 : 0
            enabled: row.clearable
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Accessible.name: page.text("Remove date")
            onClicked: row.cleared()
        }
    }

    // A picture select of the Style section with its × column (shown when the tasks have their own).
    component OwnStyleRow: Item {
        id: ownRow
        required property int part
        required property string label
        required property string resetName
        property int list: 0
        property var value: -1
        property bool resettable: false
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        StyleSelectRow {
            objectName: ownRow.objectName + "Select"
            anchors.left: parent.left
            anchors.right: ownReset.left
            label: ownRow.label
            menu: page.menu
            choices: page.ganttStyle.choices(ownRow.list)
            iconKind: page.ganttStyle.iconKind(ownRow.list)
            value: ownRow.value >= 0 ? ownRow.value : undefined
            mixed: ownRow.value < 0
            onPicked: value => page.task.setOwnStyle(ownRow.part, value)
        }
        CrossButton {
            id: ownReset
            objectName: ownRow.objectName + "Reset"
            anchors.right: parent.right
            enabled: ownRow.resettable
            opacity: ownRow.resettable ? 1 : 0
            Accessible.name: ownRow.resetName
            onClicked: page.task.clearOwnStyle(ownRow.part)
        }
    }

    component CrossButton: AbstractButton {
        id: cross
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        contentItem: Label {
            text: "×"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.darkTextMuted
            font.pixelSize: Theme.largeSize
        }
        background: Rectangle {
            anchors.centerIn: parent
            width: Theme.buttonHeight
            height: Theme.buttonHeight
            radius: Theme.controlRadius
            color: cross.down ? Theme.darkDivider : "transparent"
        }
    }

    // Predecessors or Successors: "+ Add" at the title's right opens the tasks a link may reach; each
    // link a card with its task (and its summary, "in …"), ×, the type and the lag.
    component LinkSection: StyleSection {
        id: links
        required property bool predecessors
        property var links: []
        readonly property string key: predecessors ? "GanttPredecessors" : "GanttSuccessors"
        visible: page.fields.linksShown === true
        foldable: page.foldable
        open: page.sectionOpen(key)
        onToggled: page.sectionToggled(key)
        actionText: page.text("+ Add")
        onActionClicked: {
            candidates.choices = page.task.linkCandidates(links.predecessors).map(task => ({
                value: task.id, group: "",
                name: task.parent === "" ? task.name : qsTranslate("GanttTaskPanel", "%1 · in %2").arg(task.name).arg(task.parent) }))
            if (candidates.choices.length === 0)
                candidates.choices = [{ value: "", name: page.text("No other task can be linked"), group: "" }]
            page.menu.openFor(candidates)
        }
        // The choice menu's target, over the "+ Add" button (it is placed from it).
        StyleSelectRow {
            id: candidates
            visible: false
            parent: links.actionButton
            width: parent.width
            height: parent.height
            onPicked: value => { if (value !== "") page.task.addLink(links.predecessors, value) }
        }
        Label {
            visible: links.links.length === 0
            height: Theme.touchTarget
            verticalAlignment: Text.AlignVCenter
            text: page.text("None")
            color: Theme.darkTextMuted
            font.pixelSize: Theme.bodySize
        }
        Column {
            width: links.width
            spacing: 2 * Theme.grid
            Repeater {
                model: links.links
                Rectangle {
                    id: card
                    required property var modelData
                    objectName: "ganttLink_" + modelData.id
                    width: links.width
                    height: cardColumn.implicitHeight
                    radius: 6
                    color: Theme.darkSunken
                    Column {
                        id: cardColumn
                        width: parent.width
                        leftPadding: 2 * Theme.grid
                        Item {
                            width: parent.width - parent.leftPadding
                            height: Theme.touchTarget
                            Label {
                                anchors.left: parent.left
                                anchors.right: remove.left
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                textFormat: Text.PlainText
                                text: card.modelData.parent === "" ? card.modelData.name
                                      : card.modelData.name + "  " + qsTranslate("GanttTaskPanel", "in %1").arg(card.modelData.parent)
                                color: Theme.darkText
                                font.pixelSize: Theme.bodySize
                            }
                            CrossButton {
                                id: remove
                                objectName: "ganttLinkRemove"
                                anchors.right: parent.right
                                Accessible.name: page.text("Delete dependency")
                                onClicked: page.task.removeLink(links.predecessors, card.modelData.id)
                            }
                        }
                        StyleSelectRow {
                            objectName: "ganttLinkType"
                            width: parent.width - parent.leftPadding - 2 * Theme.grid
                            label: ""
                            menu: page.menu
                            choices: page.task.typeNames.map((name, index) => ({ value: index, name: name, group: "" }))
                            value: card.modelData.type
                            onPicked: value => page.task.setLinkType(links.predecessors, card.modelData.id, value)
                        }
                        StyleStepper {
                            objectName: "ganttLinkLag"
                            width: parent.width - parent.leftPadding - 2 * Theme.grid
                            label: page.text("Lag")
                            value: card.modelData.lag
                            minimum: -page.task.maxLag
                            maximum: page.task.maxLag
                            onStepped: delta => page.task.setLinkLag(links.predecessors, card.modelData.id, card.modelData.lag + delta)
                            onTyped: number => page.task.setLinkLag(links.predecessors, card.modelData.id, number)
                        }
                    }
                }
            }
        }
        Item { width: 1; height: Theme.grid }
    }
}
