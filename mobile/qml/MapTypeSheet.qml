import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-072 B16 on the phone: the map type menu (the desktop's MapTypeSelect popup) in a dark modal
// titled "Map type": the variants grouped by type under 12 px semibold headers that fold (▾ / ▸),
// three in a row, each the grey sample map on white (radius 6), the sheet's one ringed 2 px in
// darkText. A tap chooses it (the sheet's type and style, one Undo step) and closes. FEAT-123: opened
// with openToCreate (the Home's New map), a tap emits created(variant) instead, nothing ringed; then
// only the X or Esc closes it (a tap outside does not).
ModalDialog {
    id: sheet
    objectName: "mapTypeSheet"
    property MobileStyle style: null
    readonly property int revision: style ? style.revision : -1
    readonly property string current: revision >= 0 && style && !creating ? style.mapVariant() : ""
    property var folded: ({})
    property bool creating: false
    signal created(string variant)
    // The list's room: the page's height less the 16 px margins, the title, the buttons and the gaps,
    // so the list never runs under the buttons on a short (landscape) page.
    readonly property real listRoom: (parent ? parent.height - 8 * Theme.grid : 600) - (header ? header.implicitHeight : 0)
                                     - (footer ? footer.implicitHeight : 0) - topPadding - bottomPadding - 2 * spacing

    title: qsTranslate("MapTypeSelect", "Map type")
    buttonsShown: !creating
    closePolicy: creating ? Popup.CloseOnEscape : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 480)
    height: Math.min(parent ? parent.height - 8 * Theme.grid : 0, implicitHeight)

    function openFor(style) {
        sheet.style = style
        sheet.creating = false
        sheet.open()
    }

    // style: any MobileStyle, for the list of the types only.
    function openToCreate(style) {
        sheet.style = style
        sheet.creating = true
        sheet.open()
    }

    Flickable {
        id: list
        width: sheet.availableWidth
        height: Math.max(0, Math.min(contentHeight, sheet.listRoom))
        contentHeight: groups.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: groups
            width: list.width
            spacing: 2 * Theme.grid
            Repeater {
                model: sheet.style ? sheet.style.mapTypes() : []
                Column {
                    id: group
                    required property var modelData
                    readonly property bool open: sheet.folded[modelData.type] !== true
                    width: groups.width
                    spacing: 2 * Theme.grid
                    AbstractButton {
                        objectName: "mapTypeGroup-" + group.modelData.type
                        width: parent.width
                        height: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        Accessible.name: group.modelData.name
                        onClicked: {
                            const next = Object.assign({}, sheet.folded)
                            next[group.modelData.type] = group.open
                            sheet.folded = next
                        }
                        background: Item {}
                        contentItem: Label {
                            text: (group.open ? "▾ " : "▸ ") + group.modelData.name
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.darkText
                            font.pixelSize: 12
                            font.weight: Font.DemiBold
                        }
                    }
                    Grid {
                        id: tiles
                        visible: group.open
                        columns: 3
                        spacing: 2 * Theme.grid
                        readonly property real tileWidth: (group.width - 2 * spacing) / 3
                        Repeater {
                            model: group.modelData.variants
                            AbstractButton {
                                id: tile
                                required property string modelData
                                required property int index
                                objectName: "mapTypeTile-" + modelData
                                width: tiles.tileWidth
                                height: Math.round(width * 54 / 82)  // the desktop's 78 x 50 thumbnail in its 82 x 54 tile
                                focusPolicy: Qt.NoFocus
                                Accessible.name: group.modelData.name + " " + (index + 1)
                                onClicked: {
                                    sheet.close()
                                    if (sheet.creating) sheet.created(modelData)
                                    else sheet.style.chooseMapVariant(modelData)
                                }
                                background: Rectangle {
                                    color: "white"
                                    radius: 6
                                    border.width: 2
                                    border.color: tile.modelData === sheet.current ? Theme.darkText : "transparent"
                                }
                                contentItem: Item {
                                    VariantSample {
                                        anchors.fill: parent
                                        anchors.margins: 2
                                        mono: true
                                        variant: tile.modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    buttons: [
        DarkButton {
            objectName: "mapTypeCloseButton"
            text: qsTranslate("ColorSheet", "Done")
            primary: true
            onClicked: sheet.close()
        }
    ]
}
