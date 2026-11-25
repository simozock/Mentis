import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-122 stage A: the desktop's Gantt (FEAT-075) in place of the map. A frozen Name column (40%
// of the width, user 2026-11-15) with the summaries bold and their fold arrow, the shown columns
// beside it (stage F: a tap on the header opens the Columns modal), the chart beside them
// as the desktop draws it (MobileGantt, the core's GanttCanvas): the 48 px two-line header, weekends
// shaded, today's line, bars, milestones, summaries, names on bars, arrows, the critical path; light
// or dark with the map. A drag scrolls: up and down both parts, sideways the chart only; two fingers
// zoom the days 50-300 % (the date between them kept). A tap on a row or a bar selects its topic
// (the map's and the outline's selection), on a summary's arrow folds or opens it (view state).
// The bottom group picks the time scale (Days, Weeks, Months), goes to today, shows the critical
// path or not. An empty plan shows "No tasks yet" and Add task, as the desktop's.
Item {
    id: view
    objectName: "ganttView"
    required property MobileMap map
    // What covers the view: the top groups above topInset.
    property real topInset: 0
    // FEAT-122 B2: the side panel docked at the bottom (portrait) or the left (landscape, as the
    // desktop's task panel; user's request 2026-11-15): the chart keeps the part it leaves; the bottom
    // groups hide under the bottom panel, as the map's topic bar.
    property real bottomInset: 0
    property real leftInset: 0
    signal resourcesRequested()  // FEAT-122 C1: the Resources modal (the page's)
    // FEAT-122 D: the snapshot compared, for the side panel's Task page (the page passes it on).
    readonly property string comparedBaseline: gantt.comparedBaseline
    readonly property int ganttScale: gantt.scale  // FEAT-127: the Resources modal's load

    Rectangle {
        anchors.fill: parent
        color: gantt.dark ? Theme.darkSurface : "#ffffff"
    }

    MobileGantt {
        id: gantt
        objectName: "ganttChart"
        anchors.fill: parent
        anchors.topMargin: view.topInset
        anchors.bottomMargin: view.bottomInset
        anchors.leftMargin: view.leftInset
        visible: !gantt.empty
        map: view.map
        active: view.visible  // the plan is not read again while the map is shown
        contentX: flick.contentX
        contentY: flick.contentY
        onScrollRequested: x => flick.contentX = x
        onScrollTopRequested: flick.contentY = 0
        onHeaderTapped: columnsDialog.open()  // FEAT-122 F: the table's header
    }

    Flickable {
        id: flick
        objectName: "ganttFlick"
        anchors.fill: gantt
        visible: !gantt.empty
        contentWidth: Math.max(width, gantt.contentWidth)
        // The bottom group's room under the last row, so it never covers it.
        contentHeight: Math.max(height, gantt.contentHeight + bottomGroup.height + 2 * Theme.grid)
        boundsBehavior: Flickable.StopAtBounds
        interactive: !pinch.active && !handles.dragging && !rowLift.lifted
        // A drag begun on the frozen table scrolls only the rows: the days stay still.
        property bool tableDrag: false
        flickableDirection: tableDrag ? Flickable.VerticalFlick : Flickable.AutoFlickDirection
        clip: true
        // Stage B3: a press on a handle of the selected bar (link circles, progress badge, end grip,
        // the bar itself) drags it, and the Flickable does not scroll; so does the second press of a
        // double tap in the row of a task without a date (its start, and its duration while the finger
        // moves sideways). Anywhere else the press is refused and the drag scrolls, the tap selects.
        MouseArea {
            id: handles
            objectName: "ganttHandles"
            width: flick.contentWidth
            height: flick.contentHeight
            enabled: !view.map.multiSelect
            preventStealing: true
            property bool dragging: false
            onPressed: mouse => {
                handles.dragging = gantt.pressAt(mouse.x - flick.contentX, mouse.y - flick.contentY)
                mouse.accepted = handles.dragging
            }
            onPositionChanged: mouse => { if (handles.dragging) gantt.dragTo(mouse.x - flick.contentX, mouse.y - flick.contentY) }
            onReleased: mouse => {
                if (handles.dragging) gantt.releaseAt(mouse.x - flick.contentX, mouse.y - flick.contentY)
                handles.dragging = false
            }
            onCanceled: {
                gantt.cancelGesture()
                handles.dragging = false
            }
        }
        // A long press on the frozen table lifts the row (the desktop's drag by the name): the finger
        // then drops it before, after or inside another row. Before the long press a drag still
        // scrolls (the Flickable takes it, only up and down) and a tap selects: the area takes the
        // table's presses, so it does what the handlers below do on the chart.
        MouseArea {
            id: rowLift
            objectName: "ganttRowLift"
            x: flick.contentX
            width: gantt.tableWidth
            height: flick.contentHeight
            enabled: !view.map.multiSelect
            preventStealing: lifted
            property bool lifted: false
            property real fingerY: 0  // in the view, for the scrolling near the edges
            onPressed: flick.tableDrag = true
            onClicked: mouse => gantt.tapAt(mouse.x, mouse.y - flick.contentY)
            onPressAndHold: mouse => {
                rowLift.lifted = gantt.liftRow(mouse.x, mouse.y - flick.contentY)
                rowLift.fingerY = mouse.y - flick.contentY
            }
            onPositionChanged: mouse => {
                if (!rowLift.lifted) return
                rowLift.fingerY = mouse.y - flick.contentY
                gantt.dragRowTo(mouse.x, rowLift.fingerY)
            }
            onReleased: mouse => {
                if (!rowLift.lifted) return
                rowLift.lifted = false
                gantt.dragRowTo(mouse.x, mouse.y - flick.contentY)
                gantt.dropRow()
            }
            onCanceled: if (rowLift.lifted) { rowLift.lifted = false; gantt.cancelRowDrag() }
        }
        // Near the rows' top or bottom (a touch target deep) the lifted row scrolls them, faster nearer
        // the edge, up to 12 px a frame (the outline's, FEAT-121 A2).
        Timer {
            interval: 16
            repeat: true
            running: rowLift.lifted
            onTriggered: {
                const y = rowLift.fingerY
                const zone = Theme.touchTarget
                const top = gantt.headerHeight + zone
                const bottom = flick.height - zone
                const step = y < top ? -Math.min(1, (top - y) / zone) * 12 : y > bottom ? Math.min(1, (y - bottom) / zone) * 12 : 0
                const next = Math.max(0, Math.min(flick.contentHeight - flick.height, flick.contentY + step))
                if (step === 0 || next === flick.contentY) return
                flick.contentY = next
                gantt.dragRowTo(0, y)
            }
        }
        // Handlers in the content: their positions less the scrolling are the view's.
        PointHandler {
            objectName: "ganttPress"
            onActiveChanged: if (active) flick.tableDrag = point.pressPosition.x - flick.contentX < gantt.tableWidth
        }
        TapHandler {
            objectName: "ganttTap"
            onTapped: point => gantt.tapAt(point.position.x - flick.contentX, point.position.y - flick.contentY)
        }
        PinchHandler {
            id: pinch
            objectName: "ganttPinch"
            target: null
            property int startPercent: 100
            onActiveChanged: if (active) startPercent = gantt.zoomPercent
            onActiveScaleChanged: if (active) gantt.zoomTo(Math.round(startPercent * activeScale), centroid.position.x - flick.contentX)
        }
    }

    // Name's grip (user's request 2026-11-22): a drag sideways on the header at Name's right edge
    // changes its width, kept in the settings; drawn as the bars' end grip (two 10 px lines, 3 px
    // apart, the text colour at 150/255) in a 48 x 48 target over the header, so the rows still scroll.
    Item {
        id: nameGrip
        objectName: "ganttNameGrip"
        visible: !gantt.empty
        z: 3
        x: gantt.x + gantt.nameRight - width / 2
        y: gantt.y
        width: 12 * Theme.grid
        height: gantt.headerHeight
        Row {
            anchors.centerIn: parent
            spacing: 3
            Repeater {
                model: 2
                Rectangle {
                    width: 1
                    height: 10
                    color: Qt.rgba(gantt.dark ? 1 : 0x1f / 255, gantt.dark ? 1 : 0x29 / 255, gantt.dark ? 1 : 0x37 / 255, 150 / 255)
                }
            }
        }
        MouseArea {
            objectName: "ganttNameGripArea"
            anchors.fill: parent
            preventStealing: true  // the Flickable never takes the drag
            property real startX: 0
            property real startWidth: 0
            onPressed: mouse => {
                startX = mapToItem(view, mouse.x, 0).x
                startWidth = gantt.nameWidth
            }
            onPositionChanged: mouse => gantt.setNameWidth(startWidth + mapToItem(view, mouse.x, 0).x - startX, false)
            onReleased: mouse => gantt.setNameWidth(startWidth + mapToItem(view, mouse.x, 0).x - startX, true)
        }
    }

    // The other shown columns' grips (user's request 2026-11-23), drawn and dragged as Name's at each
    // column's right edge; the width is the desktop's setting. No. fits its numbers: no grip. A
    // column cut at the chart's edge has none.
    Repeater {
        objectName: "ganttColumnGrips"
        model: {
            gantt.nameRight  // re-read when the columns move or change
            return gantt.empty ? [] : gantt.shownFields().filter(field => field >= 2)
        }
        Item {
            id: columnGrip
            required property int modelData
            readonly property real edge: { gantt.nameRight; return gantt.columnLeft(modelData) + gantt.columnWidth(modelData) }
            objectName: "ganttColumnGrip" + modelData
            visible: edge <= gantt.tableWidth
            z: 3
            x: gantt.x + edge - width / 2
            y: gantt.y
            width: 12 * Theme.grid
            height: gantt.headerHeight
            Row {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 2
                    Rectangle {
                        width: 1
                        height: 10
                        color: Qt.rgba(gantt.dark ? 1 : 0x1f / 255, gantt.dark ? 1 : 0x29 / 255, gantt.dark ? 1 : 0x37 / 255, 150 / 255)
                    }
                }
            }
            MouseArea {
                anchors.fill: parent
                preventStealing: true
                property real startX: 0
                property real startWidth: 0
                onPressed: mouse => {
                    startX = mapToItem(view, mouse.x, 0).x
                    startWidth = gantt.columnWidth(columnGrip.modelData)
                }
                onPositionChanged: mouse => gantt.setColumnWidth(columnGrip.modelData, startWidth + mapToItem(view, mouse.x, 0).x - startX, false)
                onReleased: mouse => gantt.setColumnWidth(columnGrip.modelData, startWidth + mapToItem(view, mouse.x, 0).x - startX, true)
            }
        }
    }

    // The empty plan (the desktop's m_empty): its text and the accent button.
    Column {
        objectName: "ganttEmpty"
        anchors.centerIn: parent
        visible: gantt.empty
        spacing: 3 * Theme.grid
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("No tasks yet")
            color: gantt.dark ? Theme.darkTextMuted : Theme.textSecondary
            font.pixelSize: Theme.bodySize
        }
        DarkButton {
            objectName: "ganttAddFirst"
            anchors.horizontalCenter: parent.horizontalCenter
            primary: true
            text: qsTr("Add task")
            onClicked: gantt.addTask()
        }
    }

    // Stage B1: with a task selected, or in the multiple selection, the row bar in the scale group's
    // place (as the map's topic bar): Select more tasks, then the desktop's actions row (Add task,
    // Add subtask, Level up, Level down, Link in a chain / Remove links), Delete and ⋮ with the
    // rarer actions of the desktop's row menu.
    readonly property bool rowBarShown: !gantt.empty && (gantt.actions.tasks > 0 || view.map.multiSelect)
    readonly property real groupMargin: 3 * Theme.grid  // 16 px less the group's 4 px touch margin
    readonly property bool groupsShown: view.bottomInset === 0
    FloatingGroup {
        id: rowBar
        objectName: "ganttRowBar"
        visible: view.rowBarShown && view.groupsShown
        maximumWidth: view.width - view.leftInset - 2 * view.groupMargin
        x: view.leftInset + Math.round((view.width - view.leftInset - width) / 2)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: view.groupMargin
        GroupButton {
            objectName: "ganttSelectMore"
            icon.source: "qrc:/assets/hugeicons/select.svg"
            text: qsTr("Select more tasks")
            checked: view.map.multiSelect
            onClicked: view.map.multiSelect ? view.map.stopMultiSelect() : view.map.startMultiSelect()
        }
        GroupButton {
            objectName: "ganttAddTask"
            icon.source: "qrc:/assets/hugeicons/sibling.svg"
            text: qsTr("Add task")
            enabled: !view.map.multiSelect
            onClicked: gantt.addRow(false)
        }
        GroupButton {
            objectName: "ganttAddSubtask"
            icon.source: "qrc:/assets/hugeicons/child.svg"
            text: qsTr("Add subtask")
            enabled: !view.map.multiSelect && gantt.actions.tasks > 0
            onClicked: gantt.addRow(true)
        }
        GroupButton {
            objectName: "ganttOutdent"
            icon.source: "qrc:/assets/hugeicons/indent-left.svg"
            text: qsTr("Level up")
            enabled: gantt.actions.canOutdent
            onClicked: gantt.outdentSelected()
        }
        GroupButton {
            objectName: "ganttIndent"
            icon.source: "qrc:/assets/hugeicons/indent-right.svg"
            text: qsTr("Level down")
            enabled: gantt.actions.canIndent
            onClicked: gantt.indentSelected()
        }
        GroupButton {
            objectName: "ganttChain"
            icon.source: "qrc:/assets/hugeicons/relation.svg"
            text: gantt.actions.chainUnlinks ? qsTr("Remove links") : qsTr("Link in a chain")
            enabled: gantt.actions.canChain
            onClicked: gantt.chainSelected()
        }
        GroupButton {
            objectName: "ganttDelete"
            icon.source: "qrc:/assets/hugeicons/delete.svg"
            text: qsTr("Delete")
            enabled: gantt.actions.tasks > 0
            onClicked: view.map.removeSelected()
        }
        GroupButton {
            id: rowMenuButton
            objectName: "ganttRowMenu"
            icon.source: "qrc:/assets/hugeicons/more-vertical.svg"
            text: qsTr("More")
            onClicked: rowMenu.toggleAbove(rowMenuButton, view.rowTools())
        }
    }
    AddMenu {
        id: rowMenu
        objectName: "ganttMenu"
    }
    // The menu's rows: never shown as buttons, the menu reads their name and state.
    component MenuRow: GroupButton {
        visible: false
        menuText: text
    }
    // The desktop's row menu (GanttView::showRowMenu) less the bar's actions, then Go to today and
    // Resources (the bottom group's, hidden while a task is selected).
    MenuRow { id: colorRow; objectName: "ganttColor"; text: qsTr("Colour…"); onClicked: colorSheet.openWith(qsTr("Colour"), gantt.actions.color, color => gantt.setColor(color)) }
    MenuRow { id: defaultColorRow; objectName: "ganttDefaultColor"; text: qsTr("Default colour"); onClicked: gantt.clearColor() }
    MenuRow { id: nameRow; objectName: "ganttNameOnBar"; text: qsTr("Name on the bar"); checkable: true; onClicked: gantt.setNameOnBar(!gantt.actions.nameOnBar) }
    MenuRow { id: undateRow; objectName: "ganttRemoveDate"; text: qsTr("Remove date"); onClicked: gantt.removeDates() }
    MenuRow { id: copyStyleRow; objectName: "ganttCopyStyle"; text: qsTr("Copy style"); onClicked: view.map.copyStyle() }
    MenuRow { id: pasteStyleRow; objectName: "ganttPasteStyle"; text: qsTr("Paste style"); onClicked: view.map.pasteStyle() }
    MenuRow { id: todayRow; objectName: "ganttTodayRow"; text: qsTr("Go to today"); onClicked: gantt.scrollToToday() }
    MenuRow { id: resourcesRow; objectName: "ganttResourcesRow"; text: qsTranslate("MainWindow", "Resources"); onClicked: view.resourcesRequested() }
    MenuRow { id: baselineRow; objectName: "ganttBaselineRow"; text: qsTranslate("MainWindow", "Baseline"); onClicked: baselinesDialog.openBaselines() }
    MenuRow { id: columnsRow; objectName: "ganttColumnsRow"; text: qsTr("Columns…"); onClicked: columnsDialog.open() }
    // The keyboard's Tab and Shift+Tab (MapPage), as the row bar's Level down and Level up.
    function indentSelected() { return gantt.indentSelected() }
    function previewName(id, name) { gantt.setNamePreview(id, name) }  // FEAT-002: the Task page's typing
    function addTask() { return gantt.addRow(false) }  // Enter: the row bar's Add task
    function outdentSelected() { return gantt.outdentSelected() }
    function rowTools() {
        const actions = gantt.actions
        colorRow.enabled = actions.tasks > 0
        defaultColorRow.enabled = actions.hasColor
        nameRow.enabled = actions.tasks > 0
        nameRow.checked = actions.nameOnBar
        undateRow.enabled = actions.canRemoveDate
        copyStyleRow.enabled = actions.tasks === 1 && !view.map.multiSelect
        pasteStyleRow.enabled = actions.tasks > 0 && view.map.canPasteStyle
        return [colorRow, defaultColorRow, nameRow, undateRow, copyStyleRow, pasteStyleRow, todayRow, resourcesRow, baselineRow, columnsRow]
    }
    MobileStyle {
        id: colors  // the colour sheet's swatches and recent colours
        map: view.map
    }
    ColorSheet {
        id: colorSheet
        objectName: "ganttColorSheet"
        parent: Overlay.overlay
        style: colors
    }

    // Stage B3: a tap on an arrow opens the desktop's arrow menu (GanttChart::dependencyMenu): the four
    // types (the current one checked), Lag… and Delete dependency, by the finger.
    property var tappedLink: ({})
    Connections {
        target: gantt
        function onDependencyTapped(link, x, y) {
            view.tappedLink = link
            arrowAnchor.x = x - arrowAnchor.width / 2
            arrowAnchor.y = gantt.y + y - arrowAnchor.height / 2
            const below = y < gantt.height / 2
            for (let i = 0; i < typeRows.count; ++i) typeRows.itemAt(i).checked = i === link.type
            const tools = []
            for (let i = 0; i < typeRows.count; ++i) tools.push(typeRows.itemAt(i))
            tools.push(lagRow, removeLinkRow)
            if (below) arrowMenu.toggleBelow(arrowAnchor, tools)
            else arrowMenu.toggleAbove(arrowAnchor, tools)
        }
    }
    Item {
        id: arrowAnchor  // where the arrow was tapped: the menu opens from it
        readonly property real visualWidth: Theme.groupButtonSize
        width: Theme.touchTarget
        height: Theme.touchTarget
    }
    AddMenu {
        id: arrowMenu
        objectName: "ganttArrowMenu"
    }
    Repeater {
        id: typeRows
        model: gantt.dependencyTypes()
        MenuRow {
            required property int index
            required property string modelData
            objectName: "ganttLinkType" + index
            text: modelData
            checkable: true
            onClicked: gantt.setDependency(view.tappedLink.from, view.tappedLink.to, index, view.tappedLink.lag)
        }
    }
    MenuRow { id: lagRow; objectName: "ganttLinkLagRow"; text: qsTranslate("GanttChart", "Lag…"); onClicked: lagDialog.openFor(view.tappedLink) }
    MenuRow {
        id: removeLinkRow
        objectName: "ganttLinkRemoveRow"
        text: qsTranslate("GanttChart", "Delete dependency")
        onClicked: gantt.removeDependency(view.tappedLink.from, view.tappedLink.to)
    }
    ModalDialog {
        id: lagDialog
        objectName: "ganttLagDialog"
        property var link: ({})
        property int lag: 0
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 320)
        title: qsTranslate("GanttChart", "Lag")
        function openFor(link) {
            lagDialog.link = link
            lagDialog.lag = link.lag
            lagDialog.open()
        }
        onAccepted: gantt.setDependency(link.from, link.to, link.type, lag)
        Column {
            width: lagDialog.availableWidth
            spacing: 2 * Theme.grid
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTranslate("GanttChart", "Working days between the two tasks; negative = lead.")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            StyleStepper {
                objectName: "ganttLagField"
                width: parent.width
                label: qsTranslate("GanttChart", "Lag")
                value: lagDialog.lag
                minimum: -gantt.maxLag
                maximum: gantt.maxLag
                onStepped: delta => lagDialog.lag = Math.max(minimum, Math.min(maximum, lagDialog.lag + delta))
                onTyped: number => lagDialog.lag = number
            }
        }
        buttons: [
            DarkButton { objectName: "ganttLagApply"; text: qsTranslate("GanttChart", "Apply"); primary: true; onClicked: lagDialog.accept() }
        ]
    }

    // Days, Weeks, Months and Go to today (the desktop's top bar select and action), at the bottom.
    FloatingGroup {
        id: bottomGroup
        objectName: "ganttScaleGroup"
        visible: !gantt.empty && !view.rowBarShown && view.groupsShown
        x: view.leftInset + Math.round((view.width - view.leftInset - width) / 2)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3 * Theme.grid
        Repeater {
            model: [qsTr("Days"), qsTr("Weeks"), qsTr("Months")]
            GroupButton {
                required property int index
                required property string modelData
                objectName: "ganttScale" + index
                text: modelData
                checked: gantt.scale === index
                onClicked: gantt.scale = index
            }
        }
        GroupButton {
            objectName: "ganttToday"
            icon.source: "qrc:/assets/hugeicons/clock.svg"
            text: qsTr("Go to today")
            onClicked: gantt.scrollToToday()
        }
        // FEAT-122 C1: the desktop's Resources button of the Gantt's top bar group.
        GroupButton {
            objectName: "ganttResources"
            icon.source: "qrc:/assets/hugeicons/user-group.svg"
            text: qsTranslate("MainWindow", "Resources")
            onClicked: view.resourcesRequested()
        }
        // The desktop's Critical path button, pressed while the path is shown.
        GroupButton {
            objectName: "ganttCritical"
            icon.source: "qrc:/assets/hugeicons/alert.svg"
            text: qsTranslate("MainWindow", "Critical path")
            checked: gantt.criticalShown
            onClicked: gantt.criticalShown = !gantt.criticalShown
        }
        // FEAT-122 D: the desktop's Baseline button (camera), pressed while comparing.
        GroupButton {
            objectName: "ganttBaseline"
            icon.source: "qrc:/assets/hugeicons/camera.svg"
            text: qsTranslate("MainWindow", "Baseline")
            checked: gantt.comparedBaseline !== ""
            onClicked: baselinesDialog.openBaselines()
        }
    }
    BaselinesDialog {
        id: baselinesDialog
        parent: Overlay.overlay
        gantt: gantt
    }
    ColumnsDialog {
        id: columnsDialog
        parent: Overlay.overlay
        gantt: gantt
    }
}
