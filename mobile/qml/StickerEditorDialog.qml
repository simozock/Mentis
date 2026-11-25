import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-070 on the phone (FEAT-097): the desktop's sticker editor (StickerEditorDialog) as a dark
// modal, opened by a long press on an SVG sticker in the stickers modal > Edit…. The same parts in
// one column that scrolls: the tools (48 px targets), the chosen tool's options or buttons, the
// preview (StickerCanvasView, a drag there edits and does not scroll), Undo and Redo, the three
// pages Style / Frame / Text, then Save as. The finger stands for the mouse; what the desktop does
// with keys has a button here: Shift (a switch), Enter / Backspace / Esc of the pen, Delete and Esc
// of the points, and "Edit points" beside the double tap. The file name is the desktop class's, so
// both share their texts and translations. The X asks before discarding changes, as on the desktop.
ModalDialog {
    id: dialog
    objectName: "stickerEditor"
    title: qsTr("Edit sticker")
    anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 480)
    height: parent ? parent.height - 8 * Theme.grid : 0
    closePolicy: Popup.NoAutoClose  // the X, Esc and Back ask first (askClose)
    canClose: () => dialog.askClose()

    readonly property alias editor: stickerEdit
    // The page shown under the preview: 0 Style, 1 Frame, 2 Text.
    property int page: 0
    // The collection choice's value for a new collection (ids are never empty).
    readonly property string newCollection: "new"
    property string saveError: ""
    // After saving an own sticker: the picker asks whether to delete the original.
    signal saved(bool canDeleteOriginal, string originalName)

    function openOn(path, collectionId) {
        if (!stickerEdit.open(path, collectionId)) return false
        dialog.page = 0
        dialog.saveError = ""
        nameField.text = stickerEdit.originalName
        collectionRow.value = stickerEdit.collectionId
        newCollectionField.text = ""
        labelField.text = ""
        dialog.open()
        return true
    }
    // false: the editor stays open (a question first, or nothing to close).
    function askClose() {
        stickerEdit.settle()
        if (!stickerEdit.modified) return true
        discardDialog.open()
        return false
    }
    function save() {
        const target = collectionRow.value === dialog.newCollection ? "" : collectionRow.value
        const result = stickerEdit.save(nameField.text, target, newCollectionField.text)
        if (!result.ok) { dialog.saveError = result.reason; return }
        const original = stickerEdit.originalName
        dialog.accept()
        dialog.saved(result.canDeleteOriginal, original)
    }

    MobileStickerEditor {
        id: stickerEdit
        onChanged: if (labelField.text.trim() !== labelText && !labelField.activeFocus) labelField.text = labelText
    }

    Shortcut {
        sequences: [StandardKey.Cancel, "Back"]
        enabled: dialog.opened && !discardDialog.opened && !colorSheet.opened && !choiceMenu.opened
        onActivated: if (dialog.askClose()) dialog.reject()
    }

    MobileStyle { id: colors }  // the colour sheet's swatches and recent colours
    StyleChoiceMenu {
        id: choiceMenu
        objectName: "stickerChoiceMenu"  // not the side panel's
        parent: Overlay.overlay
    }
    ColorSheet {
        id: colorSheet
        objectName: "stickerColorSheet"
        parent: Overlay.overlay
        style: colors
    }

    ModalDialog {
        id: discardDialog
        objectName: "discardStickerDialog"
        title: qsTr("Discard changes?")
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(dialog.width, 400)
        onAccepted: dialog.reject()
        Label {
            width: discardDialog.availableWidth
            text: qsTr("The changes to this sticker will be lost.")
            wrapMode: Text.Wrap
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton { objectName: "discardButton"; text: qsTr("Discard"); primary: true; onClicked: discardDialog.accept() }
        ]
    }

    // A 48 px target with a 32 px icon button (the desktop's kIconButtonPx), raised, accent when on.
    component IconButton: AbstractButton {
        id: iconButton
        property string glyph
        property bool on: false
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        Accessible.name: text
        background: Item {
            Rectangle {
                anchors.centerIn: parent
                width: Theme.buttonHeight
                height: Theme.buttonHeight
                radius: Theme.controlRadius
                color: iconButton.on ? Theme.accent : iconButton.down ? Theme.darkDivider : Theme.darkRaised
                border.width: iconButton.on ? 0 : 1
                border.color: Theme.darkDivider
            }
        }
        contentItem: Item {
            opacity: iconButton.enabled ? 1 : 0.4
            IconImage {
                anchors.centerIn: parent
                width: Theme.iconSize
                height: Theme.iconSize
                source: "qrc:/assets/hugeicons/editor-" + iconButton.glyph + ".svg"
                color: Theme.darkText
                sourceSize: Qt.size(Theme.iconSize * Screen.devicePixelRatio, Theme.iconSize * Screen.devicePixelRatio)
            }
        }
    }
    // A colour of a control: a 28 px swatch in a 48 px target, dashed when there is none.
    component ColorButton: AbstractButton {
        id: colorButton
        property color color: "transparent"
        property bool none: false
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        Accessible.name: text
        opacity: enabled ? 1 : 0.4
        background: Item {
            Rectangle {
                anchors.centerIn: parent
                width: 28
                height: 28
                radius: Theme.controlRadius
                color: colorButton.none ? "transparent" : colorButton.color
                border.width: 1
                border.color: colorButton.down ? Theme.accent : Theme.darkDivider
            }
            Rectangle {  // "none": a diagonal line, as an empty colour field
                visible: colorButton.none
                anchors.centerIn: parent
                width: 1
                height: 36
                rotation: 45
                color: Theme.darkTextMuted
            }
        }
    }
    // A label / control row of a page, as the desktop's rows (label left, controls right).
    component Row48: Item {
        id: row
        property string label
        default property alias controls: controlRow.data
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        TipLabel {
            anchors.left: parent.left
            anchors.right: controlRow.left
            anchors.rightMargin: 2 * Theme.grid
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: row.label
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        Row {
            id: controlRow
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
        }
    }
    component Caption: Label {
        width: parent ? parent.width : 0
        topPadding: 2 * Theme.grid
        color: Theme.darkTextMuted
        font.pixelSize: Theme.captionSize
        font.bold: true
    }
    // The modals' field (ui-components.md, "Modali"): 30 high, radius 4, sunken, the accent's border
    // while it has the focus; 48 px touch target.
    component Field: TextField {
        id: field
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        color: Theme.darkText
        placeholderTextColor: Theme.darkTextMuted
        font.pixelSize: Theme.bodySize
        verticalAlignment: TextInput.AlignVCenter
        leftPadding: 2 * Theme.grid
        rightPadding: 2 * Theme.grid
        selectByMouse: true
        background: Item {
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: Theme.fieldHeight
                radius: Theme.controlRadius
                color: Theme.darkSunken
                border.width: field.activeFocus ? 1 : 0
                border.color: Theme.accent
            }
        }
    }

    Flickable {
        id: list
        objectName: "stickerEditorList"
        width: dialog.availableWidth
        height: dialog.contentItem.height
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // A drag on the preview edits the sticker; the list scrolls everywhere else.
        interactive: !canvas.pressed

        Column {
            id: column
            width: list.width
            spacing: 2 * Theme.grid

            Caption { text: qsTr("Tools") }
            // Select and Frame, then the drawing tools (the desktop's order and tips).
            Flow {
                id: tools
                width: parent.width
                Repeater {
                    model: [
                        { icon: "select", name: "StickerShapesMode", tip: qsTr("Select: move and resize the shapes; double click to edit the points") },
                        { icon: "crop", name: "StickerFrameMode", tip: qsTr("Frame: drag the edges to crop") },
                        { icon: "rectangle", name: "StickerRectangleTool", tip: qsTr("Rectangle (Shift: square)") },
                        { icon: "ellipse", name: "StickerEllipseTool", tip: qsTr("Ellipse (Shift: circle)") },
                        { icon: "line", name: "StickerLineTool", tip: qsTr("Line (Shift: steps of 45°)") },
                        { icon: "pen", name: "StickerPenTool", tip: qsTr("Pen: click for a corner, drag for a curve; double click or Enter ends, a click on the first point closes") },
                        { icon: "polygon", name: "StickerPolygonTool", tip: qsTr("Polygon, from its centre (Shift: upright)") },
                        { icon: "star", name: "StickerStarTool", tip: qsTr("Star, from its centre (Shift: upright)") }
                    ]
                    AbstractButton {
                        id: tool
                        required property int index
                        required property var modelData
                        readonly property bool active: stickerEdit.mode === index
                        objectName: modelData.name
                        width: Theme.touchTarget
                        height: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        Accessible.name: modelData.tip
                        Accessible.checkable: true
                        Accessible.checked: active
                        onClicked: stickerEdit.mode = index
                        // The desktop's 40 px tool buttons: sunken, accent when chosen.
                        background: Item {
                            Rectangle {
                                anchors.centerIn: parent
                                width: 40
                                height: 40
                                radius: Theme.groupButtonRadius
                                color: tool.active ? Theme.accent : tool.down ? Theme.darkDivider : Theme.darkSunken
                            }
                        }
                        contentItem: Item {
                            IconImage {
                                anchors.centerIn: parent
                                width: 22
                                height: 22
                                source: "qrc:/assets/hugeicons/editor-" + tool.modelData.icon + ".svg"
                                color: Theme.darkText
                                sourceSize: Qt.size(22 * Screen.devicePixelRatio, 22 * Screen.devicePixelRatio)
                            }
                        }
                    }
                }
            }

            // The chosen tool's options, or the buttons that stand for the desktop's keys.
            Column {
                id: options
                width: parent.width
                readonly property int mode: stickerEdit.mode
                Label {
                    objectName: "stickerToolHint"
                    width: parent.width
                    visible: text !== ""
                    wrapMode: Text.Wrap
                    color: Theme.darkTextMuted
                    font.pixelSize: Theme.captionSize
                    text: stickerEdit.penActive ? qsTr("Tap for corners, drag for curves; a tap on the first point closes the path")
                        : stickerEdit.pointEditing ? qsTr("Drag the points and their handles; a tap on the outline adds a point")
                        : options.mode === 0 ? qsTr("Tap a shape to select it, double tap to edit its points")
                        : options.mode === 1 ? qsTr("Drag the frame's edges to crop the sticker")
                        : options.mode === 5 ? qsTr("Tap for corners, drag for curves; a tap on the first point closes the path")
                        : ""
                }
                DarkSwitch {
                    objectName: "stickerShift"
                    width: parent.width
                    visible: options.mode !== 1 && !stickerEdit.pointEditing && !stickerEdit.penActive
                    text: options.mode === 0 ? qsTr("Add to the selection, keep the proportions") : qsTr("Keep the proportions, steps of 45°")
                    checked: stickerEdit.shift
                    onToggled: stickerEdit.shift = checked
                }
                StyleStepper {
                    objectName: "stickerCornerRadius"
                    visible: options.mode === 2
                    label: qsTr("Corners (px)")
                    value: stickerEdit.cornerRadius
                    minimum: 0
                    maximum: 128
                    onStepped: delta => stickerEdit.cornerRadius = stickerEdit.cornerRadius + delta
                    onTyped: number => stickerEdit.cornerRadius = number
                }
                StyleStepper {
                    objectName: "stickerSides"
                    visible: options.mode === 6
                    label: qsTr("Sides")
                    value: stickerEdit.sides
                    minimum: 3
                    maximum: 12
                    onStepped: delta => stickerEdit.sides = stickerEdit.sides + delta
                    onTyped: number => stickerEdit.sides = number
                }
                StyleStepper {
                    objectName: "stickerPoints"
                    visible: options.mode === 7
                    label: qsTr("Points")
                    value: stickerEdit.points
                    minimum: 3
                    maximum: 12
                    onStepped: delta => stickerEdit.points = stickerEdit.points + delta
                    onTyped: number => stickerEdit.points = number
                }
                Flow {
                    width: parent.width
                    spacing: 2 * Theme.grid
                    visible: children.length > 0
                    DarkButton {
                        objectName: "stickerEditPoints"
                        visible: options.mode === 0 && !stickerEdit.pointEditing && stickerEdit.selectionCount === 1
                        text: qsTr("Edit points")
                        onClicked: stickerEdit.editSelectedPoints()
                    }
                    DarkButton {
                        objectName: "stickerDeletePoint"
                        visible: stickerEdit.pointEditing
                        enabled: stickerEdit.chosenPoint
                        text: qsTr("Delete point")
                        onClicked: stickerEdit.removeChosenPoint()
                    }
                    DarkButton {
                        objectName: "stickerPointsDone"
                        visible: stickerEdit.pointEditing
                        text: qsTranslate("ColorSheet", "Done")
                        onClicked: stickerEdit.endPointEditing()
                    }
                    DarkButton {
                        objectName: "stickerPenFinish"
                        visible: stickerEdit.penActive
                        text: qsTr("Finish")
                        onClicked: stickerEdit.finishPen(false)
                    }
                    DarkButton {
                        objectName: "stickerPenBack"
                        visible: stickerEdit.penActive
                        text: qsTr("Remove last point")
                        onClicked: stickerEdit.removeLastPenPoint()
                    }
                    DarkButton {
                        objectName: "stickerPenDiscard"
                        visible: stickerEdit.penActive
                        text: qsTr("Discard path")
                        onClicked: stickerEdit.cancelPen()
                    }
                }
            }

            StickerCanvasView {
                id: canvas
                objectName: "stickerCanvas"
                width: parent.width
                height: width
                editor: stickerEdit
                raisedColor: Theme.darkRaised
                sunkenColor: Theme.darkSunken
                surfaceColor: Theme.darkSurface
                accentColor: Theme.accent
            }

            Row {
                spacing: 0
                IconButton {
                    objectName: "stickerUndo"
                    glyph: "undo"
                    text: qsTr("Undo")
                    enabled: stickerEdit.canUndo
                    onClicked: stickerEdit.undo()
                }
                IconButton {
                    objectName: "stickerRedo"
                    glyph: "redo"
                    text: qsTr("Redo")
                    enabled: stickerEdit.canRedo
                    onClicked: stickerEdit.redo()
                }
            }

            DarkSegments {
                objectName: "stickerPages"
                width: parent.width
                segments: [{ text: qsTr("Style") }, { text: qsTr("Frame") }, { text: qsTr("Text") }]
                current: dialog.page
                onChosen: index => dialog.page = index
            }

            // Style: the drawing's colours, the selected or next shape's fill and outline, the order.
            Column {
                width: parent.width
                visible: dialog.page === 0
                Caption { text: qsTr("Colors of the drawing") }
                Flow {
                    width: parent.width
                    Repeater {
                        model: stickerEdit.colors
                        ColorButton {
                            required property int index
                            required property var modelData
                            objectName: "stickerSwatch"
                            text: qsTr("Color %1").arg(index + 1)
                            color: modelData
                            onClicked: {
                                // Each pick replaces the previous one (the custom panel stays open):
                                // followed by value, as the order of the colours can change.
                                let current = modelData
                                colorSheet.openWith(text, current, chosen => {
                                    stickerEdit.replaceColor(current, chosen)
                                    current = chosen
                                })
                            }
                        }
                    }
                }
                Row48 {
                    label: qsTr("Fill")
                    DarkSegments {
                        objectName: "stickerFillKind"
                        width: 140
                        segments: [{ text: qsTr("Color") }, { text: qsTr("None") }]
                        current: stickerEdit.fillNone ? 1 : 0
                        onChosen: index => stickerEdit.setFillNone(index === 1)
                    }
                    ColorButton {
                        objectName: "stickerFillColor"
                        text: qsTr("Fill")
                        enabled: !stickerEdit.fillNone
                        color: stickerEdit.fillColor
                        none: stickerEdit.fillColor.a === 0
                        onClicked: colorSheet.openWith(qsTr("Fill"), stickerEdit.fillColor, c => stickerEdit.setFillColor(c))
                    }
                }
                Row48 {
                    label: qsTr("Outline")
                    ColorButton {
                        objectName: "stickerStrokeColor"
                        text: qsTr("Outline")
                        color: stickerEdit.strokeColor
                        none: stickerEdit.strokeColor.a === 0
                        onClicked: colorSheet.openWith(qsTr("Outline"), stickerEdit.strokeColor.a > 0 ? stickerEdit.strokeColor : "",
                                                       c => stickerEdit.setStrokeColor(c))
                    }
                }
                StyleStepper {
                    objectName: "stickerStrokeWidth"
                    label: qsTr("Outline width (px)")
                    value: stickerEdit.strokeWidth
                    step: 0.5
                    decimals: 1
                    minimum: 0
                    maximum: 40
                    onStepped: delta => stickerEdit.setStrokeWidth(Math.max(0, Math.min(40, stickerEdit.strokeWidth + delta)))
                    onTyped: number => stickerEdit.setStrokeWidth(number)
                }
                Row48 {
                    label: qsTr("Selected shapes")
                    IconButton { glyph: "bring-forward"; text: qsTr("Bring forward"); enabled: stickerEdit.selectionCount > 0; onClicked: stickerEdit.reorder(0) }
                    IconButton { glyph: "send-backward"; text: qsTr("Send backward"); enabled: stickerEdit.selectionCount > 0; onClicked: stickerEdit.reorder(1) }
                    IconButton { glyph: "bring-to-front"; text: qsTr("Bring to front"); enabled: stickerEdit.selectionCount > 0; onClicked: stickerEdit.reorder(2) }
                    IconButton { glyph: "send-to-back"; text: qsTr("Send to back"); enabled: stickerEdit.selectionCount > 0; onClicked: stickerEdit.reorder(3) }
                    IconButton {
                        objectName: "stickerDeleteShapes"
                        glyph: "delete"
                        text: qsTr("Delete")
                        enabled: stickerEdit.selectionCount > 0
                        onClicked: stickerEdit.removeSelection()
                    }
                }
            }

            // Frame: turns, flips, the angle, fitting and a margin; the edges are dragged with Frame.
            Column {
                width: parent.width
                visible: dialog.page === 1
                Row48 {
                    label: qsTr("Rotate")
                    IconButton { objectName: "stickerRotateLeft"; glyph: "rotate-left"; text: qsTr("Rotate left"); onClicked: stickerEdit.rotateBy(-90) }
                    IconButton { objectName: "stickerRotateRight"; glyph: "rotate-right"; text: qsTr("Rotate right"); onClicked: stickerEdit.rotateBy(90) }
                }
                Row48 {
                    label: qsTr("Flip")
                    IconButton { objectName: "stickerFlipH"; glyph: "flip-horizontal"; text: qsTr("Flip horizontally"); onClicked: stickerEdit.flipHorizontally() }
                    IconButton { objectName: "stickerFlipV"; glyph: "flip-vertical"; text: qsTr("Flip vertically"); onClicked: stickerEdit.flipVertically() }
                }
                StyleStepper {
                    objectName: "stickerAngle"
                    label: qsTr("Angle (°)")
                    value: Math.round(stickerEdit.rotation)
                    minimum: -180
                    maximum: 180
                    onStepped: delta => stickerEdit.setRotation(Math.round(stickerEdit.rotation) + delta)
                    onTyped: number => stickerEdit.setRotation(number)
                }
                Row48 {
                    label: qsTr("Fit to the drawing")
                    DarkButton { objectName: "stickerFit"; text: qsTr("Fit"); onClicked: stickerEdit.fitToDrawing() }
                }
                StyleStepper {
                    objectName: "stickerMargin"
                    label: qsTr("Margin (%)")
                    value: stickerEdit.margin
                    minimum: 1
                    maximum: 50
                    onStepped: delta => stickerEdit.margin = stickerEdit.margin + delta
                    onTyped: number => stickerEdit.margin = number
                }
                Row48 {
                    label: ""
                    DarkButton { objectName: "stickerAddMargin"; text: qsTr("Add"); onClicked: stickerEdit.addMargin() }
                }
            }

            // Text: one label on top of the drawing, dragged on the preview.
            Column {
                width: parent.width
                visible: dialog.page === 2
                spacing: Theme.grid
                Field {
                    id: labelField
                    objectName: "stickerLabelText"
                    placeholderText: qsTr("A word or a number")
                    onTextEdited: stickerEdit.typeLabelText(text)
                    onEditingFinished: stickerEdit.settle()
                }
                StyleSelectRow {
                    objectName: "stickerLabelFont"
                    label: qsTr("Font")
                    menu: choiceMenu
                    choices: stickerEdit.labelFamilies.map(f => ({ value: f, name: f }))
                    value: stickerEdit.labelFamily
                    onPicked: v => stickerEdit.setLabelFamily(v)
                }
                StyleStepper {
                    objectName: "stickerLabelSize"
                    label: qsTr("Size (%)")
                    value: stickerEdit.labelSize
                    minimum: 5
                    maximum: 100
                    onStepped: delta => stickerEdit.setLabelSize(Math.max(5, Math.min(100, stickerEdit.labelSize + delta)))
                    onTyped: number => stickerEdit.setLabelSize(number)
                }
                Row48 {
                    label: qsTr("Color")
                    ColorButton {
                        objectName: "stickerLabelColor"
                        text: qsTr("Color")
                        color: stickerEdit.labelColor
                        onClicked: colorSheet.openWith(qsTr("Color"), stickerEdit.labelColor, c => stickerEdit.setLabelColor(c))
                    }
                }
                Row48 {
                    label: qsTr("Bold")
                    IconButton {
                        objectName: "stickerLabelBold"
                        glyph: "bold"
                        text: qsTr("Bold")
                        on: stickerEdit.labelBold
                        onClicked: stickerEdit.setLabelBold(!stickerEdit.labelBold)
                    }
                }
            }

            // Where the new sticker goes.
            Caption { text: qsTr("Save as") }
            Column {
                width: parent.width
                spacing: Theme.grid
                Label {
                    text: qsTr("Name")
                    color: Theme.darkTextMuted
                    font.pixelSize: Theme.captionSize
                }
                Field {
                    id: nameField
                    objectName: "stickerName"
                }
                StyleSelectRow {
                    id: collectionRow
                    objectName: "stickerCollection"
                    label: qsTr("Collection")
                    menu: choiceMenu
                    choices: stickerEdit.collections.concat([{ value: dialog.newCollection, name: qsTr("New collection…") }])
                    onPicked: v => collectionRow.value = v
                }
                Field {
                    id: newCollectionField
                    objectName: "stickerNewCollection"
                    visible: collectionRow.value === dialog.newCollection
                    placeholderText: qsTr("Name of the new collection")
                }
                Label {
                    objectName: "stickerSaveError"
                    width: parent.width
                    visible: dialog.saveError !== ""
                    text: dialog.saveError
                    wrapMode: Text.Wrap
                    color: Theme.danger
                    font.pixelSize: Theme.captionSize
                }
            }
        }
    }

    buttons: [
        DarkButton {
            objectName: "stickerSaveButton"
            text: qsTr("Save as new")
            primary: true
            enabled: stickerEdit.valid && stickerEdit.cleanName(nameField.text) !== ""
                     && (collectionRow.value !== dialog.newCollection || newCollectionField.text.trim() !== "")
            onClicked: dialog.save()
        }
    ]
}
