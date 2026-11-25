import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-119: the choices of a side panel select (ui-mobile.md, "Side panel"), with the marker menu's
// look (darkSurface, 1 px darkDivider, radius 8, padding 4). Icon choices are the desktop's grid
// menus: 48 px cells (the 36 x 22 icon), as many columns as fit, each group under its 12 px title;
// the "none" choice last. Text choices are 48 px rows with the 14 px name. The current one is
// on darkRaised. It opens above its row when the room above is larger (the bottom half panel), else
// below, inside 16 px from the page's edges, and scrolls when taller than the room. Modal with a
// transparent overlay: a tap outside only closes it; a choice closes it and is picked.
Popup {
    id: menu
    objectName: "styleChoiceMenu"
    property Item row: null
    readonly property var choices: row ? row.choices : []
    readonly property bool icons: row ? row.iconKind >= 0 : false
    readonly property bool images: row ? row.imageGrid : false
    readonly property bool presets: row ? row.presetPreview : false
    readonly property int cellSize: Theme.touchTarget
    readonly property int columns: icons ? Math.max(1, Math.floor((width - 2 * padding) / (cellSize + Theme.grid))) : 1

    padding: Theme.grid
    modal: true
    Overlay.modal: Item {}
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // Where it opens: the row's top and bottom on the page and the room on its larger side. The
    // height follows the content, which is laid out after the row is set.
    property real rowTop: 0
    property real rowBottom: 0
    property real room: 0
    property bool above: false
    height: Math.min(content.implicitHeight + 2 * padding, room)
    y: above ? rowTop - Theme.grid - height : rowBottom + Theme.grid

    function openFor(target) {
        menu.row = target
        const margin = 4 * Theme.grid
        const page = parent
        menu.width = Math.min(page.width - 2 * margin, icons ? 7 * (cellSize + Theme.grid) + 2 * padding : images ? 320 : 280)
        menu.rowTop = target.mapToItem(page, 0, 0).y
        menu.rowBottom = menu.rowTop + target.height
        const roomAbove = menu.rowTop - margin - Theme.grid
        const roomBelow = page.height - menu.rowBottom - margin - Theme.grid
        menu.above = roomAbove > roomBelow
        menu.room = Math.max(Theme.touchTarget + 2 * padding, Math.max(roomAbove, roomBelow))
        const right = target.mapToItem(page, target.width, 0).x
        menu.x = Math.max(margin, Math.min(right - menu.width, page.width - menu.width - margin))
        menu.open()
    }
    function pick(value) {
        const target = menu.row
        menu.close()
        target.picked(value)
    }
    onClosed: row = null

    background: Rectangle {
        radius: Theme.surfaceRadius
        color: Theme.darkSurface
        border.width: 1
        border.color: Theme.darkDivider
    }
    contentItem: Flickable {
        clip: true
        contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: content
            width: parent.width
            // Icon choices: a Flow per group, the title over it.
            Repeater {
                model: menu.icons ? menu.groups() : []
                Column {
                    required property var modelData
                    width: content.width
                    Label {
                        visible: modelData.title !== ""
                        height: visible ? 6 * Theme.grid : 0
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: Theme.grid
                        text: modelData.title
                        color: Theme.darkTextMuted
                        font.pixelSize: Theme.captionSize
                        font.weight: Font.DemiBold
                    }
                    Flow {
                        width: parent.width
                        spacing: Theme.grid
                        Repeater {
                            model: modelData.choices
                            AbstractButton {
                                id: cell
                                required property var modelData
                                readonly property bool current: menu.row && !menu.row.mixed && menu.row.value === modelData.value
                                objectName: "choice" + modelData.value
                                width: menu.cellSize
                                height: menu.cellSize
                                focusPolicy: Qt.NoFocus
                                Accessible.name: modelData.name
                                onClicked: menu.pick(modelData.value)
                                background: Rectangle {
                                    radius: Theme.controlRadius
                                    color: cell.down ? Theme.darkDivider : cell.current ? Theme.darkRaised : "transparent"
                                }
                                contentItem: Item {
                                    StyleIcon {
                                        anchors.centerIn: parent
                                        width: 36
                                        height: 22
                                        kind: menu.row ? menu.row.iconKind : -1
                                        value: cell.modelData.value
                                        ink: Theme.darkText
                                        muted: Theme.darkTextMuted
                                    }
                                }
                            }
                        }
                    }
                }
            }
            // FEAT-026: the structures as the desktop's labelled tiles, two columns: thumbnail 48 x 32 and
            // the 12 px name under it.
            Grid {
                columns: 2
                width: parent.width
                Repeater {
                    model: menu.images ? menu.choices : []
                    AbstractButton {
                        id: tile
                        required property var modelData
                        readonly property bool current: menu.row && !menu.row.mixed && menu.row.value === modelData.value
                        objectName: "choice" + modelData.value
                        width: content.width / 2
                        height: 64 + Theme.grid
                        focusPolicy: Qt.NoFocus
                        Accessible.name: modelData.name
                        onClicked: menu.pick(modelData.value)
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: tile.down ? Theme.darkDivider : tile.current ? Theme.darkRaised : "transparent"
                        }
                        contentItem: Column {
                            topPadding: Theme.grid
                            spacing: Theme.grid
                            Image {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 48
                                height: 32
                                source: tile.modelData.image
                                sourceSize: Qt.size(48 * Screen.devicePixelRatio, 32 * Screen.devicePixelRatio)
                            }
                            Label {
                                width: tile.width
                                horizontalAlignment: Text.AlignHCenter
                                elide: Text.ElideRight
                                text: tile.modelData.name
                                color: Theme.darkText
                                font.pixelSize: Theme.captionSize
                            }
                        }
                    }
                }
            }
            // Text choices: 48 px rows; FEAT-020 the presets as their preview pills.
            Repeater {
                model: menu.icons || menu.images ? [] : menu.choices
                ItemDelegate {
                    id: item
                    required property var modelData
                    readonly property bool current: menu.row && !menu.row.mixed && menu.row.value === modelData.value
                    objectName: "choice" + modelData.value
                    width: content.width
                    implicitHeight: Theme.touchTarget
                    leftPadding: 3 * Theme.grid
                    rightPadding: modelData.saved === true ? Theme.touchTarget : 3 * Theme.grid
                    enabled: modelData.enabled !== false
                    Accessible.name: modelData.name
                    onClicked: menu.pick(modelData.value)
                    contentItem: Item {
                        Label {
                            anchors.fill: parent
                            visible: !menu.presets
                            text: item.modelData.name
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.darkText
                            opacity: item.enabled ? 1 : 0.4
                            font.pixelSize: Theme.bodySize
                            font.bold: item.current
                        }
                        StyleIcon {
                            visible: menu.presets
                            anchors.centerIn: parent
                            width: 176
                            height: 30
                            preset: menu.presets ? item.modelData.value : -1
                            presetName: item.modelData.name
                            presetColor: menu.row ? menu.row.presetColor : "#ff6b6b"
                            presetDepth: menu.row ? menu.row.presetDepth : 1
                            themeColors: menu.row ? menu.row.themeColors : []
                        }
                    }
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: item.down ? Theme.darkDivider : item.current ? Theme.darkRaised : "transparent"
                    }
                    // FEAT-122 C1: a choice saved on this device (the desktop's saved currencies) has an ×
                    // at its right that removes it, without choosing it; the list stays open.
                    AbstractButton {
                        objectName: "choiceRemove" + item.modelData.value
                        visible: item.modelData.saved === true
                        anchors.right: parent.right
                        width: Theme.touchTarget
                        height: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTranslate("ResourcesDialog", "Remove")
                        onClicked: if (menu.row) menu.row.removed(item.modelData.value)
                        contentItem: Label {
                            text: "×"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: Theme.darkTextMuted
                            font.pixelSize: Theme.largeSize
                        }
                    }
                }
            }
        }
    }

    // The icon choices split at their group titles (the "none" choice is the last, as on the desktop).
    function groups() {
        const list = []
        for (const choice of menu.choices) {
            if (choice.group || list.length === 0) list.push({ title: choice.group || "", choices: [] })
            list[list.length - 1].choices.push(choice)
        }
        return list
    }
}
