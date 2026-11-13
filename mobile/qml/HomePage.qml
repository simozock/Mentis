import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import QtQuick.Dialogs
import QtQuick.Layouts
import Mentis.Mobile

// FEAT-097 stage A: the maps of the Mentis folder, newest first, and "Open file" (the system
// picker, a content:// URI on Android). FEAT-107: one folder level at a time as the desktop's Home
// (FEAT-105 decision 6), coloured folders first; Back (the button, Android's Back, the ".." row) goes
// up. The row's ⋮ opens its menu; a long press lifts the row at once and a drag moves the map or the
// folder onto a folder, onto ".." or onto Back.
Page {
    id: page
    required property MapLibrary library
    signal openRequested(url url)

    function showError(message) {
        errorText.text = message
        errorText.visible = message !== ""
    }

    // The Mentis folder could not be used (shared storage missing): where the maps go for now.
    Component.onCompleted: showError(library.lastError)

    // Inside a folder ".." first (the level above), then folders, then maps; each entry says what it is.
    readonly property string parentPath: page.library.atRoot ? "" : page.library.shown.substring(0, page.library.shown.lastIndexOf("/"))
    readonly property var entries: (page.library.atRoot ? [] : [{ kind: "up", name: "..", path: page.parentPath }])
                                       .concat(page.library.folders.map(folder => Object.assign({ kind: "folder" }, folder)))
                                       .concat(page.library.maps.map(map => Object.assign({ kind: "map" }, map)))
    property bool holding: false  // a row is held (long press): the list does not scroll
    readonly property bool covered: entryMenu.visible || nameDialog.visible || colorDialog.visible || moveDialog.visible

    // The result of an action on the library: its error, or none.
    function done(ok) { showError(ok ? "" : library.lastError) }

    // Dark as the desktop's Home (ui-foundations.md, "Interfaccia scura"): the page on
    // surfaceSunken, "Mentis" 20 px bold, "Open file" a primary button as the Home's "New map".
    background: Rectangle { color: Theme.darkSunken }

    header: Item {
        implicitHeight: Theme.touchTarget + 4 * Theme.grid
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: (page.library.atRoot ? 6 : 2) * Theme.grid
            anchors.rightMargin: 6 * Theme.grid
            spacing: 2 * Theme.grid
            // Back to the level above; a map or a folder dropped on it moves there.
            AbstractButton {
                id: upButton
                objectName: "folderUpButton"
                visible: !page.library.atRoot
                implicitWidth: Theme.touchTarget
                implicitHeight: Theme.touchTarget
                Accessible.name: qsTr("Back")
                onClicked: page.library.up()
                background: Rectangle {
                    anchors.centerIn: parent
                    width: Theme.groupButtonSize
                    height: Theme.groupButtonSize
                    radius: Theme.groupButtonRadius
                    color: upButton.down || upDrop.containsDrag ? Theme.darkDivider : "transparent"
                }
                contentItem: Item {
                    IconImage {
                        anchors.centerIn: parent
                        width: Theme.iconSize
                        height: Theme.iconSize
                        source: "qrc:/assets/hugeicons/arrow-left.svg"
                        sourceSize: Qt.size(Theme.iconSize * Screen.devicePixelRatio, Theme.iconSize * Screen.devicePixelRatio)
                        color: Theme.darkText
                    }
                }
                DropArea {
                    id: upDrop
                    objectName: "folderUpDrop"
                    anchors.fill: parent
                    keys: ["mentis/entry"]
                }
            }
            Label {
                objectName: "homeTitle"
                text: page.library.atRoot ? "Mentis" : page.library.shownName
                color: Theme.darkText
                font.pixelSize: Theme.subtitleSize
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            AbstractButton {
                id: newFolderButton
                objectName: "newFolderButton"
                implicitWidth: Theme.touchTarget
                implicitHeight: Theme.touchTarget
                Accessible.name: qsTr("New folder")
                onClicked: nameDialog.ask("", "")
                background: Rectangle {
                    anchors.centerIn: parent
                    width: Theme.buttonHeight
                    height: Theme.buttonHeight
                    radius: Theme.controlRadius
                    color: newFolderButton.down ? Theme.darkDivider : Theme.darkRaised
                    border.width: 1
                    border.color: Theme.darkDivider
                }
                contentItem: Item {
                    IconImage {
                        anchors.centerIn: parent
                        width: Theme.iconSize
                        height: Theme.iconSize
                        source: "qrc:/assets/hugeicons/plus.svg"
                        sourceSize: Qt.size(Theme.iconSize * Screen.devicePixelRatio, Theme.iconSize * Screen.devicePixelRatio)
                        color: Theme.darkText
                    }
                }
            }
            DarkButton {
                objectName: "openFileButton"
                text: qsTr("Open file")
                primary: true
                onClicked: picker.open()
            }
        }
    }

    // Android's Back in a folder goes up a level; at the top it is left to the system. A dialog or
    // the menu takes it first and closes.
    Shortcut {
        sequence: "Back"
        enabled: page.visible && !page.library.atRoot && !page.covered
        onActivated: page.library.up()
    }

    FileDialog {
        id: picker
        title: qsTr("Open a map")
        nameFilters: [qsTr("Mentis maps (*.mentis)")]
        onAccepted: page.openRequested(selectedFile)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Label {
            id: errorText
            objectName: "errorText"
            visible: false
            wrapMode: Text.Wrap
            color: Theme.danger
            font.pixelSize: Theme.bodySize
            Layout.fillWidth: true
            Layout.margins: 4 * Theme.grid
        }

        Label {
            visible: page.library.folders.length === 0 && page.library.maps.length === 0
            text: page.library.atRoot ? qsTr("No maps yet. Open a .mentis file to see it here.")
                                      : qsTr("No maps in this folder yet.")
            wrapMode: Text.Wrap
            color: Theme.darkTextMuted
            font.pixelSize: Theme.bodySize
            horizontalAlignment: Text.AlignHCenter
            Layout.fillWidth: true
            Layout.margins: 8 * Theme.grid
        }

        ListView {
            id: mapList
            objectName: "mapList"
            model: page.entries
            clip: true
            interactive: !page.holding
            Layout.fillWidth: true
            Layout.fillHeight: true
            leftMargin: 3 * Theme.grid
            rightMargin: 3 * Theme.grid
            // Rows as the Home's lists: pressed divider, radius 6 (HomeRow).
            delegate: Item {
                id: row
                required property var modelData
                readonly property bool isFolder: modelData.kind === "folder"
                readonly property bool isUp: modelData.kind === "up"
                // ".." and every folder take a dragged entry.
                readonly property bool takesDrop: isFolder || isUp
                objectName: isUp ? "upRow" : isFolder ? "folderRow" : "mapRow"
                width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
                implicitHeight: Math.max(Theme.touchTarget, content.implicitHeight + 4 * Theme.grid)
                height: implicitHeight

                function open() {
                    if (row.isUp) page.library.up()
                    else if (row.isFolder) page.library.enter(row.modelData.path)
                    else page.openRequested(row.modelData.url)
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.groupButtonRadius
                    color: drop.lit ? Theme.darkRaised
                         : area.pressed || area.containsMouse ? Theme.darkDivider : "transparent"
                    border.width: drop.lit ? 1 : 0
                    border.color: Theme.accent
                }

                RowLayout {
                    id: content
                    anchors.fill: parent
                    anchors.leftMargin: 3 * Theme.grid
                    anchors.rightMargin: row.isUp ? 3 * Theme.grid : Theme.touchTarget  // room for the ⋮
                    spacing: 3 * Theme.grid
                    FolderIcon {
                        visible: row.isFolder || row.isUp
                        color: row.isFolder ? row.modelData.color : ""
                        width: 32
                        height: 32
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                    }
                    ColumnLayout {
                        spacing: Theme.grid / 2
                        Layout.fillWidth: true
                        Label { text: row.modelData.name; color: Theme.darkText; font.pixelSize: Theme.largeSize; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            visible: row.modelData.kind === "map"
                            text: visible ? row.modelData.modified.toLocaleString(Qt.locale(), Locale.ShortFormat) : ""
                            color: Theme.darkTextMuted
                            font.pixelSize: Theme.captionSize
                        }
                    }
                }

                DropArea {
                    id: drop
                    anchors.fill: parent
                    enabled: row.takesDrop
                    keys: ["mentis/entry"]
                    // A folder is not lit under itself.
                    readonly property bool lit: containsDrag && ghost.entryPath !== row.modelData.path
                }

                // Tap: open. Long press: the row lifts at once under the finger; dropped on a folder,
                // on ".." or on Back, the entry moves there; anywhere else, nothing.
                MouseArea {
                    id: area
                    anchors.fill: parent
                    hoverEnabled: true
                    property bool dragging: false
                    preventStealing: dragging
                    onClicked: row.open()
                    onPressAndHold: mouse => {
                        if (row.isUp) return
                        dragging = true
                        page.holding = true
                        ghost.start(row.modelData)
                        ghost.follow(area, mouse.x, mouse.y)
                    }
                    // The rows made again mid-drag.
                    Component.onDestruction: if (dragging) { page.holding = false; ghost.cancel() }
                    onPositionChanged: mouse => { if (dragging) ghost.follow(area, mouse.x, mouse.y) }
                    onReleased: mouse => {
                        if (!dragging) return
                        dragging = false
                        page.holding = false
                        ghost.finish(area.mapToItem(mapList.contentItem, mouse.x, mouse.y), area.mapToItem(upButton, mouse.x, mouse.y))
                    }
                    onCanceled: {
                        if (!dragging) return
                        dragging = false
                        page.holding = false
                        ghost.cancel()
                    }
                }

                // The row's menu: a 48 px target at the right end, three 4 px dots.
                AbstractButton {
                    id: moreButton
                    objectName: "entryMoreButton"
                    visible: !row.isUp
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: Theme.touchTarget
                    Accessible.name: qsTr("More")
                    onClicked: entryMenu.openFor(row.modelData, row)
                    background: Rectangle {
                        anchors.centerIn: parent
                        width: Theme.groupButtonSize
                        height: Theme.groupButtonSize
                        radius: Theme.groupButtonRadius
                        color: moreButton.down ? Theme.darkDivider : "transparent"
                    }
                    contentItem: Item {
                        Column {
                            anchors.centerIn: parent
                            spacing: 3
                            Repeater {
                                model: 3
                                Rectangle { width: 4; height: 4; radius: 2; color: Theme.darkText }
                            }
                        }
                    }
                }
            }
        }
    }

    // The dragged map or folder under the finger, at 60% as a dragged topic (ui-mobile.md).
    Rectangle {
        id: ghost
        objectName: "entryGhost"
        property string entryPath
        property string entryKind
        visible: Drag.active
        z: 10
        width: Math.min(page.width - 8 * Theme.grid, 280)
        height: Theme.touchTarget
        radius: Theme.groupButtonRadius
        color: Theme.darkRaised
        opacity: 0.6
        Drag.keys: ["mentis/entry"]
        Drag.hotSpot.x: width / 2
        Drag.hotSpot.y: height / 2
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 3 * Theme.grid
            anchors.rightMargin: 3 * Theme.grid
            spacing: 3 * Theme.grid
            FolderIcon {
                id: ghostIcon
                visible: ghost.entryKind === "folder"
                width: 32
                height: 32
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
            }
            Label {
                id: ghostLabel
                color: Theme.darkText
                font.pixelSize: Theme.largeSize
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
        function start(entry) {
            entryPath = entry.path
            entryKind = entry.kind
            ghostIcon.color = entry.kind === "folder" ? entry.color : ""
            ghostLabel.text = entry.name
            Drag.active = true
        }
        function follow(item, x, y) {
            const p = item.mapToItem(ghost.parent, x, y)
            ghost.x = p.x - ghost.width / 2
            ghost.y = p.y - ghost.height / 2
        }
        // Dropped where the finger is: on Back or ".." (the level above) or on a folder's row. The
        // DropAreas only light the place up; the row under the finger decides.
        function finish(inList, onUp) {
            if (!Drag.active) return
            Drag.active = false
            let folder = ""
            if (upButton.visible && upButton.contains(onUp)) {
                folder = page.parentPath
            } else {
                const target = mapList.itemAt(inList.x, inList.y)
                if (target && target.takesDrop) folder = target.modelData.path
            }
            if (folder === "" || folder === entryPath) return
            page.done(entryKind === "folder" ? page.library.moveFolder(entryPath, folder) : page.library.moveMap(entryPath, folder))
        }
        function cancel() { Drag.active = false }
    }

    // The row's menu (its ⋮), dark as the marker menu (ui-mobile.md): folder: Open, Rename…, Colour…,
    // Move to…, Delete (only when empty); map: Open, Move to….
    Popup {
        id: entryMenu
        objectName: "entryMenu"
        property var entry: ({})
        property var actions: []
        padding: Theme.grid
        width: 220
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        function openFor(entry, item) {
            entryMenu.entry = entry
            entryMenu.actions = entry.kind === "folder"
                ? [{ id: "open", label: qsTr("Open"), enabled: true },
                   { id: "rename", label: qsTr("Rename…"), enabled: true },
                   { id: "color", label: qsTr("Color…"), enabled: true },
                   { id: "move", label: qsTr("Move to…"), enabled: true },
                   { id: "delete", label: qsTr("Delete"), enabled: page.library.isEmptyFolder(entry.path) }]
                : [{ id: "open", label: qsTr("Open"), enabled: true },
                   { id: "move", label: qsTr("Move to…"), enabled: true }]
            const p = item.mapToItem(page.contentItem, item.width - width, item.height)
            const menuHeight = actions.length * Theme.touchTarget + 2 * padding
            x = Math.max(Theme.grid, Math.min(p.x, page.width - width - Theme.grid))
            y = p.y + menuHeight <= page.contentItem.height - Theme.grid ? p.y : Math.max(Theme.grid, p.y - item.height - menuHeight)
            open()
        }
        function run(action) {
            close()
            const entry = entryMenu.entry
            if (action === "open") {
                if (entry.kind === "folder") page.library.enter(entry.path)
                else page.openRequested(entry.url)
            } else if (action === "rename") {
                nameDialog.ask(entry.path, entry.name)
            } else if (action === "color") {
                colorDialog.ask(entry.path, entry.color)
            } else if (action === "delete") {
                page.done(page.library.removeFolder(entry.path))
            } else if (action === "move") {
                moveDialog.ask(entry)
            }
        }

        background: Rectangle {
            radius: Theme.surfaceRadius
            color: Theme.darkSurface
            border.width: 1
            border.color: Theme.darkDivider
        }
        contentItem: Column {
            Repeater {
                model: entryMenu.actions
                ItemDelegate {
                    id: actionRow
                    required property var modelData
                    objectName: "entryAction_" + modelData.id
                    width: entryMenu.availableWidth
                    implicitHeight: Theme.touchTarget
                    enabled: modelData.enabled
                    padding: 0
                    leftPadding: 3 * Theme.grid
                    rightPadding: 3 * Theme.grid
                    contentItem: Label {
                        text: actionRow.modelData.label
                        color: actionRow.enabled ? Theme.darkText : Theme.darkTextMuted
                        font.pixelSize: Theme.bodySize
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle { radius: Theme.controlRadius; color: actionRow.down ? Theme.darkDivider : "transparent" }
                    onClicked: entryMenu.run(actionRow.modelData.id)
                }
            }
        }
    }

    // New folder (no path) or Rename… (the folder's path).
    ModalDialog {
        id: nameDialog
        objectName: "folderNameDialog"
        property string path
        anchors.centerIn: parent
        width: Math.min(page.width - 8 * Theme.grid, 400)
        function ask(path, name) {
            nameDialog.path = path
            nameDialog.title = path === "" ? qsTr("New folder") : qsTr("Rename folder")
            nameField.text = path === "" ? qsTr("New folder") : name
            open()
            nameField.selectAll()
            nameField.forceActiveFocus()
        }
        onAccepted: page.done(path === "" ? page.library.createFolder(nameField.text) : page.library.renameFolder(path, nameField.text))
        Column {
            width: nameDialog.availableWidth
            spacing: Theme.grid
            Label {
                text: qsTr("Name")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
            // The modals' field (ui-components.md, "Modali").
            TextField {
                id: nameField
                objectName: "folderNameField"
                width: parent.width
                height: Theme.touchTarget
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                verticalAlignment: TextInput.AlignVCenter
                leftPadding: 2 * Theme.grid
                rightPadding: 2 * Theme.grid
                selectByMouse: true
                onAccepted: nameDialog.accept()
                background: Item {
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: Theme.fieldHeight
                        radius: Theme.controlRadius
                        color: Theme.darkSunken
                        border.width: nameField.activeFocus ? 1 : 0
                        border.color: Theme.accent
                    }
                }
            }
        }
        buttons: [
            DarkButton { text: qsTr("Cancel"); onClicked: nameDialog.reject() },
            DarkButton {
                objectName: "folderNameAcceptButton"
                text: nameDialog.path === "" ? qsTr("Create") : qsTr("Rename")
                primary: true
                enabled: nameField.text.trim() !== ""
                onClicked: nameDialog.accept()
            }
        ]
    }

    // Colour…: the fixed palette, "No colour" first; a tap applies and closes.
    ModalDialog {
        id: colorDialog
        objectName: "folderColorDialog"
        property string path
        property string current
        title: qsTr("Folder color")
        anchors.centerIn: parent
        width: Math.min(page.width - 8 * Theme.grid, 400)
        function ask(path, color) { colorDialog.path = path; colorDialog.current = color; open() }
        Grid {
            id: swatches
            // From the page, not from the dialog's width: that loops through its implicit height.
            columns: Math.max(1, Math.floor((Math.min(page.width - 8 * Theme.grid, 400) - 12 * Theme.grid) / Theme.touchTarget))
            Repeater {
                model: page.library.folderColors
                AbstractButton {
                    id: swatch
                    required property var modelData
                    objectName: "folderColor_" + (modelData.color === "" ? "none" : modelData.color.substring(1))
                    width: Theme.touchTarget
                    height: Theme.touchTarget
                    Accessible.name: modelData.name
                    onClicked: { colorDialog.close(); page.done(page.library.setFolderColor(colorDialog.path, modelData.color)) }
                    contentItem: Item {
                        Rectangle {  // the chosen colour ringed
                            anchors.centerIn: parent
                            width: 36; height: 36; radius: 18
                            color: "transparent"
                            border.width: swatch.modelData.color === colorDialog.current ? 2 : 0
                            border.color: Theme.darkText
                        }
                        Rectangle {
                            anchors.centerIn: parent
                            width: 28; height: 28; radius: 14
                            color: swatch.modelData.color === "" ? "transparent" : swatch.modelData.color
                            border.width: swatch.modelData.color === "" ? 1.5 : 0
                            border.color: Theme.darkText
                            Rectangle {  // "No colour": a crossed circle
                                visible: swatch.modelData.color === ""
                                anchors.centerIn: parent
                                width: 1.5; height: 22
                                rotation: 45
                                color: Theme.darkText
                            }
                        }
                    }
                }
            }
        }
        buttons: [ DarkButton { text: qsTr("Cancel"); onClicked: colorDialog.reject() } ]
    }

    // Move to…: every folder of the tree; the folder the entry is in cannot be chosen, and a folder
    // is not offered itself or its subfolders.
    ModalDialog {
        id: moveDialog
        objectName: "moveMapDialog"
        property string path
        property string kind
        property var targets: []
        anchors.centerIn: parent
        width: Math.min(page.width - 8 * Theme.grid, 400)
        function ask(entry) {
            moveDialog.path = entry.path
            moveDialog.kind = entry.kind
            moveDialog.title = qsTr("Move \"%1\" to").arg(entry.name)
            moveDialog.targets = page.library.moveTargets(entry.path)
            open()
        }
        ListView {
            width: moveDialog.availableWidth
            height: Math.min(contentHeight, page.height / 2)
            clip: true
            model: moveDialog.targets
            delegate: ItemDelegate {
                id: target
                required property var modelData
                objectName: "moveTarget"
                width: ListView.view.width
                implicitHeight: Theme.touchTarget
                enabled: !modelData.here
                padding: 0
                leftPadding: 2 * Theme.grid
                rightPadding: 2 * Theme.grid
                contentItem: Label {
                    text: target.modelData.label
                    color: target.enabled ? Theme.darkText : Theme.darkTextMuted
                    font.pixelSize: Theme.bodySize
                    elide: Text.ElideMiddle
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { radius: Theme.controlRadius; color: target.down ? Theme.darkDivider : "transparent" }
                onClicked: {
                    moveDialog.close()
                    page.done(moveDialog.kind === "folder" ? page.library.moveFolder(moveDialog.path, target.modelData.path)
                                                           : page.library.moveMap(moveDialog.path, target.modelData.path))
                }
            }
        }
        buttons: [ DarkButton { text: qsTr("Cancel"); onClicked: moveDialog.reject() } ]
    }
}
