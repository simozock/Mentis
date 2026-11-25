import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-032 on the phone: the desktop's "Create new color theme" (NewPaletteDialog) in a dark modal:
// the hint, six empty squares (48 px, radius 8, 1 px darkDivider; darkSunken until chosen) whose tap
// opens the colour sheet without opacity, in the main topics' order; the name (empty = the next
// "Custom N", shown as the placeholder) with the desktop's error when it is taken; the preview of the
// levels (NodeStyle::resolveTree). Save, with six colours and a free name, adds the own theme and
// puts it on the sheet (one Undo step); the X leaves everything.
ModalDialog {
    id: sheet
    objectName: "newPaletteSheet"
    property MobileStyle style: null
    required property ColorSheet colorSheet
    property var colors: ["", "", "", "", "", ""]
    readonly property bool complete: colors.indexOf("") < 0
    readonly property bool taken: style !== null && nameField.text.trim() !== "" && style.isPaletteNameTaken(nameField.text)

    title: qsTranslate("NewPaletteDialog", "Create new color theme")
    closePolicy: Popup.CloseOnEscape
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 520)
    height: Math.min(parent ? parent.height - 8 * Theme.grid : 0, implicitHeight)

    function openFor(style) {
        sheet.style = style
        sheet.colors = ["", "", "", "", "", ""]
        nameField.text = ""
        nameField.placeholderText = style.nextPaletteName()
        sheet.open()
    }
    function setColor(index, color) {
        const next = sheet.colors.slice()
        next[index] = color.toString()  // #rrggbb: opaque, the sheet is opened without opacity
        sheet.colors = next
    }

    Flickable {
        id: body
        width: sheet.availableWidth
        // The room left above the buttons on a short page.
        height: Math.max(0, Math.min(content.height, (sheet.parent ? sheet.parent.height - 8 * Theme.grid : 600)
                                                     - (sheet.header ? sheet.header.implicitHeight : 0)
                                                     - (sheet.footer ? sheet.footer.implicitHeight : 0) - 2 * sheet.spacing))
        contentHeight: content.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: content
            width: body.width
            spacing: 2 * Theme.grid
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTranslate("NewPaletteDialog", "Choose 6 colors to generate your color theme.")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            Row {
                spacing: Math.max(0, Math.min(2 * Theme.grid, (content.width - 6 * Theme.touchTarget) / 5))
                Repeater {
                    model: 6
                    AbstractButton {
                        id: swatch
                        required property int index
                        readonly property string chosen: sheet.colors[index]
                        objectName: "paletteSwatch" + index
                        width: Theme.touchTarget
                        height: Theme.touchTarget
                        focusPolicy: Qt.NoFocus
                        Accessible.name: qsTranslate("NewPaletteDialog", "Color %1").arg(index + 1)
                        onClicked: sheet.colorSheet.openWith(Accessible.name, chosen, color => sheet.setColor(index, color))
                        background: Rectangle {
                            radius: 8
                            color: swatch.chosen === "" ? Theme.darkSunken : swatch.chosen
                            border.width: swatch.down ? 2 : 1
                            border.color: swatch.down ? Theme.accent : Theme.darkDivider
                        }
                    }
                }
            }
            Label {
                text: qsTranslate("NewPaletteDialog", "Name")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            TextField {
                id: nameField
                objectName: "paletteNameField"
                width: parent.width
                height: Theme.touchTarget
                color: Theme.darkText
                placeholderTextColor: Theme.darkTextMuted
                font.pixelSize: Theme.bodySize
                verticalAlignment: TextInput.AlignVCenter
                leftPadding: 2 * Theme.grid
                rightPadding: 2 * Theme.grid
                Accessible.name: qsTranslate("NewPaletteDialog", "Name")
                background: Item {
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: Theme.fieldHeight
                        radius: Theme.controlRadius
                        color: Theme.darkSunken
                        border.width: 1
                        border.color: nameField.activeFocus ? Theme.accent : Theme.darkDivider
                    }
                }
            }
            Label {
                objectName: "paletteNameError"
                visible: sheet.taken
                width: parent.width
                wrapMode: Text.Wrap
                text: qsTranslate("NewPaletteDialog", "A palette with this name already exists.")
                color: Theme.darkTextMuted  // the desktop's ModalError
                font.pixelSize: 12
            }
            Label {
                text: qsTranslate("NewPaletteDialog", "Preview")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            PalettePreview {
                objectName: "palettePreview"
                width: parent.width
                height: 132
                colors: sheet.colors
            }
        }
    }

    buttons: [
        DarkButton {
            objectName: "savePalette"
            text: qsTranslate("NewPaletteDialog", "Save")
            primary: true
            enabled: sheet.complete && !sheet.taken
            onClicked: {
                sheet.style.createPalette(nameField.text, sheet.colors)
                sheet.close()
            }
        }
    ]
}
