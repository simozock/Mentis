import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-119 stage A: the side panel's Style page. With a picture, a relationship or a boundary selected
// it shows that object's page, as MainWindow::refreshStylePage. The topics' page is the desktop's StylePage in its order (preset; Shape
// with fill, border and thickness; width with Fit; Text; Structure; Branch; Numbering; the actions),
// with the phone's controls (ui-mobile.md, "Side panel"). Every change is applied at once and is one
// Undo step (MobileStyle). The texts are the desktop's (context StylePage).
Column {
    id: page
    required property MobileStyle style
    required property StyleChoiceMenu menu
    required property ColorSheet colorSheet
    // The sections fold in the bottom half panel; their open state is the panel's.
    property bool foldable: false
    property var openSections: ({})
    signal sectionToggled(string name)

    readonly property int revision: style.revision
    readonly property bool any: revision >= 0 && style.topicCount > 0
    readonly property bool one: revision >= 0 && style.topicCount === 1
    readonly property int target: revision >= 0 ? style.target : MobileStyle.Topics

    function read(property) { return page.revision >= 0 ? page.style.reading(property) : ({}) }
    function value(reading) { return reading.empty || reading.mixed ? undefined : reading.value }
    function sectionOpen(name) { return page.openSections[name] !== false }

    spacing: 0

    component Section: StyleSection {
        foldable: page.foldable
        open: page.sectionOpen(title)
        onToggled: page.sectionToggled(title)
    }
    // A select bound to one style property.
    component PropertySelect: StyleSelectRow {
        required property int property
        readonly property var reading: page.read(property)
        menu: page.menu
        choices: page.revision >= 0 ? page.style.choices(property) : []
        iconKind: page.style.iconKind(property)
        value: page.value(reading)
        mixed: reading.mixed === true
        manual: reading.manual === true
        enabled: reading.empty !== true
        onPicked: v => page.style.choose(property, v)
    }
    // The desktop's ColorField: the whole 28 px field is the colour, "—" for mixed values.
    component ColorButton: AbstractButton {
        id: colorButton
        required property int property
        required property string sheetTitle
        readonly property var reading: page.read(property)
        width: Theme.touchTarget
        height: Theme.touchTarget
        enabled: reading.empty !== true
        focusPolicy: Qt.NoFocus
        Accessible.name: sheetTitle
        onClicked: page.colorSheet.openFor(property, sheetTitle, reading)
        background: Item {}
        contentItem: Item {
            opacity: colorButton.enabled ? 1 : 0.4
            Rectangle {
                anchors.centerIn: parent
                width: 28
                height: 28
                radius: Theme.controlRadius
                color: colorButton.reading.mixed || colorButton.reading.empty ? Theme.darkSunken : colorButton.reading.value
                border.width: 1
                border.color: colorButton.down ? Theme.darkText : Theme.darkDivider
                Label {
                    anchors.centerIn: parent
                    visible: colorButton.reading.mixed === true
                    text: "—"
                    color: Theme.darkText
                }
                // A fully transparent colour (no fill, no border): crossed, as "None".
                Rectangle {
                    visible: !colorButton.reading.mixed && !colorButton.reading.empty && colorButton.reading.value.a === 0
                    anchors.centerIn: parent
                    width: parent.width * 1.3
                    height: 1.5
                    rotation: -45
                    color: Theme.darkTextMuted
                }
            }
        }
    }
    // A select and its colour on one row (Fill, Border, the branch's width).
    component SelectWithColor: Item {
        id: pair
        property alias label: select.label
        property alias property: select.property
        property alias colorProperty: color.property
        property alias colorTitle: color.sheetTitle
        property bool colorEnabled: true
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        PropertySelect { id: select; property: 0; width: pair.width - color.width - Theme.grid }
        ColorButton {
            id: color
            property: 0
            sheetTitle: ""
            anchors.right: parent.right
            enabled: reading.empty !== true && pair.colorEnabled
        }
    }
    // A toggle bound to one style property (the picture's shadow and outline).
    component PropertySwitch: DarkSwitch {
        required property int property
        readonly property var reading: page.read(property)
        width: parent ? parent.width : 0
        enabled: reading.empty !== true
        // Not toggled by the tap: the model's value stays the one shown.
        checkable: false
        checked: reading.value === true
        onClicked: page.style.choose(property, !checked)
    }
    component WideButton: DarkButton {
        width: parent ? parent.width : 0
    }

    // The topics' page (StylePage).
    Column {
        id: topicPage
        objectName: "topicStylePage"
        width: parent.width
        visible: page.target === MobileStyle.Topics

        // FEAT-020: the preset at the top, its previews in the topic's colour and level.
        StyleSelectRow {
            id: preset
            objectName: "presetSelect"
            readonly property var reading: page.read(MobileStyle.Preset)
            menu: page.menu
            choices: page.revision >= 0 ? page.style.choices(MobileStyle.Preset) : []
            presetPreview: true
            presetColor: page.revision >= 0 ? page.style.presetColor() : "#ff6b6b"
            presetDepth: page.revision >= 0 ? page.style.presetDepth() : 1
            themeColors: page.revision >= 0 ? page.style.themeColors() : []
            topicPreview: page.revision >= 0 ? page.style.topicPreview() : ({})
            value: page.value(reading)
            mixed: reading.mixed === true
            enabled: page.any
            onPicked: v => page.style.choose(MobileStyle.Preset, v)
        }

        Section {
            title: qsTranslate("StylePage", "Shape")
            PropertySelect { objectName: "shapeSelect"; label: qsTranslate("StylePage", "Shape"); property: MobileStyle.Shape }
            SelectWithColor {
                objectName: "fillRow"
                label: qsTranslate("StylePage", "Fill")
                property: MobileStyle.FillStyle
                colorProperty: MobileStyle.FillColor
                colorTitle: qsTranslate("StylePage", "Fill")
            }
            SelectWithColor {
                objectName: "borderRow"
                label: qsTranslate("StylePage", "Border")
                property: MobileStyle.BorderStyle
                colorProperty: MobileStyle.BorderColor
                colorTitle: qsTranslate("StylePage", "Border")
            }
            StyleStepper {
                objectName: "borderWidthStepper"
                readonly property var reading: page.read(MobileStyle.BorderWidth)
                label: qsTranslate("StylePage", "Thickness (px)")
                value: page.value(reading)
                minimum: 0
                maximum: 20
                decimals: 1
                enabled: page.any
                onStepped: delta => page.style.choose(MobileStyle.BorderWidth, Math.max(minimum, Math.min(maximum, (value === undefined ? 0 : value) + delta)))
                onTyped: number => page.style.choose(MobileStyle.BorderWidth, number)
            }
        }

        // FEAT-021: the text box width and Fit, a block of its own between Shape and Text.
        Column {
            width: parent.width
            Rectangle { width: parent.width; height: 1; color: Theme.darkDivider }
            StyleStepper {
                objectName: "widthStepper"
                readonly property var reading: page.revision >= 0 ? page.style.widthReading() : ({})
                label: qsTranslate("StylePage", "Width (px)")
                value: page.value(reading)
                minimum: 0
                maximum: 9999
                enabled: page.any && !reading.empty
                onStepped: delta => page.style.stepWidth(delta)
                onTyped: number => page.style.chooseWidth(number)
            }
            WideButton {
                objectName: "fitWidthButton"
                text: qsTranslate("StylePage", "Fit")
                enabled: page.revision >= 0 && page.style.canFitWidth()
                onClicked: page.style.fitWidth()
            }
        }

        Section {
            title: qsTranslate("StylePage", "Text")
            PropertySelect { objectName: "fontSelect"; label: qsTranslate("StylePage", "Font"); property: MobileStyle.FontFamily }
            StyleStepper {
                objectName: "fontSizeStepper"
                readonly property var reading: page.read(MobileStyle.FontSize)
                label: qsTr("Size (px)")
                value: page.value(reading)
                minimum: 1
                maximum: 96
                enabled: page.any
                onStepped: delta => page.style.chooseFontSizePx(Math.round((value === undefined ? 18 : value) + delta))
                onTyped: number => page.style.chooseFontSizePx(number)
            }
            SelectWithColor {
                objectName: "weightRow"
                label: qsTranslate("StylePage", "Weight")
                property: MobileStyle.FontWeight
                colorProperty: MobileStyle.TextColor
                colorTitle: qsTranslate("StylePage", "Text")
            }
            // Bold, italic, strikethrough, underline: toggles; mixed values show off, as the desktop.
            DarkSegments {
                objectName: "textToggles"
                width: parent.width
                readonly property var weight: page.read(MobileStyle.FontWeight)
                readonly property var italic: page.read(MobileStyle.Italic)
                readonly property var strike: page.read(MobileStyle.StrikeOut)
                readonly property var underline: page.read(MobileStyle.Underline)
                enabled: page.any
                segments: [{ icon: "qrc:/assets/hugeicons/text-bold.svg", text: qsTranslate("StylePage", "Bold"), objectName: "boldToggle" },
                           { icon: "qrc:/assets/hugeicons/text-italic.svg", text: qsTranslate("StylePage", "Italic"), objectName: "italicToggle" },
                           { icon: "qrc:/assets/hugeicons/text-strikethrough.svg", text: qsTranslate("StylePage", "Strikethrough"), objectName: "strikeToggle" },
                           { icon: "qrc:/assets/hugeicons/text-underline.svg", text: qsTranslate("StylePage", "Underline"), objectName: "underlineToggle" }]
                function on(reading, test) { return !reading.empty && !reading.mixed && test(reading.value) }
                readonly property var states: [on(weight, v => v >= 700), on(italic, v => v), on(strike, v => v), on(underline, v => v)]
                // Several can be on: the active look is drawn per segment through `current` = -1 and `checkedSet`.
                current: -1
                checkedSet: states
                onChosen: index => {
                    if (index === 0) page.style.choose(MobileStyle.FontWeight, states[0] ? 400 : 700)
                    else page.style.choose([MobileStyle.Italic, MobileStyle.StrikeOut, MobileStyle.Underline][index - 1], !states[index])
                }
            }
            DarkSegments {
                objectName: "caseSegments"
                width: parent.width
                readonly property var reading: page.read(MobileStyle.TextCase)
                enabled: page.any
                segments: [{ text: "Aa" }, { text: "AA" }, { text: "aa" }]
                current: reading.empty || reading.mixed ? -1 : reading.value
                onChosen: index => page.style.choose(MobileStyle.TextCase, index)
            }
            DarkSegments {
                objectName: "alignSegments"
                width: parent.width
                readonly property var reading: page.read(MobileStyle.TextAlign)
                enabled: page.any
                segments: [{ icon: "qrc:/assets/hugeicons/text-align-left.svg", text: qsTranslate("StylePage", "Align left") },
                           { icon: "qrc:/assets/hugeicons/text-align-center.svg", text: qsTranslate("StylePage", "Center") },
                           { icon: "qrc:/assets/hugeicons/text-align-right.svg", text: qsTranslate("StylePage", "Align right") },
                           { icon: "qrc:/assets/hugeicons/text-align-justify.svg", text: qsTranslate("StylePage", "Justify") }]
                current: reading.empty || reading.mixed ? -1 : reading.value
                onChosen: index => page.style.choose(MobileStyle.TextAlign, index)
            }
        }

        // FEAT-026: one topic's structure (the central topic's is the sheet's).
        Section {
            id: structure
            title: qsTranslate("StylePage", "Structure")
            visible: page.one
            readonly property string type: page.revision >= 0 ? page.style.structureType() : ""
            StyleSelectRow {
                objectName: "structureSelect"
                label: qsTranslate("StylePage", "Structure")
                menu: page.menu
                choices: {
                    const list = []
                    if (page.revision < 0) return list
                    for (const item of page.style.structureTypes())
                        list.push({ value: item.key, name: item.name, image: "qrc:/assets/structures/" + (item.key === "" ? "inherit" : item.key) + ".svg" })
                    return list
                }
                imageGrid: true
                value: structure.type
                onPicked: v => page.style.chooseStructure(v, "")
            }
            StyleSelectRow {
                objectName: "directionSelect"
                visible: structure.type !== ""
                label: qsTranslate("StructurePicker", "Direction")
                menu: page.menu
                choices: {
                    const list = []
                    if (page.revision < 0) return list
                    for (const item of page.style.structureDirections(structure.type)) list.push({ value: item.key, name: item.name, enabled: item.enabled })
                    return list
                }
                // Disabled when no other direction would change the layout.
                enabled: choices.some(c => c.enabled && c.value !== value)
                value: page.revision >= 0 ? page.style.structureDirection() : ""
                onPicked: v => page.style.chooseStructure(structure.type, v)
            }
        }

        Section {
            title: qsTranslate("StylePage", "Branch")
            PropertySelect { objectName: "branchShapeSelect"; label: qsTranslate("StylePage", "Branch"); property: MobileStyle.BranchShape }
            PropertySelect { objectName: "branchStyleSelect"; label: qsTranslate("StylePage", "Line"); property: MobileStyle.BranchStyle }
            SelectWithColor {
                objectName: "branchWidthRow"
                label: ""
                property: MobileStyle.BranchWidth
                colorProperty: MobileStyle.BranchColor
                colorTitle: qsTranslate("StylePage", "Branch")
            }
            PropertySelect { objectName: "branchStartSelect"; label: qsTranslate("StylePage", "Line start"); property: MobileStyle.BranchStart }
            PropertySelect { objectName: "branchEndSelect"; label: qsTranslate("StylePage", "Line end"); property: MobileStyle.BranchEnd }
        }

        // FEAT-024: NumberingFields.
        Section {
            title: qsTranslate("StylePage", "Numbering")
            NumberingFields {
                objectName: "topicNumbering"
                style: page.style
                menu: page.menu
                revision: page.revision
            }
        }

        // FEAT-072 B15: the actions, full width at the bottom.
        Column {
            width: parent.width
            spacing: 0
            bottomPadding: 4 * Theme.grid
            Rectangle { width: parent.width; height: 1; color: Theme.darkDivider }
            WideButton {
                objectName: "applyToRelatedButton"
                text: qsTranslate("StylePage", "Apply to related")
                visible: page.revision >= 0 && page.style.showApplyToRelated()
                enabled: page.revision >= 0 && page.style.canApplyToRelated()
                onClicked: page.style.applyToRelated()
            }
            WideButton {
                objectName: "applyToChildrenButton"
                text: qsTranslate("StylePage", "Apply to children")
                visible: page.any
                enabled: page.revision >= 0 && page.style.canApplyToChildren()
                onClicked: page.style.applyToChildren()
            }
            WideButton {
                objectName: "resetStyleButton"
                text: qsTranslate("StylePage", "Reset style")
                visible: page.any
                enabled: page.revision >= 0 && page.style.canResetStyle()
                onClicked: page.style.resetStyle()
            }
        }
    }

    // FEAT-040: the picture's page (ImageStylePage): size with the proportions kept, its reset, the
    // turn, border, thickness, shadow, the sticker's outline, opacity and Reset style.
    Column {
        id: picturePage
        objectName: "pictureStylePage"
        width: parent.width
        visible: page.target === MobileStyle.Picture
        readonly property var size: page.revision >= 0 ? page.style.pictureSize() : ({ empty: true })
        // The desktop's chain: on by default (the user's decision), kept while the app runs.
        property bool keepProportions: true
        bottomPadding: 4 * Theme.grid

        StyleStepper {
            objectName: "pictureWidthStepper"
            label: qsTranslate("ImageStylePage", "Width")
            value: picturePage.size.empty ? undefined : picturePage.size.width
            step: 10
            minimum: 24
            maximum: 3000
            enabled: !picturePage.size.empty
            onStepped: delta => page.style.choosePictureWidth(Math.max(minimum, Math.min(maximum, value + delta)), picturePage.keepProportions)
            onTyped: number => page.style.choosePictureWidth(number, picturePage.keepProportions)
        }
        StyleStepper {
            objectName: "pictureHeightStepper"
            label: qsTranslate("ImageStylePage", "Height")
            value: picturePage.size.empty ? undefined : picturePage.size.height
            step: 10
            minimum: 1
            maximum: 3000
            enabled: !picturePage.size.empty
            onStepped: delta => page.style.choosePictureHeight(Math.max(minimum, Math.min(maximum, value + delta)), picturePage.keepProportions)
            onTyped: number => page.style.choosePictureHeight(number, picturePage.keepProportions)
        }
        DarkSwitch {
            objectName: "keepProportionsSwitch"
            width: parent.width
            text: qsTranslate("ImageStylePage", "Keep proportions")
            checked: picturePage.keepProportions
            onToggled: picturePage.keepProportions = checked
        }
        WideButton {
            objectName: "resetPictureSizeButton"
            text: qsTranslate("ImageStylePage", "Reset size")
            enabled: !picturePage.size.empty
            onClicked: page.style.resetPictureSize()
        }
        WideButton {
            objectName: "rotatePictureButton"
            id: rotateButton
            text: qsTranslate("ImageStylePage", "Rotate 90°")
            enabled: !picturePage.size.empty
            onClicked: page.style.rotatePicture()
            // The desktop's icon beside the text.
            contentItem: Item {
                Row {
                    anchors.centerIn: parent
                    spacing: 2 * Theme.grid
                    IconImage {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 16
                        height: 16
                        sourceSize: Qt.size(16, 16)
                        source: "qrc:/assets/hugeicons/menu-rotate.svg"
                        color: rotateButton.enabled ? Theme.darkText : Theme.darkTextMuted
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: rotateButton.text
                        color: rotateButton.enabled ? Theme.darkText : Theme.darkTextMuted
                        font.pixelSize: Theme.bodySize
                    }
                }
            }
        }
        Rectangle { width: parent.width; height: 1; color: Theme.darkDivider }
        // FEAT-129: the topic shapes; a sticker keeps its own outline.
        PropertySelect {
            objectName: "pictureShapeSelect"
            label: qsTranslate("ImageStylePage", "Shape")
            property: MobileStyle.PictureShape
            visible: page.revision >= 0 && !page.style.pictureIsSticker()
        }
        // FEAT-131: a colour effect, stickers and formulas too.
        PropertySelect {
            objectName: "pictureEffectSelect"
            label: qsTranslate("ImageStylePage", "Effect")
            property: MobileStyle.PictureEffect
        }
        SelectWithColor {
            objectName: "pictureBorderRow"
            label: qsTranslate("ImageStylePage", "Border")
            property: MobileStyle.PictureBorder
            colorProperty: MobileStyle.PictureBorderColor
            colorTitle: qsTranslate("ImageStylePage", "Border color")
            // Thickness and colour only with a border (ImageStylePage::setNode).
            colorEnabled: page.value(page.read(MobileStyle.PictureBorder)) !== 0
        }
        StyleStepper {
            objectName: "pictureBorderWidthStepper"
            readonly property var reading: page.read(MobileStyle.PictureBorderWidth)
            label: qsTranslate("ImageStylePage", "Thickness (px)")
            value: page.value(reading)
            minimum: 1
            maximum: 6
            enabled: !reading.empty && page.value(page.read(MobileStyle.PictureBorder)) !== 0
            onStepped: delta => page.style.choose(MobileStyle.PictureBorderWidth, value + delta)
            onTyped: number => page.style.choose(MobileStyle.PictureBorderWidth, number)
        }
        PropertySwitch {
            objectName: "pictureShadowSwitch"
            text: qsTranslate("ImageStylePage", "Shadow")
            property: MobileStyle.PictureShadow
        }
        PropertySwitch {
            objectName: "pictureOutlineSwitch"
            text: qsTranslate("ImageStylePage", "Sticker outline")
            property: MobileStyle.PictureOutline
            visible: page.revision >= 0 && page.style.pictureIsSticker()
        }
        StyleStepper {
            objectName: "pictureOpacityStepper"
            readonly property var reading: page.read(MobileStyle.PictureOpacity)
            label: qsTranslate("ImageStylePage", "Opacity")
            value: page.value(reading)
            step: 10
            minimum: 0
            maximum: 100
            enabled: !reading.empty
            onStepped: delta => page.style.choose(MobileStyle.PictureOpacity, Math.max(minimum, Math.min(maximum, value + delta)))
            onTyped: number => page.style.choose(MobileStyle.PictureOpacity, number)
        }
        Rectangle { width: parent.width; height: 1; color: Theme.darkDivider }
        WideButton {
            objectName: "resetPictureStyleButton"
            text: qsTranslate("ImageStylePage", "Reset style")
            enabled: !picturePage.size.empty
            onClicked: page.style.resetPictureStyle()
        }
    }

    // FEAT-053: the relationship's page (RelationshipStylePage): Shape; Line (style, width, colour,
    // start and end); Text (font, size and colour).
    Column {
        id: relationPage
        objectName: "relationStylePage"
        width: parent.width
        visible: page.target === MobileStyle.Relation
        bottomPadding: 4 * Theme.grid
        Section {
            title: qsTranslate("RelationshipStylePage", "Shape")
            PropertySelect { objectName: "relationShapeSelect"; label: qsTranslate("RelationshipStylePage", "Shape"); property: MobileStyle.RelationShape }
        }
        Section {
            title: qsTranslate("RelationshipStylePage", "Line")
            PropertySelect { objectName: "relationPatternSelect"; label: qsTranslate("RelationshipStylePage", "Style"); property: MobileStyle.RelationPattern }
            SelectWithColor {
                objectName: "relationWidthRow"
                label: qsTranslate("RelationshipStylePage", "Width")
                property: MobileStyle.RelationWidth
                colorProperty: MobileStyle.RelationLineColor
                colorTitle: qsTranslate("RelationshipStylePage", "Line color")
            }
            PropertySelect { objectName: "relationStartSelect"; label: qsTranslate("StylePage", "Line start"); property: MobileStyle.RelationStart }
            PropertySelect { objectName: "relationEndSelect"; label: qsTranslate("StylePage", "Line end"); property: MobileStyle.RelationEnd }
        }
        Section {
            title: qsTranslate("RelationshipStylePage", "Text")
            PropertySelect { objectName: "relationFontSelect"; label: qsTranslate("RelationshipStylePage", "Font"); property: MobileStyle.RelationFont }
            SelectWithColor {
                objectName: "relationSizeRow"
                label: qsTranslate("RelationshipStylePage", "Size")
                property: MobileStyle.RelationFontSize
                colorProperty: MobileStyle.RelationTextColor
                colorTitle: qsTranslate("RelationshipStylePage", "Text color")
            }
        }
    }

    // FEAT-052: the boundary's page (BoundaryStylePage): Shape with the fill; Border (style, width,
    // colour).
    Column {
        id: boundaryPage
        objectName: "boundaryStylePage"
        width: parent.width
        visible: page.target === MobileStyle.BoundaryTarget
        bottomPadding: 4 * Theme.grid
        Section {
            title: qsTranslate("BoundaryStylePage", "Shape")
            PropertySelect { objectName: "boundaryShapeSelect"; label: qsTranslate("BoundaryStylePage", "Shape"); property: MobileStyle.BoundaryShape }
            // FEAT-134: the topics' fill styles beside the colour, as the topics' Fill row.
            SelectWithColor {
                objectName: "boundaryFillRow"
                label: qsTranslate("BoundaryStylePage", "Fill")
                property: MobileStyle.BoundaryFillStyle
                colorProperty: MobileStyle.BoundaryFillColor
                colorTitle: qsTranslate("BoundaryStylePage", "Fill color")
            }
        }
        Section {
            title: qsTranslate("BoundaryStylePage", "Border")
            PropertySelect { objectName: "boundaryPatternSelect"; label: qsTranslate("BoundaryStylePage", "Style"); property: MobileStyle.BoundaryPattern }
            SelectWithColor {
                objectName: "boundaryWidthRow"
                label: qsTranslate("BoundaryStylePage", "Width")
                property: MobileStyle.BoundaryWidth
                colorProperty: MobileStyle.BoundaryLineColor
                colorTitle: qsTranslate("BoundaryStylePage", "Line color")
            }
        }
        // FEAT-130: the title's text, as the relationships' Text section.
        Section {
            title: qsTranslate("BoundaryStylePage", "Title")
            PropertySelect { objectName: "boundaryTitleFontSelect"; label: qsTranslate("BoundaryStylePage", "Font"); property: MobileStyle.BoundaryTitleFont }
            SelectWithColor {
                objectName: "boundaryTitleSizeRow"
                label: qsTranslate("BoundaryStylePage", "Size")
                property: MobileStyle.BoundaryTitleSize
                colorProperty: MobileStyle.BoundaryTitleColor
                colorTitle: qsTranslate("BoundaryStylePage", "Text color")
            }
            PropertySwitch {
                objectName: "boundaryTitleBoldSwitch"
                text: qsTranslate("BoundaryStylePage", "Bold")
                property: MobileStyle.BoundaryTitleBold
            }
        }
    }
}
