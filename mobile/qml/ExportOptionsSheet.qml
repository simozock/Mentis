import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-120 stage C2: the desktop's Export image and Export PDF dialogs (ExportDialog, PageDialog)
// as a dark modal over Export, their rows in their order, without the preview, Selected branch,
// Copy to clipboard and Print (user's decision 2026-11-14). A caption (12 px darkTextMuted) over
// each segmented control, select rows through the marker menu's list, switches, the frame's rows
// (ExportFrameControls) only while it is on. Mind map is the only kind of Image; PDF also offers
// Pitch (the chosen sheet's slides with the frame). FEAT-121 stage C: PDF's Outliner, the outline on
// paper as the desktop's (no Fit to map, A4 instead; no Scale, no Background). FEAT-122 stage E: the
// Gantt tab, enabled when the chosen sheet's Gantt has tasks, with its rows (Content, Time unit; PDF:
// the page rows, Fit width or Actual size, the note of what fit width did). The texts are the desktop's.
ModalDialog {
    id: sheet
    objectName: "exportOptionsSheet"
    required property MapExport exporter
    property bool pdf: false
    signal exported()

    // The choices, as the desktop's dialogs start (frame off).
    property bool pitch: false
    property bool gantt: false
    property bool outline: false  // PDF's Outliner tab
    property bool ganttAvailable: false  // the chosen sheet's Gantt has tasks
    property int ganttContent: 0  // GanttPages::Content
    property int ganttUnit: 0  // GanttCanvas::Scale, Days first
    property bool ganttLandscape: true  // the Gantt's own orientation, landscape at first
    property bool ganttActualSize: false
    property bool allSheets: false
    property int sheetIndex: 0
    property bool withBackground: true
    property int pngScale: 2
    property int pageSize: 0  // MapPages::PageSize, FitToMap first
    property bool landscape: false
    property bool actualSize: false
    property bool monochrome: false
    property bool frameOn: false
    property int frameStyle: 1  // ExportFrame::Style::Solid
    property real frameThickness: 4
    property color frameColor: "#7047eb"
    property bool frameRounded: false
    property string error: ""
    readonly property string context: pdf ? "PageDialog" : "ExportDialog"
    readonly property bool severalSheets: exporter.sheets.length > 1
    readonly property bool mapRows: !sheet.pitch && !sheet.gantt
    readonly property int ganttIndex: pdf ? 2 : 1
    // PageDialog's note, worked out again when a choice changes.
    readonly property string ganttNote: sheet.pdf && sheet.gantt && sheet.opened ? sheet.exporter.ganttNote(sheet.ganttOptions()) : ""

    title: pdf ? qsTranslate("PageDialog", "Export PDF") : qsTranslate("ExportDialog", "Export image")
    closePolicy: Popup.CloseOnEscape
    anchors.centerIn: parent
    width: Math.min((parent ? parent.width : 400) - 8 * Theme.grid, 400)
    height: Math.min((parent ? parent.height : 600) - 8 * Theme.grid, implicitHeight)

    function openFor(pdf) {
        sheet.pdf = pdf
        sheet.pitch = false
        sheet.gantt = false
        sheet.outline = false
        sheet.allSheets = false
        sheet.sheetIndex = sheet.exporter.currentSheet()
        sheet.checkGantt()
        sheet.error = ""
        sheet.open()
    }
    function checkGantt() {
        sheet.ganttAvailable = sheet.exporter.hasGantt(sheet.sheetIndex)
        if (!sheet.ganttAvailable) sheet.gantt = false
    }
    function ganttOptions() {
        return { gantt: true, sheet: sheet.sheetIndex, svg: false, scale: sheet.pngScale, content: sheet.ganttContent, unit: sheet.ganttUnit,
                 pageSize: sheet.pageSize, landscape: sheet.ganttLandscape, actualSize: sheet.ganttActualSize, monochrome: sheet.monochrome }
    }
    function frame() {
        return { style: sheet.frameOn ? sheet.frameStyle : 0, thickness: sheet.frameThickness,
                 color: sheet.frameColor.toString(), rounded: sheet.frameRounded }
    }
    function run(svg) {
        const sheetChosen = sheet.allSheets && sheet.mapRows ? -1 : sheet.sheetIndex
        const options = sheet.ganttOptions()
        options.svg = svg
        options.frame = sheet.frame()
        const done = sheet.gantt ? (sheet.pdf ? sheet.exporter.runPdf(options) : sheet.exporter.runImage(options))
            : sheet.pdf
            ? sheet.exporter.runPdf({ pitch: sheet.pitch, sheet: sheetChosen, pageSize: sheet.pageSize, landscape: sheet.landscape,
                                      actualSize: sheet.actualSize, background: sheet.withBackground, monochrome: sheet.monochrome,
                                      frame: sheet.frame(), outliner: sheet.outline })
            : sheet.exporter.runImage({ sheet: sheetChosen, svg: svg, background: sheet.withBackground, scale: sheet.pngScale, frame: sheet.frame() })
        if (!done) { sheet.error = sheet.exporter.lastError; return }
        sheet.close()
        sheet.exported()
    }
    function named(names) {
        const list = []
        for (let i = 0; i < names.length; ++i) list.push({ value: i, name: names[i] })
        return list
    }

    component Caption: Label {
        width: parent ? parent.width : 0
        topPadding: 2 * Theme.grid
        bottomPadding: Theme.grid
        color: Theme.darkTextMuted
        font.pixelSize: 12
    }

    MobileStyle { id: colors }  // the colour sheet's swatches and recent colours
    StyleChoiceMenu {
        id: choiceMenu
        objectName: "exportChoiceMenu"  // not the side panel's
        parent: Overlay.overlay
    }
    ColorSheet {
        id: colorSheet
        objectName: "exportColorSheet"  // not the side panel's "colorSheet"
        parent: Overlay.overlay
        style: colors
    }

    Flickable {
        id: list
        width: sheet.availableWidth
        height: Math.min(contentHeight, (sheet.parent ? sheet.parent.height : 600) - 8 * Theme.grid
                         - (sheet.header ? sheet.header.implicitHeight : 0) - (sheet.footer ? sheet.footer.implicitHeight : 0)
                         - sheet.topPadding - sheet.bottomPadding - 2 * sheet.spacing)
        contentHeight: rows.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: rows
            width: list.width
            DarkSegments {
                objectName: "exportKind"
                width: parent.width
                segments: sheet.pdf
                    ? [{ text: qsTranslate("PageDialog", "Mind map") }, { text: qsTranslate("PageDialog", "Outliner"), objectName: "exportKindOutliner" },
                       { text: qsTranslate("PageDialog", "Gantt"), enabled: sheet.ganttAvailable, objectName: "exportKindGantt" },
                       { text: qsTranslate("PageDialog", "Pitch"), objectName: "exportKindPitch" }]
                    : [{ text: qsTranslate("ExportDialog", "Mind map") },
                       { text: qsTranslate("ExportDialog", "Gantt"), enabled: sheet.ganttAvailable, objectName: "exportKindGantt" }]
                current: sheet.pitch ? 3 : sheet.gantt ? sheet.ganttIndex : sheet.outline ? 1 : 0
                onChosen: index => {
                    sheet.pitch = index === 3
                    sheet.gantt = index === sheet.ganttIndex
                    sheet.outline = sheet.pdf && index === 1
                    // PageDialog::syncControls: the outline goes on paper of a fixed size.
                    if (sheet.outline && sheet.pageSize === 0) sheet.pageSize = 2
                }
            }
            Caption {
                visible: sheet.mapRows
                text: qsTranslate(sheet.context, "Content")
            }
            DarkSegments {
                objectName: "exportScope"
                visible: sheet.mapRows
                width: parent.width
                segments: [{ text: qsTranslate(sheet.context, "Active sheet") }, { text: qsTranslate(sheet.context, "All sheets"), objectName: "exportAllSheets" }]
                current: sheet.allSheets ? 1 : 0
                onChosen: index => sheet.allSheets = index === 1
            }
            StyleSelectRow {
                objectName: "exportSheetSelect"
                visible: sheet.severalSheets && (!sheet.mapRows || !sheet.allSheets)
                label: qsTr("Sheet")
                menu: choiceMenu
                choices: sheet.named(sheet.exporter.sheets)
                value: sheet.sheetIndex
                onPicked: v => {
                    sheet.sheetIndex = v
                    sheet.checkGantt()
                }
            }
            // FEAT-083: the Gantt tab's Content and Time unit (the unit off for the cost tables).
            Caption {
                visible: sheet.gantt
                text: qsTranslate(sheet.context, "Content")
            }
            DarkSegments {
                objectName: "exportGanttContent"
                visible: sheet.gantt
                width: parent.width
                segments: [{ text: qsTranslate(sheet.context, "Gantt") }, { text: qsTranslate(sheet.context, "Gantt and costs") },
                           { text: qsTranslate(sheet.context, "Costs") }]
                current: sheet.ganttContent
                onChosen: index => sheet.ganttContent = index
            }
            Caption {
                visible: sheet.gantt
                opacity: sheet.ganttContent !== 2 ? 1 : 0.4
                text: qsTranslate(sheet.context, "Time unit")
            }
            DarkSegments {
                objectName: "exportGanttUnit"
                visible: sheet.gantt
                enabled: sheet.ganttContent !== 2
                opacity: enabled ? 1 : 0.4
                width: parent.width
                segments: [{ text: qsTranslate(sheet.context, "Days") }, { text: qsTranslate(sheet.context, "Weeks") },
                           { text: qsTranslate(sheet.context, "Months") }]
                current: sheet.ganttUnit
                onChosen: index => sheet.ganttUnit = index
            }
            // PageDialog: the page rows for the map.
            StyleSelectRow {
                objectName: "exportPageSize"
                visible: sheet.pdf && !sheet.pitch
                label: qsTranslate("PageDialog", "Page size")
                menu: choiceMenu
                choices: (sheet.outline ? [] : [{ value: 0, name: qsTranslate("PageDialog", "Fit to map") }])
                         .concat([{ value: 1, name: "A3" }, { value: 2, name: "A4" }, { value: 3, name: "A5" },
                                  { value: 4, name: "Letter" }, { value: 5, name: "Legal" }])
                value: sheet.pageSize
                onPicked: v => sheet.pageSize = v
            }
            Caption {
                visible: sheet.pdf && !sheet.pitch
                opacity: sheet.pageSize > 0 ? 1 : 0.4
                text: qsTranslate("PageDialog", "Orientation")
            }
            DarkSegments {
                objectName: "exportOrientation"
                visible: sheet.pdf && !sheet.pitch
                enabled: sheet.pageSize > 0
                opacity: enabled ? 1 : 0.4
                width: parent.width
                segments: [{ text: qsTranslate("PageDialog", "Portrait") }, { text: qsTranslate("PageDialog", "Landscape") }]
                current: (sheet.gantt ? sheet.ganttLandscape : sheet.landscape) ? 1 : 0
                onChosen: index => { if (sheet.gantt) sheet.ganttLandscape = index === 1; else sheet.landscape = index === 1 }
            }
            // PageDialog's Gantt Scale: fit width or actual size; a page of the Gantt's size has no width to fit.
            Caption {
                visible: sheet.pdf && sheet.gantt
                opacity: sheet.ganttContent !== 2 && sheet.pageSize > 0 ? 1 : 0.4
                text: qsTranslate("PageDialog", "Scale")
            }
            DarkSegments {
                objectName: "exportGanttFit"
                visible: sheet.pdf && sheet.gantt
                enabled: sheet.ganttContent !== 2 && sheet.pageSize > 0
                opacity: enabled ? 1 : 0.4
                width: parent.width
                segments: [{ text: qsTranslate("PageDialog", "Fit width") }, { text: qsTranslate("PageDialog", "Actual size") }]
                current: sheet.ganttActualSize ? 1 : 0
                onChosen: index => sheet.ganttActualSize = index === 1
            }
            Caption {
                visible: !sheet.pitch && !(sheet.pdf && sheet.gantt)
                opacity: !sheet.pdf || (sheet.pageSize > 0 && !sheet.outline) ? 1 : 0.4
                text: qsTranslate(sheet.context, "Scale")
            }
            DarkSegments {
                objectName: "exportScale"
                visible: !sheet.pitch && !(sheet.pdf && sheet.gantt)
                enabled: !sheet.pdf || (sheet.pageSize > 0 && !sheet.outline)
                opacity: enabled ? 1 : 0.4
                width: parent.width
                segments: sheet.pdf ? [{ text: qsTranslate("PageDialog", "Fit to page") }, { text: qsTranslate("PageDialog", "Actual size") }]
                                    : [{ text: "1×" }, { text: "2×" }, { text: "3×" }]
                current: sheet.pdf ? (sheet.actualSize ? 1 : 0) : sheet.pngScale - 1
                onChosen: index => { if (sheet.pdf) sheet.actualSize = index === 1; else sheet.pngScale = index + 1 }
            }
            DarkSwitch {
                objectName: "exportBackground"
                visible: sheet.mapRows
                enabled: !sheet.outline
                opacity: enabled ? 1 : 0.4
                width: parent.width
                text: qsTranslate(sheet.context, "Background")
                checked: sheet.withBackground
                onClicked: sheet.withBackground = checked
            }
            DarkSwitch {
                visible: sheet.pdf && !sheet.pitch
                width: parent.width
                text: qsTranslate("PageDialog", "Monochrome")
                checked: sheet.monochrome
                onClicked: sheet.monochrome = checked
            }
            // ExportFrameControls (FEAT-084): its rows while the frame is on.
            DarkSwitch {
                objectName: "exportFrame"
                width: parent.width
                text: qsTranslate("ExportFrameControls", "Frame")
                checked: sheet.frameOn
                onClicked: sheet.frameOn = checked
            }
            StyleSelectRow {
                objectName: "exportFrameStyle"
                visible: sheet.frameOn
                label: qsTranslate("ExportFrameControls", "Style")
                menu: choiceMenu
                choices: {
                    const names = sheet.exporter.frameStyleNames()
                    const list = []
                    for (let i = 0; i < names.length; ++i) list.push({ value: i + 1, name: names[i] })
                    return list
                }
                value: sheet.frameStyle
                onPicked: v => sheet.frameStyle = v
            }
            StyleStepper {
                visible: sheet.frameOn
                width: parent.width
                label: qsTranslate("ExportFrameControls", "Thickness (px)")
                value: sheet.frameThickness
                minimum: 1
                maximum: 40
                onStepped: delta => sheet.frameThickness = Math.max(minimum, Math.min(maximum, sheet.frameThickness + delta))
                onTyped: number => sheet.frameThickness = number
            }
            Item {
                visible: sheet.frameOn
                width: parent.width
                height: Theme.touchTarget
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTranslate("ExportFrameControls", "Colour")
                    color: Theme.darkText
                    font.pixelSize: Theme.bodySize
                }
                AbstractButton {
                    id: swatch
                    anchors.right: parent.right
                    width: Theme.touchTarget
                    height: Theme.touchTarget
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTranslate("ExportFrameControls", "Colour")
                    onClicked: colorSheet.openWith(qsTranslate("ExportFrameControls", "Colour"), sheet.frameColor, c => sheet.frameColor = c)
                    background: Item {}
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: 28
                            height: 28
                            radius: Theme.controlRadius
                            color: sheet.frameColor
                            border.width: 1
                            border.color: swatch.down ? Theme.darkText : Theme.darkDivider
                        }
                    }
                }
            }
            Caption {
                visible: sheet.frameOn
                text: qsTranslate("ExportFrameControls", "Corners")
            }
            DarkSegments {
                visible: sheet.frameOn
                width: parent.width
                segments: [{ text: qsTranslate("ExportFrameControls", "Square") }, { text: qsTranslate("ExportFrameControls", "Rounded") }]
                current: sheet.frameRounded ? 1 : 0
                onChosen: index => sheet.frameRounded = index === 1
            }
            Label {
                objectName: "exportGanttNote"
                visible: sheet.ganttNote !== ""
                width: parent.width
                topPadding: Theme.grid
                wrapMode: Text.Wrap
                text: sheet.ganttNote
                color: Theme.darkTextMuted
                font.pixelSize: 12
            }
            Label {
                objectName: "exportOptionsError"
                visible: sheet.error !== ""
                width: parent.width
                topPadding: Theme.grid
                wrapMode: Text.Wrap
                text: sheet.error
                color: Theme.danger
                font.pixelSize: 12
            }
        }
    }

    // The desktop's buttons: PNG and SVG for an image, Export for a PDF.
    buttons: [
        DarkButton {
            objectName: "exportSvg"
            visible: !sheet.pdf
            text: "SVG"
            onClicked: sheet.run(true)
        },
        DarkButton {
            objectName: "exportRun"
            primary: true
            text: sheet.pdf ? qsTranslate("PageDialog", "Export") : "PNG"
            onClicked: sheet.run(false)
        }
    ]
}
