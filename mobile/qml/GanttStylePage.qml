import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-122 C2: the side panel's Gantt page while the Gantt is shown, as the desktop's (FEAT-080):
// Tasks, Summaries and Milestones (Shape, Fill, Border as picture selects; Border colour and Colour,
// with × back to the default when the sheet has its own), then Relations (Line, End, Thickness, End size, Colour). Each
// choice changes every bar of that kind on the sheet but those with their own value, one Undo step
// (MobileGanttStyle). Texts and pictures are the desktop's.
Column {
    id: page
    required property MobileGanttStyle style
    required property var menu              // the panel's StyleChoiceMenu
    required property ColorSheet colorSheet
    property bool foldable: false
    property var openSections: ({})
    signal sectionToggled(string name)
    function sectionOpen(name) { return openSections[name] !== false }
    function text(source) { return qsTranslate("GanttTaskPanel", source) }
    readonly property var values: style.values
    function valueOf(key, kind) {
        const value = page.values[key]
        if (value === undefined) return undefined
        return kind === undefined ? value : value[kind]
    }

    Repeater {
        model: [{ kind: 0, title: page.text("Tasks"), key: "GanttTasks", shapes: MobileGanttStyle.BarShape, shape: "barShape" },
                { kind: 1, title: page.text("Summaries"), key: "GanttSummaries", shapes: MobileGanttStyle.SummaryShape, shape: "summaryShape" },
                { kind: 2, title: page.text("Milestones"), key: "GanttMilestones", shapes: MobileGanttStyle.MilestoneShape, shape: "milestoneShape" }]
        StyleSection {
            id: kindSection
            required property var modelData
            objectName: "ganttStyle_" + modelData.key
            title: modelData.title
            foldable: page.foldable
            open: page.sectionOpen(modelData.key)
            onToggled: page.sectionToggled(modelData.key)
            StyleSelectRow {
                objectName: "ganttSheetShape" + kindSection.modelData.kind
                label: page.text("Shape")
                menu: page.menu
                choices: page.style.choices(kindSection.modelData.shapes)
                iconKind: page.style.iconKind(kindSection.modelData.shapes)
                value: page.valueOf(kindSection.modelData.shape)
                onPicked: value => page.style.choose(kindSection.modelData.shapes, value)
            }
            StyleSelectRow {
                objectName: "ganttSheetFill" + kindSection.modelData.kind
                label: page.text("Fill")
                menu: page.menu
                choices: page.style.choices(MobileGanttStyle.Fill)
                iconKind: page.style.iconKind(MobileGanttStyle.Fill)
                value: page.valueOf("fill", kindSection.modelData.kind)
                onPicked: value => page.style.choose(MobileGanttStyle.Fill, value, kindSection.modelData.kind)
            }
            StyleSelectRow {
                objectName: "ganttSheetBorder" + kindSection.modelData.kind
                label: page.text("Border")
                menu: page.menu
                choices: page.style.choices(MobileGanttStyle.Border)
                iconKind: page.style.iconKind(MobileGanttStyle.Border)
                value: page.valueOf("border", kindSection.modelData.kind)
                onPicked: value => page.style.choose(MobileGanttStyle.Border, value, kindSection.modelData.kind)
            }
            GanttColorRow {
                objectName: "ganttSheetBorderColor" + kindSection.modelData.kind
                label: page.text("Border colour")
                colorSheet: page.colorSheet
                color: page.valueOf("borderColor", kindSection.modelData.kind)
                resettable: page.valueOf("borderColorChosen", kindSection.modelData.kind) === true
                resetName: page.text("Default border colour")
                onPicked: color => page.style.setBorderColor(kindSection.modelData.kind, color)
                onReset: page.style.clearBorderColor(kindSection.modelData.kind)
            }
            GanttColorRow {
                objectName: "ganttSheetColor" + kindSection.modelData.kind
                label: page.text("Colour")
                sheetTitle: kindSection.modelData.title
                colorSheet: page.colorSheet
                color: page.valueOf("color", kindSection.modelData.kind)
                resettable: page.valueOf("colorChosen", kindSection.modelData.kind) === true
                resetName: page.text("Default colour")
                onPicked: color => page.style.setColor(kindSection.modelData.kind, color)
                onReset: page.style.clearColor(kindSection.modelData.kind)
            }
        }
    }

    StyleSection {
        objectName: "ganttStyle_GanttRelations"
        title: page.text("Relations")
        foldable: page.foldable
        open: page.sectionOpen("GanttRelations")
        onToggled: page.sectionToggled("GanttRelations")
        Repeater {
            model: [{ list: MobileGanttStyle.LinkLine, key: "linkLine", label: page.text("Line") },
                    { list: MobileGanttStyle.LinkEnd, key: "linkEnd", label: page.text("End") },
                    { list: MobileGanttStyle.LinkWidth, key: "linkWidth", label: page.text("Thickness") },
                    { list: MobileGanttStyle.LinkEndSize, key: "linkEndSize", label: page.text("End size") }]
            StyleSelectRow {
                required property var modelData
                objectName: "ganttSheet_" + modelData.key
                label: modelData.label
                menu: page.menu
                choices: page.style.choices(modelData.list)
                iconKind: page.style.iconKind(modelData.list)
                value: page.valueOf(modelData.key)
                onPicked: value => page.style.choose(modelData.list, value)
            }
        }
        GanttColorRow {
            objectName: "ganttSheetLinkColor"
            label: page.text("Colour")
            sheetTitle: page.text("Relations")
            colorSheet: page.colorSheet
            color: page.valueOf("linkColor")
            resettable: page.valueOf("linkColorChosen") === true
            resetName: page.text("Default colour")
            onPicked: color => page.style.setLinkColor(color)
            onReset: page.style.clearLinkColor()
        }
        Item { width: 1; height: 2 * Theme.grid }
    }
}
