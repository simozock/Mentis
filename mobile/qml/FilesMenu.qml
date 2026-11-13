import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-114: a tap on a topic's paper clip, as the desktop's file list (MainWindow::showFilesMenu,
// FEAT-049): per file its name and size, then Open, Share and Remove; at the bottom Add files.
// The marker menu's look (darkSurface, 1 px darkDivider border, radius 8, 48 px rows); taller than
// the page, it scrolls. Open asks first for a program or a script (the desktop's question).
Popup {
    id: menu
    objectName: "filesMenu"
    required property MobileMap map
    signal addRequested()
    signal riskyOpenRequested(int index, string name)

    padding: Theme.grid
    width: 280
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    // x, y: the item point below the paper clip (MapCanvas.filesRequested), in `parent`'s frame;
    // the menu stays inside it.
    function openAt(x, y) {
        const files = menu.map.selectedFiles
        menu.x = Math.max(Theme.grid, Math.min(x - width / 2, parent.width - width - Theme.grid))
        // The rows' height from their count: the column is laid out only at the next polish.
        const wanted = (2 * files.length + 1) * Theme.touchTarget + files.length + 2 * padding
        menu.height = Math.min(wanted, parent.height - 2 * Theme.grid)
        const below = y + Theme.grid
        menu.y = below + menu.height <= parent.height - Theme.grid ? below
            : Math.max(Theme.grid, Math.min(y - menu.height - 6 * Theme.grid, parent.height - menu.height - Theme.grid))
        menu.open()
    }

    background: Rectangle {
        radius: Theme.surfaceRadius
        color: Theme.darkSurface
        border.width: 1
        border.color: Theme.darkDivider
    }

    component MenuRow: ItemDelegate {
        id: row
        property alias label: rowText.text
        implicitHeight: Theme.touchTarget
        padding: 0
        leftPadding: 3 * Theme.grid
        rightPadding: 3 * Theme.grid
        contentItem: Label {
            id: rowText
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: Theme.controlRadius
            color: row.down ? Theme.darkDivider : "transparent"
        }
    }

    contentItem: Flickable {
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: column
            width: parent.width
            Repeater {
                model: menu.map.selectedFiles
                Column {
                    id: file
                    required property var modelData
                    required property int index
                    width: column.width
                    // The file's name and size (the desktop's "name  (size)" header).
                    Item {
                        width: parent.width
                        height: Theme.touchTarget
                        Label {
                            objectName: "fileName"
                            anchors.left: parent.left
                            anchors.leftMargin: 3 * Theme.grid
                            anchors.right: sizeText.left
                            anchors.rightMargin: 2 * Theme.grid
                            anchors.verticalCenter: parent.verticalCenter
                            text: file.modelData.name
                            elide: Text.ElideMiddle
                            color: Theme.darkText
                            font.pixelSize: Theme.bodySize
                            font.bold: true
                        }
                        Label {
                            id: sizeText
                            anchors.right: parent.right
                            anchors.rightMargin: 3 * Theme.grid
                            anchors.verticalCenter: parent.verticalCenter
                            text: file.modelData.size
                            color: Theme.darkTextMuted
                            font.pixelSize: Theme.captionSize
                        }
                    }
                    Row {
                        width: parent.width
                        MenuRow {
                            objectName: "fileOpenButton"
                            width: parent.width / 3
                            label: qsTr("Open")
                            onClicked: {
                                menu.close()
                                if (menu.map.fileIsRisky(file.index)) menu.riskyOpenRequested(file.index, file.modelData.name)
                                else menu.map.openFile(file.index)
                            }
                        }
                        MenuRow {
                            objectName: "fileShareButton"
                            width: parent.width / 3
                            label: qsTr("Share")
                            onClicked: { menu.close(); menu.map.shareFile(file.index) }
                        }
                        MenuRow {
                            objectName: "fileRemoveButton"
                            width: parent.width / 3
                            label: qsTr("Remove")
                            onClicked: { menu.close(); menu.map.removeFile(file.index) }
                        }
                    }
                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.darkDivider
                    }
                }
            }
            MenuRow {
                objectName: "addFilesButton"
                width: column.width
                label: qsTr("Add files…")
                onClicked: { menu.close(); menu.addRequested() }
            }
        }
    }
}
