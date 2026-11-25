import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Shapes
import Mentis.Mobile

// FEAT-121 stage A: the desktop's outliner (FEAT-003) in place of the map. The central topic's
// title on top, then one row per topic (MobileOutline), with the desktop's measures (MainWindow::
// applyOutlineZoom at 100%: 18 px text, rows at least 40 high with 4 px of padding, 36 px per
// level, the marker 24 px wide, the title 39 px after the selection's left edge; the central
// topic's title 28 px ExtraBold, 10 px in) and colours (lightOutlineColors, darkOutlineColors with a
// dark map). The desktop's 22 px drag grip is left out: on the phone a long press drags the row,
// with the desktop's look (OutlineTreeView): the row at 50% under the finger, a line with a dot
// before or after the row under it (its top and bottom 30%), the row tinted to drop inside it.
//
// A tap on a row selects it (its highlight and ⋮); a tap on the selected row's title puts the
// cursor there and opens the keyboard; a tap on a branch's triangle folds or opens it in the
// outline (not on the map, as the desktop's tree). Typing writes the title at once, with its bold, italic,
// strikethrough, underline and highlight (MobileOutline.applyTitle; one Undo step for the whole
// typing, MobileMap.editingTitle). Enter confirms and closes the keyboard, as the desktop; Backspace
// on an empty title outdents the row. While typing, the keyboard bar holds the desktop's Outdent,
// Indent, Move up, Move down and "+" (its outliner "+" menu), and with text selected the format
// buttons (OutlineFormatBar). The selected row's ⋮ opens the desktop's context menu. Pasted in the
// main title, Markdown of two topics or more becomes the whole sheet (FEAT-055).
//
// FEAT-121 D, below 600 dp: 20 px levels with guide lines, the triangle left of the bullet, a tap on
// the bullet enters the branch (the path back over its title), compact rows (the contents as icons
// after the title, under it with Show contents or the picture's icon).
Item {
    id: view
    objectName: "outlinerView"
    required property MobileMap map
    // What covers the view: the top groups above topInset, the keyboard below keyboardTop.
    property real topInset: 0
    property real keyboardTop: height
    // FEAT-121 B: whether the multiple selection's bar may show, and the width it is centred in.
    property bool barShown: true
    property real barWidth: width
    // The topic whose title is typed ("" = none) and its field.
    property string editingId: ""
    property Item editField: null
    readonly property bool editing: editingId !== ""
    // The "+" menu's contents rows other than Text: the page opens their modals (MapPage).
    signal insertRequested(string kind)
    // FEAT-121 A3: a tap on a row's contents, as on the phone's map (the row is selected first); x and
    // y are the point under the tapped icon, in the view.
    signal markerRequested(string marker, real x, real y)
    signal filesRequested(real x, real y)
    signal webLinkRequested()
    signal topicLinkRequested()
    signal noteRequested()
    signal labelsRequested()

    readonly property real sideMargin: 4 * Theme.grid
    // FEAT-121 D: the phone's layout below Android's medium width class (600 dp, as MapPage.widePage).
    readonly property bool narrow: width < 600
    readonly property real indentation: (narrow ? 20 : 36) * zoom
    readonly property real markerWidth: (narrow ? 20 : 24) * zoom  // the bullet's place
    readonly property real firstLevel: (narrow ? 12 : 0) * zoom    // room for the first level's triangle
    readonly property bool compact: narrow && !outline.showContents
    readonly property MobileOutline outlineModel: outline
    // Android's Back in the outline: up one level from an entered branch; false when none is entered.
    function goUp() {
        if (!outline.focused) return false
        view.endEdit()
        menu.close()
        outline.focusUp()
        Qt.callLater(view.revealSelected)
        return true
    }
    // A tap on the path back: that level's branch; the selection stays (the topic left is a row there).
    function goBackTo(id) {
        view.endEdit()
        menu.close()
        outline.focusOn(id)
        Qt.callLater(view.revealSelected)
    }
    // A tap on a bullet, or on the path back: the branch of `id` as the outline (the whole sheet for
    // the central topic). Entering selects the topic, so "+" adds under it.
    function enterBranch(id) {
        view.endEdit()
        menu.close()
        outline.focusOn(id)
        view.map.selectTopic(id)
        Qt.callLater(() => list.positionViewAtBeginning())
    }
    function revealSelected() {
        const row = outline.rowOf(view.map.selectedId)
        if (row >= 0) list.positionViewAtIndex(row, ListView.Contain)
        else list.positionViewAtBeginning()
    }
    // FEAT-121 A4: the outline's zoom, 50% to 300% as the desktop's (applyOutlineZoom): every measure
    // of the rows and the main title grows with it, but the 16 px sides and the 48 px targets.
    readonly property real zoom: outline.zoomPercent / 100
    readonly property bool dark: outline.dark
    readonly property color pageColor: dark ? Theme.darkSunken : "#ffffff"
    readonly property color textColor: dark ? Theme.darkText : "#202630"
    readonly property color selectionColor: dark ? Theme.darkRaised : "#e7efff"
    readonly property color badgeColor: dark ? Qt.rgba(1, 1, 1, 30 / 255) : Qt.rgba(15 / 255, 23 / 255, 42 / 255, 15 / 255)
    readonly property color dropInsideColor: dark ? Qt.rgba(112 / 255, 71 / 255, 235 / 255, 90 / 255) : "#dce9ff"
    readonly property color dropLineColor: dark ? Theme.accent : "#8fb8f4"
    // FEAT-065 contents: the desktop's grip colour (note text, missing picture), note box, label pills.
    readonly property color mutedColor: dark ? Theme.darkTextMuted : "#7f8792"
    readonly property color noteColor: dark ? Qt.rgba(1, 1, 1, 18 / 255) : Qt.rgba(15 / 255, 23 / 255, 42 / 255, 10 / 255)
    readonly property color pillColor: dark ? Qt.rgba(1, 1, 1, 20 / 255) : Qt.rgba(1, 1, 1, 230 / 255)
    readonly property color pillBorderColor: dark ? Qt.rgba(1, 1, 1, 110 / 255) : Qt.rgba(15 / 255, 23 / 255, 42 / 255, 40 / 255)
    readonly property color pillTextColor: dark ? Qt.rgba(1, 1, 1, 220 / 255) : "#6b7280"

    MobileOutline {
        id: outline
        objectName: "outlineModel"
        map: view.map
        active: view.visible  // the rows are not read again while the map is shown
    }
    // True while a keyboard bar command moves the row being typed: its delegate may be made again
    // (a reset), and the focus it loses or the keyboard closing meanwhile do not end the typing.
    property bool relocating: false

    Rectangle {
        anchors.fill: parent
        color: view.pageColor
    }

    // The title of `id` typed in `field` from `cursor` (-1: the whole title selected, as a new row).
    function beginEdit(field, id, cursor) {
        if (view.editingId !== "" && view.editField !== field) view.endEdit(true)  // the keyboard stays
        menu.close()
        const typing = view.editField === field && field.activeFocus
        view.map.selectTopic(id)  // a picked picture is let go, as a tap elsewhere on the row
        view.map.stopFormatPainter()
        view.map.stopRelation()
        view.map.editingTitle = true
        view.editingId = id
        view.editField = field
        if (!typing) view.loadField(field, id)
        if (cursor < 0) field.selectAll()
        else field.cursorPosition = Math.min(cursor, field.length)
        field.forceActiveFocus()
        Qt.inputMethod.show()
        Qt.callLater(view.revealEditing)
    }

    // The title of `id` in `field`: with a formula, plain text with each formula a picture (FEAT-116,
    // as the map's title field); otherwise with the row's formats.
    function loadField(field, id) {
        const math = outline.mathTitle(id)
        if (math !== "") field.formulas.load(math)
        else {
            outline.loadTitle(field.textDocument, id)
            field.formulas.refresh(-1)  // no picture: nothing changes, hasMath is read again
        }
        titleMarkdown.boldWeight = outline.boldWeight(field.textDocument, id)  // the row's, as loaded
    }
    // FEAT-124: the desktop's Markdown shortcuts (FEAT-068) in the title being typed.
    // FEAT-133: the braces of a formula closed while typing.
    BraceTyping {
        document: view.editField ? view.editField.textDocument : null
        active: view.editingId !== ""
    }
    MarkdownTyping {
        id: titleMarkdown
        document: view.editField ? view.editField.textDocument : null
        active: view.editingId !== ""
        onApplied: {
            Qt.inputMethod.update(Qt.ImQueryInput)
            Qt.callLater(view.applyTitle)
            view.formatRevision++
        }
    }
    // FEAT-116: the formulas of the title being typed as pictures, the one the cursor touches as its
    // code; not while a word is composed or text is selected (the keyboard and the selection hold
    // their positions).
    function refreshFormulas() {
        if (view.afterUndo) { view.afterUndo = false; return }  // until the next move or key
        const field = view.editField
        if (!field || field.preeditText !== "" || field.selectionStart !== field.selectionEnd) return
        const cursor = field.formulas.refresh(field.cursorPosition)
        if (cursor >= 0) field.cursorPosition = cursor
    }
    readonly property bool editingMath: editField !== null && editField.formulas.hasMath
    // A hardware keyboard's Undo and Redo in a title with formulas pass over the pictures' steps
    // (FormulaField.undo, as the map's title field). True when the key was taken.
    property bool afterUndo: false
    function formulaUndo(event) {
        const redo = event.matches(StandardKey.Redo)
        if (!view.editingMath || !(redo || event.matches(StandardKey.Undo))) return false
        const cursor = view.editField.formulas.undo(redo)
        view.afterUndo = true
        if (cursor >= 0) view.editField.cursorPosition = cursor
        return true
    }

    function applyTitle() {
        if (view.editField) outline.applyTitle(view.editField.textDocument, view.editingId, view.editField.preeditText, view.editField.cursorPosition)
    }

    // Confirms: Enter, the keyboard closed, the focus lost, another command. The step closes.
    // keepKeyboard: another title is typed next (no hide and show of the keyboard in between).
    function endEdit(keepKeyboard) {
        if (view.editingId === "") return
        Qt.inputMethod.commit()  // the word being composed belongs to the title
        view.applyTitle()
        const field = view.editField
        view.editingId = ""
        view.editField = null
        if (field) {
            field.deselect()
            field.focus = false
        }
        if (keepKeyboard !== true) Qt.inputMethod.hide()
        view.map.editingTitle = false
    }

    // A keyboard bar command on the row being typed: the title's step closes, the command is its own
    // step, then the typing goes on in the same row (its delegate may be a new one after a move).
    function runOnRow(command) {
        const id = view.editingId
        const cursor = view.editField ? view.editField.cursorPosition : -1
        Qt.inputMethod.commit()
        view.applyTitle()
        view.map.editingTitle = false
        view.relocating = true
        command()
        Qt.callLater(view.resumeEdit, id, cursor)
    }
    function resumeEdit(id, cursor) {
        view.relocating = false
        const field = view.fieldOf(id)
        if (!field) { view.endEdit(); return }
        if (field === view.editField && field.activeFocus) {
            view.editingId = id  // a new central topic after a paste
            view.map.editingTitle = true
            view.loadField(field, id)
            field.cursorPosition = Math.min(cursor, field.length)
            view.revealEditing()
        } else {
            view.editingId = ""  // the old field is gone: nothing to confirm in it
            view.editField = null
            view.beginEdit(field, id, cursor)
        }
    }

    // The title field of a row (the central topic's in the header), its row shown.
    function fieldOf(id) {
        if (id === outline.headId) return list.headerItem ? list.headerItem.field : null
        const row = outline.rowOf(id)
        if (row < 0) return null
        list.positionViewAtIndex(row, ListView.Contain)
        const item = list.itemAtIndex(row)
        return item ? item.field : null
    }
    function revealEditing() {
        const row = outline.rowOf(view.editingId)
        if (row >= 0) list.positionViewAtIndex(row, ListView.Contain)
        else if (view.editingId === outline.headId) list.positionViewAtBeginning()
    }

    // FEAT-055: Android's Paste in the main title (its Ctrl+V). Markdown of two topics or more takes
    // the place of the sheet and the typing goes on in the new title; otherwise the text is pasted.
    function pasteAsMap() {
        Qt.inputMethod.commit()
        view.applyTitle()
        view.relocating = true
        if (!view.map.pasteAsMap()) { view.relocating = false; return false }
        Qt.callLater(view.resumeEdit, outline.rootId, Number.MAX_SAFE_INTEGER)
        return true
    }

    // FEAT-121 A2: the row dragged after a long press (its branch moves with it) and where it drops.
    property string dragId: ""
    property int dragRow: -1
    property int dragDepth: 0
    property string dropId: ""
    property int dropWhere: 0  // -1 before, 0 inside, 1 after the drop row
    readonly property bool dragging: dragId !== ""
    property point dragPoint: Qt.point(0, 0)  // the finger, in the view
    function beginDrag(row, x, y) {
        view.endEdit()
        menu.close()
        view.map.selectTopic(row.nodeId)
        // The list keeps its current item: the row holding the finger is not destroyed when the
        // list scrolls it out of sight.
        list.currentIndex = row.index
        view.dragId = row.nodeId
        view.dragRow = row.index
        view.dragDepth = row.depth
        ghost.grab = Qt.point(x - row.rowLeft, y - row.body.y)
        ghost.source.sourceItem = row.body
        ghost.source.sourceRect = Qt.rect(row.rowLeft, 0,
            Math.min(row.width - row.rowLeft, row.field.x - row.rowLeft + Math.min(row.field.contentWidth, row.field.width) + 32 * view.zoom), row.body.height)
        ghost.source.scheduleUpdate()
        view.followDrag(row, x, y)
    }
    function followDrag(item, x, y) {
        view.dragPoint = item.mapToItem(view, x, y)
        view.updateDrop()
    }
    // The row under the finger and the part of it (the desktop's dragMoveEvent); none on the dragged
    // branch.
    function updateDrop() {
        const p = view.mapToItem(list.contentItem, view.dragPoint.x, view.dragPoint.y)
        const target = list.itemAt(p.x, p.y)
        if (!target || target.nodeId === undefined || view.inDraggedBranch(target.index)) {
            view.dropId = ""
            return
        }
        const relative = (p.y - target.y - target.body.y) / target.body.height
        view.dropWhere = relative < 0.30 ? -1 : relative > 0.70 ? 1 : 0
        view.dropId = target.nodeId
    }
    function inDraggedBranch(index) {
        if (index < view.dragRow) return false
        for (let i = view.dragRow + 1; i <= index; ++i)
            if (outline.data(outline.index(i, 0), MobileOutline.DepthRole) <= view.dragDepth) return false
        return true
    }
    function endDrag(drop) {
        if (!view.dragging) return
        const id = view.dragId
        const target = view.dropId
        const where = view.dropWhere
        view.dragId = ""
        view.dropId = ""
        view.dragRow = -1
        ghost.source.sourceItem = null
        list.currentIndex = -1
        if (drop && target !== "") view.map.dropInOutline(id, target, where)
    }

    // The desktop's "+" > Text (addOutlineSibling): a sibling of the selected row, or a child of the
    // central topic, its title typed with the add in one step.
    // child: a child of the selected row (the map's Tab).
    function addRow(child) {
        view.endEdit()
        view.map.editingTitle = true
        if (view.map.selectedId === "") view.map.select(outline.headId)
        const added = child === true || view.map.selectedId === outline.headId ? view.map.addChild() : view.map.addSibling()
        if (!added) { view.map.editingTitle = false; return }
        const id = view.map.selectedId
        Qt.callLater(() => {
            const field = view.fieldOf(id)
            if (field) view.beginEdit(field, id, -1)
            else view.map.editingTitle = false
        })
    }

    // A hardware keyboard on the outline with the map's keys (MapPage's KeyCommand, user 2026-11-17):
    // Space types at the end of the selected row's title, F2 with the title selected.
    function typeSelected(atEnd) {
        const id = view.map.selectedId
        if (id === "" || view.map.multiSelect) return
        const field = view.fieldOf(id)
        if (field) view.beginEdit(field, id, atEnd ? Number.MAX_SAFE_INTEGER : -1)
    }
    // FEAT-002: a printable key with a hardware keyboard: the selected row's whole title becomes `text`
    // and the typing goes on (the page's Keys.onPressed). False when no single row is selected.
    function typeOver(text) {
        const id = view.map.selectedId
        if (id === "" || view.map.multiSelect) return false
        const field = view.fieldOf(id)
        if (!field) return false
        view.beginEdit(field, id, -1)
        field.remove(0, field.length)
        field.insert(0, text)
        field.cursorPosition = field.length
        return true
    }
    // The arrows walk the rows: Up and Down the row above and below (the title on top above the
    // first), Left the parent, Right the first child (a folded branch opens). With none selected, or
    // one out of the rows, the title on top.
    function selectRow(key) {
        const rowId = i => outline.data(outline.index(i, 0), MobileOutline.IdRole)
        const depthAt = i => outline.data(outline.index(i, 0), MobileOutline.DepthRole)
        const id = view.map.selectedId
        const row = outline.rowOf(id)
        const count = outline.rowCount()
        let target = ""
        if (row < 0) {
            if (id !== outline.headId) target = outline.headId
            else if ((key === Qt.Key_Down || key === Qt.Key_Right) && count > 0) target = rowId(0)
        } else if (key === Qt.Key_Up) {
            target = row > 0 ? rowId(row - 1) : outline.headId
        } else if (key === Qt.Key_Down) {
            if (row + 1 < count) target = rowId(row + 1)
        } else if (key === Qt.Key_Left) {
            target = outline.headId
            for (let i = row - 1; i >= 0; --i)
                if (depthAt(i) < depthAt(row)) { target = rowId(i); break }
        } else if (key === Qt.Key_Right) {
            if (outline.data(outline.index(row, 0), MobileOutline.FoldedRole)) outline.toggleFolded(id)
            if (row + 1 < outline.rowCount() && depthAt(row + 1) > depthAt(row)) target = rowId(row + 1)
        }
        if (target === "") return
        menu.close()
        view.map.selectTopic(target)
        view.revealSelected()
    }
    // The map's Collapse or expand: the selected branch folded or opened in the outline.
    function toggleSelectedFold() {
        const row = outline.rowOf(view.map.selectedId)
        if (row >= 0 && outline.data(outline.index(row, 0), MobileOutline.HasChildrenRole))
            outline.toggleFolded(view.map.selectedId)
    }

    ListView {
        id: list
        objectName: "outlinerList"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        // Above the keyboard and its bar while typing, so the row typed in stays in sight.
        height: view.editing ? Math.max(0, keyboardBar.y) : view.height
        topMargin: view.topInset
        bottomMargin: selectionBar.visible ? view.height - selectionBar.y + Theme.grid : 8  // the last row above the bar
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: !view.dragging && !pinch.active
        currentIndex: -1
        model: outline
        header: rootTitle
        delegate: outlineRow
        // FEAT-121 A4: two fingers zoom the outline (not the map); the point between them stays
        // about where it was (the content grows with the zoom).
        PinchHandler {
            id: pinch
            objectName: "outlinePinch"
            target: null
            enabled: !view.dragging
            property int startPercent: 100
            onActiveChanged: if (active) startPercent = outline.zoomPercent
            onActiveScaleChanged: {
                if (!active) return
                const before = outline.zoomPercent
                outline.zoomPercent = Math.round(startPercent * activeScale)
                if (outline.zoomPercent === before) return
                const centre = centroid.position.y
                const first = list.originY - list.topMargin
                const wanted = (list.contentY - first + centre) * outline.zoomPercent / before - centre + first
                list.contentY = Math.max(first, Math.min(wanted, list.originY + list.contentHeight + list.bottomMargin - list.height))
            }
        }
        // A reset (rows added, removed or moved) keeps the place the list was scrolled to.
        property real keptY: 0
        Connections {
            target: outline
            function onModelAboutToBeReset() { list.keptY = list.contentY }
            function onModelReset() {
                list.contentY = Math.max(list.originY - list.topMargin,
                                         Math.min(list.keptY, list.originY + list.contentHeight - list.height + list.bottomMargin))
            }
        }
    }

    // The dragged row at 50% under the finger, held where it was taken (the desktop's startDrag:
    // the row as far as its title and 32 px more). Its picture is taken once, as the desktop's grab.
    Item {
        id: ghost
        objectName: "outlineDragGhost"
        property point grab: Qt.point(0, 0)
        readonly property alias source: ghostSource
        visible: view.dragging
        x: view.dragPoint.x - grab.x
        y: view.dragPoint.y - grab.y
        width: ghostSource.sourceRect.width
        height: ghostSource.sourceRect.height
        opacity: 0.5
        ShaderEffectSource {
            id: ghostSource
            anchors.fill: parent
            live: false
        }
    }
    // Near the list's top or bottom edge (a touch target deep) the dragged row scrolls the list,
    // faster nearer the edge, up to 12 px a frame (a value of ours, not from a guideline).
    Timer {
        interval: 16
        repeat: true
        running: view.dragging
        onTriggered: {
            const y = view.mapToItem(list, view.dragPoint.x, view.dragPoint.y).y
            const zone = Theme.touchTarget
            const top = view.topInset + zone
            const bottom = list.height - zone
            const step = y < top ? -Math.min(1, (top - y) / zone) * 12 : y > bottom ? Math.min(1, (y - bottom) / zone) * 12 : 0
            if (step === 0) return
            const first = list.originY - list.topMargin
            const last = Math.max(first, list.originY + list.contentHeight + list.bottomMargin - list.height)
            const next = Math.max(first, Math.min(last, list.contentY + step))
            if (next === list.contentY) return
            list.contentY = next
            view.updateDrop()
        }
    }

    Component {
        id: rootTitle
        Item {
            id: header
            readonly property alias field: rootField
            readonly property bool editingRow: view.editingId === outline.headId && outline.headId !== ""
            width: list.width
            height: rootField.y + rootField.height + (8 + 1 + 8) * view.zoom  // the edit's 8 + 1 px padding, then the tree's 8
            // FEAT-121 D: the way back from an entered branch, the main title first; with more than
            // three, the main title, "…" and the last two. A 48 px target row.
            readonly property var shownCrumbs: {
                const all = outline.crumbs
                if (all.length <= 3) return all
                return [all[0], { id: "", title: "…" }, all[all.length - 2], all[all.length - 1]]
            }
            Row {
                id: crumbRow
                objectName: "outlineCrumbs"
                visible: outline.focused
                x: view.sideMargin + 10 * view.zoom
                width: list.width - x - view.sideMargin
                height: visible ? Theme.touchTarget : 0
                Repeater {
                    model: header.shownCrumbs
                    Row {
                        required property var modelData
                        required property int index
                        readonly property string crumbId: modelData.id
                        height: crumbRow.height
                        Text {
                            visible: index > 0
                            anchors.verticalCenter: parent.verticalCenter
                            text: " › "
                            font.pixelSize: 14 * view.zoom
                            color: view.mutedColor
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(implicitWidth, crumbRow.width / Math.max(1, header.shownCrumbs.length) - 16 * view.zoom)
                            text: modelData.title
                            elide: Text.ElideRight
                            font.pixelSize: 14 * view.zoom
                            color: view.mutedColor
                        }
                    }
                }
            }
            TextEdit {
                id: rootField
                objectName: "outlineRootTitle"
                x: view.sideMargin + 10 * view.zoom
                y: crumbRow.height + (crumbRow.visible ? 0 : 9 * view.zoom)
                width: list.width - x - Theme.touchTarget
                textFormat: TextEdit.RichText
                // Off while not typed: a tap reaches the header's handler, which places the cursor.
                enabled: header.editingRow
                selectByMouse: true
                activeFocusOnPress: false
                font.pixelSize: 28 * view.zoom
                // One step under the title's bold, so the formats are read back as the desktop's: an
                // entered topic under the first level has the normal weights (FEAT-121 D).
                font.weight: outline.headHeavy ? Font.ExtraBold : Font.DemiBold
                color: view.textColor
                selectionColor: Theme.textSelection
                selectedTextColor: "#202630"
                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                EnterKey.type: Qt.EnterKeyDone
                // Enter confirms and adds a row, as the desktop's main title (returnPressed).
                Keys.onReturnPressed: { view.endEdit(); view.map.select(outline.headId); view.addRow() }
                Keys.onEnterPressed: { view.endEdit(); view.map.select(outline.headId); view.addRow() }
                Keys.onPressed: event => {
                    // The whole sheet from Markdown only in the real main title.
                    if (view.formulaUndo(event) || (event.matches(StandardKey.Paste) && !outline.focused && view.pasteAsMap())) event.accepted = true
                }
                onTextChanged: if (header.editingRow) { Qt.callLater(view.applyTitle); Qt.callLater(view.refreshFormulas) }
                onPreeditTextChanged: if (header.editingRow) { Qt.callLater(view.applyTitle); Qt.callLater(view.refreshFormulas) }
                onCursorPositionChanged: if (header.editingRow) Qt.callLater(view.refreshFormulas)
                onSelectedTextChanged: if (header.editingRow) Qt.callLater(view.refreshFormulas)
                readonly property FormulaField formulas: FormulaField {
                    document: rootField.textDocument
                    font: rootField.font
                    color: rootField.color
                    pixelRatio: Screen.devicePixelRatio
                }
                onActiveFocusChanged: if (!activeFocus && header.editingRow && !view.relocating) view.endEdit()
            }
            function reload() { if (!editingRow) view.loadField(rootField, outline.headId) }
            Component.onCompleted: reload()
            onEditingRowChanged: reload()
            Connections {
                target: outline
                function onRootChanged() { header.reload() }
            }
            TapHandler {
                onTapped: eventPoint => {
                    if (view.map.multiSelect) {  // FEAT-121 B: the title above the rows ends the mode
                        view.map.stopMultiSelect()
                        return
                    }
                    if (crumbRow.visible && eventPoint.position.y < crumbRow.height) {
                        const c = crumbRow.mapFromItem(header, eventPoint.position)
                        const crumb = crumbRow.childAt(c.x, crumbRow.height / 2)
                        if (crumb && crumb.crumbId !== undefined && crumb.crumbId !== "") view.goBackTo(crumb.crumbId)
                        return
                    }
                    const p = rootField.mapFromItem(header, eventPoint.position)
                    if (header.editingRow && p.x >= 0 && p.x <= rootField.width && p.y >= 0 && p.y <= rootField.height) return
                    if (p.x >= 0 && p.x <= rootField.width && p.y >= 0 && p.y <= rootField.height)
                        view.beginEdit(rootField, outline.headId, rootField.positionAt(p.x, p.y))
                    else view.endEdit()
                }
            }
        }
    }

    Component {
        id: outlineRow
        Item {
            id: row
            required property int index
            required property string nodeId
            required property int depth
            required property string title
            required property string html
            required property bool hasChildren
            required property bool folded
            required property int hiddenCount
            required property bool floatingStart
            required property var markers
            required property int task
            required property color branchColor
            required property color tickColor
            required property bool topicLink
            required property bool webLink
            required property int files
            required property real pictureWidth
            required property real pictureHeight
            required property string pictureKey
            required property real pictureOpacity
            required property string noteKey
            required property var labels
            required property bool heavy
            required property bool expanded
            // FEAT-121 D: compact rows show their contents under the title only when asked.
            readonly property bool contentsShown: !view.compact || expanded
            // FEAT-065 (OutlineTreeView): the markers 22 px each after the bullet, then the checkbox's
            // 24, then the title; after the title the links' and the files' 24 px icons.
            readonly property real markersLeft: rowLeft + view.markerWidth
            readonly property real taskLeft: markersLeft + markers.length * 22 * view.zoom
            readonly property real titleLeft: taskLeft + ((task > 0 ? 24 : 0) + 15) * view.zoom
            readonly property var trailing: {
                const icons = []
                // FEAT-121 D: the compact rows' contents, in their order under the title.
                if (view.compact) {
                    if (pictureWidth > 0) icons.push("picture")
                    if (noteKey !== "") icons.push("note")
                    if (labels.length > 0) icons.push("labels")
                }
                if (topicLink) icons.push("topic")
                if (webLink) icons.push("web")
                if (files > 0) icons.push("files")
                return icons
            }
            readonly property real titleHeight: Math.max(40 * view.zoom, titleField.height + 2 * 4 * view.zoom)
            readonly property real textRight: titleField.x + Math.min(titleField.contentWidth, titleField.width)
            // Under the title, aligned with its text: the picture, the note box, the labels, as wide as
            // the row less 12 px (the desktop's noteBoxRect).
            readonly property real belowWidth: Math.max(1, width - 12 * view.zoom - titleLeft)
            // A picture wider than the row is shown smaller (the phone is narrower than the map's
            // picture can be); its size on the map is unchanged.
            readonly property real pictureFit: pictureWidth > 0 ? Math.min(1, belowWidth / (pictureWidth * view.zoom)) : 1
            readonly property bool pictureChosen: selected && view.map.pictureSelected
            property real resizeFactor: 1  // while a handle is dragged
            readonly property alias field: titleField
            readonly property alias body: body
            readonly property bool selected: view.map.selectedId === nodeId
            // FEAT-121 B: tinted as selected, every row of the multiple selection.
            readonly property bool inSelection: view.map.multiSelect ? view.map.selectedIds.indexOf(nodeId) >= 0 : selected
            readonly property bool editingRow: view.editingId === nodeId
            // The selection's left edge: the desktop's row left plus its 22 px grip.
            readonly property real rowLeft: view.sideMargin + view.firstLevel + depth * view.indentation
            // The drop under the dragged row: before or after opens 14 px for the line (the desktop's
            // sizeHint), inside tints the row.
            readonly property bool dropTarget: view.dropId === nodeId
            readonly property bool dropLine: dropTarget && view.dropWhere !== 0
            objectName: "outlineRow_" + nodeId
            width: list.width
            height: body.height + (dropLine ? 14 * view.zoom : 0)

            // Under the title field, which takes its own presses while typed. A tap: the triangle
            // folds, the title of the selected row types, elsewhere the row is selected. A long press lifts the row; the
            // list no longer scrolls under the finger until it is lifted.
            MouseArea {
                id: area
                anchors.fill: parent
                preventStealing: view.dragging
                property bool lifted: false
                onClicked: mouse => {
                    if (view.map.extendSelection(row.nodeId)) return  // Ctrl + tap, as the desktop's Ctrl+click
                    if (view.map.multiSelect) {  // FEAT-121 B: a tap only adds or removes the row
                        view.map.toggleInSelection(row.nodeId)
                        return
                    }
                    const p = Qt.point(mouse.x, mouse.y)
                    const inside = item => {
                        const q = item.mapFromItem(row, p)
                        return item.visible && q.x >= 0 && q.y >= 0 && q.x < item.width && q.y < item.height
                    }
                    const firstLine = p.y >= body.y && p.y < body.y + row.titleHeight
                    const plain = row.markers.length === 0 && row.task === 0
                    const bulletEnd = plain ? titleField.x - 4 : row.markersLeft
                    if (view.narrow) {
                        // FEAT-121 D: left of the bullet folds, the bullet enters the branch.
                        if (row.hasChildren && firstLine && p.x < row.rowLeft) {
                            outline.toggleFolded(row.nodeId)
                            return
                        }
                        if (firstLine && p.x >= row.rowLeft && p.x < bulletEnd) {
                            view.enterBranch(row.nodeId)
                            return
                        }
                    } else if (row.hasChildren && firstLine && p.x >= marker.x - 11 && p.x < bulletEnd) {
                        outline.toggleFolded(row.nodeId)
                        return
                    }
                    // The contents' taps, as on the phone's map: the row is selected first.
                    if (firstLine && p.x >= row.markersLeft && p.x < row.taskLeft) {
                        view.endEdit()
                        view.map.selectTopic(row.nodeId)
                        const i = Math.floor((p.x - row.markersLeft) / (22 * view.zoom))
                        const at = row.mapToItem(view, row.markersLeft + (i + 0.5) * 22 * view.zoom, body.y + (row.titleHeight + 22 * view.zoom) / 2)
                        view.markerRequested(row.markers[i], at.x, at.y)
                        return
                    }
                    if (firstLine && row.task > 0 && p.x >= row.taskLeft && p.x < row.taskLeft + 24 * view.zoom) {
                        view.endEdit()
                        view.map.selectTopic(row.nodeId)
                        view.map.toggleTaskDone()
                        return
                    }
                    if (firstLine && inside(trailingIcons)) {
                        view.endEdit()
                        view.map.selectTopic(row.nodeId)
                        const i = Math.floor(trailingIcons.mapFromItem(row, p).x / (24 * view.zoom))
                        const kind = row.trailing[i]
                        if (kind === "picture") outline.toggleContents(row.nodeId)
                        else if (kind === "note") view.noteRequested()
                        else if (kind === "labels") view.labelsRequested()
                        else if (kind === "topic") view.topicLinkRequested()
                        else if (kind === "web") view.webLinkRequested()
                        else {
                            const at = trailingIcons.mapToItem(view, (i + 0.5) * 24 * view.zoom, 24 * view.zoom)
                            view.filesRequested(at.x, at.y)
                        }
                        return
                    }
                    if (inside(picture)) {
                        view.endEdit()
                        view.map.selectPicture(row.nodeId)
                        return
                    }
                    if (inside(noteBox)) {
                        view.endEdit()
                        view.map.selectTopic(row.nodeId)
                        view.noteRequested()
                        return
                    }
                    // Only on a pill, as the desktop's labelAt.
                    if (inside(labelFlow) && labelFlow.childAt(labelFlow.mapFromItem(row, p).x, labelFlow.mapFromItem(row, p).y)) {
                        view.endEdit()
                        view.map.selectTopic(row.nodeId)
                        view.labelsRequested()
                        return
                    }
                    const t = titleField.mapFromItem(row, p)
                    const textRight = Math.min(titleField.contentWidth, titleField.width)
                    if (row.editingRow && t.x >= 0 && t.x <= titleField.width && t.y >= 0 && t.y <= titleField.height) return  // the field's own
                    // The first tap on a title selects the row (its highlight and ⋮), the next one
                    // types in it (user, 2026-11-15).
                    if (row.selected && t.x >= 0 && t.x <= textRight + 8 && t.y >= 0 && t.y <= titleField.height) {
                        view.beginEdit(titleField, row.nodeId, titleField.positionAt(t.x, t.y))
                        return
                    }
                    view.endEdit()
                    view.map.selectTopic(row.nodeId)
                }
                // In the multiple selection the rows are not dragged (FEAT-003, stage B).
                onPressAndHold: mouse => {
                    if (view.map.multiSelect) return
                    lifted = true
                    view.beginDrag(row, mouse.x, mouse.y)
                }
                onPositionChanged: mouse => { if (lifted) view.followDrag(area, mouse.x, mouse.y) }
                onReleased: mouse => {
                    if (!lifted) return
                    lifted = false
                    view.followDrag(area, mouse.x, mouse.y)
                    view.endDrag(true)
                }
                onCanceled: if (lifted) { lifted = false; view.endDrag(false) }
                Component.onDestruction: if (lifted) view.endDrag(false)
            }
            Rectangle {  // the desktop's line over the first floating topic
                visible: row.floatingStart
                x: row.rowLeft
                width: row.width - x - 4
                height: 1
                color: view.badgeColor
            }
            // FEAT-121 D: a 1 px line under each ancestor's bullet (on its child's triangle).
            Repeater {
                model: view.narrow ? row.depth : 0
                Rectangle {
                    required property int index
                    x: Math.round(view.sideMargin + view.firstLevel + index * view.indentation + view.markerWidth / 2)
                    width: 1
                    height: row.height
                    color: view.badgeColor
                }
            }
            Item {
                id: body
                y: row.dropTarget && view.dropWhere < 0 ? 14 * view.zoom : 0
                width: row.width
                height: row.titleHeight + below.height
                Rectangle {
                    visible: row.inSelection || (row.dropTarget && view.dropWhere === 0)
                    x: row.rowLeft
                    y: 2 * view.zoom
                    width: row.width - x - 4
                    height: body.height - 4 * view.zoom
                    radius: 4
                    color: row.dropTarget && view.dropWhere === 0 ? view.dropInsideColor : view.selectionColor
                }
                // The marker: a branch's triangle (down when open), a bullet otherwise (the desktop's
                // OutlineDelegate). Geometry nodes, not a Canvas texture per row.
                Item {
                    id: marker
                    x: row.rowLeft
                    width: view.markerWidth
                    height: row.titleHeight
                    Rectangle {
                        objectName: "outlineBullet"
                        visible: !row.hasChildren || view.narrow
                        anchors.centerIn: parent
                        width: 6.2 * view.zoom
                        height: 6.2 * view.zoom
                        radius: 3.1 * view.zoom
                        color: view.textColor
                    }
                    Shape {
                        objectName: "outlineTriangle"
                        visible: row.hasChildren
                        // Below 600 dp left of the bullet, in the level's place (FEAT-121 D).
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: view.narrow ? -view.indentation : 0
                        width: 10
                        height: 10
                        scale: view.zoom  // the triangle in 10 px units
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: "transparent"
                            fillColor: view.textColor
                            // Around the centre (5, 5): open (-5,-3) (5,-3) (0,4); closed (-3,-5) (-3,5) (4,0).
                            PathPolyline {
                                path: row.folded ? [Qt.point(2, 0), Qt.point(2, 10), Qt.point(9, 5)]
                                                 : [Qt.point(0, 2), Qt.point(10, 2), Qt.point(5, 9)]
                            }
                        }
                    }
                }
                // FEAT-065: the markers as on the map (16 px each in its 22 px place), then the
                // checkbox (FEAT-048: empty with the text's border, done filled in the branch colour
                // with the tick in a readable colour), as the desktop's OutlineDelegate.
                Repeater {
                    model: row.markers
                    OutlineImage {
                        required property int index
                        required property string modelData
                        x: row.markersLeft + (index * 22 + 3) * view.zoom
                        y: (row.titleHeight - height) / 2
                        width: 16 * view.zoom
                        height: 16 * view.zoom
                        marker: modelData
                    }
                }
                Item {
                    objectName: "outlineCheckbox"
                    visible: row.task > 0
                    x: row.taskLeft + 4 * view.zoom
                    y: (row.titleHeight - height) / 2
                    width: 16 * view.zoom
                    height: 16 * view.zoom
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: row.task === 2 ? 0 : 0.75 * view.zoom
                        radius: 4 * view.zoom
                        color: row.task === 2 ? row.branchColor : "transparent"
                        border.width: row.task === 2 ? 0 : 1.5 * view.zoom
                        border.color: view.textColor
                    }
                    Shape {
                        visible: row.task === 2
                        width: 16; height: 16; scale: view.zoom; transformOrigin: Item.TopLeft  // the tick in 16 px units
                        preferredRendererType: Shape.CurveRenderer
                        ShapePath {
                            strokeColor: row.tickColor
                            strokeWidth: 2
                            fillColor: "transparent"
                            capStyle: ShapePath.RoundCap
                            joinStyle: ShapePath.RoundJoin
                            PathPolyline { path: [Qt.point(3.5, 8.5), Qt.point(6.5, 12), Qt.point(12.5, 4.5)] }
                        }
                    }
                }
                TextEdit {
                    id: titleField
                    x: row.titleLeft
                    y: (row.titleHeight - height) / 2
                    // The ⋮ of the selected row has its target at the right of every row, so a title
                    // wraps the same way selected or not; the trailing icons keep their place after it.
                    width: Math.max(1, row.width - x - Theme.touchTarget - row.trailing.length * 24 * view.zoom)
                    textFormat: TextEdit.RichText
                    // Off while not typed: a tap reaches the row's handler, which places the cursor.
                    enabled: row.editingRow
                    selectByMouse: true
                    activeFocusOnPress: false
                    font.pixelSize: 18 * view.zoom
                    font.weight: row.heavy ? Font.Bold : Font.Normal
                    color: view.textColor
                    selectionColor: Theme.textSelection
                    selectedTextColor: "#202630"
                    wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                    EnterKey.type: Qt.EnterKeyDone
                    Keys.onReturnPressed: view.endEdit()
                    Keys.onEnterPressed: view.endEdit()
                    // The desktop's Backspace on an empty title: Outdent. Tab and Shift+Tab while
                    // typing: Indent and Outdent by one level, the typing goes on (the desktop's editor).
                    Keys.onPressed: event => {
                        if (view.formulaUndo(event)) { event.accepted = true; return }
                        if (event.key === Qt.Key_Backtab || (event.key === Qt.Key_Tab && (event.modifiers & Qt.ShiftModifier))) {
                            event.accepted = true
                            if (row.depth > 0) view.runOnRow(() => view.map.outdentSelected())
                            return
                        }
                        if (event.key === Qt.Key_Tab) {
                            event.accepted = true
                            view.runOnRow(() => view.map.indentSelected())
                            return
                        }
                        if (event.key === Qt.Key_Backspace && titleField.length === 0 && titleField.preeditText === ""
                            && row.depth > 0) {
                            event.accepted = true
                            view.runOnRow(() => view.map.outdentSelected())
                        }
                    }
                    onTextChanged: if (row.editingRow) { Qt.callLater(view.applyTitle); Qt.callLater(view.refreshFormulas) }
                    onPreeditTextChanged: if (row.editingRow) { Qt.callLater(view.applyTitle); Qt.callLater(view.refreshFormulas) }
                    onCursorPositionChanged: if (row.editingRow) Qt.callLater(view.refreshFormulas)
                    onSelectedTextChanged: if (row.editingRow) Qt.callLater(view.refreshFormulas)
                    // FEAT-116: the title's formulas as pictures, in the row's font and colour.
                    readonly property FormulaField formulas: FormulaField {
                        document: titleField.textDocument
                        font: titleField.font
                        color: titleField.color
                        pixelRatio: Screen.devicePixelRatio
                    }
                    onActiveFocusChanged: if (!activeFocus && row.editingRow && !view.relocating) view.endEdit()
                }
                // FEAT-065: the topic link, web link and paper clip right after the title, 16 px icons
                // in 24 px places in the text colour.
                Row {
                    id: trailingIcons
                    objectName: "outlineTrailingIcons"
                    visible: row.trailing.length > 0
                    x: row.textRight + 4 * view.zoom
                    y: (row.titleHeight - height) / 2
                    Repeater {
                        model: row.trailing
                        Item {
                            required property string modelData
                            width: 24 * view.zoom
                            height: 24 * view.zoom
                            IconImage {
                                anchors.centerIn: parent
                                width: 16 * view.zoom
                                height: 16 * view.zoom
                                sourceSize: Qt.size(Math.ceil(16 * view.zoom), Math.ceil(16 * view.zoom))
                                color: view.textColor
                                source: modelData === "topic" ? "qrc:/assets/hugeicons/link.svg"
                                      : modelData === "web" ? "qrc:/assets/hugeicons/link-web.svg"
                                      : modelData === "picture" ? "qrc:/assets/hugeicons/image.svg"
                                      : modelData === "note" ? "qrc:/assets/hugeicons/note.svg"
                                      : modelData === "labels" ? "qrc:/assets/hugeicons/label.svg" : "qrc:/assets/hugeicons/attachment.svg"
                            }
                        }
                    }
                }
                // The desktop's badge after a folded branch's title (and its icons): its hidden topics.
                Rectangle {
                    visible: row.folded && !row.editingRow
                    x: (row.trailing.length > 0 ? trailingIcons.x + trailingIcons.width - 4 * view.zoom : row.textRight) + 8 * view.zoom
                    y: (row.titleHeight - height) / 2
                    height: 20 * view.zoom
                    width: Math.max(height, badgeText.implicitWidth + 2 * 4 * view.zoom)
                    radius: height / 2
                    color: view.badgeColor
                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: row.hiddenCount
                        color: view.textColor
                        font.pixelSize: 12 * view.zoom
                        font.weight: Font.DemiBold
                    }
                }
                // FEAT-065 under the title, aligned with its text (OutlineTreeView): the picture at its
                // size on the map (4 px above it, 8 under it in all), the note's grey box (FEAT-059) and
                // 8 px, the label pills (FEAT-060) and 8 px.
                Column {
                    id: below
                    x: row.titleLeft
                    y: row.titleHeight
                    width: row.belowWidth
                    Item {
                        visible: row.pictureWidth > 0 && row.contentsShown
                        width: parent.width
                        height: picture.shownHeight + 8 * view.zoom
                        OutlineImage {
                            id: picture
                            objectName: "outlinePicture"
                            // While a handle moves the picture is scaled, not decoded again (as the
                            // map's resize); the new size is decoded on release.
                            readonly property real shownWidth: width * row.resizeFactor
                            readonly property real shownHeight: height * row.resizeFactor
                            y: 4 * view.zoom
                            width: Math.max(1, row.pictureWidth * view.zoom * row.pictureFit)
                            height: Math.max(1, row.pictureHeight * view.zoom * row.pictureFit)
                            scale: row.resizeFactor
                            transformOrigin: Item.TopLeft
                            visible: row.pictureWidth > 0 && row.contentsShown
                            opacity: row.pictureOpacity
                            map: view.map
                            nodeId: row.pictureWidth > 0 ? row.nodeId : ""  // nothing decoded without a picture
                            revision: row.pictureKey
                        }
                        // A picture whose file cannot be read: the desktop's dashed frame.
                        Shape {
                            visible: picture.visible && !picture.loaded
                            x: picture.x
                            y: picture.y
                            width: picture.width
                            height: picture.height
                            ShapePath {
                                strokeColor: view.mutedColor
                                strokeWidth: 1
                                strokeStyle: ShapePath.DashLine
                                fillColor: "transparent"
                                PathRectangle { x: 0.5; y: 0.5; width: picture.width - 1; height: picture.height - 1; radius: 4 }
                            }
                        }
                        // Picked (ui-mobile.md, "Picked picture"): a 1 px outline in the selection colour
                        // and the round handles at its corners, which resize it with its proportions.
                        Rectangle {
                            visible: row.pictureChosen
                            x: picture.x
                            y: picture.y
                            width: picture.shownWidth
                            height: picture.shownHeight
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.selection
                        }
                        Repeater {
                            model: row.pictureChosen ? 4 : 0
                            Item {
                                id: handle
                                required property int index
                                readonly property bool atRight: index === 1 || index === 2
                                readonly property bool atBottom: index >= 2
                                objectName: "outlinePictureHandle" + index
                                width: Theme.touchTarget
                                height: Theme.touchTarget
                                x: picture.x + (atRight ? picture.shownWidth : 0) - width / 2
                                y: picture.y + (atBottom ? picture.shownHeight : 0) - height / 2
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 8 * view.zoom
                                    height: 8 * view.zoom
                                    radius: 4 * view.zoom
                                    color: "white"
                                    border.width: 1.5
                                    border.color: Theme.selection
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    preventStealing: true
                                    property point start
                                    // Not over the title line, the note or the labels: they keep their taps.
                                    onPressed: mouse => {
                                        const over = item => {
                                            const q = mapToItem(item, mouse.x, mouse.y)
                                            return item.visible && q.x >= 0 && q.y >= 0 && q.x < item.width && q.y < item.height
                                        }
                                        if (mapToItem(row, mouse.x, mouse.y).y < body.y + row.titleHeight || over(noteBox) || over(labelFlow)) {
                                            mouse.accepted = false
                                            return
                                        }
                                        start = mapToItem(view, mouse.x, mouse.y)
                                        view.endEdit()
                                    }
                                    onPositionChanged: mouse => {
                                        const at = mapToItem(view, mouse.x, mouse.y)
                                        const w = row.pictureWidth * view.zoom * row.pictureFit
                                        const h = row.pictureHeight * view.zoom * row.pictureFit
                                        // Along the corner's diagonal: the proportions stay. The map's
                                        // width rounded within the handles' limits (MapCanvas::moveTo).
                                        const dx = (at.x - start.x) * (handle.atRight ? 1 : -1)
                                        const dy = (at.y - start.y) * (handle.atBottom ? 1 : -1)
                                        const wanted = row.pictureWidth * (1 + (dx / w + dy / h) / 2)
                                        const width = Math.max(view.map.minPictureWidth, Math.min(view.map.maxPictureWidth, Math.round(wanted)))
                                        row.resizeFactor = width / row.pictureWidth
                                    }
                                    onReleased: {
                                        const width = Math.round(row.pictureWidth * row.resizeFactor)
                                        row.resizeFactor = 1
                                        if (width !== Math.round(row.pictureWidth))
                                            view.map.resizePicture(Qt.size(width, Math.round(width * row.pictureHeight / row.pictureWidth)))
                                    }
                                    onCanceled: row.resizeFactor = 1
                                }
                            }
                        }
                    }
                    Item {
                        visible: row.noteKey !== "" && row.contentsShown
                        width: parent.width
                        height: noteBox.height + 8 * view.zoom
                        Rectangle {
                            id: noteBox
                            objectName: "outlineNote"
                            width: parent.width
                            height: noteField.height + 2 * 8 * view.zoom
                            radius: 4 * view.zoom
                            color: view.noteColor
                            TextEdit {
                                id: noteField
                                x: 10 * view.zoom
                                y: 8 * view.zoom
                                width: parent.width - 2 * 10 * view.zoom
                                // Shown, not typed: a tap opens the Note modal (the row's handler).
                                enabled: false
                                readOnly: true
                                textFormat: TextEdit.RichText
                                wrapMode: TextEdit.WrapAtWordBoundaryOrAnywhere
                                font.pixelSize: 14 * view.zoom
                                color: view.mutedColor
                                function reload() { if (row.noteKey !== "") outline.loadNote(noteField.textDocument, row.nodeId, view.mutedColor) }
                                Component.onCompleted: reload()
                                Connections {
                                    target: row
                                    function onNoteKeyChanged() { noteField.reload() }
                                }
                                Connections {
                                    target: view
                                    function onMutedColorChanged() { noteField.reload() }
                                }
                            }
                        }
                    }
                    Item {
                        visible: row.labels.length > 0 && row.contentsShown
                        width: parent.width
                        height: labelFlow.height + 8 * view.zoom
                        Flow {
                            id: labelFlow
                            objectName: "outlineLabels"
                            width: parent.width
                            spacing: 4 * view.zoom
                            Repeater {
                                model: row.labels
                                Rectangle {
                                    required property string modelData
                                    width: Math.min(labelFlow.width, labelText.implicitWidth + 2 * 8 * view.zoom)
                                    height: 20 * view.zoom
                                    radius: 10 * view.zoom
                                    color: view.pillColor
                                    border.width: 1
                                    border.color: view.pillBorderColor
                                    Text {
                                        id: labelText
                                        anchors.centerIn: parent
                                        width: Math.min(implicitWidth, parent.width - 2 * 8 * view.zoom)
                                        text: modelData
                                        elide: Text.ElideRight
                                        font.pixelSize: 12 * view.zoom
                                        color: view.pillTextColor
                                    }
                                }
                            }
                        }
                    }
                }
                AbstractButton {
                    id: rowMenuButton
                    objectName: "outlineRowMenu"
                    readonly property real visualWidth: Theme.groupButtonSize  // for AddMenu's placing
                    visible: row.selected && !view.editing && !view.map.multiSelect
                    anchors.right: parent.right
                    y: (row.titleHeight - height) / 2
                    width: Theme.touchTarget
                    height: Theme.touchTarget
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("More")
                    // Three 3 px dots, 3 px apart, in the secondary text colour (user, 2026-11-14: smaller
                    // and lighter than the 20 px icon), as the Home rows' ⋮.
                    contentItem: Item {
                        Column {
                            anchors.centerIn: parent
                            spacing: 3
                            Repeater {
                                model: 3
                                Rectangle {
                                    width: 3
                                    height: 3
                                    radius: 1.5
                                    color: view.dark ? Theme.darkTextMuted : Theme.textSecondary
                                    opacity: rowMenuButton.down ? 1 : 0.8
                                }
                            }
                        }
                    }
                    onClicked: {
                        view.map.selectTopic(row.nodeId)  // the menu acts on the topic
                        view.menuAnchor = rowMenuButton
                        menu.toggleBelow(rowMenuButton, view.rowTools())
                    }
                }
            }
            function reload() { if (!editingRow) view.loadField(titleField, nodeId) }
            Component.onCompleted: reload()
            onTitleChanged: reload()
            onHtmlChanged: reload()
            onEditingRowChanged: reload()
            // The desktop's drop line before or after the row: 4 px, radius 2, a dot of 8 at its left.
            Item {
                visible: row.dropLine
                y: view.dropWhere < 0 ? 6 * view.zoom : row.height - 6 * view.zoom
                Rectangle {
                    x: row.rowLeft
                    y: -2 * view.zoom
                    width: Math.max(20 * view.zoom, row.width - row.rowLeft - 8 * view.zoom)
                    height: 4 * view.zoom
                    radius: 2 * view.zoom
                    color: view.dropLineColor
                }
                Rectangle {
                    x: row.rowLeft - 4 * view.zoom
                    y: -4 * view.zoom
                    width: 8 * view.zoom
                    height: 8 * view.zoom
                    radius: 4 * view.zoom
                    color: view.dropLineColor
                }
            }
        }
    }

    // The keyboard bar: on the keyboard's top edge while a title is typed (16 px from the edges).
    readonly property bool hasSelection: editField !== null && editField.selectionStart !== editField.selectionEnd
    property int formatRevision: 0
    readonly property int formatState: {
        formatRevision  // read again after a button
        if (!hasSelection) return 0
        return outline.formatState(editField.textDocument, editingId, editField.selectionStart, editField.selectionEnd)
    }
    function toggleFormat(flag) {
        if (!view.hasSelection) return
        outline.applyFormat(view.editField.textDocument, view.editingId, view.editField.selectionStart,
                            view.editField.selectionEnd, flag, (view.formatState & flag) === 0)
        view.applyTitle()
        view.formatRevision++
    }
    function rowFlag(role) {
        const row = outline.rowOf(view.editingId)
        return row >= 0 && outline.data(outline.index(row, 0), role) === true
    }
    property int rowRevision: 0
    Connections {
        target: outline
        function onDataChanged() { view.rowRevision++ }
        function onModelReset() { view.rowRevision++ }
    }
    readonly property bool canIndent: { rowRevision; editingId; return rowFlag(MobileOutline.CanIndentRole) }
    readonly property bool canOutdent: { rowRevision; editingId; return rowFlag(MobileOutline.CanOutdentRole) }
    readonly property bool canMoveUp: { rowRevision; editingId; return rowFlag(MobileOutline.CanMoveUpRole) }
    readonly property bool canMoveDown: { rowRevision; editingId; return rowFlag(MobileOutline.CanMoveDownRole) }

    FloatingGroup {
        id: keyboardBar
        objectName: "outlineKeyboardBar"
        visible: view.editing
        maximumWidth: view.width - 2 * 3 * Theme.grid
        anchors.horizontalCenter: parent.horizontalCenter
        y: view.keyboardTop - height - Theme.grid
        GroupButton {
            objectName: "outlineOutdentButton"
            icon.source: "qrc:/assets/hugeicons/indent-left.svg"
            text: qsTr("Outdent")
            enabled: view.canOutdent
            onClicked: view.runOnRow(() => view.map.outdentSelected())
        }
        GroupButton {
            objectName: "outlineIndentButton"
            icon.source: "qrc:/assets/hugeicons/indent-right.svg"
            text: qsTr("Indent")
            enabled: view.canIndent
            onClicked: view.runOnRow(() => view.map.indentSelected())
        }
        GroupButton {
            objectName: "outlineMoveUpButton"
            icon.source: "qrc:/assets/hugeicons/arrow-up.svg"
            text: qsTr("Move up")
            enabled: view.canMoveUp
            onClicked: view.runOnRow(() => view.map.moveInOutline(-1))
        }
        GroupButton {
            objectName: "outlineMoveDownButton"
            icon.source: "qrc:/assets/hugeicons/arrow-down.svg"
            text: qsTr("Move down")
            enabled: view.canMoveDown
            onClicked: view.runOnRow(() => view.map.moveInOutline(1))
        }
        GroupButton {
            id: plusButton
            objectName: "outlinePlusButton"
            icon.source: "qrc:/assets/hugeicons/plus.svg"
            text: qsTr("Add content to the branch")
            // The typing goes on while the menu is open; a row of it ends the typing.
            onClicked: menu.toggleAbove(plusButton, view.insertTools())
        }
        FormatButton { objectName: "outlineBoldButton"; flag: 8; icon.source: "qrc:/assets/hugeicons/text-bold.svg"; text: qsTr("Bold") }
        FormatButton { objectName: "outlineItalicButton"; flag: 1; icon.source: "qrc:/assets/hugeicons/text-italic.svg"; text: qsTr("Italic") }
        FormatButton { objectName: "outlineStrikeButton"; flag: 2; icon.source: "qrc:/assets/hugeicons/text-strikethrough.svg"; text: qsTr("Strikethrough") }
        FormatButton { objectName: "outlineUnderlineButton"; flag: 4; icon.source: "qrc:/assets/hugeicons/text-underline.svg"; text: qsTr("Underline") }
        FormatButton { objectName: "outlineHighlightButton"; flag: 16; icon.source: "qrc:/assets/hugeicons/text-highlight.svg"; text: qsTr("Highlight") }
    }

    // Closing the keyboard (Android's Back) confirms, once it has been shown for this typing.
    Connections {
        target: Qt.inputMethod
        property bool shown: false
        function onVisibleChanged() {
            if (Qt.inputMethod.visible) shown = view.editing
            else if (shown && !view.relocating) { shown = false; view.endEdit() }
        }
    }
    onKeyboardTopChanged: if (view.editing) Qt.callLater(view.revealEditing)

    AddMenu {
        id: menu
        objectName: "outlineMenu"
    }

    component FormatButton: GroupButton {
        required property int flag
        shown: view.hasSelection && !view.editingMath  // a title with formulas stays plain text
        checked: (view.formatState & flag) !== 0
        onClicked: view.toggleFormat(flag)
    }
    // The menus' rows: never shown as buttons, the menu reads their icon, name and state.
    component MenuRow: GroupButton {
        visible: false
        menuText: text
    }
    // The desktop's outliner "+" menu (outlineAddMenu).
    MenuRow { id: addText; objectName: "outlineAddText"; icon.source: "qrc:/assets/hugeicons/menu-text.svg"; text: qsTr("Text"); onClicked: view.addRow() }
    MenuRow { id: addFormula; objectName: "outlineAddFormula"; icon.source: "qrc:/assets/hugeicons/menu-formula.svg"; text: qsTr("LaTeX formula…"); onClicked: view.insertRequested("formula") }
    MenuRow { id: addImage; objectName: "outlineAddImage"; icon.source: "qrc:/assets/hugeicons/menu-image.svg"; text: qsTr("Image…"); onClicked: view.insertRequested("image") }
    MenuRow { id: addNote; objectName: "outlineAddNote"; icon.source: "qrc:/assets/hugeicons/menu-note.svg"; text: qsTr("Note"); onClicked: view.insertRequested("note") }
    MenuRow { id: addSticker; objectName: "outlineAddSticker"; icon.source: "qrc:/assets/hugeicons/menu-sticker.svg"; text: qsTr("Sticker…"); onClicked: view.insertRequested("sticker") }
    MenuRow { id: addMarker; objectName: "outlineAddMarker"; icon.source: "qrc:/assets/hugeicons/menu-marker.svg"; text: qsTr("Marker"); onClicked: view.insertRequested("marker") }
    MenuRow { id: addFile; objectName: "outlineAddFile"; icon.source: "qrc:/assets/hugeicons/menu-attachment.svg"; text: qsTr("File…"); onClicked: view.insertRequested("file") }
    MenuRow { id: addLabel; objectName: "outlineAddLabel"; icon.source: "qrc:/assets/hugeicons/menu-label.svg"; text: qsTr("Label"); onClicked: view.insertRequested("label") }
    MenuRow { id: addCheckbox; objectName: "outlineAddCheckbox"; icon.source: "qrc:/assets/hugeicons/menu-checkbox.svg"; text: qsTr("Checkbox"); onClicked: { view.endEdit(); view.map.toggleCheckbox() } }
    function insertTools() {
        // The contents go on a selected topic (the desktop's outlineSelectedNode).
        const topic = view.map.selectedId !== ""
        for (const tool of [addFormula, addImage, addNote, addSticker, addMarker, addFile, addLabel, addCheckbox]) tool.enabled = topic
        return [addText, addFormula, addImage, addNote, addSticker, addMarker, addFile, addLabel, addCheckbox]
    }

    // FEAT-121 B: the multiple selection's bar at the bottom (the Gantt's row bar place): Select more
    // (checked: a tap ends the mode), Delete, Copy, Cut, each on every selected row in one step.
    readonly property real groupMargin: 3 * Theme.grid  // 16 px less the group's 4 px touch margin
    FloatingGroup {
        id: selectionBar
        objectName: "outlineSelectionBar"
        visible: view.map.multiSelect && view.barShown && !view.editing
        maximumWidth: view.barWidth - 2 * view.groupMargin
        x: Math.round((view.barWidth - width) / 2)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: view.groupMargin
        GroupButton {
            objectName: "outlineSelectMoreButton"
            icon.source: "qrc:/assets/hugeicons/select.svg"
            text: qsTr("Select more topics")
            checked: true
            onClicked: view.map.stopMultiSelect()
        }
        GroupButton {
            objectName: "outlineSelectionDelete"
            icon.source: "qrc:/assets/hugeicons/delete.svg"
            text: qsTr("Delete")
            enabled: view.map.canRemoveSelection
            onClicked: view.map.removeSelected()
        }
        GroupButton {
            objectName: "outlineSelectionCopy"
            icon.source: "qrc:/assets/hugeicons/copy.svg"
            text: qsTr("Copy the selected topics")
            enabled: view.map.selectedIds.length > 0
            onClicked: view.map.copyBranches()
        }
        GroupButton {
            objectName: "outlineSelectionCut"
            icon.source: "qrc:/assets/hugeicons/cut.svg"
            text: qsTr("Cut the selected topics")
            enabled: view.map.canRemoveSelection
            onClicked: view.map.cutBranches()
        }
    }

    // The desktop's context menu of a row (showOutlineContextMenu), Insert first; FEAT-121 B: Select
    // more before it.
    MenuRow {
        id: rowSelectMore
        objectName: "outlineRowSelectMore"
        text: qsTr("Select more")
        onClicked: { view.endEdit(); view.map.startMultiSelect() }
    }
    MenuRow { id: rowInsert; objectName: "outlineRowInsert"; text: qsTr("Insert"); onClicked: Qt.callLater(() => menu.toggleBelow(view.menuAnchor, view.insertTools())) }
    MenuRow { id: rowCopy; objectName: "outlineRowCopy"; text: qsTr("Copy"); onClicked: view.map.copyBranches() }
    MenuRow { id: rowCut; objectName: "outlineRowCut"; text: qsTr("Cut"); onClicked: view.map.cutBranches() }
    MenuRow { id: rowPaste; objectName: "outlineRowPaste"; text: qsTr("Paste"); onClicked: view.map.pasteBranches() }
    MenuRow { id: rowDuplicate; objectName: "outlineRowDuplicate"; text: qsTr("Duplicate"); onClicked: view.map.duplicateSelected() }
    MenuRow { id: rowIndent; objectName: "outlineRowIndent"; text: qsTr("Indent"); onClicked: view.map.indentSelected() }
    MenuRow { id: rowOutdent; objectName: "outlineRowOutdent"; text: qsTr("Outdent"); onClicked: view.map.outdentSelected() }
    MenuRow { id: rowDelete; objectName: "outlineRowDelete"; text: qsTr("Delete"); onClicked: view.map.removeSelected() }
    MenuRow {
        id: rowClear
        objectName: "outlineRowClear"
        text: qsTr("Clear text")
        // As the desktop: the field opens empty, ready for the new text (Undo gives the title back).
        onClicked: {
            const id = view.map.selectedId
            Qt.callLater(() => {
                const field = view.fieldOf(id)
                if (!field) return
                view.beginEdit(field, id, 0)
                field.clear()
            })
        }
    }
    MenuRow { id: rowCopyStyle; objectName: "outlineRowCopyStyle"; text: qsTr("Copy style"); onClicked: view.map.copyStyle() }
    MenuRow { id: rowPasteStyle; objectName: "outlineRowPasteStyle"; text: qsTr("Paste style"); onClicked: view.map.pasteStyle() }
    property Item menuAnchor: null
    function rowTools() {
        const row = outline.rowOf(view.map.selectedId)
        const flag = role => row >= 0 && outline.data(outline.index(row, 0), role) === true
        rowIndent.enabled = flag(MobileOutline.CanIndentRole)
        rowOutdent.enabled = flag(MobileOutline.CanOutdentRole)
        rowPasteStyle.enabled = view.map.canPasteStyle
        return [rowSelectMore, rowInsert, rowCopy, rowCut, rowPaste, rowDuplicate, rowIndent, rowOutdent, rowDelete, rowClear, rowCopyStyle, rowPasteStyle]
    }
}
