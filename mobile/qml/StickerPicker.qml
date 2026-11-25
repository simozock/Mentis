import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Mentis.Mobile

// FEAT-097: markers and stickers for the selected topic, the desktop's panel with its Marker and
// Sticker pages (FEAT-051, IconPage) as a dark modal on two segments. Each collection under its
// name; stickers in cells of 56 px with 4 px between them (IconPage::kStickerCellPx,
// kCellSpacingPx), markers in 48 px cells (the phone's touch target, ui-mobile.md) with the marker
// drawn at 28 px and the topic's markers on darkDivider. A tap on a sticker puts it in the topic; a
// tap on a marker gives it to the topic or takes it off; either closes the modal. Add takes a
// picture from the system's picker into the own group or collection chosen with its name (or a new
// one, as the desktop's New group / New collection) and the topic. The collections are
// read when the modal opens or a segment is chosen; the list makes only the rows on screen.
// FEAT-070: a long press on an SVG sticker offers Edit…, the sticker editor (StickerEditorDialog);
// its copy goes into the library and the list is read again.
ModalDialog {
    id: picker
    objectName: "stickerPicker"
    required property MobileMap map
    // The segment shown: true = Marker, false = Sticker. openOn() sets it before opening.
    property bool markers: false
    title: qsTr("Markers and stickers")
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 480)
    height: parent ? parent.height - 16 * Theme.grid : 0

    readonly property int cell: markers ? Theme.touchTarget : 56
    readonly property int markerSize: 28
    readonly property int cellSpacing: Theme.grid
    // Why the last added file was not added (the desktop's "File not added"), until the next Add.
    property string addError: ""
    // The collection choice's value for a new own collection (ids are never empty).
    readonly property string newCollection: "new"

    function openOn(markerPage) {
        picker.markers = markerPage
        picker.open()
    }
    function reload() { collections.model = library.collections() }

    StickerLibrary {
        id: library
        markers: picker.markers
        onMarkersChanged: if (picker.opened) picker.reload()
    }
    onAboutToShow: { picker.addError = ""; picker.reload() }
    onClosed: collections.model = []

    // The desktop's SegmentedControl (ui-window.md) in the dark tones: track 32, radius 8, 1 px
    // border, padding 4; segments 24 high, radius 4, 12 px text, the active one raised at weight 600.
    Item {
        id: segments
        width: picker.availableWidth
        height: Theme.touchTarget
        Rectangle {
            id: track
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: Theme.groupButtonSize
            radius: Theme.surfaceRadius
            color: Theme.darkSurface
            border.width: 1
            border.color: Theme.darkDivider
        }
        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.grid
            anchors.rightMargin: Theme.grid
            Repeater {
                model: [qsTr("Marker"), qsTr("Sticker")]
                AbstractButton {
                    id: segment
                    required property int index
                    required property string modelData
                    readonly property bool active: (index === 0) === picker.markers
                    objectName: index === 0 ? "markerSegment" : "stickerSegment"
                    width: (segments.width - 2 * Theme.grid) / 2
                    height: segments.height
                    text: modelData
                    onClicked: picker.markers = index === 0
                    background: Rectangle {
                        anchors.centerIn: parent
                        width: parent.width
                        height: 6 * Theme.grid
                        radius: Theme.controlRadius
                        color: segment.active ? Theme.darkRaised : segment.down || segment.hovered ? Theme.darkDivider : "transparent"
                    }
                    contentItem: Label {
                        text: segment.text
                        color: Theme.darkText
                        font.pixelSize: Theme.captionSize
                        font.bold: segment.active
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    Label {
        id: errorLine
        objectName: "addErrorText"
        anchors.top: segments.bottom
        width: picker.availableWidth
        visible: picker.addError !== ""
        height: visible ? implicitHeight + 2 * Theme.grid : 0
        text: picker.addError
        wrapMode: Text.Wrap
        color: Theme.danger
        font.pixelSize: Theme.captionSize
    }

    ListView {
        id: collections
        objectName: "stickerCollections"
        anchors.top: errorLine.bottom
        anchors.topMargin: 2 * Theme.grid
        width: picker.availableWidth
        // The content item's height less the segments: a Dialog's availableHeight also holds the
        // title and the buttons, so the list ran under Cancel and past the modal (user's photo
        // 2026-11-10).
        height: picker.contentItem.height - y
        clip: true
        spacing: 4 * Theme.grid
        delegate: Column {
            id: collection
            required property var modelData
            width: ListView.view.width
            spacing: 2 * Theme.grid
            Label {
                text: collection.modelData.name
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
                font.bold: true
            }
            Flow {
                width: parent.width
                spacing: picker.cellSpacing
                Repeater {
                    model: collection.modelData.items
                    AbstractButton {
                        id: cellButton
                        required property var modelData
                        readonly property bool active: picker.markers && picker.map.selectedMarkers.indexOf(modelData.marker) >= 0
                        objectName: picker.markers ? "marker" : "sticker"
                        width: picker.cell
                        height: picker.cell
                        padding: picker.markers ? (picker.cell - picker.markerSize) / 2 : Theme.grid
                        Accessible.name: modelData.name
                        background: Rectangle {
                            radius: Theme.controlRadius
                            color: cellButton.active || cellButton.down ? Theme.darkDivider : cellButton.hovered ? Theme.darkRaised : "transparent"
                        }
                        contentItem: Loader {
                            sourceComponent: picker.markers ? markerIcon : stickerImage
                            Component {
                                id: markerIcon
                                MarkerIcon { marker: cellButton.modelData.marker }
                            }
                            Component {
                                id: stickerImage
                                Image {
                                    source: cellButton.modelData.source
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit
                                    sourceSize: Qt.size(picker.cell * Screen.devicePixelRatio, picker.cell * Screen.devicePixelRatio)
                                }
                            }
                        }
                        onClicked: {
                            if (picker.markers)
                                picker.map.toggleMarker(modelData.path, modelData.name, collection.modelData.id)
                            else
                                picker.map.insertSticker(modelData.path)
                            picker.close()
                        }
                        // FEAT-070: a long press on an SVG sticker, as the desktop's right click on it.
                        // A connected pressAndHold skips the release's click (QQuickAbstractButton).
                        onPressAndHold: {
                            if (picker.markers || !modelData.path.toLowerCase().endsWith(".svg")) {
                                clicked()
                                return
                            }
                            const at = cellButton.mapToItem(itemMenu.parent, cellButton.width / 2, cellButton.height)
                            itemMenu.openAt(modelData.path, collection.modelData.id, at.x, at.y)
                        }
                    }
                }
            }
        }
    }

    // FEAT-070: the long press's menu on an SVG sticker (IconPage::showItemMenu's Edit…), with the
    // marker menu's look; Edit… opens the editor over this modal.
    Popup {
        id: itemMenu
        objectName: "stickerItemMenu"
        property string path
        property string collectionId
        parent: Overlay.overlay
        padding: Theme.grid
        width: 220
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        function openAt(path, collectionId, x, y) {
            itemMenu.path = path
            itemMenu.collectionId = collectionId
            itemMenu.x = Math.max(Theme.grid, Math.min(x - width / 2, parent.width - width - Theme.grid))
            itemMenu.y = Math.min(y + Theme.grid, parent.height - Theme.touchTarget - 2 * padding - Theme.grid)
            itemMenu.open()
        }
        background: Rectangle {
            radius: Theme.surfaceRadius
            color: Theme.darkSurface
            border.width: 1
            border.color: Theme.darkDivider
        }
        contentItem: ItemDelegate {
            objectName: "stickerEditItem"
            implicitHeight: Theme.touchTarget
            padding: 0
            leftPadding: 3 * Theme.grid
            rightPadding: 3 * Theme.grid
            contentItem: Label {
                text: qsTranslate("IconPage", "Edit…")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: Theme.controlRadius
                color: parent.down ? Theme.darkDivider : "transparent"
            }
            onClicked: {
                itemMenu.close()
                picker.addError = ""
                if (!stickerEditor.openOn(itemMenu.path, itemMenu.collectionId))
                    picker.addError = qsTranslate("IconPage", "This SVG cannot be edited: the app cannot draw it.")
            }
        }
    }

    StickerEditorDialog {
        id: stickerEditor
        parent: Overlay.overlay
        onSaved: (canDeleteOriginal, originalName) => {
            picker.reload()
            if (!canDeleteOriginal) return
            deleteOriginalDialog.question = qsTranslate("IconPage", "The new sticker is saved. Delete the original \"%1\" from its collection?").arg(originalName)
            deleteOriginalDialog.open()
        }
    }

    ModalDialog {
        id: deleteOriginalDialog
        objectName: "deleteOriginalDialog"
        property string question
        title: qsTranslate("IconPage", "Delete the original?")
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(picker.width, 400)
        onAccepted: if (stickerEditor.editor.deleteOriginal()) picker.reload()
        Label {
            width: deleteOriginalDialog.availableWidth
            text: deleteOriginalDialog.question
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton { objectName: "deleteOriginalButton"; text: qsTranslate("IconPage", "Delete"); primary: true; onClicked: deleteOriginalDialog.accept() }
        ]
    }

    // Add: as IconPage::addFiles for one file. Checked (a refused file says why here, a small one
    // asks), then named, then into the library and the topic.
    function addPicked(url) {
        const picked = picker.map.pickLibraryFile(url, picker.markers)
        if (!picked.ok) { picker.addError = picked.reason; return }
        picker.addError = ""
        nameDialog.path = picked.path
        nameDialog.title = picked.title
        nameField.text = picked.name
        // The first own collection, or a new one when there is none (IconPage::ensureOwnCollection).
        const own = picker.map.ownCollections(picker.markers)
        collectionRow.choices = own.concat([{ value: picker.newCollection, name: picker.markers ? qsTr("New group…") : qsTr("New collection…") }])
        collectionRow.value = own.length > 0 ? own[0].value : picker.newCollection
        newCollectionField.text = own.length > 0 ? "" : picker.markers ? qsTr("My markers") : qsTr("My stickers")
        if (picked.small) {
            smallDialog.question = picked.smallReason
            smallDialog.open()
        } else {
            nameDialog.open()
        }
    }
    function addNamed(path, name) {
        const target = collectionRow.value === picker.newCollection ? "" : collectionRow.value
        if (!picker.map.addToLibrary(path, name, picker.markers, target, newCollectionField.text)) return
        picker.close()  // as a tap on a cell
    }
    ImagePicker {
        id: photoPicker
        onPicked: url => picker.addPicked(url)
    }
    FileDialog {
        id: filePicker
        title: picker.markers ? qsTr("Add markers") : qsTr("Add stickers")
        nameFilters: ["SVG, PNG (*.svg *.png)"]
        onAccepted: picker.addPicked(selectedFile)
    }

    ModalDialog {
        id: smallDialog
        objectName: "smallImageDialog"
        property string question
        title: qsTr("Small image")
        anchors.centerIn: parent
        width: Math.min(picker.width, 400)
        onAccepted: nameDialog.open()
        Label {
            width: smallDialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            text: smallDialog.question
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton { text: qsTr("Add"); primary: true; onClicked: smallDialog.accept() }
        ]
    }

    StyleChoiceMenu {
        id: collectionMenu
        objectName: "collectionMenu"  // not the side panel's
        parent: Overlay.overlay
    }

    // ModalDialog::askText: the name, prefilled with the file's; confirmed empty = the file's name.
    ModalDialog {
        id: nameDialog
        objectName: "nameDialog"
        property string path
        anchors.centerIn: parent
        width: Math.min(picker.width, 400)
        onAccepted: picker.addNamed(path, nameField.text)
        Column {
            width: nameDialog.availableWidth
            spacing: Theme.grid
            Label {
                text: qsTr("Name")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
            // The modals' field (ui-components.md, "Modali"): 30 high, radius 4, sunken, no border
            // but the accent's while it has the focus; 48 px touch target.
            TextField {
                id: nameField
                objectName: "nameField"
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
            // The own group (markers) or collection (stickers) it goes into, as the desktop's Add
            // from file… on that collection; "New group…" names a new one below.
            StyleSelectRow {
                id: collectionRow
                objectName: "collectionSelect"
                label: picker.markers ? qsTr("Group") : qsTr("Collection")
                menu: collectionMenu
                onPicked: v => collectionRow.value = v
            }
            TextField {
                id: newCollectionField
                objectName: "newCollectionField"
                visible: collectionRow.value === picker.newCollection
                width: parent.width
                height: Theme.touchTarget
                placeholderText: picker.markers ? qsTr("Group name") : qsTr("Collection name")
                placeholderTextColor: Theme.darkTextMuted
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
                        border.width: newCollectionField.activeFocus ? 1 : 0
                        border.color: Theme.accent
                    }
                }
            }
        }
        buttons: [
            DarkButton { objectName: "nameAddButton"; text: qsTr("Add"); primary: true; onClicked: nameDialog.accept() }
        ]
    }

    buttons: [
        DarkButton {
            objectName: "addToLibraryButton"
            text: qsTr("Add")
            onClicked: if (!photoPicker.open()) filePicker.open()
        }
    ]
}
