import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-120 stage C: Export from the Home's map row menu, the desktop's Export menu in a dark modal
// titled "Export": Image…, PDF…, Word…, Excel…, Pitch…, Markdown… as 48 px rows; Image… and PDF…
// open their options over it (ExportOptionsSheet, stage C2). Pitch… asks PowerPoint or PDF; an export of one sheet asks the sheet
// first when the map has several (user's decision 2026-11-14). The file goes to Android's Share at
// once; an error stays in the modal, 12 px in danger. FEAT-122 stage E: Word…, Excel… and Markdown…
// first ask Mind map or Gantt when a sheet's Gantt has tasks (OfficeExportDialog: Content, and Time
// unit for Word and Excel), then the sheet among those with a Gantt.
ModalDialog {
    id: sheet
    objectName: "mapExportSheet"
    required property MapLibrary library
    property string step: "formats"  // formats, pitch, office, sheets
    property int format: -1
    property bool gantt: false  // the office step's Gantt
    property int ganttContent: 0  // GanttPages::Content
    property int ganttUnit: 0  // GanttCanvas::Scale
    property var ganttSheets: []  // the sheets whose Gantt has tasks, found at the first office export
    property bool ganttChecked: false
    property string error: ""

    title: step === "office" ? officeTitle() : qsTranslate("MainWindow", "Export")
    // Kept open under the options: a tap on them is not outside.
    closePolicy: options.visible ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    anchors.centerIn: parent
    width: Math.min((parent ? parent.width : 400) - 8 * Theme.grid, 400)
    onClosed: exporter.finish()

    MapExport {
        id: exporter
        objectName: "mapExporter"
        library: sheet.library
    }

    ExportOptionsSheet {
        id: options
        parent: Overlay.overlay
        exporter: exporter
        onExported: sheet.close()
    }

    function openFor(url) {
        sheet.step = "formats"
        sheet.error = ""
        sheet.gantt = false
        sheet.ganttSheets = []
        sheet.ganttChecked = false
        if (!exporter.prepare(url)) {
            sheet.error = exporter.lastError
            sheet.step = "failed"
        }
        sheet.open()
    }
    function choose(format) {
        sheet.format = format
        sheet.error = ""
        const office = format === MapExport.Word || format === MapExport.Excel || format === MapExport.Markdown
        if (office && !sheet.ganttChecked) {
            const found = []
            for (let i = 0; i < exporter.sheets.length; ++i)
                if (exporter.hasGantt(i)) found.push(i)
            sheet.ganttSheets = found
            sheet.ganttChecked = true
        }
        if (office && sheet.ganttSheets.length > 0) sheet.step = "office"
        else sheet.chooseSheet(false)
    }
    // After the office step: the Gantt's sheet among those with tasks, the map's as before.
    function chooseSheet(gantt) {
        sheet.gantt = gantt
        if (gantt) {
            if (sheet.ganttSheets.length > 1) sheet.step = "sheets"
            else sheet.run(sheet.ganttSheets[0])
            return
        }
        const oneSheet = sheet.format !== MapExport.Word && sheet.format !== MapExport.Excel
        if (oneSheet && exporter.sheets.length > 1) sheet.step = "sheets"
        else sheet.run(0)
    }
    function run(index) {
        const done = sheet.gantt ? exporter.runGanttFile(sheet.format, index, sheet.ganttContent, sheet.ganttUnit)
                                 : exporter.run(sheet.format, index)
        if (done) sheet.close()
        else sheet.error = exporter.lastError
    }
    function sheetRows() {
        const rows = []
        for (let i = 0; i < exporter.sheets.length; ++i)
            if (!sheet.gantt || sheet.ganttSheets.indexOf(i) >= 0) rows.push({ index: i, name: exporter.sheets[i] })
        return rows
    }
    function officeTitle() {
        return sheet.format === MapExport.Word ? qsTranslate("OfficeExportDialog", "Export Word")
             : sheet.format === MapExport.Excel ? qsTranslate("OfficeExportDialog", "Export Excel")
             : qsTranslate("OfficeExportDialog", "Export Markdown")
    }

    component Caption: Label {
        width: sheet.availableWidth
        topPadding: 2 * Theme.grid
        bottomPadding: Theme.grid
        color: Theme.darkTextMuted
        font.pixelSize: 12
    }

    component ExportRow: ItemDelegate {
        id: row
        property string label
        width: sheet.availableWidth
        implicitHeight: Theme.touchTarget
        padding: 0
        leftPadding: 3 * Theme.grid
        rightPadding: 3 * Theme.grid
        contentItem: Label {
            text: row.label
            color: row.enabled ? Theme.darkText : Theme.darkTextMuted
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle { radius: Theme.controlRadius; color: row.down ? Theme.darkDivider : "transparent" }
    }

    Column {
        width: sheet.availableWidth
        Column {
            visible: sheet.step === "formats"
            ExportRow { objectName: "exportImage"; label: qsTranslate("MainWindow", "Image…"); onClicked: { sheet.error = ""; options.openFor(false) } }
            ExportRow { objectName: "exportPdf"; label: qsTranslate("MainWindow", "PDF…"); onClicked: { sheet.error = ""; options.openFor(true) } }
            ExportRow { objectName: "exportWord"; label: qsTranslate("MainWindow", "Word…"); onClicked: sheet.choose(MapExport.Word) }
            ExportRow { objectName: "exportExcel"; label: qsTranslate("MainWindow", "Excel…"); onClicked: sheet.choose(MapExport.Excel) }
            ExportRow { objectName: "exportPitch"; label: qsTranslate("MainWindow", "Pitch…"); onClicked: { sheet.error = ""; sheet.step = "pitch" } }
            ExportRow { objectName: "exportMarkdown"; label: qsTranslate("MainWindow", "Markdown…"); onClicked: sheet.choose(MapExport.Markdown) }
        }
        Column {
            visible: sheet.step === "pitch"
            ExportRow { objectName: "exportPitchPptx"; label: qsTr("PowerPoint (.pptx)"); onClicked: sheet.choose(MapExport.PitchPowerPoint) }
            ExportRow { objectName: "exportPitchPdf"; label: qsTr("PDF (.pdf)"); onClicked: sheet.choose(MapExport.PitchPdf) }
        }
        // OfficeExportDialog: Mind map (as before) or the Gantt of one sheet.
        Column {
            visible: sheet.step === "office"
            DarkSegments {
                objectName: "exportOfficeKind"
                width: sheet.availableWidth
                segments: [{ text: qsTranslate("OfficeExportDialog", "Mind map") }, { text: qsTranslate("OfficeExportDialog", "Gantt"), objectName: "exportOfficeGantt" }]
                current: sheet.gantt ? 1 : 0
                onChosen: index => sheet.gantt = index === 1
            }
            Label {
                visible: !sheet.gantt
                width: sheet.availableWidth
                topPadding: 2 * Theme.grid
                wrapMode: Text.Wrap
                text: sheet.format === MapExport.Markdown ? qsTranslate("OfficeExportDialog", "The active sheet, its attachments in a folder beside it.")
                                                          : qsTranslate("OfficeExportDialog", "Every sheet of the document.")
                color: Theme.darkTextMuted
                font.pixelSize: 12
            }
            Caption {
                visible: sheet.gantt
                text: qsTranslate("OfficeExportDialog", "Content")
            }
            DarkSegments {
                objectName: "exportOfficeContent"
                visible: sheet.gantt
                width: sheet.availableWidth
                segments: [{ text: qsTranslate("OfficeExportDialog", "Gantt") }, { text: qsTranslate("OfficeExportDialog", "Gantt and costs") },
                           { text: qsTranslate("OfficeExportDialog", "Costs") }]
                current: sheet.ganttContent
                onChosen: index => sheet.ganttContent = index
            }
            Caption {
                visible: sheet.gantt && sheet.format !== MapExport.Markdown
                text: qsTranslate("OfficeExportDialog", "Time unit")
            }
            DarkSegments {
                objectName: "exportOfficeUnit"
                visible: sheet.gantt && sheet.format !== MapExport.Markdown
                width: sheet.availableWidth
                segments: [{ text: qsTranslate("OfficeExportDialog", "Days") }, { text: qsTranslate("OfficeExportDialog", "Weeks") },
                           { text: qsTranslate("OfficeExportDialog", "Months") }]
                current: sheet.ganttUnit
                onChosen: index => sheet.ganttUnit = index
            }
        }
        Column {
            visible: sheet.step === "sheets"
            Label {
                width: sheet.availableWidth
                bottomPadding: Theme.grid
                text: qsTr("Which sheet?")
                color: Theme.darkTextMuted
                font.pixelSize: 12
            }
            Repeater {
                model: sheet.step === "sheets" ? sheet.sheetRows() : []
                ExportRow {
                    required property var modelData
                    objectName: "exportSheet" + modelData.index
                    label: modelData.name
                    onClicked: sheet.run(modelData.index)
                }
            }
        }
        Label {
            objectName: "exportError"
            visible: sheet.error !== ""
            width: sheet.availableWidth
            topPadding: Theme.grid
            wrapMode: Text.Wrap
            text: sheet.error
            color: Theme.danger
            font.pixelSize: 12
        }
    }

    buttons: [
        DarkButton {
            objectName: "exportOfficeRun"
            visible: sheet.step === "office"
            primary: true
            text: qsTranslate("OfficeExportDialog", "Export…")
            onClicked: sheet.chooseSheet(sheet.gantt)
        }
    ]
}
