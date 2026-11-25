import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-119 stage B: the side panel's Map page, the desktop's Map page in its order: the map type card
// (B2: the sheet's variant as a grey sample map and the type's name; a tap opens the types), the colour theme
// (its strip and name, the menu of themes), the six variants, the background colour; the global font,
// the branch line width, Colored branch and its Colour; then the groups Map style (Balance map,
// Compact map), Topic display (Justify, Show all notes), Numbering (the central topic's) and Advanced
// (Free branch positions); Reset map style at the bottom. Every choice is one Undo step (MobileStyle).
// The texts are the desktop's (contexts MainWindow, SheetStylePanel, VariantPicker).
Column {
    id: page
    required property MobileStyle style
    required property StyleChoiceMenu menu
    required property ColorSheet colorSheet
    required property PaletteSheet paletteSheet
    required property MapTypeSheet mapTypeSheet
    required property NewPaletteSheet newPaletteSheet
    property bool foldable: false
    property var openSections: ({})
    signal sectionToggled(string name)

    readonly property int revision: style.revision
    readonly property var sheet: revision >= 0 ? style.sheet() : ({})
    readonly property bool open: sheet.paletteId !== undefined
    function sectionOpen(name) { return page.openSections[name] !== false }

    spacing: 0
    bottomPadding: 4 * Theme.grid

    component Section: StyleSection {
        foldable: page.foldable
        open: page.sectionOpen(title)
        onToggled: page.sectionToggled(title)
    }
    // A 12 px darkTextMuted caption above a field, as the desktop's FieldCaption.
    component Caption: TipLabel {
        width: parent ? parent.width : 0
        topPadding: 2 * Theme.grid
        color: Theme.darkTextMuted
        font.pixelSize: 12
    }
    component Divider: Rectangle {
        width: parent ? parent.width : 0
        height: 1
        color: Theme.darkDivider
    }
    // A sheet toggle (SheetStylePanel's ToggleSwitch rows): not toggled by the tap, the sheet's value shown.
    component SheetSwitch: DarkSwitch {
        required property string key
        width: parent ? parent.width : 0
        checkable: false
        enabled: page.open
        checked: page.sheet[key] === true
        onClicked: page.style.chooseSheet(key, !checked)
    }
    // A colour of the sheet, the desktop's ColorField: a 28 px square in a 48 px target.
    component SheetColorRow: Item {
        id: colorRow
        required property int property
        property alias label: caption.text
        readonly property var reading: page.revision >= 0 ? page.style.reading(property) : ({ empty: true })
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        opacity: enabled ? 1 : 0.4
        TipLabel {
            id: caption
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        AbstractButton {
            id: swatch
            anchors.right: parent.right
            width: Theme.touchTarget
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            Accessible.name: colorRow.label
            onClicked: page.colorSheet.openFor(colorRow.property, colorRow.label, colorRow.reading)
            background: Item {}
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    radius: Theme.controlRadius
                    color: colorRow.reading.empty ? Theme.darkSunken : colorRow.reading.value
                    border.width: 1
                    border.color: swatch.down ? Theme.darkText : Theme.darkDivider
                }
            }
        }
    }

    // FEAT-072 B16: the map type card, 62 px high, radius 8, a 1 px darkDivider border: the 77 x 46
    // sample on white (radius 4), the 13 px type name, the chevron.
    AbstractButton {
        id: mapTypeCard
        objectName: "mapTypeCard"
        readonly property string variant: page.revision >= 0 ? page.style.mapVariant() : ""
        width: parent.width
        height: 62
        enabled: page.open
        focusPolicy: Qt.NoFocus
        Accessible.name: qsTranslate("MapTypeSelect", "Map type") + ": " + typeName.text
        onClicked: page.mapTypeSheet.openFor(page.style)
        background: Rectangle {
            radius: 8
            color: mapTypeCard.down ? Theme.darkDivider : "transparent"
            border.width: 1
            border.color: Theme.darkDivider
        }
        contentItem: Item {
            Rectangle {
                id: thumb
                x: 2 * Theme.grid
                anchors.verticalCenter: parent.verticalCenter
                width: 77
                height: 46
                radius: 4
                color: "white"
                VariantSample {
                    objectName: "mapTypeCardSample"
                    anchors.fill: parent
                    mono: true
                    variant: mapTypeCard.variant
                }
            }
            Label {
                id: typeName
                anchors.left: thumb.right
                anchors.leftMargin: 3 * Theme.grid
                anchors.right: parent.right
                anchors.rightMargin: Theme.smallIconSize + 3 * Theme.grid
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: page.revision >= 0 ? page.style.mapTypeName(mapTypeCard.variant) : ""
                color: Theme.darkText
                font.pixelSize: 13
            }
            Image {
                anchors.right: parent.right
                anchors.rightMargin: 2 * Theme.grid
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.smallIconSize
                height: Theme.smallIconSize
                source: "qrc:/assets/hugeicons/arrow-down.svg"
                sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
                opacity: 0.6
            }
        }
    }
    Item { width: 1; height: 3 * Theme.grid }
    Divider {}

    // FEAT-031: the colour theme, its strip and name; a tap opens the themes' menu.
    Caption { text: qsTranslate("MainWindow", "Color theme") }
    // FEAT-032: "+" beside the theme opens "Create new color theme".
    Item {
        width: parent.width
        height: Theme.touchTarget
        AbstractButton {
            id: paletteRow
            objectName: "paletteSelect"
            width: parent.width - newPalette.width - 2 * Theme.grid
            height: Theme.touchTarget
            enabled: page.open
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTranslate("MainWindow", "Color theme") + ": " + (page.sheet.paletteName || "")
            onClicked: page.paletteSheet.openFor(page.style)
            background: Rectangle { color: paletteRow.down ? Theme.darkDivider : "transparent"; radius: Theme.controlRadius }
            contentItem: Item {
                PaletteStrip {
                    id: strip
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 72
                    height: 24
                    colors: page.sheet.paletteColors || []
                }
                Label {
                    anchors.left: strip.right
                    anchors.leftMargin: 2 * Theme.grid
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.smallIconSize + 2 * Theme.grid
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideRight
                    text: page.sheet.paletteName || ""
                    color: Theme.darkText
                    font.pixelSize: Theme.bodySize
                }
                Image {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.smallIconSize
                    height: Theme.smallIconSize
                    source: "qrc:/assets/hugeicons/arrow-down.svg"
                    sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
                    opacity: 0.6
                }
            }
        }
        AbstractButton {
            id: newPalette
            objectName: "newPaletteButton"
            anchors.right: parent.right
            width: Theme.touchTarget
            height: Theme.touchTarget
            enabled: page.open
            focusPolicy: Qt.NoFocus
            Accessible.name: qsTranslate("MainWindow", "Create new color theme")
            onClicked: page.newPaletteSheet.openFor(page.style)
            background: Item {
                // MainWindow's NewPaletteButton: a 24 px square, radius 4, 1 px divider border.
                Rectangle {
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    radius: Theme.controlRadius
                    color: newPalette.down ? Theme.darkDivider : Theme.darkRaised
                    border.width: 1
                    border.color: Theme.darkDivider
                }
            }
            contentItem: Item {
                IconImage {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    color: newPalette.enabled ? Theme.darkText : Theme.darkTextMuted
                    source: "qrc:/assets/hugeicons/menu-plus.svg"
                    sourceSize: Qt.size(16 * Screen.devicePixelRatio, 16 * Screen.devicePixelRatio)
                }
            }
        }
    }
    // FEAT-036: the variants, the backdrop taken from a colour of the theme, three in a row.
    Grid {
        id: variants
        objectName: "variantGrid"
        columns: 3
        spacing: Theme.grid
        readonly property real tileWidth: (page.width - 2 * spacing) / 3
        Repeater {
            model: page.open ? Math.max(0, page.sheet.variantCount) : 0
            AbstractButton {
                id: tile
                required property int index
                objectName: "variantTile" + index
                width: variants.tileWidth
                height: Math.round(width * 46 / 73)  // VariantPicker's 73 x 46 proportions
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTranslate("VariantPicker", "Background %1").arg(index + 1)
                onClicked: page.style.chooseVariant(index)
                background: Rectangle {
                    color: "transparent"
                    radius: 6
                    border.width: page.sheet.variant === tile.index ? 2 : 1
                    border.color: page.sheet.variant === tile.index ? Theme.darkText : Theme.darkDivider
                }
                contentItem: Item {
                    VariantThumbnail {
                        anchors.fill: parent
                        anchors.margins: 3
                        paletteId: page.sheet.paletteId || ""
                        colors: page.sheet.paletteColors || []
                        variant: tile.index
                    }
                }
            }
        }
    }
    Item { width: 1; height: 2 * Theme.grid }
    Divider {}
    SheetColorRow {
        objectName: "backgroundColorRow"
        property: MobileStyle.SheetBackground
        label: qsTranslate("MainWindow", "Background color")
        enabled: page.open
    }
    Divider {}

    // SheetStylePanel: the global font and the branch line width, captions above.
    Caption { text: qsTranslate("SheetStylePanel", "Global font") }
    StyleSelectRow {
        objectName: "sheetFontSelect"
        menu: page.menu
        choices: page.revision >= 0 ? page.style.sheetFonts() : []
        value: page.sheet.fontFamily
        enabled: page.open
        onPicked: v => page.style.chooseSheet("fontFamily", v)
    }
    Caption { text: qsTranslate("SheetStylePanel", "Branch line width") }
    StyleSelectRow {
        objectName: "sheetWidthSelect"
        menu: page.menu
        choices: page.revision >= 0 ? page.style.sheetWidths() : []
        value: page.sheet.width
        enabled: page.open
        onPicked: v => page.style.chooseSheet("width", v)
    }
    SheetSwitch {
        objectName: "coloredBranchSwitch"
        key: "coloredBranches"
        text: qsTranslate("SheetStylePanel", "Colored branch")
    }
    SheetColorRow {
        objectName: "branchLineColorRow"
        property: MobileStyle.SheetLineColor
        label: qsTranslate("SheetStylePanel", "Color")
        enabled: page.open && page.sheet.coloredBranches !== true  // only without Colored branch
    }

    Section {
        title: qsTranslate("SheetStylePanel", "Map style")
        DarkSwitch {
            objectName: "balanceSwitch"
            width: parent.width
            text: qsTranslate("MainWindow", "Balance map")
            checkable: false
            enabled: page.sheet.balanceEnabled === true
            checked: page.sheet.balanced === true
            onClicked: page.style.chooseBalanced(!checked)
        }
        SheetSwitch { objectName: "compactSwitch"; key: "compact"; text: qsTranslate("SheetStylePanel", "Compact map") }
    }
    Section {
        title: qsTranslate("SheetStylePanel", "Topic display")
        SheetSwitch { objectName: "justifySwitch"; key: "justify"; text: qsTranslate("SheetStylePanel", "Justify topic alignment") }
        SheetSwitch { objectName: "showNotesSwitch"; key: "showNotes"; text: qsTranslate("SheetStylePanel", "Show all notes") }
    }
    Section {
        title: qsTranslate("SheetStylePanel", "Numbering")
        NumberingFields {
            objectName: "sheetNumbering"
            style: page.style
            menu: page.menu
            revision: page.revision
            sheet: true
        }
    }
    Section {
        title: qsTranslate("SheetStylePanel", "Advanced")
        SheetSwitch { objectName: "freeBranchesSwitch"; key: "freeBranches"; text: qsTranslate("SheetStylePanel", "Free branch positions") }
    }
    Divider {}
    DarkButton {
        objectName: "resetMapStyleButton"
        width: parent.width
        text: qsTranslate("SheetStylePanel", "Reset map style")
        enabled: page.sheet.canReset === true
        onClicked: page.style.resetMapStyle()
    }
}
