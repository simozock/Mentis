import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-119: the desktop's side panel on the phone (ui-mobile.md, "Side panel"). Portrait: docked at
// the bottom, full width, half the page high, top corners rounded 10. Landscape: docked at the right,
// full height, the desktop's 276 px. Dark (darkSurface), not modal: the map stays live beside it.
// The header is Style | Map | Pitch (Pitch since FEAT-120; the mobile's view order Map, Outliner,
// Gantt, Pitch, user's request 2026-11-23, where the desktop has Style | Pitch | Map). The sections fold in the bottom half panel (their state is kept while the app
// runs); in landscape they are unfolded, as the desktop's.
Item {
    id: panel
    objectName: "sidePanel"
    required property MobileMap map
    required property Item page
    required property MobilePitch pitch  // FEAT-120: the page's, shared with Present
    readonly property bool landscape: page.width > page.height
    // The desktop's side panel width (ui-window.md: 276 px): the default and narrowest docked at the side.
    readonly property int sideWidth: 276
    // User's request 2026-11-22: the edge toward the map is dragged; the size is kept in the settings.
    // At the side 276 px up to half the page; at the bottom 30-85% of the page's height (half by default).
    PanelSize { id: savedSize }
    property real dragSize: -1  // the size while a finger drags the edge, written on release
    readonly property real minBottomShare: 0.3
    readonly property real maxBottomShare: 0.85
    function sideWidthFor(wanted) {
        return Math.round(Math.max(Math.min(sideWidth, page.width), Math.min(wanted, page.width / 2)))
    }
    function bottomHeightFor(wanted) {
        return Math.round(Math.max(page.height * minBottomShare, Math.min(wanted, page.height * maxBottomShare)))
    }
    property int currentPage: 0
    // FEAT-122 B2: while the Gantt is shown the panel is the desktop's Gantt panel: its Task page.
    readonly property bool gantt: page.ganttShown === true
    onCurrentPageChanged: flick.contentY = 0  // each page opens at its top
    // Section name -> open; missing = open.
    property var openSections: ({})
    // The part of the page the panel covers, for the map's band.
    readonly property real coverTop: visible && !landscape ? y : page.height
    readonly property real coverLeft: visible && landscape ? x : page.width

    // User's request 2026-11-15: with the Gantt, at the left in landscape, as the desktop's task panel.
    readonly property bool atLeft: landscape && gantt
    x: landscape && !atLeft ? page.width - width : 0
    y: landscape ? 0 : page.height - height
    width: landscape ? sideWidthFor(dragSize >= 0 ? dragSize : savedSize.sideWidth > 0 ? savedSize.sideWidth : sideWidth)
                     : page.width
    height: landscape ? page.height
                      : bottomHeightFor(dragSize >= 0 ? dragSize
                                                      : page.height * (savedSize.bottomShare > 0 ? savedSize.bottomShare : 0.5))
    onLandscapeChanged: dragSize = -1

    MobileStyle {
        id: styleAdapter
        map: panel.map
    }
    MobileGanttTask {
        id: ganttTask
        map: panel.map
        active: panel.visible && panel.gantt && panel.ganttPage === 0  // the plan read only while the page is shown
        baseline: panel.page.ganttBaseline !== undefined ? panel.page.ganttBaseline : ""  // FEAT-122 D: the Gantt's compared snapshot
    }
    // FEAT-122 C2: the Gantt page (Task | Gantt while the Gantt is shown), its page kept apart from the map's.
    property int ganttPage: 0
    onGanttPageChanged: {
        flick.contentY = 0
        panel.shownTask = panel.map.selectedId
    }
    // As the desktop: a tap on another task shows it on the Task page, also from the Gantt page (an edit
    // there keeps the selection and the page).
    property string shownTask: ""
    Component.onCompleted: shownTask = map.selectedId  // not a binding: the handler below compares
    Connections {
        target: panel.map
        enabled: panel.gantt
        function onSelectionChanged() {
            if (panel.map.selectedId === panel.shownTask) return
            panel.shownTask = panel.map.selectedId
            if (panel.shownTask !== "") panel.ganttPage = 0
        }
    }
    // FEAT-002: a key typed on the Gantt's selected task: its Name on the Task page, replaced by `text`.
    function typeTaskName(text) {
        panel.ganttPage = 0
        return taskPage.typeName(text)
    }
    function editTaskName() {
        panel.ganttPage = 0
        return taskPage.editName()
    }
    MobileGanttStyle {
        id: ganttStyle
        map: panel.map
        active: panel.visible && panel.gantt && panel.ganttPage === 1
    }

    // Eats the touches that reach the panel, so they never fall through to the map.
    MouseArea { anchors.fill: parent; onWheel: wheel => wheel.accepted = true }

    Rectangle {
        anchors.fill: parent
        color: Theme.darkSurface
        radius: panel.landscape ? 0 : Theme.groupRadius
        // Square bottom corners in portrait: the panel reaches the page's edge.
        Rectangle {
            visible: !panel.landscape
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.groupRadius
            color: Theme.darkSurface
        }
        Rectangle {
            visible: panel.landscape
            anchors.right: panel.atLeft ? parent.right : undefined  // the edge toward the chart
            width: 1
            height: parent.height
            color: Theme.darkDivider
        }
    }

    DarkSegments {
        id: header
        objectName: "panelSegments"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 3 * Theme.grid
        anchors.bottomMargin: 0
        segments: panel.gantt ? [{ text: qsTranslate("GanttTaskPanel", "Task"), objectName: "taskSegment" },
                                 { text: qsTranslate("GanttTaskPanel", "Gantt"), objectName: "ganttSegment" }]
                              : [{ text: qsTranslate("MainWindow", "Style"), objectName: "styleSegment" },
                                 { text: qsTranslate("MainWindow", "Map"), objectName: "mapSegment" },
                                 { text: qsTranslate("MainWindow", "Pitch"), objectName: "pitchSegment" }]
        current: panel.gantt ? panel.ganttPage : panel.currentPage
        onChosen: index => { if (panel.gantt) panel.ganttPage = index; else panel.currentPage = index }
    }

    Flickable {
        id: flick
        objectName: "panelContent"
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 5 * Theme.grid
        anchors.rightMargin: 5 * Theme.grid
        clip: true
        contentHeight: panel.gantt ? (panel.ganttPage === 1 ? ganttStylePage.implicitHeight : taskPage.implicitHeight)
                     : panel.currentPage === 1 ? mapPage.implicitHeight
                     : panel.currentPage === 2 ? pitchPage.implicitHeight : stylePage.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        // Only the content scrolls: the header above stays (user's request 2026-11-13). The indicator
        // shows while it moves: 4 px, radius 2, darkTextMuted, at the panel's right edge.
        ScrollIndicator.vertical: ScrollIndicator {
            objectName: "panelScrollIndicator"
            parent: flick.parent
            anchors.top: flick.top
            anchors.bottom: flick.bottom
            anchors.right: parent.right
            anchors.rightMargin: Theme.grid
            contentItem: Rectangle {
                implicitWidth: 4
                implicitHeight: 4 * Theme.grid
                radius: 2
                color: Theme.darkTextMuted
                opacity: flick.moving ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 150 } }
            }
        }
        StylePanelPage {
            id: stylePage
            visible: !panel.gantt && panel.currentPage === 0
            width: flick.width
            style: styleAdapter
            menu: choiceMenu
            colorSheet: colorSheet
            foldable: !panel.landscape
            openSections: panel.openSections
            onSectionToggled: name => {
                const sections = Object.assign({}, panel.openSections)
                sections[name] = !stylePage.sectionOpen(name)
                panel.openSections = sections
            }
        }
        // FEAT-120: the Pitch page.
        PitchPanelPage {
            id: pitchPage
            objectName: "pitchPanelPage"
            visible: !panel.gantt && panel.currentPage === 2
            width: flick.width
            pitch: panel.pitch
            menu: choiceMenu
            themeSheet: pitchThemeSheet
            colorSheet: colorSheet
        }
        // FEAT-122 B2: the Task page.
        GanttTaskPage {
            id: taskPage
            objectName: "ganttTaskPage"
            visible: panel.gantt && panel.ganttPage === 0
            width: flick.width
            task: ganttTask
            menu: choiceMenu
            datePicker: datePicker
            colorSheet: colorSheet
            ganttStyle: ganttStyle
            foldable: !panel.landscape
            onResourcesRequested: panel.page.openResources()
            onNameTyped: (id, name) => panel.page.previewTaskName(id, name)
            openSections: panel.openSections
            onSectionToggled: name => {
                const sections = Object.assign({}, panel.openSections)
                sections[name] = !taskPage.sectionOpen(name)
                panel.openSections = sections
            }
        }
        // FEAT-122 C2: the Gantt page.
        GanttStylePage {
            id: ganttStylePage
            objectName: "ganttStylePage"
            visible: panel.gantt && panel.ganttPage === 1
            width: flick.width
            style: ganttStyle
            menu: choiceMenu
            colorSheet: colorSheet
            foldable: !panel.landscape
            openSections: panel.openSections
            onSectionToggled: name => {
                const sections = Object.assign({}, panel.openSections)
                sections[name] = !ganttStylePage.sectionOpen(name)
                panel.openSections = sections
            }
        }
        // FEAT-119 stage B: the Map page.
        MapPanelPage {
            id: mapPage
            objectName: "mapPanelPage"
            visible: !panel.gantt && panel.currentPage === 1
            width: flick.width
            style: styleAdapter
            menu: choiceMenu
            colorSheet: colorSheet
            paletteSheet: paletteSheet
            mapTypeSheet: mapTypeSheet
            newPaletteSheet: newPaletteSheet
            foldable: !panel.landscape
            openSections: panel.openSections
            onSectionToggled: name => {
                const sections = Object.assign({}, panel.openSections)
                sections[name] = !mapPage.sectionOpen(name)
                panel.openSections = sections
            }
        }
    }

    StyleChoiceMenu {
        id: choiceMenu
        parent: Overlay.overlay
    }
    DatePickerSheet {
        id: datePicker
        parent: Overlay.overlay
        task: ganttTask
    }
    NewPaletteSheet {
        id: newPaletteSheet
        parent: Overlay.overlay
        colorSheet: colorSheet
    }
    MapTypeSheet {
        id: mapTypeSheet
        parent: Overlay.overlay
    }
    PitchThemeSheet {
        id: pitchThemeSheet
        parent: Overlay.overlay
    }
    PaletteSheet {
        id: paletteSheet
        parent: Overlay.overlay
        onRemoveRequested: (id, name) => {
            removePaletteDialog.paletteId = id
            removePaletteDialog.paletteName = name
            removePaletteDialog.open()
        }
    }
    // FEAT-032: the desktop's confirmation before removing an own theme (not an Undo step).
    ModalDialog {
        id: removePaletteDialog
        objectName: "removePaletteDialog"
        property string paletteId
        property string paletteName
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(panel.page.width - 8 * Theme.grid, 400)
        title: qsTranslate("PaletteSelect", "Remove palette")
        onAccepted: styleAdapter.removePalette(paletteId)
        Label {
            width: removePaletteDialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            wrapMode: Text.Wrap
            text: qsTranslate("PaletteSelect", "\"%1\" will be removed from your palettes. Sheets that use it keep their colors. This cannot be undone.")
                .arg(removePaletteDialog.paletteName)
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton { objectName: "removePaletteConfirm"; text: qsTranslate("PaletteSelect", "Remove"); primary: true; onClicked: removePaletteDialog.accept() }
        ]
    }
    // The tooltips of the panel's labels (TipLabel), over its rows.
    readonly property Item tipArea: tips
    TipArea {
        id: tips
        anchors.fill: parent
        z: 1
    }
    ColorSheet {
        id: colorSheet
        parent: Overlay.overlay
        style: styleAdapter
    }

    // The drag grip on the edge toward the map (user's request 2026-11-22): a 32 x 4 pill, radius 2,
    // darkTextMuted (Material's bottom sheet drag handle, https://m3.material.io/components/bottom-sheets/specs),
    // across the top edge in portrait, along the side edge in landscape; its 96 x 48 target lies
    // mostly over the map, so it never takes the panel's header.
    Item {
        id: resizeGrip
        objectName: "panelResizeGrip"
        z: 2
        readonly property bool side: panel.landscape
        readonly property bool rightEdge: panel.atLeft  // the Gantt's panel at the left grows to the right
        width: side ? 12 * Theme.grid : 24 * Theme.grid
        height: side ? 24 * Theme.grid : 12 * Theme.grid
        x: side ? (rightEdge ? panel.width - 3 * Theme.grid : -9 * Theme.grid) : (panel.width - width) / 2
        y: side ? (panel.height - height) / 2 : -9 * Theme.grid
        Rectangle {
            objectName: "panelResizePill"
            width: resizeGrip.side ? 4 : 32
            height: resizeGrip.side ? 32 : 4
            radius: 2
            color: Theme.darkTextMuted
            // 4 px inside the panel's edge.
            x: resizeGrip.side ? (resizeGrip.rightEdge ? 3 * Theme.grid - 4 - width : 9 * Theme.grid + 4) : (parent.width - width) / 2
            y: resizeGrip.side ? (parent.height - height) / 2 : 9 * Theme.grid + 4
        }
        MouseArea {
            objectName: "panelResizeArea"
            anchors.fill: parent
            preventStealing: true  // the map and the panel's content never take the drag
            property real startPos: 0
            property real startSize: 0
            function pagePos(mouse) {
                const point = mapToItem(panel.page, mouse.x, mouse.y)
                return resizeGrip.side ? point.x : point.y
            }
            onPressed: mouse => {
                startPos = pagePos(mouse)
                startSize = resizeGrip.side ? panel.width : panel.height
                panel.dragSize = startSize
            }
            onPositionChanged: mouse => {
                const delta = pagePos(mouse) - startPos
                // At the right or bottom the edge moves against the size; the Gantt's left panel with it.
                const wanted = startSize + (resizeGrip.side && resizeGrip.rightEdge ? delta : -delta)
                panel.dragSize = resizeGrip.side ? panel.sideWidthFor(wanted) : panel.bottomHeightFor(wanted)
            }
            onReleased: finish()
            onCanceled: finish()
            function finish() {
                if (panel.dragSize < 0) return
                if (resizeGrip.side) savedSize.sideWidth = panel.dragSize
                else savedSize.bottomShare = panel.dragSize / panel.page.height
                panel.dragSize = -1
            }
        }
    }
}
