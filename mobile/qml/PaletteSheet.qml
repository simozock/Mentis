import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-031: the colour theme menu (the desktop's PalettePicker) in a dark modal: Favourites |
// Colourful | Classic | Custom, then the themes in two columns, each with its star, its name and the
// strip of its colours; the sheet's one ringed 2 px in darkText. A tap on a theme chooses it (one
// Undo step) and closes; a tap on its star stars it or not (Favourites, kept in the app's settings).
// A long press on an own theme offers to remove it (FEAT-032, confirmed: it is not an Undo step). A
// sheet theme this phone does not have can be added to the own ones.
ModalDialog {
    id: sheet
    objectName: "paletteSheet"
    property MobileStyle style: null
    property int group: 1
    readonly property int revision: style ? style.revision : -1
    readonly property var current: revision >= 0 ? style.sheet() : ({})
    readonly property var palettes: revision >= 0 ? style.palettes(sheet.group) : []
    // The list's room: the page's height less the 16 px margins, the title, the buttons, the group
    // control, the empty text or the add button and the gaps, so it never runs under the buttons.
    readonly property real listRoom: (parent ? parent.height - 8 * Theme.grid : 600) - (header ? header.implicitHeight : 0)
                                     - (footer ? footer.implicitHeight : 0) - topPadding - bottomPadding - 2 * spacing
                                     - groupControl.height - 2 * Theme.grid - (addCurrent.visible ? addCurrent.height + 2 * Theme.grid : 0)
    // FEAT-032: a long press on an own theme asks the panel to confirm its removal.
    signal removeRequested(string id, string name)

    title: qsTranslate("MainWindow", "Color theme")
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 480)
    height: Math.min(parent ? parent.height - 8 * Theme.grid : 0, implicitHeight)

    function openFor(style) {
        sheet.style = style
        sheet.group = style.paletteGroup(style.sheet().paletteId)
        sheet.open()
    }

    Column {
        width: sheet.availableWidth
        spacing: 2 * Theme.grid
        DarkSegments {
            id: groupControl
            objectName: "paletteGroups"
            width: parent.width
            segments: [{ text: qsTranslate("PalettePicker", "Favorites") }, { text: qsTranslate("PalettePicker", "Colorful") },
                       { text: qsTranslate("PalettePicker", "Classic") }, { text: qsTranslate("PalettePicker", "Custom") }]
            current: sheet.group
            onChosen: index => sheet.group = index
        }
        Label {
            visible: sheet.palettes.length === 0
            width: parent.width
            wrapMode: Text.Wrap
            text: sheet.group === 0 ? qsTranslate("PalettePicker", "Click the star of a palette to add it here.")
                                    : qsTranslate("PalettePicker", "The palettes you create with + will appear here.")
            color: Theme.darkTextMuted
            font.pixelSize: 12
        }
        Flickable {
            id: list
            width: parent.width
            height: Math.max(0, Math.min(contentHeight, sheet.listRoom))
            contentHeight: grid.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Grid {
                id: grid
                columns: 2
                spacing: Theme.grid
                readonly property real tileWidth: (list.width - spacing) / 2
                Repeater {
                    model: sheet.palettes
                    AbstractButton {
                        id: tile
                        required property var modelData
                        readonly property bool chosen: modelData.id === sheet.current.paletteId
                        objectName: "paletteTile-" + modelData.id
                        width: grid.tileWidth
                        height: Theme.touchTarget + 28
                        focusPolicy: Qt.NoFocus
                        Accessible.name: modelData.name
                        onClicked: {
                            sheet.style.choosePalette(modelData.id)
                            sheet.close()
                        }
                        onPressAndHold: if (modelData.custom) sheet.removeRequested(modelData.id, modelData.name)
                        background: Rectangle { color: tile.down ? Theme.darkDivider : "transparent"; radius: Theme.controlRadius }
                        contentItem: Item {
                            AbstractButton {
                                id: star
                                objectName: "paletteStar"
                                width: 10 * Theme.grid
                                height: Theme.touchTarget
                                focusPolicy: Qt.NoFocus
                                Accessible.name: tile.modelData.name
                                Accessible.checkable: true
                                Accessible.checked: tile.modelData.favorite
                                onClicked: sheet.style.setFavoritePalette(tile.modelData.id, !tile.modelData.favorite)
                                background: Item {}
                                contentItem: Item {
                                    // PaletteTile's star: yellow when favourite, else an outline.
                                    Canvas {
                                        anchors.left: parent.left
                                        anchors.leftMargin: Theme.grid
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 16
                                        height: 16
                                        readonly property bool on: tile.modelData.favorite
                                        onOnChanged: requestPaint()
                                        onPaint: {
                                            const ctx = getContext("2d")
                                            ctx.reset()
                                            ctx.beginPath()
                                            for (let i = 0; i < 10; ++i) {
                                                const r = i % 2 === 0 ? 7.5 : 7.5 * 0.45
                                                const a = -Math.PI / 2 + i * Math.PI / 5
                                                const px = 8 + r * Math.cos(a), py = 8 + r * Math.sin(a)
                                                if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
                                            }
                                            ctx.closePath()
                                            if (on) { ctx.fillStyle = "#f5c518"; ctx.fill() }
                                            ctx.lineWidth = 1.2
                                            ctx.strokeStyle = on ? "#f5b301" : Theme.darkTextMuted
                                            ctx.stroke()
                                        }
                                    }
                                }
                            }
                            Label {
                                anchors.left: star.right
                                anchors.right: parent.right
                                height: Theme.touchTarget
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                                text: tile.modelData.name
                                color: Theme.darkText
                                font.pixelSize: 12
                                font.weight: tile.chosen ? Font.DemiBold : Font.Normal
                            }
                            Rectangle {
                                // The current theme: its strip ringed 2 px.
                                visible: tile.chosen
                                x: strip.x - 3
                                y: strip.y - 3
                                width: strip.width + 6
                                height: strip.height + 6
                                radius: 6
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.darkText
                            }
                            PaletteStrip {
                                id: strip
                                x: Theme.grid
                                y: Theme.touchTarget
                                width: parent.width - 2 * Theme.grid
                                height: 20
                                colors: tile.modelData.colors
                            }
                        }
                    }
                }
            }
        }
        DarkButton {
            id: addCurrent
            objectName: "addCurrentPalette"
            visible: sheet.current.notInstalled === true
            width: parent.width
            text: qsTranslate("PalettePicker", "Add \"%1\" to my palettes").arg(sheet.current.paletteSavedName || "")
            onClicked: sheet.style.addCurrentPalette()
        }
    }

    buttons: [
        DarkButton {
            text: qsTranslate("ColorSheet", "Done")
            primary: true
            onClicked: sheet.close()
        }
    ]
}
