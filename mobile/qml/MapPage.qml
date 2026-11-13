import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Mentis.Mobile

// FEAT-097: the map. One finger pans, two fingers pinch to zoom, a tap selects a topic, a double
// tap on a topic edits its title, on the background fits the map. The sheet select switches sheet.
// Stage B1: Undo/Redo in the top right group, then Present as on the desktop's top bar (disabled
// until Pitch comes to the phone). With a topic selected, the desktop's blue "+" knobs beside and under it add a child or a
// sibling (then edit its title), and its collapse control closes or opens its branch; the topic
// bar at the bottom edits the title or deletes it. The title is typed in the topic itself, as on the desktop (ui-text-editing.md,
// "Mappa"): the topic grows while it is typed and sits in the middle of the map's free band, above
// the keyboard.
// Stage B2: a long press on a topic picks it up, the finger drags it and lifting the finger drops it
// where the slot shows (MapCanvas.beginMove, the desktop's rules).
// The topic bar at the bottom also holds the desktop's topic tools (TopicTools), with Image,
// Sticker and Format painter working and the tools the phone cannot draw yet disabled.
// A hardware keyboard runs the desktop's keys for the commands the phone has (KeyCommands).
// FEAT-110: Relationship works as Format painter (a tap arms it, a double tap keeps it on); a tap on
// a relationship selects it, the bar then holds Rename (its label, also by a double tap) and Delete.
// Stage B3: the topic bar's Note, the note's icon on a topic and the desktop's key open the note
// (NoteDialog); a formatted title is edited keeping its formatting.
// FEAT-111: the topic bar's Select more turns on the multiple selection: a tap adds or removes a
// topic, and the bar holds Delete, Marker and sticker and Paste style for all of them.
// FEAT-115: the topic bar's Boundary puts one around the selected topics; a tap on a boundary
// selects it, the bar then holds Rename (its title, typed in its tab; also by a double tap) and Delete.
// Its Summary adds a summary over them and opens the new summary topic's title.
Page {
    id: page
    required property MobileMap map
    signal backRequested()

    // True while the selected topic's title is typed in titleEditor.
    property bool editing: false
    // FEAT-110: the field holds the selected relationship's label instead (applied when it ends),
    // FEAT-115 the selected boundary's title: one line, one step, Esc leaves it as it was.
    property bool editingRelation: false
    property bool editingBoundary: false
    readonly property bool editingLabel: editingRelation || editingBoundary
    // True while a popup (a modal, a select's list) is over the map: the map's handlers stop, as a
    // modal does not keep them from a finger dragged over its list (user's report 2026-11-10).
    readonly property bool covered: Overlay.overlay ? Overlay.overlay.children.length > 0 : false

    // A new topic and its title are one Undo step: the step stays open from the add to the end of
    // the editing (MobileMap.editingTitle).
    function addAndName(add) {
        page.map.editingTitle = true
        if (add()) page.beginEdit(false)
        else page.map.editingTitle = false
    }

    // The whole title selected, so the first key replaces it (as the desktop's double click); atEnd:
    // the cursor after it (the desktop's Space).
    function beginEdit(atEnd) {
        page.editingRelation = page.map.selectedRelationId !== ""
        page.editingBoundary = page.map.selectedBoundaryId !== ""
        if ((page.map.selectedId === "" && !page.editingLabel) || page.map.multiSelect) return  // FEAT-111: no title editing
        page.map.stopFormatPainter()
        page.map.stopRelation()
        page.map.editingTitle = true
        // B3: a formatted title as rich text, with its formatting; any other as plain text.
        titleEditor.rich = !page.editingLabel && page.map.selectedTitleRich
        titleEditor.textFormat = titleEditor.rich ? TextEdit.RichText : TextEdit.PlainText
        if (titleEditor.rich) page.map.loadTitle(titleEditor.textDocument)
        else titleEditor.text = page.editingRelation ? page.map.selectedRelationLabel
            : page.editingBoundary ? page.map.selectedBoundaryTitle : page.map.selectedTitle
        page.editing = true
        canvas.editing = true
        if (atEnd) titleEditor.cursorPosition = titleEditor.length
        else titleEditor.selectAll()
        titleEditor.forceActiveFocus()
        Qt.inputMethod.show()
        canvas.centerSelected()
    }

    // Each change goes into the map at once, so the topic and the layout follow the text. Called
    // later: several changes in one event make one layout.
    function applyTitle() {
        if (!page.editing || page.editingLabel) return  // a label is one step, written at the end
        if (titleEditor.rich) page.map.renameFrom(titleEditor.textDocument, titleEditor.preeditText, titleEditor.cursorPosition)
        else page.map.rename(titleEditor.liveText)
    }

    // Confirms (Enter, a tap on the map, the keyboard closed, the focus lost, another command): the
    // step with the title closes.
    function goBack() { endEdit(); backRequested() }

    function endEdit() {
        if (!page.editing) return
        Qt.inputMethod.commit()  // the word being composed belongs to the title
        if (page.editingRelation) {
            page.map.renameRelation(titleEditor.text)  // unchanged: no step
            page.editingRelation = false
        } else if (page.editingBoundary) {
            page.map.renameBoundary(titleEditor.text)  // unchanged: no step
            page.editingBoundary = false
        } else {
            page.applyTitle()
        }
        page.editing = false
        canvas.editing = false
        titleEditor.focus = false
        Qt.inputMethod.hide()
        page.map.editingTitle = false
    }

    background: Rectangle { color: Theme.surface }

    MapCanvas {
        id: canvas
        objectName: "mapCanvas"
        anchors.fill: parent
        map: page.map
        // What covers the map: the top groups, the keyboard while a title is edited, otherwise the
        // bottom groups. A topic selected by a key or an edit is shown between them.
        bandTop: historyGroup.y + historyGroup.height - Theme.grid
        bandBottom: page.editing ? page.keyboardTop : Math.min(page.keyboardTop, page.bottomGroupsTop)
        onBandChanged: if (page.editing) canvas.centerSelected()
        onNoteRequested: page.openNote()
        onWebLinkRequested: page.map.openWebLink()
        onLabelMenuRequested: (x, y) => {
            page.endEdit()
            labelMenu.openAt(x, y)
        }
        onTopicLinkRequested: {
            page.endEdit()
            if (page.map.followTopicLink()) canvas.revealSelected()
        }
        onFilesRequested: (x, y) => {
            page.endEdit()
            page.map.stopFormatPainter()
            filesMenu.openAt(x, y)
        }
        onMarkerRequested: (marker, x, y) => {
            page.endEdit()
            page.map.stopFormatPainter()
            markerMenu.openAt(marker, x, y)
        }

        // The handlers report increments: the canvas applies each one at once.
        DragHandler {
            id: drag
            enabled: !page.covered
            target: null
            maximumPointCount: 1
            property point last
            // After a long press the finger moves the picked-up topic instead of the map; a drag
            // that starts on a corner of the selected picture resizes it, one on the picture moves it
            // (FEAT-106), one on a handle of the selected relationship bends it (FEAT-110).
            onActiveChanged: {
                last = centroid.position
                const at = centroid.pressPosition
                if (active && !canvas.moving && !canvas.beginRelationBend(at.x, at.y) && !canvas.beginPictureResize(at.x, at.y))
                    canvas.beginPictureDrag(at.x, at.y)
                if (!active && canvas.moving) canvas.endMove(true)
            }
            onCentroidChanged: {
                if (!active) return
                if (canvas.moving) canvas.moveTo(centroid.position.x, centroid.position.y)
                else canvas.panBy(centroid.position.x - last.x, centroid.position.y - last.y)
                last = centroid.position
            }
        }
        PinchHandler {
            id: pinch
            enabled: !page.covered
            target: null
            property real lastScale: 1
            property point lastCentre
            onActiveChanged: { lastScale = 1; lastCentre = centroid.position }
            onActiveScaleChanged: {
                canvas.zoomBy(activeScale / lastScale, centroid.position.x, centroid.position.y)
                lastScale = activeScale
            }
            onCentroidChanged: {
                if (!active) return
                canvas.panBy(centroid.position.x - lastCentre.x, centroid.position.y - lastCentre.y)
                lastCentre = centroid.position
            }
        }
        TapHandler {
            enabled: !page.covered
            // True when the last tap went to the format painter or the Relationship tool (FEAT-110)
            // or to the multiple selection (FEAT-111): its double tap does not edit or fit.
            property bool painted: false
            onTapped: eventPoint => {
                page.endEdit()
                painted = page.map.formatPainter || page.map.relationTool || page.map.multiSelect
                canvas.tapAt(eventPoint.position.x, eventPoint.position.y)
            }
            // The tap before selected what is under the finger: a topic's title or a relationship's
            // label is edited; FEAT-116: a picked formula opens its modal.
            onDoubleTapped: if (!painted) page.map.pictureSelected && page.map.selectedFormula !== "" ? page.openFormula()
                                          : page.map.selectedId !== "" || page.map.selectedRelationId !== "" || page.map.selectedBoundaryId !== ""
                                          ? page.beginEdit(false) : canvas.fit()
            onLongPressed: {
                // While armed a topic is only painted (FEAT-085) or related (FEAT-110); in the multiple
                // selection a tap only adds or removes (FEAT-111).
                if (page.map.formatPainter || page.map.relationTool || page.map.multiSelect) return
                page.endEdit()
                // FEAT-114: on a tag pill the menu with Edit tags instead of picking the topic up.
                if (canvas.labelMenuAt(point.position.x, point.position.y)) return
                canvas.beginMove(point.position.x, point.position.y)
            }
            // Lifted without dragging: the drop has no effect, the move ends. When the drag takes
            // the finger over, this handler lets it go too; the drag is active by the time this runs.
            onPressedChanged: if (!pressed && canvas.moving) Qt.callLater(() => { if (canvas.moving && !drag.active) canvas.endMove(true) })
        }
        // Mouse wheel on the desktop build, for trying the prototype without a phone.
        WheelHandler {
            enabled: !page.covered
            target: null
            onWheel: event => canvas.zoomBy(Math.pow(1.0015, event.angleDelta.y), event.x, event.y)
        }
    }

    // The desktop's "+" knobs of the selected topic (TopicTools, FEAT-072 F3; user's request
    // 2026-11-11): 18 px blue circles at zoom 1 in the selection colour, the child one on the branch side, the
    // sibling one under the topic. Under the floating groups, hidden while a title is typed or a topic
    // is dragged; the canvas gives each one's place and its 48 px target beside the topic.
    component PlusKnob: AbstractButton {
        id: knob
        required property point centre
        required property rect target
        visible: !page.editing && !canvas.moving && !page.map.multiSelect && target.width > 0
        x: target.x
        y: target.y
        width: target.width
        height: target.height
        padding: 0
        focusPolicy: Qt.NoFocus
        Accessible.name: text
        background: Item {}
        contentItem: Item {
            Rectangle {
                x: knob.centre.x - knob.target.x - width / 2
                y: knob.centre.y - knob.target.y - height / 2
                width: Theme.knobSize
                height: Theme.knobSize
                scale: canvas.zoom  // BUG-051: as the topic
                radius: width / 2
                color: knob.down ? Qt.darker(Theme.selection, 1.15) : Theme.selection
                // The white "+", arms of 4 px and 1.6 px thick as the desktop's.
                Rectangle { anchors.centerIn: parent; width: 9.6; height: 1.6; radius: 0.8; color: Theme.surface }
                Rectangle { anchors.centerIn: parent; width: 1.6; height: 9.6; radius: 0.8; color: Theme.surface }
            }
        }
    }
    // The desktop's collapse control of the selected topic (NodeItem, FEAT-016; user's request
    // 2026-11-11, in the place of the topic bar's Collapse/Expand): an open branch's circle on the
    // children's side, a closed one's pill with the count of its hidden topics, tied to the topic by
    // a line. Under the knobs; the canvas gives its place and its target from the topic's border.
    AbstractButton {
        id: collapseControl
        objectName: "collapseControl"
        readonly property rect target: canvas.collapseTarget
        readonly property rect control: canvas.collapseControl
        readonly property bool closed: canvas.collapseCount > 0
        visible: !page.editing && !canvas.moving && !page.map.multiSelect && target.width > 0
        x: target.x
        y: target.y
        width: target.width
        height: target.height
        padding: 0
        focusPolicy: Qt.NoFocus
        text: closed ? qsTr("Expand branch") : qsTr("Collapse branch")
        Accessible.name: text
        background: Item {}
        contentItem: Item {
            // The line from the topic to a closed branch's pill.
            Rectangle {
                visible: collapseControl.closed
                readonly property real fromX: canvas.collapseEdge.x - collapseControl.target.x
                readonly property real fromY: canvas.collapseEdge.y - collapseControl.target.y
                readonly property real toX: collapseControl.control.x - collapseControl.target.x
                                           + (fromX <= collapseControl.control.x - collapseControl.target.x ? 0 : collapseControl.control.width)
                readonly property real toY: collapseControl.control.y - collapseControl.target.y
                                           + (fromY <= collapseControl.control.y - collapseControl.target.y ? 0 : collapseControl.control.height)
                readonly property bool vertical: Math.abs(fromX - toX) < 0.5
                x: vertical ? fromX - 0.75 * canvas.zoom : Math.min(fromX, toX)
                y: vertical ? Math.min(fromY, toY) : fromY - 0.75 * canvas.zoom
                width: vertical ? 1.5 * canvas.zoom : Math.abs(toX - fromX)
                height: vertical ? Math.abs(toY - fromY) : 1.5 * canvas.zoom
                color: canvas.collapseColor
            }
            Rectangle {
                x: collapseControl.control.x - collapseControl.target.x + 0.75 * canvas.zoom
                y: collapseControl.control.y - collapseControl.target.y + 0.75 * canvas.zoom
                width: collapseControl.control.width - 1.5 * canvas.zoom
                height: collapseControl.control.height - 1.5 * canvas.zoom
                radius: height / 2
                color: collapseControl.down ? Qt.darker(canvas.mapBackground, 1.08) : canvas.mapBackground
                border.width: 1.5 * canvas.zoom
                border.color: canvas.collapseColor
                // BUG-052: the digits' ink box goes to the pill's centre. Centring the line box
                // (ascent + descent) left the digits off centre, as the desktop's labels were (FEAT-060).
                TextMetrics {
                    id: countMetrics
                    font: countText.font
                    text: countText.text
                }
                Text {
                    id: countText
                    objectName: "collapseCount"
                    readonly property rect ink: countMetrics.tightBoundingRect
                    visible: collapseControl.closed
                    text: canvas.collapseCount
                    font.pixelSize: Theme.captionSize
                    transformOrigin: Item.TopLeft
                    scale: canvas.zoom
                    x: parent.width / 2 - (ink.x + ink.width / 2) * canvas.zoom
                    y: parent.height / 2 - (baselineOffset + ink.y + ink.height / 2) * canvas.zoom
                    color: canvas.collapseColor
                }
            }
        }
        onClicked: page.map.toggleCollapsed()
    }
    PlusKnob {
        objectName: "addChildKnob"
        text: qsTr("Add a subtopic")
        centre: canvas.childKnob
        target: canvas.childKnobTarget
        onClicked: page.addAndName(() => page.map.addChild())
    }
    PlusKnob {
        objectName: "addSiblingKnob"
        text: qsTr("Add a topic after")
        centre: canvas.siblingKnob
        target: canvas.siblingKnobTarget
        onClicked: page.addAndName(() => page.map.addSibling())
    }

    // The floating groups over the map, as the desktop's (docs/design/ui-components.md): at the top
    // left Back, the sheet select and Add sheet, at the top right Undo/Redo; with a topic selected,
    // the topic bar at the bottom. The visible frames sit 16 px from the edges.
    readonly property int groupMargin: 3 * Theme.grid   // 16 px less the group's 4 px touch margin
    // The top of the bottom groups' visible frames (each group has a 4 px touch margin).
    readonly property real bottomGroupsTop: topicBar.visible ? topicBar.y + Theme.grid : page.height

    // FEAT-112 (user's request 2026-11-12): Back is the arrow alone, then the sheet select in the
    // place of the map's title and Add sheet beside it; shown also with one sheet. A long press on
    // the select opens the sheet menu, a row dragged in its list moves the sheet.
    FloatingGroup {
        id: sheetGroup
        objectName: "sheetGroup"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: page.groupMargin
        GroupButton {
            id: backButton
            objectName: "backButton"
            icon.source: "qrc:/assets/hugeicons/arrow-left.svg"
            text: qsTr("Back")
            onClicked: page.goBack()
        }
        SheetSelect {
            id: sheetSelect
            objectName: "sheetSelect"
            model: page.map.sheets
            currentIndex: page.map.currentSheet
            // What is left of the row by the other group: both groups' touch margins, 4 px between
            // the frames, Back, Add sheet and the row's side padding; the name is elided.
            width: Math.min(implicitWidth, page.width - 2 * page.groupMargin - historyGroup.width - Theme.grid
                - backButton.width - addSheetButton.width - 2 * sheetGroup.sidePadding)
            onActivated: index => { page.endEdit(); page.map.switchSheet(index) }
            onMenuRequested: {
                page.endEdit()
                sheetMenu.x = sheetGroup.x + sheetSelect.x
                sheetMenu.y = sheetGroup.y + sheetGroup.height + Theme.grid
                sheetMenu.open()
            }
            onMoveRequested: (from, to) => { page.endEdit(); page.map.moveSheet(from, to) }
        }
        GroupButton {
            id: addSheetButton
            objectName: "addSheetButton"
            icon.source: "qrc:/assets/hugeicons/plus.svg"
            text: qsTr("Add sheet")
            onClicked: { page.endEdit(); page.map.addSheet() }
        }
    }

    FloatingGroup {
        id: historyGroup
        objectName: "historyGroup"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: page.groupMargin
        GroupButton {
            // FEAT-113: the desktop's Find (Ctrl+F, also on a hardware keyboard).
            objectName: "findButton"
            icon.source: "qrc:/assets/hugeicons/search.svg"
            text: qsTr("Find")
            onClicked: page.openSearch()
        }
        GroupButton {
            objectName: "undoButton"
            icon.source: "qrc:/assets/hugeicons/undo.svg"
            text: qsTr("Undo")
            enabled: page.map.canUndo
            onClicked: { page.endEdit(); page.map.undo() }
        }
        GroupButton {
            objectName: "redoButton"
            icon.source: "qrc:/assets/hugeicons/redo.svg"
            text: qsTr("Redo")
            enabled: page.map.canRedo
            onClicked: { page.endEdit(); page.map.redo() }
        }
        GroupButton {
            // The desktop's Fit to window (user's request 2026-11-12); also the background's double tap.
            objectName: "fitButton"
            icon.source: "qrc:/assets/hugeicons/fit.svg"
            text: qsTr("Fit to window")
            onClicked: { page.endEdit(); canvas.fit() }
        }
        GroupButton {
            objectName: "presentButton"
            icon.source: "qrc:/assets/hugeicons/present.svg"
            text: qsTr("Present")
            enabled: false  // Pitch is not on the phone yet (FEAT-097)
        }
    }

    // FEAT-113: ends the title editing and the tools first, as a sheet switch.
    function openSearch() {
        page.endEdit()
        page.map.stopFormatPainter()
        page.map.stopRelation()
        page.map.stopMultiSelect()
        searchPanel.openSearch()
    }

    // FEAT-113: at the top, 16 px from the edges, at most 480 px wide (as the note modal).
    SearchPanel {
        id: searchPanel
        map: page.map
        keyboardTop: page.keyboardTop
        x: (page.width - width) / 2
        y: page.groupMargin + Theme.grid
        width: Math.min(page.width - 8 * Theme.grid, 480)
        onRevealed: canvas.revealSelected()
    }

    StickerPicker {
        id: stickerPicker
        map: page.map
    }

    MarkerMenu {
        id: markerMenu
        map: page.map
    }

    NoteDialog {
        id: noteDialog
        map: page.map
        keyboardTop: page.keyboardTop
    }
    WebLinkDialog {
        id: webLinkDialog
        map: page.map
    }
    FormulaDialog {  // FEAT-116
        id: formulaDialog
        map: page.map
        keyboardTop: page.keyboardTop
    }
    LabelsDialog {
        id: labelsDialog
        map: page.map
    }
    LabelMenu {
        id: labelMenu
        onEditRequested: page.openLabels()
    }

    FilesMenu {
        id: filesMenu
        map: page.map
        onAddRequested: page.pickFiles()
        onRiskyOpenRequested: (index, name) => {
            riskyFile.index = index
            riskyFile.name = name
            riskyFile.open()
        }
    }

    // FEAT-114: the desktop's question before opening a program or a script.
    ModalDialog {
        id: riskyFile
        objectName: "riskyFileDialog"
        property int index: -1
        property string name
        title: qsTr("Open program or script?")
        anchors.centerIn: parent
        width: Math.min(page.width - 8 * Theme.grid, 400)
        onAccepted: page.map.openFile(index)
        Label {
            width: riskyFile.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            text: qsTr("\"%1\" is a program or a script: it can change your computer. Open it only if you trust where it comes from.").arg(riskyFile.name)
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton {
                text: qsTr("Cancel")
                onClicked: riskyFile.reject()
            },
            DarkButton {
                objectName: "riskyFileOpenButton"
                text: qsTr("Open")
                primary: true
                onClicked: riskyFile.accept()
            }
        ]
    }

    // FEAT-114, the desktop's Insert > File... (MainWindow::attachFiles): the picked files copied and
    // checked one by one; a large one asked about (the images' "Large file"); then all the accepted
    // ones attached in one Undo step.
    function pickFiles() {
        if (page.map.selectedId === "") return
        page.map.stopFormatPainter()
        filesPicker.open()
    }
    function attachPicked(urls) {
        const reasons = []
        const queue = []
        for (const url of urls) {
            const picked = page.map.pickFile(url)
            if (!picked.ok) reasons.push(picked.reason)
            else queue.push(picked)
        }
        largeFiles.reasons = reasons.join("\n")
        largeFiles.accepted = []
        largeFiles.queue = queue
        largeFiles.next()
    }
    FileDialog {
        id: filesPicker
        title: qsTr("Attach files")
        fileMode: FileDialog.OpenFiles
        onAccepted: page.attachPicked(selectedFiles)
    }
    ModalDialog {
        id: largeFiles
        objectName: "largeFileDialog"
        property var queue: []
        property var accepted: []
        property string reasons  // why picked files were refused, said once the others are in
        property string question
        title: qsTr("Large file")
        anchors.centerIn: parent
        width: Math.min(page.width - 8 * Theme.grid, 400)
        // The next picked file: a large one is asked about, the others go straight in.
        function next() {
            while (queue.length > 0) {
                const picked = queue.shift()
                if (!picked.large) { accepted.push(picked.path); continue }
                question = picked.largeReason
                pendingPath = picked.path
                open()
                return
            }
            if (accepted.length > 0) page.map.attachFiles(accepted)
            accepted = []
            page.imageError = reasons  // after the attach: its selection change clears the note
        }
        property string pendingPath
        onAccepted: { accepted.push(pendingPath); Qt.callLater(next) }
        onRejected: Qt.callLater(next)
        Label {
            width: largeFiles.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            text: largeFiles.question
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton {
                text: qsTr("Cancel")
                onClicked: largeFiles.reject()
            },
            DarkButton {
                text: qsTr("Insert")
                primary: true
                onClicked: largeFiles.accept()
            }
        ]
    }

    function openTopicLink() {  // FEAT-114
        page.endEdit()
        page.map.stopFormatPainter()
        page.map.stopRelation()
        searchPanel.openPicker()
    }

    function openLabels() {  // FEAT-114
        page.endEdit()
        page.map.stopFormatPainter()
        labelsDialog.openLabels()
    }

    function openNote() {
        page.endEdit()
        page.map.stopFormatPainter()
        noteDialog.openNote()
    }

    // FEAT-116, the desktop's Insert > LaTeX formula... and the formula's double click.
    function openFormula() {
        if (page.map.selectedId === "" || page.map.multiSelect) return
        page.endEdit()
        page.map.stopFormatPainter()
        formulaDialog.openFormula()
    }

    // The system's picture picker on the phone, Qt's file dialog elsewhere.
    function pickImage() {
        if (page.map.selectedId === "") return
        page.map.stopFormatPainter()
        if (!photoPicker.open()) imagePicker.open()
    }

    // A picked picture: copied and checked, then inserted, or asked about when it is large.
    function insertPicked(url) {
        const picked = page.map.pickImage(url)
        if (!picked.ok) { page.imageError = picked.reason; return }
        page.imageError = ""
        if (!picked.large) { page.map.insertImage(picked.path); return }
        largeImage.path = picked.path
        largeImage.question = picked.largeReason
        largeImage.open()
    }
    ImagePicker {
        id: photoPicker
        onPicked: url => page.insertPicked(url)
    }
    FileDialog {
        id: imagePicker
        title: qsTr("Choose image")
        nameFilters: page.map.imageFilters
        onAccepted: page.insertPicked(selectedFile)
    }
    // Why the picked files were not inserted (the desktop's "File not inserted"), until the next pick.
    property string imageError: ""

    ModalDialog {
        id: largeImage
        objectName: "largeImageDialog"
        property string path
        property string question
        title: qsTr("Large file")
        anchors.centerIn: parent
        width: Math.min(page.width - 8 * Theme.grid, 400)
        onAccepted: page.map.insertImage(path)
        Label {
            width: largeImage.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            text: largeImage.question
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton {
                text: qsTr("Cancel")
                onClicked: largeImage.reject()
            },
            DarkButton {
                text: qsTr("Insert")
                primary: true
                onClicked: largeImage.accept()
            }
        ]
    }

    // With a topic selected, the topic bar at the bottom (2026-11-11, user's request: in the place of
    // the floating bar over the topic): edit and delete, then the desktop's topic tools
    // (TopicTools, FEAT-072 F3) in their order. Wider than the page, it takes two rows. With a
    // relationship selected (FEAT-110) only Rename (its label) and Delete. The bar changes with the
    // selection (user's request 2026-11-12): with nothing selected only Select more.
    FloatingGroup {
        id: topicBar
        objectName: "topicBar"
        readonly property bool topic: page.map.selectedId !== ""
        visible: !page.editing
        maximumWidth: page.width - 2 * page.groupMargin
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: page.groupMargin
        // FEAT-111: in the multiple selection only Select more (checked), Delete, Image, Checkbox,
        // Marker and sticker and Paste style, enabled once a topic is in it. FEAT-106: with the
        // picture selected only Remove, Image (replace it) and Copy (user's request 2026-11-12);
        // FEAT-116: with a formula picked Remove, Edit the formula and Copy the LaTeX code.
        readonly property bool picture: page.map.pictureSelected && !page.map.multiSelect
        readonly property bool formula: picture && page.map.selectedFormula !== ""
        readonly property bool single: topic && !page.map.multiSelect && !picture
        readonly property bool none: !topic && !page.map.multiSelect && page.map.selectedRelationId === "" && page.map.selectedBoundaryId === ""
        readonly property bool many: page.map.multiSelect
        readonly property bool anyInSelection: many && page.map.selectedIds.length > 0
        GroupButton {
            objectName: "selectMoreButton"
            visible: (topicBar.topic && !topicBar.picture) || topicBar.none || topicBar.many
            icon.source: "qrc:/assets/hugeicons/select.svg"
            text: qsTr("Select more topics")
            checked: page.map.multiSelect
            onClicked: {
                page.endEdit()
                if (page.map.multiSelect) page.map.stopMultiSelect()
                else page.map.startMultiSelect()
            }
        }
        GroupButton {
            objectName: "renameButton"
            visible: !page.map.multiSelect && !topicBar.picture && !topicBar.none
            icon.source: "qrc:/assets/hugeicons/pen.svg"
            text: qsTr("Rename")
            onClicked: page.beginEdit(false)
        }
        GroupButton {
            objectName: "deleteButton"
            visible: !topicBar.none
            icon.source: "qrc:/assets/hugeicons/delete.svg"
            // FEAT-106: with the picture selected only the picture goes, also from the central topic.
            text: page.map.pictureSelected ? qsTr("Remove") : qsTr("Delete")
            // FEAT-111: in the mode the central topic is skipped, so it needs another topic.
            enabled: page.map.multiSelect ? page.map.canRemoveSelection : !page.map.selectedIsCentral || page.map.pictureSelected
            onClicked: page.map.removeSelected()
        }
        // As the desktop's Ctrl+C and Ctrl+V on the map (user's request 2026-11-12): the selected
        // topics' branches, then pasted under the selected topic (the last one tapped in the mode).
        GroupButton {
            objectName: "copyTopicsButton"
            visible: topicBar.single || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/copy.svg"
            text: topicBar.many ? qsTr("Copy the selected topics") : qsTr("Copy the topic")
            onClicked: page.map.copyBranches()
        }
        GroupButton {
            objectName: "pasteTopicsButton"
            visible: topicBar.single || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/paste.svg"
            text: qsTr("Paste under the topic")
            onClicked: page.map.pasteBranches()
        }
        GroupButton {
            objectName: "relationButton"
            visible: topicBar.single
            icon.source: "qrc:/assets/hugeicons/relation.svg"
            text: qsTr("Create a relationship between two topics (double tap: keep it on)")
            checked: page.map.relationTool
            // FEAT-110, as the format painter below: a tap arms it from the topic or turns it off; a
            // second tap within the double tap time keeps it on.
            property double lastTap: 0
            onClicked: {
                const now = Date.now()
                if (page.map.relationTool && !page.map.relationToolKept && now - lastTap < Qt.styleHints.mouseDoubleClickInterval)
                    page.map.startRelation(true)
                else if (page.map.relationTool) page.map.stopRelation()
                else page.map.startRelation(false)
                lastTap = now
            }
            onDoubleClicked: page.map.startRelation(true)
        }
        GroupButton {
            objectName: "boundaryButton"  // FEAT-115, the desktop's Insert > Boundary
            visible: topicBar.single || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/boundary.svg"
            text: topicBar.many ? qsTr("Add a boundary around the selected topics") : qsTr("Add a boundary around the topic")
            onClicked: { page.endEdit(); page.map.addBoundary() }
        }
        GroupButton {
            objectName: "summaryButton"  // FEAT-115 step 2, the desktop's Insert > Summary
            visible: topicBar.single || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/summary.svg"
            text: topicBar.many ? qsTr("Add a summary to the selected topics") : qsTr("Add a summary to the topic")
            // The new summary topic's title opens, as a new topic's.
            onClicked: { page.endEdit(); page.addAndName(() => page.map.addSummary()) }
        }
        GroupButton {
            objectName: "webLinkButton"
            visible: topicBar.single
            icon.source: "qrc:/assets/hugeicons/link-web.svg"
            text: qsTr("Insert a web link in the topic")
            onClicked: { page.map.stopFormatPainter(); webLinkDialog.openLink() }
        }
        GroupButton {
            objectName: "topicLinkButton"  // FEAT-114, the desktop's Insert > Link > Topic...
            visible: topicBar.single
            icon.source: "qrc:/assets/hugeicons/link.svg"
            text: qsTr("Link the topic to another topic")
            onClicked: page.openTopicLink()
        }
        GroupButton {
            objectName: "imageButton"
            visible: topicBar.single || (topicBar.picture && !topicBar.formula) || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/image.svg"
            text: topicBar.picture ? qsTr("Replace the image")
                : topicBar.many ? qsTr("Insert an image in the selected topics") : qsTr("Insert an image in the topic")
            onClicked: page.pickImage()
        }
        GroupButton {
            objectName: "copyPictureButton"
            visible: topicBar.picture && !topicBar.formula
            icon.source: "qrc:/assets/hugeicons/copy.svg"
            text: qsTr("Copy the image")
            onClicked: page.map.copyPicture()
        }
        GroupButton {
            objectName: "formulaButton"  // FEAT-116, the desktop's Insert > LaTeX formula...
            visible: topicBar.single || topicBar.formula
            icon.source: "qrc:/assets/hugeicons/formula.svg"
            text: topicBar.formula || page.map.selectedFormula !== "" ? qsTr("Edit the formula") : qsTr("Insert a LaTeX formula in the topic")
            onClicked: page.openFormula()
        }
        GroupButton {
            objectName: "copyFormulaButton"
            visible: topicBar.formula
            icon.source: "qrc:/assets/hugeicons/copy.svg"
            text: qsTr("Copy the LaTeX code")
            onClicked: page.map.copyFormulaCode()
        }
        GroupButton {
            objectName: "noteButton"
            visible: topicBar.single
            icon.source: "qrc:/assets/hugeicons/note.svg"
            text: qsTr("Add a note to the topic")
            onClicked: page.openNote()
        }
        GroupButton {
            objectName: "labelsButton"  // FEAT-114, the desktop's Insert > Tag
            visible: topicBar.single
            icon.source: "qrc:/assets/hugeicons/label.svg"
            text: qsTr("Add labels to the topic")
            onClicked: page.openLabels()
        }
        GroupButton {
            objectName: "attachFilesButton"  // FEAT-114, the desktop's Insert > File...
            visible: topicBar.single
            icon.source: "qrc:/assets/hugeicons/attachment.svg"
            text: qsTr("Attach files to the topic")
            onClicked: page.pickFiles()
        }
        GroupButton {
            objectName: "checkboxButton"
            visible: topicBar.single || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/checkbox.svg"
            text: topicBar.many ? qsTr("Add or remove the checkbox of the selected topics") : qsTr("Add or remove the topic's checkbox")
            onClicked: page.map.toggleCheckbox()
        }
        GroupButton {
            objectName: "topicStickerButton"
            visible: (topicBar.topic && !topicBar.picture) || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/sticker.svg"
            text: qsTr("Add a marker or a sticker to the topic")
            onClicked: { page.map.stopFormatPainter(); stickerPicker.openOn(true) }  // on Marker, as the desktop's panel
        }
        GroupButton {
            // FEAT-111: the copied style (Copy style, or the format painter's last start topic) on
            // every selected topic, one Undo step.
            objectName: "pasteStyleButton"
            visible: page.map.multiSelect
            icon.source: "qrc:/assets/hugeicons/format-painter.svg"
            text: qsTr("Paste the copied style on the selected topics")
            enabled: page.map.canPasteStyle && topicBar.anyInSelection
            onClicked: page.map.pasteStyle()
        }
        GroupButton {
            objectName: "formatPainterButton"
            visible: topicBar.single
            icon.source: "qrc:/assets/hugeicons/format-painter.svg"
            text: qsTr("Format painter (double tap: keep it on)")
            // Not checkable: the map's state sets it, a tap does not toggle it on its own.
            checked: page.map.formatPainter
            // A tap arms it with the topic's style, or disarms it; a second tap within the double tap
            // time keeps it on (FEAT-085). A touch double tap comes as two clicks, a mouse one as a
            // click and doubleClicked.
            property double lastTap: 0
            onClicked: {
                const now = Date.now()
                if (page.map.formatPainter && !page.map.formatPainterSticky && now - lastTap < Qt.styleHints.mouseDoubleClickInterval)
                    page.map.startFormatPainter(true)
                else if (page.map.formatPainter) page.map.stopFormatPainter()
                else page.map.startFormatPainter(false)
                lastTap = now
            }
            onDoubleClicked: page.map.startFormatPainter(true)
        }
    }

    SheetMenu {
        id: sheetMenu
        map: page.map
        onRenameRequested: renameSheetDialog.openRename()
        onDeleteRequested: deleteSheetDialog.open()
    }

    // FEAT-112: the desktop's "Rename sheet" (ModalDialog::askText), at the top of the page so the
    // on-screen keyboard does not cover it.
    ModalDialog {
        id: renameSheetDialog
        objectName: "renameSheetDialog"
        title: qsTr("Rename sheet")
        x: (page.width - width) / 2
        y: 4 * Theme.grid
        width: Math.min(page.width - 8 * Theme.grid, 400)
        function openRename() {
            sheetNameField.text = page.map.sheets[page.map.currentSheet] ?? ""
            open()
            sheetNameField.selectAll()
            sheetNameField.forceActiveFocus()
        }
        onAccepted: page.map.renameSheet(sheetNameField.text)
        onClosed: {
            Qt.inputMethod.commit()
            sheetNameField.focus = false
            Qt.inputMethod.hide()
        }
        Column {
            width: renameSheetDialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            spacing: Theme.grid
            Label {
                text: qsTr("Name")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
            // The modals' field (ui-components.md, "Modali"), as WebLinkDialog's.
            TextField {
                id: sheetNameField
                objectName: "sheetNameField"
                width: parent.width
                height: Theme.touchTarget
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                verticalAlignment: TextInput.AlignVCenter
                leftPadding: 2 * Theme.grid
                rightPadding: 2 * Theme.grid
                selectByMouse: true
                onAccepted: renameSheetDialog.accept()
                background: Item {
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: Theme.fieldHeight
                        radius: Theme.controlRadius
                        color: Theme.darkSunken
                        border.width: sheetNameField.activeFocus ? 1 : 0
                        border.color: Theme.accent
                    }
                }
            }
        }
        buttons: [
            DarkButton {
                text: qsTr("Cancel")
                onClicked: renameSheetDialog.reject()
            },
            DarkButton {
                objectName: "renameSheetConfirm"
                text: qsTr("Rename")
                primary: true
                onClicked: renameSheetDialog.accept()
            }
        ]
    }

    // FEAT-112: the desktop's confirmation; deleting a sheet cannot be undone.
    ModalDialog {
        id: deleteSheetDialog
        objectName: "deleteSheetDialog"
        title: qsTr("Delete sheet")
        anchors.centerIn: parent
        width: Math.min(page.width - 8 * Theme.grid, 400)
        onAccepted: page.map.deleteSheet()
        Label {
            width: deleteSheetDialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            text: qsTr("Delete \"%1\" and all its topics? This cannot be undone.").arg(page.map.sheets[page.map.currentSheet] ?? "")
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton {
                text: qsTr("Cancel")
                onClicked: deleteSheetDialog.reject()
            },
            DarkButton {
                objectName: "deleteSheetConfirm"
                text: qsTr("Delete")
                primary: true
                onClicked: deleteSheetDialog.accept()
            }
        ]
    }

    // The page y where the on-screen keyboard starts (page.height without one). Android reports the
    // rectangle in physical screen pixels and does not shrink the window (edge to edge), so the
    // keyboard covers the bottom of the page; a tablet with a hardware keyboard shows none.
    readonly property real keyboardTop: {
        const rect = Qt.inputMethod.keyboardRectangle
        if (!Qt.inputMethod.visible || rect.height <= 0) return page.height
        const scale = Qt.platform.os === "android" ? Screen.devicePixelRatio : 1
        return Math.min(page.height, rect.y / scale - page.mapToGlobal(0, 0).y)
    }

    // FEAT-115: the boundary's tab behind its title while it is typed, growing with the text as
    // BoundaryItem's editor; "Title" in it while empty.
    Rectangle {
        visible: page.editing && page.editingBoundary
        x: titleEditor.x - canvas.tabPadding * canvas.zoom
        y: canvas.editorAnchor.y - canvas.tabHeight * canvas.zoom / 2
        width: (titleEditor.width + 2 * canvas.tabPadding) * canvas.zoom
        height: canvas.tabHeight * canvas.zoom
        radius: 2 * canvas.zoom  // Boundaries::kTitleRadiusPx; the bottom corners squared below
        color: canvas.editorBackdrop
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: parent.radius
            color: parent.color
        }
        Text {
            visible: titleEditor.length === 0 && titleEditor.preeditText === ""
            x: canvas.tabPadding * canvas.zoom
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Title")
            font: canvas.editorFont
            color: canvas.editorColor
            opacity: 0.5
            transformOrigin: Item.Left
            scale: canvas.zoom
        }
    }

    // The title typed in the topic: the desktop's field (InlineTitleEditor) in Qt Quick, over the
    // topic in the place of its title, with its font, colour, alignment and wrap width, scaled by
    // the zoom. The scene leaves that title out meanwhile (MapCanvas.editing).
    TextEdit {
        id: titleEditor
        objectName: "titleEditor"
        visible: page.editing
        // B3: a formatted title, edited as rich text (beginEdit).
        property bool rich: false
        // Android keeps the word being composed out of text until it is committed: the topic shows
        // it already. For a rich title text is its HTML: liveText only says when to apply it.
        readonly property string liveText: preeditText === "" ? text
            : text.slice(0, cursorPosition) + preeditText + text.slice(cursorPosition)
        font: canvas.editorFont
        color: canvas.editorColor
        selectionColor: Theme.textSelection
        selectedTextColor: canvas.editorColor
        wrapMode: page.editingLabel ? TextEdit.NoWrap : TextEdit.WrapAtWordBoundaryOrAnywhere
        horizontalAlignment: canvas.editorAlignment === Qt.AlignHCenter ? TextEdit.AlignHCenter
            : canvas.editorAlignment === Qt.AlignRight ? TextEdit.AlignRight : TextEdit.AlignLeft
        // A label grows with its text (implicitWidth is the unwrapped text's).
        width: page.editingLabel ? Math.max(implicitWidth, canvas.editorWidth) : Math.max(1, canvas.editorWidth)
        transformOrigin: Item.TopLeft
        scale: canvas.zoom
        x: canvas.editorAnchor.x - width * scale * (horizontalAlignment === TextEdit.AlignHCenter ? 0.5
            : horizontalAlignment === TextEdit.AlignRight ? 1 : 0)
        y: canvas.editorAnchor.y - height * scale / 2
        EnterKey.type: Qt.EnterKeyDone
        onLiveTextChanged: if (page.editing) Qt.callLater(page.applyTitle)
        // Enter confirms, Shift+Enter starts a new line, as on the desktop; a label is one line and
        // Esc leaves it as it was (RelationshipItem's editor).
        Keys.onReturnPressed: event => {
            if ((event.modifiers & Qt.ShiftModifier) && !page.editingLabel) event.accepted = false
            else page.endEdit()
        }
        Keys.onEscapePressed: event => {
            if (!page.editingLabel) { event.accepted = false; return }
            // the label or title stays as it was
            titleEditor.text = page.editingRelation ? page.map.selectedRelationLabel : page.map.selectedBoundaryTitle
            page.endEdit()
        }
        Keys.onEnterPressed: page.endEdit()
        onActiveFocusChanged: if (!activeFocus) page.endEdit()
    }

    // Closing the keyboard (Android's Back) confirms, once it has been shown for this editing.
    Connections {
        target: Qt.inputMethod
        property bool shown: false
        function onVisibleChanged() {
            if (Qt.inputMethod.visible) shown = page.editing
            else if (shown) { shown = false; page.endEdit() }
        }
    }

    // The desktop's keys (ShortcutCatalog, as chosen in the app's settings) for the commands the
    // phone has, as MainWindow runs them; none while a title is typed or a modal is open.
    KeyCommands { id: keyCommands }
    // FEAT-111: in the multiple selection only the commands marked inMultiSelect.
    component KeyCommand: Shortcut {
        required property string command
        property bool inMultiSelect: false
        sequences: keyCommands.sequences(command)
        enabled: page.visible && !page.editing && !page.covered && (inMultiSelect || !page.map.multiSelect)
    }
    KeyCommand { command: "undo"; inMultiSelect: true; onActivated: page.map.undo() }
    KeyCommand { command: "redo"; inMultiSelect: true; onActivated: page.map.redo() }
    KeyCommand { command: "save"; inMultiSelect: true; onActivated: page.map.save() }
    KeyCommand { command: "home"; inMultiSelect: true; onActivated: page.backRequested() }
    // Android's Back (Qt::Key_Back): a key Qt quits the app on when nothing accepts it. On the map
    // it goes back to the Home as the Back button; a popup over the map takes it first and closes.
    // FEAT-111: in the multiple selection it ends the mode instead.
    Shortcut {
        sequence: "Back"
        enabled: page.visible && !page.covered
        // FEAT-114: right after jumps along topic links it returns along them first.
        // A tag's highlight ends first, as the desktop's Esc.
        onActivated: {
            if (canvas.labelHighlight !== "") canvas.clearLabelHighlight()
            else if (page.map.multiSelect) page.map.stopMultiSelect()
            else if (page.map.canFollowBack) { if (page.map.followBack()) canvas.revealSelected() }
            else page.goBack()
        }
    }
    KeyCommand { command: "fit"; inMultiSelect: true; onActivated: canvas.fit() }
    KeyCommand { command: "find"; inMultiSelect: true; onActivated: page.openSearch() }
    KeyCommand { command: "zoomIn"; inMultiSelect: true; onActivated: canvas.zoomStep(10) }
    KeyCommand { command: "zoomOut"; inMultiSelect: true; onActivated: canvas.zoomStep(-10) }
    KeyCommand { command: "actualSize"; inMultiSelect: true; onActivated: canvas.zoomStep(0) }
    KeyCommand { command: "image"; inMultiSelect: true; onActivated: page.pickImage() }
    KeyCommand { command: "note"; onActivated: if (page.map.selectedId !== "") page.openNote() }
    KeyCommand { command: "webLink"; onActivated: { page.map.stopFormatPainter(); webLinkDialog.openLink() } }
    KeyCommand { command: "formula"; onActivated: page.openFormula() }  // FEAT-116
    KeyCommand { command: "labels"; onActivated: page.openLabels() }  // FEAT-114
    KeyCommand { command: "attachFiles"; onActivated: page.pickFiles() }
    KeyCommand { command: "topicLink"; onActivated: page.openTopicLink() }
    KeyCommand { command: "addTask"; inMultiSelect: true; onActivated: page.map.toggleCheckbox() }
    KeyCommand { command: "copyStyle"; onActivated: page.map.copyStyle() }
    KeyCommand { command: "mapCopy"; inMultiSelect: true; onActivated: page.map.copyBranches() }
    KeyCommand { command: "mapPaste"; inMultiSelect: true; onActivated: page.map.pasteBranches() }
    KeyCommand { command: "pasteStyle"; inMultiSelect: true; onActivated: page.map.pasteStyle() }
    KeyCommand {
        command: "prevSheet"
        inMultiSelect: true
        onActivated: if (page.map.currentSheet > 0) page.map.switchSheet(page.map.currentSheet - 1)
    }
    KeyCommand {
        command: "nextSheet"
        inMultiSelect: true
        onActivated: if (page.map.currentSheet + 1 < page.map.sheets.length) page.map.switchSheet(page.map.currentSheet + 1)
    }
    KeyCommand { command: "toggleIconPanel"; onActivated: if (page.map.selectedId !== "") stickerPicker.openOn(false) }
    KeyCommand { command: "markers"; inMultiSelect: true; onActivated: if (page.map.selectedId !== "") stickerPicker.openOn(true) }
    KeyCommand { command: "stickers"; inMultiSelect: true; onActivated: if (page.map.selectedId !== "") stickerPicker.openOn(false) }
    KeyCommand { command: "addChild"; onActivated: if (page.map.selectedId !== "") page.addAndName(() => page.map.addChild()) }
    KeyCommand { command: "addSibling"; onActivated: if (page.map.selectedId !== "") page.addAndName(() => page.map.addSibling()) }
    KeyCommand { command: "topicBefore"; onActivated: page.map.addTopicBefore() }
    KeyCommand { command: "parentTopic"; onActivated: page.map.addParentTopic() }
    KeyCommand { command: "edit"; onActivated: page.beginEdit(false) }
    KeyCommand { command: "editAtEnd"; onActivated: page.beginEdit(true) }
    KeyCommand {
        command: "remove"
        inMultiSelect: true
        onActivated: if (page.map.multiSelect ? page.map.canRemoveSelection : !page.map.selectedIsCentral || page.map.pictureSelected) page.map.removeSelected()
    }
    KeyCommand { command: "toggleCollapse"; onActivated: if (page.map.canCollapse) page.map.toggleCollapsed() }
    KeyCommand { command: "goToCentral"; onActivated: { page.map.selectCentral(); canvas.revealSelected() } }
    KeyCommand { command: "moveUp"; onActivated: page.map.moveAmongSiblings(-1) }
    KeyCommand { command: "moveDown"; onActivated: page.map.moveAmongSiblings(1) }
    // The arrows, then the view moves just enough to show the topic, as on the desktop.
    KeyCommand { command: "mapSelectLeft"; onActivated: if (page.map.selectNeighbour(Qt.Key_Left)) canvas.revealSelected() }
    KeyCommand { command: "mapSelectRight"; onActivated: if (page.map.selectNeighbour(Qt.Key_Right)) canvas.revealSelected() }
    KeyCommand { command: "mapSelectUp"; onActivated: if (page.map.selectNeighbour(Qt.Key_Up)) canvas.revealSelected() }
    KeyCommand { command: "mapSelectDown"; onActivated: if (page.map.selectNeighbour(Qt.Key_Down)) canvas.revealSelected() }
    // Esc disarms the format painter and the Relationship tool, as on the desktop, and ends the
    // multiple selection (FEAT-111).
    Shortcut {
        sequence: "Esc"
        enabled: page.visible && (page.map.formatPainter || page.map.relationTool || page.map.multiSelect) && !page.covered
        onActivated: { page.map.stopFormatPainter(); page.map.stopRelation(); page.map.stopMultiSelect() }
    }

    // As the desktop's status bar: the note shows for 3 s.
    Timer { id: messageTimer; interval: 3000 }
    Connections {
        target: page.map
        function onMessageChanged() { if (page.map.message !== "") messageTimer.restart() }
        function onSelectionChanged() { page.imageError = "" }
    }

    // Notes under the top groups, where the bottom groups and the keyboard do not cover them: the
    // message (3 s) and the last error, on the groups' white.
    component Note: Label {
        wrapMode: Text.Wrap
        color: Theme.text
        font.pixelSize: Theme.bodySize
        padding: 2 * Theme.grid
        width: parent.width
        background: Rectangle {
            color: Theme.groupFill
            radius: Theme.surfaceRadius
            border.width: 1
            border.color: Theme.groupBorder
        }
    }
    Column {
        anchors.top: historyGroup.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 4 * Theme.grid
        anchors.rightMargin: 4 * Theme.grid
        spacing: 2 * Theme.grid
        Note {
            objectName: "messageText"
            visible: page.map.message !== "" && messageTimer.running
            text: page.map.message
        }
        Note {
            visible: page.map.lastError !== ""
            text: page.map.lastError
            color: Theme.danger
        }
        Note {
            objectName: "imageErrorText"
            visible: page.imageError !== ""
            text: page.imageError
            color: Theme.danger
        }
    }
}
