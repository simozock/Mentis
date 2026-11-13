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
// picture from the system's picker into the first own collection and the topic. The collections are
// read when the modal opens or a segment is chosen; the list makes only the rows on screen.
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
                    }
                }
            }
        }
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
        if (picked.small) {
            smallDialog.question = picked.smallReason
            smallDialog.open()
        } else {
            nameDialog.open()
        }
    }
    function addNamed(path, name) {
        if (!picker.map.addToLibrary(path, name, picker.markers)) return
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
            DarkButton { text: qsTr("Cancel"); onClicked: smallDialog.reject() },
            DarkButton { text: qsTr("Add"); primary: true; onClicked: smallDialog.accept() }
        ]
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
        }
        buttons: [
            DarkButton { text: qsTr("Cancel"); onClicked: nameDialog.reject() },
            DarkButton { objectName: "nameAddButton"; text: qsTr("Add"); primary: true; onClicked: nameDialog.accept() }
        ]
    }

    buttons: [
        DarkButton {
            objectName: "addToLibraryButton"
            text: qsTr("Add")
            onClicked: if (!photoPicker.open()) filePicker.open()
        },
        DarkButton {
            text: qsTr("Cancel")
            onClicked: picker.reject()
        }
    ]
}
