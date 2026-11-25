import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import Mentis.Mobile

// FEAT-097: the map. One finger pans, two fingers pinch to zoom, a tap selects a topic, a double
// tap on a topic edits its title, on the background fits the map. The sheet select switches sheet.
// The top right group: the map menu's arrow (Find, Present since FEAT-120, Outliner since FEAT-121, Gantt
// since FEAT-122), Undo, Redo, Fit to window (user's request 2026-11-13) and (FEAT-119) the side panel's
// button; below 600 dp Fit is the map menu's first row instead. With a topic selected, the desktop's blue "+" knobs beside and under it add a child or a
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
// FEAT-121: the map menu's Outliner shows the sheet as the desktop's outliner (OutlinerView) in place
// of the map, under the same top groups; its row then reads Mind map, which comes back, as Android's
// Back does. FEAT-122: its Gantt row the same with the Gantt (GanttChartView).
Page {
    id: page
    required property MobileMap map
    signal backRequested()

    // FEAT-002 (user 2026-11-19): with a hardware keyboard, a printable key on the selected topic,
    // relationship or boundary opens the field and replaces the whole title, as on the desktop
    // (NodeItem::keyPressEvent); Enter keeps it, Esc puts the old title back. The page holds the
    // focus between editings so the keys reach it; shortcuts are matched before this.
    // The same in the outliner (the selected row's title, written as it is typed) and in the Gantt
    // (the single selected task's Name on the side panel's Task page, opened if closed; user 2026-11-22).
    focus: true
    StackView.onActivated: page.forceActiveFocus()
    Keys.onPressed: event => {
        if (page.editing || outliner.editing || page.typingInField || page.covered || page.map.multiSelect) return
        if (page.map.selectedId === "" && page.map.selectedRelationId === "" && page.map.selectedBoundaryId === "") return
        // AltGr arrives as Ctrl+Alt on Windows keyboards (@, #, [ on the Italian layout): it still writes.
        const mods = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)
        const altGr = mods === (Qt.ControlModifier | Qt.AltModifier)
        const code = event.text.length > 0 ? event.text.charCodeAt(0) : 0
        if (code < 0x20 || code === 0x7f || (mods && !altGr)) return
        if (page.outlinerShown) { event.accepted = outliner.typeOver(event.text); return }
        if (page.ganttShown) {
            if (page.map.selectedId === "") return
            page.panelOpen = true
            event.accepted = sidePanel.item !== null && sidePanel.item.typeTaskName(event.text)
            return
        }
        page.beginEdit(false)
        if (!page.editing) return
        event.accepted = true
        titleEditor.remove(0, titleEditor.length)
        titleEditor.insert(0, event.text)
        titleEditor.cursorPosition = titleEditor.length
    }

    // True while the selected topic's title is typed in titleEditor.
    property bool editing: false
    // FEAT-121: the outliner in place of the map.
    property bool outlinerShown: false
    // FEAT-002: the Task page's Name as it is typed, in the Gantt's row and bar (empty id: done).
    function previewTaskName(id, name) { ganttView.previewName(id, name) }
    function showOutliner(on) {
        page.endEdit()
        mapMenu.close()
        page.ganttShown = false
        if (on) page.panelOpen = false  // user's request 2026-11-17: no side panel with the outliner
        page.outlinerShown = on
    }
    // FEAT-122: the Gantt in place of the map (one other view at a time, as the desktop's).
    property bool ganttShown: false
    readonly property string ganttBaseline: ganttView.comparedBaseline  // FEAT-122 D: for the Task page
    function showGantt(on) {
        page.endEdit()
        mapMenu.close()
        page.outlinerShown = false
        page.ganttShown = on
    }
    readonly property bool otherView: outlinerShown || ganttShown
    // FEAT-119: Android's medium width class (600 dp,
    // https://developer.android.com/develop/ui/compose/layouts/adaptive/use-window-size-classes): Fit
    // stays in the top right group; below it Fit is the map menu's first row.
    readonly property bool widePage: width >= 600
    // Android's expanded width class (840 dp, same source) in landscape: the views' buttons in the
    // top right group instead of the map menu (user's request 2026-11-22).
    readonly property bool viewButtons: width >= 840 && width > height
    // FEAT-119: the side panel (Style, Pitch, Map) is open.
    property bool panelOpen: false
    // FEAT-110: the field holds the selected relationship's label instead (applied when it ends),
    // FEAT-115 the selected boundary's title: one line, one step, Esc leaves it as it was.
    property bool editingRelation: false
    property bool editingBoundary: false
    readonly property bool editingLabel: editingRelation || editingBoundary
    // True while a popup (a modal, a select's list) is over the map: the map's handlers stop, as a
    // modal does not keep them from a finger dragged over its list (user's report 2026-11-10).
    // FEAT-124: a text field has the keys (the Gantt task page's name, a panel's field).
    readonly property bool typingInField: Window.activeFocusItem instanceof TextEdit || Window.activeFocusItem instanceof TextInput
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
        page.map.keepTitle()  // Esc puts it back
        // B3: a formatted title as rich text, with its formatting; any other as plain text.
        // FEAT-124: a Markdown shortcut or a format key formats a plain title's document too (the
        // field draws its formats, as the formulas' pictures): it is a formatted title from then on.
        titleEditor.rich = !page.editingLabel && page.map.selectedTitleRich
        titleEditor.formulas = !page.editingLabel && !titleEditor.rich
        titleEditor.textFormat = titleEditor.rich ? TextEdit.RichText : TextEdit.PlainText
        if (titleEditor.rich) page.map.loadTitle(titleEditor.textDocument)
        else if (titleEditor.formulas) titleFormulas.load(page.map.selectedTitle)
        else titleEditor.text = page.editingRelation ? page.map.selectedRelationLabel : page.map.selectedBoundaryTitle
        page.editing = true
        canvas.editing = true
        if (atEnd) titleEditor.cursorPosition = titleEditor.length
        else titleEditor.selectAll()
        titleEditor.forceActiveFocus()
        Qt.inputMethod.show()
        canvas.revealForEditing()
    }

    // Each change goes into the map at once, so the topic and the layout follow the text. Called
    // later: several changes in one event make one layout. FEAT-116: while typed, a formula's code
    // is laid out as text, as the field shows it (FormulaField.layoutSource); final, the title itself.
    function applyTitle(finished) {
        if (!page.editing || page.editingLabel) return  // a label is one step, written at the end
        if (titleEditor.rich) page.map.renameFrom(titleEditor.textDocument, titleEditor.preeditText, titleEditor.cursorPosition)
        else if (finished && titleEditor.formulas) page.map.rename(titleFormulas.source(titleEditor.preeditText, titleEditor.cursorPosition))
        else page.map.rename(titleEditor.liveText)
    }

    // FEAT-116: the title the topic is laid out with while typed; text is passed so that liveText
    // follows it (the field's document is read).
    function formulaTitle(text, preedit, cursor) { return titleFormulas.layoutSource(preedit, cursor) }

    // FEAT-116: the title's formulas as pictures, the one the cursor touches as code; not while a
    // word is composed or text is selected (the keyboard and the selection hold their positions).
    function refreshFormulas() {
        if (titleEditor.afterUndo) { titleEditor.afterUndo = false; return }  // until the next move or key
        if (!page.editing || !titleEditor.formulas || titleEditor.preeditText !== ""
            || titleEditor.selectionStart !== titleEditor.selectionEnd) return
        const cursor = titleFormulas.refresh(titleEditor.cursorPosition)
        if (cursor >= 0) titleEditor.cursorPosition = cursor
    }

    // FEAT-124: a Markdown shortcut or a format key changed the title field: a plain title is a
    // formatted one from then on (no formula pictures: it has none); the keyboard reads the new text.
    function titleFormatted() {
        titleEditor.rich = true
        titleEditor.formulas = false
        Qt.inputMethod.update(Qt.ImQueryInput)
        Qt.callLater(page.applyTitle, false)
    }

    // Confirms (Enter, a tap on the map, the keyboard closed, the focus lost, another command): the
    // step with the title closes.
    function goBack() { endEdit(); backRequested() }

    function endEdit() { finishEdit(false) }
    // Esc, as the desktop: the topic's title as it was when the editing started.
    function cancelEdit() { finishEdit(true) }

    function finishEdit(cancel) {
        outliner.endEdit()  // FEAT-121
        if (!page.editing) return
        Qt.inputMethod.commit()  // the word being composed belongs to the title
        if (cancel && !page.editingLabel) {
            page.map.restoreTitle()
        } else if (page.editingRelation) {
            page.map.renameRelation(titleEditor.text)  // unchanged: no step
            page.editingRelation = false
        } else if (page.editingBoundary) {
            page.map.renameBoundary(titleEditor.text)  // unchanged: no step
            page.editingBoundary = false
        } else {
            page.applyTitle(true)
        }
        page.editing = false
        canvas.editing = false
        // The next key on the selected topic types its title again; a field that took the focus keeps it.
        const hadFocus = titleEditor.activeFocus
        titleEditor.focus = false
        if (hadFocus) page.forceActiveFocus()
        Qt.inputMethod.hide()
        page.map.editingTitle = false
    }

    background: Rectangle { color: Theme.surface }

    MapCanvas {
        id: canvas
        objectName: "mapCanvas"
        anchors.fill: parent
        visible: !page.otherView  // FEAT-121, FEAT-122
        map: page.map
        // What covers the map: the top groups, the keyboard while a title is edited, otherwise the
        // bottom groups. A topic selected by a key or an edit is shown between them.
        bandTop: historyGroup.y + historyGroup.height - Theme.grid
        // FEAT-119: the side panel covers the bottom half (portrait) or the right side (landscape).
        bandBottom: Math.min(page.editing ? page.keyboardTop : Math.min(page.keyboardTop, page.bottomGroupsTop), page.panelCoverTop)
        bandRight: page.panelOpen && page.panelLandscape ? page.panelCoverLeft : 0
        onBandChanged: if (page.editing) canvas.revealForEditing()
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

        // FEAT-119 (user's report 2026-11-13: a drag inside the open side panel panned the map): the
        // map's gestures only where the panel leaves the map, so a finger on the panel never reaches
        // them, also on its labels and gaps, which take no touch. Same origin as the canvas.
        Item {
            id: gestures
            objectName: "mapGestures"
            width: page.panelOpen && page.panelLandscape ? page.panelX : canvas.width
            height: page.panelOpen && !page.panelLandscape ? page.panelY : canvas.height
            // The handlers report increments: the canvas applies each one at once.
            DragHandler {
                id: drag
                enabled: !page.covered
                target: null
                maximumPointCount: 1
                property point last
                // After a long press the finger moves the picked-up topic instead of the map; a drag
                // that starts on a corner of the selected picture resizes it, one on the picture moves it
                // (FEAT-106), one on a handle of the selected relationship bends it (FEAT-110), one on the
                // left or right border of the selected topic sets its width (FEAT-018).
                onActiveChanged: {
                    last = centroid.position
                    const at = centroid.pressPosition
                    if (active && !canvas.moving && !canvas.beginRelationBend(at.x, at.y) && !canvas.beginPictureResize(at.x, at.y)
                            && !canvas.beginPictureDrag(at.x, at.y))
                        canvas.beginTopicResize(at.x, at.y)
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
                    painted = painted || page.map.multiSelect  // a Ctrl + tap that turned the mode on
                }
                // The tap before selected what is under the finger: a topic's title or a relationship's
                // label is edited; FEAT-116: a picked formula opens its modal. FEAT-018: on the selected
                // topic's border, off its title, the topic takes its automatic width back.
                onDoubleTapped: eventPoint => {
                    if (painted || canvas.fitTopicWidthAt(eventPoint.position.x, eventPoint.position.y)) return
                    if (page.map.pictureSelected && page.map.selectedFormula !== "") page.openFormula()
                    else if (page.map.selectedId !== "" || page.map.selectedRelationId !== "" || page.map.selectedBoundaryId !== "")
                        page.beginEdit(false)
                    else canvas.fit()
                }
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
    }

    // The desktop's "+" knobs of the selected topic (TopicTools, FEAT-072 F3; user's request
    // 2026-11-11): 18 px blue circles at zoom 1 in the selection colour, the child one on the branch side, the
    // sibling one under the topic. Under the floating groups, hidden while a title is typed or a topic
    // is dragged; the canvas gives each one's place and its 48 px target beside the topic.
    component PlusKnob: AbstractButton {
        id: knob
        required property point centre
        required property rect target
        visible: !page.editing && !page.otherView && !canvas.moving && !page.map.multiSelect && target.width > 0
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
        visible: !page.editing && !page.otherView && !canvas.moving && !page.map.multiSelect && target.width > 0
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
    // FEAT-121: the outliner, over the map's place and under the floating groups.
    OutlinerView {
        id: outliner
        objectName: "outliner"
        anchors.fill: parent
        visible: page.outlinerShown
        map: page.map
        topInset: historyGroup.y + historyGroup.height
        keyboardTop: page.keyboardTop
        // FEAT-121 B: the selection's bar is hidden under the panel (portrait), beside it in landscape.
        barShown: !(page.panelOpen && !page.panelLandscape)
        barWidth: page.panelOpen && page.panelLandscape ? page.panelX : width
        onInsertRequested: kind => {
            outliner.endEdit()
            if (kind === "formula") page.openFormula()
            else if (kind === "image") page.pickImage()
            else if (kind === "note") page.openNote()
            else if (kind === "sticker") stickerPicker.openOn(false)
            else if (kind === "marker") stickerPicker.openOn(true)
            else if (kind === "file") page.pickFiles()
            else if (kind === "label") page.openLabels()
        }
        // FEAT-121 A3: the row's contents open what the map's do (MapCanvas above); FEAT-121's card
        // gives a label pill the Labels modal.
        onMarkerRequested: (marker, x, y) => {
            page.map.stopFormatPainter()
            markerMenu.openAt(marker, x, y)
        }
        onFilesRequested: (x, y) => {
            page.map.stopFormatPainter()
            filesMenu.openAt(x, y)
        }
        onWebLinkRequested: page.map.openWebLink()
        onTopicLinkRequested: page.map.followTopicLink()
        onNoteRequested: page.openNote()
        onLabelsRequested: page.openLabels()
    }
    // FEAT-122: the Gantt, over the map's place and under the floating groups.
    GanttChartView {
        id: ganttView
        objectName: "gantt"
        anchors.fill: parent
        visible: page.ganttShown
        map: page.map
        topInset: historyGroup.y + historyGroup.height
        bottomInset: page.panelOpen && !page.panelLandscape ? page.height - page.panelY : 0
        leftInset: page.panelOpen && page.panelLandscape ? page.panelWidth : 0
        onResourcesRequested: page.openResources()
    }

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
        anchors.leftMargin: page.groupMargin + (page.panelOpen && page.panelLandscape && page.ganttShown ? page.panelWidth : 0)
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
            // The landscape side panel's width too, at either side.
            width: Math.min(implicitWidth, page.width - 2 * page.groupMargin - historyGroup.width - Theme.grid
                - (page.panelOpen && page.panelLandscape ? page.panelWidth : 0) - backButton.width - addSheetButton.width - 2 * sheetGroup.sidePadding)
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

    // FEAT-119: the side panel, over the map and under the top groups.
    // FEAT-120: the Pitch page's adapter, shared with Present.
    MobilePitch {
        id: pitchAdapter
        map: page.map
    }
    PresentationView { id: presentation }
    // The panel is made the first time it opens, then kept with its state. Built with the page it
    // was half of every map opening (82 of 153 ms on the desktop, 2026-11-14), mostly never used.
    property bool panelUsed: false
    readonly property Item mapPage: page
    onPanelOpenChanged: if (panelOpen) panelUsed = true
    // SidePanel's landscape, coverTop and coverLeft, also before the panel is made.
    readonly property bool panelLandscape: page.width > page.height
    readonly property real panelCoverTop: sidePanel.item ? sidePanel.item.coverTop : page.height
    readonly property real panelCoverLeft: sidePanel.item ? sidePanel.item.coverLeft : page.width
    readonly property real panelX: sidePanel.item ? sidePanel.item.x : page.width
    readonly property real panelY: sidePanel.item ? sidePanel.item.y : page.height
    readonly property real panelWidth: sidePanel.item ? sidePanel.item.width : 0
    Loader {
        id: sidePanel
        active: page.panelOpen || page.panelUsed
        onLoaded: canvas.revealSelected()
        // In the component `page` is SidePanel's own property: the page comes as mapPage.
        sourceComponent: SidePanel {
            map: mapPage.map
            page: mapPage
            pitch: pitchAdapter
            visible: mapPage.panelOpen
            onVisibleChanged: if (visible) canvas.revealSelected()
        }
    }
    Connections {
        // The selected topic sits in the map's part the panel leaves (FEAT-119).
        target: page.map
        enabled: page.panelOpen
        function onSelectionChanged() { canvas.revealSelected() }
    }

    FloatingGroup {
        id: historyGroup
        objectName: "historyGroup"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: page.groupMargin
        // FEAT-119: beside the side panel docked at the right, so its button still closes it.
        // User's request 2026-11-15: with the Gantt the panel is at the left, beside the sheet group.
        anchors.rightMargin: page.groupMargin + (page.panelOpen && page.panelLandscape && !page.ganttShown ? page.panelWidth : 0)
        // User's request 2026-11-13: the arrow opens the map menu (Find, Outliner, Gantt, Present).
        // User's request 2026-11-23: the views always in the order Map, Outliner, Gantt, Pitch.
        GroupButton {
            id: mapMenuButton
            objectName: "mapMenuButton"
            icon.source: "qrc:/assets/hugeicons/arrow-down.svg"
            text: qsTr("More")
            onClicked: {
                page.endEdit()
                const tools = page.viewButtons ? [findTool] : [findTool, outlinerTool, ganttTool, presentTool]
                if (page.outlinerShown && outliner.narrow) tools.push(contentsTool)  // FEAT-121 D
                if (!page.widePage && !page.ganttShown) tools.unshift(fitTool)  // FEAT-119; the Gantt has no Fit
                mapMenu.toggleBelow(mapMenuButton, tools)
            }
        }
        // User's request 2026-11-22: on a large landscape page Outliner, Gantt and Present are icons
        // here, checked while their view is shown; below it rows of the map menu.
        // The desktop's other views (user's request 2026-11-13).
        GroupButton {
            // FEAT-121: the desktop's Outliner / Mind map toggle (m_outlineToggle).
            id: outlinerTool
            objectName: "outlinerButton"
            shown: page.viewButtons  // in the map menu otherwise
            icon.source: "qrc:/assets/hugeicons/outliner.svg"
            text: page.outlinerShown ? qsTr("Mind map") : qsTr("Outliner")
            menuText: text
            checked: page.viewButtons && page.outlinerShown
            onClicked: page.showOutliner(!page.outlinerShown)
        }
        GroupButton {
            id: ganttTool
            objectName: "ganttButton"
            shown: page.viewButtons  // in the map menu otherwise
            icon.source: "qrc:/assets/hugeicons/gantt.svg"
            // FEAT-122: the desktop's Gantt / Mind map toggle, as the Outliner row.
            text: page.ganttShown ? qsTr("Mind map") : qsTr("Gantt")
            menuText: text
            checked: page.viewButtons && page.ganttShown
            onClicked: page.showGantt(!page.ganttShown)
        }
        GroupButton {
            id: presentTool
            objectName: "presentButton"
            shown: page.viewButtons  // in the map menu otherwise
            icon.source: "qrc:/assets/hugeicons/present.svg"
            text: qsTr("Present")
            menuText: text
            onClicked: presentation.openFor(pitchAdapter)  // FEAT-120
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
            shown: page.widePage && !page.ganttShown  // FEAT-119: below 600 dp a row of the map menu; not with the Gantt (2026-11-15)
            icon.source: "qrc:/assets/hugeicons/fit.svg"
            text: qsTr("Fit to window")
            onClicked: { page.endEdit(); canvas.fit() }
        }
        GroupButton {
            // FEAT-119: the desktop's side panel button, the rightmost; checked while the panel is open.
            objectName: "panelButton"
            shown: !page.outlinerShown  // user's request 2026-11-17: the outliner has no side panel
            // The Gantt's panel opens at the left: the desktop's Gantt icon, the narrow part at the left.
            icon.source: page.ganttShown ? "qrc:/assets/hugeicons/panel-left.svg" : "qrc:/assets/hugeicons/panel.svg"
            text: qsTr("Show/hide side panel")
            checkable: true
            checked: page.panelOpen
            onClicked: { page.endEdit(); page.panelOpen = !page.panelOpen }
        }
    }

    // The map menu's rows: FEAT-119, Fit first on a page below 600 dp.
    GroupButton {
        id: fitTool
        objectName: "fitMenuButton"
        visible: false
        icon.source: "qrc:/assets/hugeicons/fit.svg"
        text: qsTr("Fit")
        menuText: text
        onClicked: { page.endEdit(); canvas.fit() }
    }

    // The map menu's rows: never shown as buttons, the menu reads their icon, name and state.
    GroupButton {
        // FEAT-113: the desktop's Find (Ctrl+F, also on a hardware keyboard).
        id: findTool
        objectName: "findButton"
        visible: false
        icon.source: "qrc:/assets/hugeicons/search.svg"
        text: qsTr("Find")
        menuText: text
        onClicked: page.openSearch()
    }
    GroupButton {
        // FEAT-121 D: the phone's compact outline shows every row's contents under its title.
        id: contentsTool
        objectName: "outlineContentsButton"
        visible: false
        icon.source: "qrc:/assets/hugeicons/menu-image.svg"
        text: qsTr("Show contents")
        menuText: text
        checked: outliner.outlineModel.showContents
        onClicked: outliner.outlineModel.showContents = !outliner.outlineModel.showContents
    }
    AddMenu {
        id: mapMenu
        objectName: "mapMenu"
    }
    onWidthChanged: mapMenu.close()
    onWidePageChanged: mapMenu.close()  // FEAT-119: rotating moves Fit between the group and the menu
    onViewButtonsChanged: mapMenu.close()

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
    LinkDialog {  // FEAT-117
        id: linkDialog
        map: page.map
        keyboardTop: page.keyboardTop
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
    // FEAT-122 C1: the Resources modal, from the Gantt's bottom group, its row menu and the Task page.
    function openResources() { resourcesDialog.openResources() }
    StyleChoiceMenu {
        id: resourcesMenu
        parent: Overlay.overlay
    }
    ResourcesDialog {
        id: resourcesDialog
        map: page.map
        menu: resourcesMenu
        ganttScale: ganttView.ganttScale
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
                text: qsTr("Insert")
                primary: true
                onClicked: largeFiles.accept()
            }
        ]
    }

    function openLink() {  // FEAT-117
        page.endEdit()
        page.map.stopFormatPainter()
        page.map.stopRelation()
        linkDialog.openLink()
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
                text: qsTr("Insert")
                primary: true
                onClicked: largeImage.accept()
            }
        ]
    }

    // With a topic selected, the topic bar at the bottom (2026-11-11, user's request: in the place of
    // the floating bar over the topic): edit and delete, then the desktop's topic tools
    // (TopicTools, FEAT-072 F3) in their order. With a relationship selected (FEAT-110) only Rename
    // (its label) and Delete. The bar changes with the selection (user's request 2026-11-12): with
    // nothing selected the bar is hidden (user's request 2026-11-13). FEAT-118: first the actions on the topic (Select more,
    // Rename, Delete, Copy, Paste, Format painter or Paste style), then the adding tools; when they do
    // not all fit on one row, they all go under the "+" at the end.
    FloatingGroup {
        id: topicBar
        objectName: "topicBar"
        readonly property bool topic: page.map.selectedId !== ""
        // FEAT-119 (user's request 2026-11-13): hidden while the panel covers the bottom (portrait);
        // beside the panel (landscape) it stays, centred in the map's part left of it.
        readonly property real bandWidth: page.panelOpen && page.panelLandscape ? page.panelX : page.width
        visible: !page.editing && !page.otherView && !(page.panelOpen && !page.panelLandscape) && !none
        maximumWidth: bandWidth - 2 * page.groupMargin
        overflowButton: addToolsButton
        x: Math.round((bandWidth - width) / 2)
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
            visible: (topicBar.topic && !topicBar.picture) || topicBar.many
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
        GroupButton {
            objectName: "relationButton"
            adding: true
            menuText: qsTr("Relationship")
            shown: topicBar.single
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
            adding: true
            menuText: qsTr("Boundary")
            shown: topicBar.single || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/boundary.svg"
            text: topicBar.many ? qsTr("Add a boundary around the selected topics") : qsTr("Add a boundary around the topic")
            onClicked: { page.endEdit(); page.map.addBoundary() }
        }
        GroupButton {
            objectName: "summaryButton"  // FEAT-115 step 2, the desktop's Insert > Summary
            adding: true
            menuText: qsTr("Summary")
            shown: topicBar.single || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/summary.svg"
            text: topicBar.many ? qsTr("Add a summary to the selected topics") : qsTr("Add a summary to the topic")
            // The new summary topic's title opens, as a new topic's.
            onClicked: { page.endEdit(); page.addAndName(() => page.map.addSummary()) }
        }
        GroupButton {
            objectName: "linkButton"  // FEAT-117, the desktop's Insert > Link...
            adding: true
            menuText: qsTr("Link…")
            shown: topicBar.single
            icon.source: "qrc:/assets/hugeicons/link-web.svg"
            text: qsTr("Insert a link in the topic")
            onClicked: page.openLink()
        }
        GroupButton {
            objectName: "imageButton"
            adding: !topicBar.picture
            menuText: qsTr("Image…")
            shown: topicBar.single || (topicBar.picture && !topicBar.formula) || topicBar.many
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
            adding: !topicBar.formula
            menuText: qsTr("LaTeX formula…")
            shown: topicBar.single || topicBar.formula
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
            adding: true
            menuText: qsTr("Note")
            shown: topicBar.single
            icon.source: "qrc:/assets/hugeicons/note.svg"
            text: qsTr("Add a note to the topic")
            onClicked: page.openNote()
        }
        GroupButton {
            objectName: "labelsButton"  // FEAT-114, the desktop's Insert > Tag
            adding: true
            menuText: qsTr("Labels")
            shown: topicBar.single
            icon.source: "qrc:/assets/hugeicons/label.svg"
            text: qsTr("Add labels to the topic")
            onClicked: page.openLabels()
        }
        GroupButton {
            objectName: "attachFilesButton"  // FEAT-114, the desktop's Insert > File...
            adding: true
            menuText: qsTr("File…")
            shown: topicBar.single
            icon.source: "qrc:/assets/hugeicons/attachment.svg"
            text: qsTr("Attach files to the topic")
            onClicked: page.pickFiles()
        }
        GroupButton {
            objectName: "checkboxButton"
            adding: true
            menuText: qsTr("Checkbox")
            shown: topicBar.single || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/checkbox.svg"
            text: topicBar.many ? qsTr("Add or remove the checkbox of the selected topics") : qsTr("Add or remove the topic's checkbox")
            onClicked: page.map.toggleCheckbox()
        }
        GroupButton {
            objectName: "topicStickerButton"
            adding: true
            menuText: qsTr("Marker and sticker")
            shown: (topicBar.topic && !topicBar.picture) || topicBar.many
            enabled: !topicBar.many || topicBar.anyInSelection
            icon.source: "qrc:/assets/hugeicons/sticker.svg"
            text: qsTr("Add a marker or a sticker to the topic")
            onClicked: { page.map.stopFormatPainter(); stickerPicker.openOn(true) }  // on Marker, as the desktop's panel
        }
        // FEAT-118: the adding tools that do not fit; checked while one of them is (Relationship armed).
        GroupButton {
            id: addToolsButton
            objectName: "addToolsButton"
            visible: topicBar.folded
            icon.source: "qrc:/assets/hugeicons/plus.svg"
            text: qsTr("Add to the topic")
            checked: topicBar.addingTools.some(tool => tool.checked)
            onClicked: {
                page.endEdit()
                addMenu.toggleAbove(addToolsButton, topicBar.addingTools)
            }
        }
    }

    // FEAT-118: the "+" menu closes when the selection, the layout or the title editing changes.
    AddMenu {
        id: addMenu
    }
    Connections {
        target: page.map
        function onSelectionChanged() { addMenu.close() }
    }
    Connections {
        target: topicBar
        function onFoldedChanged() { addMenu.close() }
        function onMaximumWidthChanged() { addMenu.close() }
    }
    onHeightChanged: { addMenu.close(); mapMenu.close() }
    onEditingChanged: if (editing) addMenu.close()

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
            // The modals' field (ui-components.md, "Modali"), as LinkDialog's.
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
        // FEAT-116: a plain topic title, its formulas shown as pictures (titleFormulas).
        property bool formulas: false
        property bool afterUndo: false
        // Android keeps the word being composed out of text until it is committed: the topic shows
        // it already. For a rich title text is its HTML: liveText only says when to apply it.
        readonly property string liveText: formulas ? page.formulaTitle(text, preeditText, cursorPosition)
            : preeditText === "" ? text
            : text.slice(0, cursorPosition) + preeditText + text.slice(cursorPosition)
        font: canvas.editorFont
        color: canvas.editorColor
        selectionColor: Theme.textSelection
        selectedTextColor: canvas.editorColor
        wrapMode: page.editingLabel ? TextEdit.NoWrap : TextEdit.WrapAtWordBoundaryOrAnywhere
        horizontalAlignment: canvas.editorAlignment === Qt.AlignHCenter ? TextEdit.AlignHCenter
            : canvas.editorAlignment === Qt.AlignRight ? TextEdit.AlignRight : TextEdit.AlignLeft
        // A label grows with its text (implicitWidth is the unwrapped text's); FEAT-116 so does a
        // title with formulas, up to the topic's width (MapCanvas.editorWidth).
        width: page.editingLabel ? Math.max(implicitWidth, canvas.editorWidth)
            : formulas && titleFormulas.hasMath ? Math.max(1, Math.min(titleFormulas.naturalWidth + 1, canvas.editorWidth))
            : Math.max(1, canvas.editorWidth)
        transformOrigin: Item.TopLeft
        scale: canvas.zoom
        x: canvas.editorAnchor.x - width * scale * (horizontalAlignment === TextEdit.AlignHCenter ? 0.5
            : horizontalAlignment === TextEdit.AlignRight ? 1 : 0)
        y: canvas.editorAnchor.y - height * scale / 2
        EnterKey.type: Qt.EnterKeyDone
        onLiveTextChanged: if (page.editing) Qt.callLater(page.applyTitle, false)
        onCursorPositionChanged: if (formulas) Qt.callLater(page.refreshFormulas)
        onTextChanged: if (formulas) Qt.callLater(page.refreshFormulas)
        onPreeditTextChanged: if (formulas) Qt.callLater(page.refreshFormulas)
        onSelectedTextChanged: if (formulas) Qt.callLater(page.refreshFormulas)
        // Enter confirms, Shift+Enter starts a new line, Esc leaves the text as it was, as on the
        // desktop; a label is one line (RelationshipItem's editor).
        Keys.onReturnPressed: event => {
            if ((event.modifiers & Qt.ShiftModifier) && !page.editingLabel) event.accepted = false
            else page.endEdit()
        }
        Keys.onEscapePressed: event => {
            // the topic's title, the label or the boundary's title stays as it was
            if (page.editingLabel)
                titleEditor.text = page.editingRelation ? page.map.selectedRelationLabel : page.map.selectedBoundaryTitle
            page.cancelEdit()
        }
        Keys.onEnterPressed: page.endEdit()
        // FEAT-116: a hardware keyboard's Undo and Redo pass over the formulas' picture steps.
        Keys.onPressed: event => {
            const redo = event.matches(StandardKey.Redo)
            if (!formulas || !(redo || event.matches(StandardKey.Undo))) return
            event.accepted = true
            const cursor = titleFormulas.undo(redo)
            titleEditor.afterUndo = true
            if (cursor >= 0) titleEditor.cursorPosition = cursor
        }
        onActiveFocusChanged: if (!activeFocus) page.endEdit()
    }
    // FEAT-124: the desktop's Markdown shortcuts (FEAT-068) in a topic's title.
    // FEAT-133: the braces of a formula closed while typing.
    BraceTyping {
        document: titleEditor.textDocument
        active: page.editing
    }
    MarkdownTyping {
        id: titleMarkdown
        document: titleEditor.textDocument
        active: page.editing && !page.editingLabel
        boldWeight: page.map.selectedBoldWeight
        onApplied: page.titleFormatted()
    }
    FormulaField {
        id: titleFormulas
        document: titleEditor.textDocument
        font: canvas.editorFont
        color: canvas.editorColor
        pixelRatio: Screen.devicePixelRatio * 2  // sharp up to twice the zoom, as the scene's pictures
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
        // FEAT-121, FEAT-122: in the outliner and the Gantt only the commands marked for them (the
        // map's view keys act on the map). In the outliner the topic commands act on its rows.
        property bool inOutliner: false
        property bool inGantt: false
        property bool onMap: true  // false: the outliner's or the Gantt's own keys only
        // FEAT-124: not while a text field elsewhere (the side panel's) is typed in.
        property bool notInFields: false
        sequences: keyCommands.revision >= 0 ? keyCommands.sequences(command) : []  // FEAT-125: follows Settings
        enabled: page.visible && !page.editing && !outliner.editing && !page.covered && (inMultiSelect || !page.map.multiSelect)
                 && (page.outlinerShown ? inOutliner : page.ganttShown ? inGantt : onMap)
                 && !(notInFields && page.typingInField)
    }
    KeyCommand { command: "undo"; inMultiSelect: true; inOutliner: true; inGantt: true; onActivated: page.map.undo() }
    KeyCommand { command: "redo"; inMultiSelect: true; inOutliner: true; inGantt: true; onActivated: page.map.redo() }
    KeyCommand { command: "save"; inMultiSelect: true; inOutliner: true; inGantt: true; onActivated: page.map.save() }
    KeyCommand { command: "home"; inMultiSelect: true; inOutliner: true; inGantt: true; onActivated: page.backRequested() }
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
            else if (page.panelOpen) page.panelOpen = false  // FEAT-119
            else if (page.map.multiSelect) page.map.stopMultiSelect()
            else if (page.outlinerShown) { if (!outliner.goUp()) page.showOutliner(false) }  // FEAT-121 (D: up a level first), before the map's link history
            else if (page.ganttShown) page.showGantt(false)  // FEAT-122
            else if (page.map.canFollowBack) { if (page.map.followBack()) canvas.revealSelected() }
            else page.goBack()
        }
    }
    KeyCommand { command: "fit"; inMultiSelect: true; onActivated: canvas.fit() }
    KeyCommand { command: "find"; inMultiSelect: true; inOutliner: true; inGantt: true; onActivated: page.openSearch() }
    KeyCommand { command: "zoomIn"; inMultiSelect: true; onActivated: canvas.zoomStep(10) }
    KeyCommand { command: "zoomOut"; inMultiSelect: true; onActivated: canvas.zoomStep(-10) }
    KeyCommand { command: "actualSize"; inMultiSelect: true; onActivated: canvas.zoomStep(0) }
    KeyCommand { command: "image"; inOutliner: true; inMultiSelect: true; onActivated: page.pickImage() }
    KeyCommand { command: "note"; inOutliner: true; onActivated: if (page.map.selectedId !== "") page.openNote() }
    KeyCommand { command: "link"; inOutliner: true; onActivated: page.openLink() }  // FEAT-117
    KeyCommand { command: "formula"; inOutliner: true; onActivated: page.openFormula() }  // FEAT-116
    KeyCommand { command: "labels"; inOutliner: true; onActivated: page.openLabels() }  // FEAT-114
    KeyCommand { command: "attachFiles"; inOutliner: true; onActivated: page.pickFiles() }
    KeyCommand { command: "addTask"; inOutliner: true; inMultiSelect: true; onActivated: page.map.toggleCheckbox() }
    KeyCommand { command: "copyStyle"; inOutliner: true; onActivated: page.map.copyStyle() }
    KeyCommand { command: "mapCopy"; inOutliner: true; inMultiSelect: true; onActivated: page.map.copyBranches() }
    KeyCommand { command: "mapPaste"; inOutliner: true; inMultiSelect: true; onActivated: page.map.pasteBranches() }
    KeyCommand { command: "pasteStyle"; inOutliner: true; inMultiSelect: true; onActivated: page.map.pasteStyle() }
    KeyCommand {
        command: "prevSheet"
        inMultiSelect: true
        inOutliner: true
        onActivated: if (page.map.currentSheet > 0) page.map.switchSheet(page.map.currentSheet - 1)
    }
    KeyCommand {
        command: "nextSheet"
        inMultiSelect: true
        inOutliner: true
        onActivated: if (page.map.currentSheet + 1 < page.map.sheets.length) page.map.switchSheet(page.map.currentSheet + 1)
    }
    KeyCommand { command: "toggleIconPanel"; inOutliner: true; onActivated: if (page.map.selectedId !== "") stickerPicker.openOn(false) }
    KeyCommand { command: "markers"; inOutliner: true; inMultiSelect: true; onActivated: if (page.map.selectedId !== "") stickerPicker.openOn(true) }
    KeyCommand { command: "stickers"; inOutliner: true; inMultiSelect: true; onActivated: if (page.map.selectedId !== "") stickerPicker.openOn(false) }
    // FEAT-124, the desktop's format keys (FEAT-095; style: RichTitle::CharStyle). Not typing: every
    // character of the selected topics, rows or tasks, one Undo step. Typing: the selection as the
    // format bar's button (the outliner's keyboard bar; on the map the field's selection), nothing
    // without a selection.
    component FormatKey: KeyCommand {
        required property int style
        required property int outlineFlag  // OutlineFormats::InlineFlag
        inMultiSelect: true
        inOutliner: true
        inGantt: true
        notInFields: true  // a field's own keys take them (the Gantt task page's name)
        onActivated: page.map.toggleCharStyle(style)
    }
    FormatKey { command: "bold"; style: 0; outlineFlag: 8 }
    FormatKey { command: "italic"; style: 1; outlineFlag: 1 }
    FormatKey { command: "underline"; style: 2; outlineFlag: 4 }
    FormatKey { command: "strikeOut"; style: 3; outlineFlag: 2 }
    component TypingFormatKey: Shortcut {
        required property string command
        required property int style
        required property int outlineFlag
        sequences: keyCommands.revision >= 0 ? keyCommands.sequences(command) : []  // FEAT-125: follows Settings
        enabled: page.visible && !page.covered && ((page.editing && !page.editingLabel) || outliner.editing)
        onActivated: {
            // A title holding a formula stays plain text, as on the desktop.
            if (outliner.editing) { if (!outliner.editingMath) outliner.toggleFormat(outlineFlag) }
            else if (!(titleEditor.formulas && titleFormulas.hasMath) && page.map.toggleFieldFormat(titleEditor.textDocument, titleEditor.selectionStart, titleEditor.selectionEnd, style))
                page.titleFormatted()
        }
    }
    TypingFormatKey { command: "bold"; style: 0; outlineFlag: 8 }
    TypingFormatKey { command: "italic"; style: 1; outlineFlag: 1 }
    TypingFormatKey { command: "underline"; style: 2; outlineFlag: 4 }
    TypingFormatKey { command: "strikeOut"; style: 3; outlineFlag: 2 }
    // In the outliner Enter adds the row below, Space and F2 type the selected row's title, the
    // arrows walk the rows (user 2026-11-17: the map's keys). Tab indents the selected row or task
    // under the one before, Shift+Tab outdents, as on the desktop (user 2026-11-18).
    KeyCommand { command: "addChild"; onActivated: if (page.map.selectedId !== "") page.addAndName(() => page.map.addChild()) }
    KeyCommand { command: "outlineIndent"; onMap: false; inOutliner: true; onActivated: page.map.indentSelected() }
    KeyCommand { command: "outlineOutdent"; onMap: false; inOutliner: true; onActivated: page.map.outdentSelected() }
    KeyCommand { command: "ganttIndent"; onMap: false; inGantt: true; onActivated: ganttView.indentSelected() }
    KeyCommand { command: "ganttOutdent"; onMap: false; inGantt: true; onActivated: ganttView.outdentSelected() }
    // Enter on the selected task adds the next one and opens its Name, as the desktop's table; while
    // the Name is typed Enter keeps it first (user 2026-11-23).
    KeyCommand {
        command: "ganttAddTask"
        onMap: false
        inGantt: true
        notInFields: true
        onActivated: {
            if (page.map.selectedId === "" || ganttView.addTask() === "") return
            page.panelOpen = true
            if (sidePanel.item) sidePanel.item.editTaskName()
        }
    }
    KeyCommand {
        command: "addSibling"
        inOutliner: true
        onActivated: if (page.map.selectedId !== "") { if (page.outlinerShown) outliner.addRow(); else page.addAndName(() => page.map.addSibling()) }
    }
    KeyCommand { command: "topicBefore"; inOutliner: true; onActivated: page.map.addTopicBefore() }
    KeyCommand { command: "parentTopic"; inOutliner: true; onActivated: page.map.addParentTopic() }
    KeyCommand { command: "edit"; inOutliner: true; onActivated: if (page.outlinerShown) outliner.typeSelected(false); else page.beginEdit(false) }
    KeyCommand { command: "editAtEnd"; inOutliner: true; onActivated: if (page.outlinerShown) outliner.typeSelected(true); else page.beginEdit(true) }
    KeyCommand {
        command: "remove"
        inMultiSelect: true
        inOutliner: true
        notInFields: true  // Delete and Backspace are a field's own (FEAT-125)
        onActivated: if (page.map.multiSelect ? page.map.canRemoveSelection : !page.map.selectedIsCentral || page.map.pictureSelected) page.map.removeSelected()
    }
    KeyCommand {
        command: "toggleCollapse"
        inOutliner: true  // the outline's folding, not the map's (FEAT-121)
        onActivated: if (page.outlinerShown) outliner.toggleSelectedFold(); else if (page.map.canCollapse) page.map.toggleCollapsed()
    }
    KeyCommand {
        command: "goToCentral"
        inOutliner: true
        onActivated: { page.map.selectCentral(); if (page.outlinerShown) Qt.callLater(outliner.revealSelected); else canvas.revealSelected() }
    }
    // In the outliner as its keyboard bar's Move up and Move down.
    KeyCommand { command: "moveUp"; inOutliner: true; onActivated: if (page.outlinerShown) page.map.moveInOutline(-1); else page.map.moveAmongSiblings(-1) }
    KeyCommand { command: "moveDown"; inOutliner: true; onActivated: if (page.outlinerShown) page.map.moveInOutline(1); else page.map.moveAmongSiblings(1) }
    // The arrows, then the view moves just enough to show the topic, as on the desktop.
    component ArrowCommand: KeyCommand {
        required property int key
        inOutliner: true
        onActivated: if (page.outlinerShown) outliner.selectRow(key); else if (page.map.selectNeighbour(key)) canvas.revealSelected()
    }
    ArrowCommand { command: "mapSelectLeft"; key: Qt.Key_Left }
    ArrowCommand { command: "mapSelectRight"; key: Qt.Key_Right }
    ArrowCommand { command: "mapSelectUp"; key: Qt.Key_Up }
    ArrowCommand { command: "mapSelectDown"; key: Qt.Key_Down }
    // FEAT-126: Ctrl+arrows add the topic that way to the selection, as a tap in Select more.
    component AddArrowCommand: KeyCommand {
        required property int key
        inMultiSelect: true
        onActivated: if (page.map.addNeighbour(key)) canvas.revealSelected()
    }
    AddArrowCommand { command: "mapAddLeft"; key: Qt.Key_Left }
    AddArrowCommand { command: "mapAddRight"; key: Qt.Key_Right }
    AddArrowCommand { command: "mapAddUp"; key: Qt.Key_Up }
    AddArrowCommand { command: "mapAddDown"; key: Qt.Key_Down }
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
