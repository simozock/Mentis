import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import Mentis.Mobile

// FEAT-120 stage B: the desktop's template editor (PitchTemplateEditor) under the Pitch page's
// preview, which shows the draft. Its groups and order: Template (Name, Base theme), Colours (nine
// colour rows through the colour sheet, "Use the base theme's colours"), Text (Title font, Item
// font, Items, Motif), Pictures (Slides, Cover and topics, Logo: Chosen / None, Choose..., Remove;
// Logo corner, Logo size), Footer (Footer text, Slide numbers); then Save, Cancel, Export... and
// Delete (only for a template in the library, asked first). A colour the draft leaves to the base
// theme shows the theme's. The texts are the desktop's (context PitchTemplateEditor).
Column {
    id: fields
    required property MobilePitch pitch
    required property StyleChoiceMenu menu
    required property ColorSheet colorSheet

    readonly property int revision: pitch.revision
    readonly property var draft: revision >= 0 && pitch.editing ? pitch.draft() : ({})
    property int pictureIndex: -1  // the picture being chosen

    spacing: 0

    function choices(field) { return fields.revision >= 0 ? fields.pitch.draftChoices(field) : [] }

    component Heading: TipLabel {
        width: parent ? parent.width : 0
        topPadding: 3 * Theme.grid
        bottomPadding: Theme.grid
        color: Theme.darkText
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }
    component Divider: Rectangle {
        width: parent ? parent.width : 0
        height: 1
        color: Theme.darkDivider
    }
    // A text of the draft (Name, Footer text): the label at the left, the field at the right. The
    // draft follows each edit; the field takes the draft's text again when it is not being typed in.
    component TextRow: Item {
        id: textRow
        property alias label: caption.text
        property string value
        property alias fieldName: field.objectName
        signal edited(string text)
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        TipLabel {
            id: caption
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        TextField {
            id: field
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(parent.width * 0.6, 200)
            height: Theme.touchTarget
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 2 * Theme.grid
            rightPadding: 2 * Theme.grid
            Accessible.name: caption.text
            Binding on text { when: !field.activeFocus; value: textRow.value }
            onTextEdited: textRow.edited(text)
            background: Item {
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: Theme.fieldHeight
                    radius: Theme.controlRadius
                    color: Theme.darkSunken
                    border.width: 1
                    border.color: field.activeFocus ? Theme.accent : Theme.darkDivider
                }
            }
        }
    }
    // A colour of the draft, the desktop's ColorField: a 28 px square in a 48 px target. Whether the
    // template sets it or the base theme gives it is the desktop's tooltip, here the accessible
    // description (the panel's 276 px have no room for it beside the name).
    component ColorRow: Item {
        id: colorRow
        required property int index
        required property var modelData
        width: fields.width
        height: Theme.touchTarget
        TipLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: colorRow.modelData.name
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        AbstractButton {
            id: swatch
            objectName: "pitchTemplateColor" + colorRow.index
            anchors.right: parent.right
            width: Theme.touchTarget
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            Accessible.name: colorRow.modelData.name
            Accessible.description: colorRow.modelData.own ? qsTranslate("PitchTemplateEditor", "Set in the template")
                                                           : qsTranslate("PitchTemplateEditor", "From the base theme")
            onClicked: fields.colorSheet.openWith(colorRow.modelData.name, colorRow.modelData.value,
                                                  color => fields.pitch.setDraftColor(colorRow.index, color))
            background: Item {}
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    radius: Theme.controlRadius
                    color: colorRow.modelData.value
                    border.width: 1
                    border.color: swatch.down ? Theme.darkText : Theme.darkDivider
                }
            }
        }
    }

    Heading { text: qsTranslate("PitchTemplateEditor", "Template") }
    TextRow {
        fieldName: "pitchTemplateName"
        label: qsTranslate("PitchTemplateEditor", "Name")
        value: fields.draft.name || ""
        onEdited: text => fields.pitch.setDraft(MobilePitch.Name, text)
    }
    StyleSelectRow {
        objectName: "pitchTemplateBase"
        label: qsTranslate("PitchTemplateEditor", "Base theme")
        menu: fields.menu
        choices: fields.choices(MobilePitch.Base)
        value: fields.draft.base
        onPicked: v => fields.pitch.setDraft(MobilePitch.Base, v)
    }
    Divider {}
    Heading { text: qsTranslate("PitchTemplateEditor", "Colours") }
    Repeater {
        model: fields.draft.colors || []
        ColorRow {}
    }
    DarkButton {
        objectName: "pitchTemplateResetColors"
        width: parent.width
        text: qsTranslate("PitchTemplateEditor", "Use the base theme's colours")
        onClicked: fields.pitch.resetDraftColors()
    }
    Item { width: 1; height: 2 * Theme.grid }
    Divider {}
    Heading { text: qsTranslate("PitchTemplateEditor", "Text") }
    StyleSelectRow {
        objectName: "pitchTemplateTitleFont"
        label: qsTranslate("PitchTemplateEditor", "Title font")
        menu: fields.menu
        choices: fields.choices(MobilePitch.TitleFont)
        value: fields.draft.titleFont
        onPicked: v => fields.pitch.setDraft(MobilePitch.TitleFont, v)
    }
    StyleSelectRow {
        objectName: "pitchTemplateItemFont"
        label: qsTranslate("PitchTemplateEditor", "Item font")
        menu: fields.menu
        choices: fields.choices(MobilePitch.ItemFont)
        value: fields.draft.itemFont
        onPicked: v => fields.pitch.setDraft(MobilePitch.ItemFont, v)
    }
    StyleSelectRow {
        objectName: "pitchTemplateItemStyle"
        label: qsTranslate("PitchTemplateEditor", "Items")
        menu: fields.menu
        choices: fields.choices(MobilePitch.ItemStyle)
        value: fields.draft.itemStyle
        onPicked: v => fields.pitch.setDraft(MobilePitch.ItemStyle, v)
    }
    StyleSelectRow {
        objectName: "pitchTemplateMotif"
        label: qsTranslate("PitchTemplateEditor", "Motif")
        menu: fields.menu
        choices: fields.choices(MobilePitch.Motif)
        value: fields.draft.motif
        onPicked: v => fields.pitch.setDraft(MobilePitch.Motif, v)
    }
    Divider {}
    Heading { text: qsTranslate("PitchTemplateEditor", "Pictures") }
    Repeater {
        model: fields.draft.pictures || []
        Column {
            id: picture
            required property int index
            required property var modelData
            width: fields.width
            Item {
                width: parent.width
                height: Theme.touchTarget
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: picture.modelData.name
                    color: Theme.darkText
                    font.pixelSize: Theme.bodySize
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.right: parent.right
                    text: picture.modelData.chosen ? qsTranslate("PitchTemplateEditor", "Chosen") : qsTranslate("PitchTemplateEditor", "None")
                    color: Theme.darkTextMuted
                    font.pixelSize: 12
                }
            }
            Row {
                spacing: 2 * Theme.grid
                DarkButton {
                    objectName: "pitchTemplateChoose" + picture.index
                    width: (fields.width - 2 * Theme.grid) / 2
                    text: qsTranslate("PitchTemplateEditor", "Choose...")
                    Accessible.name: picture.modelData.name
                    onClicked: fields.choosePicture(picture.index)
                }
                DarkButton {
                    objectName: "pitchTemplateRemove" + picture.index
                    width: (fields.width - 2 * Theme.grid) / 2
                    text: qsTranslate("PitchTemplateEditor", "Remove")
                    enabled: picture.modelData.chosen
                    onClicked: fields.pitch.removeDraftPicture(picture.index)
                }
            }
            Item { width: 1; height: Theme.grid }
        }
    }
    StyleSelectRow {
        objectName: "pitchTemplateLogoCorner"
        label: qsTranslate("PitchTemplateEditor", "Logo corner")
        menu: fields.menu
        choices: fields.choices(MobilePitch.LogoCorner)
        value: fields.draft.logoCorner
        onPicked: v => fields.pitch.setDraft(MobilePitch.LogoCorner, v)
    }
    StyleSelectRow {
        objectName: "pitchTemplateLogoSize"
        label: qsTranslate("PitchTemplateEditor", "Logo size")
        menu: fields.menu
        choices: fields.choices(MobilePitch.LogoSize)
        value: fields.draft.logoSize
        onPicked: v => fields.pitch.setDraft(MobilePitch.LogoSize, v)
    }
    Divider {}
    Heading { text: qsTranslate("PitchTemplateEditor", "Footer") }
    TextRow {
        fieldName: "pitchTemplateFooter"
        label: qsTranslate("PitchTemplateEditor", "Footer text")
        value: fields.draft.footer || ""
        onEdited: text => fields.pitch.setDraft(MobilePitch.Footer, text)
    }
    DarkSwitch {
        objectName: "pitchTemplateNumbers"
        width: parent.width
        text: qsTranslate("PitchTemplateEditor", "Slide numbers")
        checked: fields.draft.numbers === true
        onClicked: fields.pitch.setDraft(MobilePitch.Numbers, checked)
    }
    Divider {}
    Item { width: 1; height: 2 * Theme.grid }
    // The desktop's two rows: Save, Cancel; Export..., Delete.
    Grid {
        columns: 2
        spacing: 2 * Theme.grid
        readonly property real cell: (fields.width - spacing) / 2
        DarkButton {
            objectName: "pitchTemplateSave"
            width: parent.cell
            primary: true
            text: qsTranslate("PitchTemplateEditor", "Save")
            onClicked: { fields.commitText(); fields.pitch.saveDraft() }
        }
        DarkButton {
            objectName: "pitchTemplateCancel"
            width: parent.cell
            text: qsTranslate("PitchTemplateEditor", "Cancel")
            onClicked: fields.pitch.cancelDraft()
        }
        DarkButton {
            objectName: "pitchTemplateExport"
            width: parent.cell
            text: qsTranslate("PitchTemplateEditor", "Export...")
            onClicked: { fields.commitText(); fields.pitch.exportDraft() }
        }
        DarkButton {
            objectName: "pitchTemplateDelete"
            width: parent.cell
            visible: fields.draft.inLibrary === true
            text: qsTranslate("PitchTemplateEditor", "Delete")
            onClicked: deleteDialog.open()
        }
    }

    // The keyboard closed first: what was typed is already in the draft (textEdited).
    function commitText() { Qt.inputMethod.commit() }

    // The system's picture picker on the phone, Qt's file dialog elsewhere (as the topic's Image).
    function choosePicture(index) {
        fields.pictureIndex = index
        if (!photoPicker.open()) picturePicker.open()
    }
    ImagePicker {
        id: photoPicker
        onPicked: url => fields.pitch.setDraftPicture(fields.pictureIndex, url)
    }
    FileDialog {
        id: picturePicker
        title: qsTranslate("PitchTemplateEditor", "Choose a picture")
        nameFilters: [qsTranslate("PitchTemplateEditor", "Pictures (*.png *.jpg *.jpeg *.bmp *.gif *.webp)")]
        onAccepted: fields.pitch.setDraftPicture(fields.pictureIndex, selectedFile)
    }

    // The desktop's confirmation before deleting from the library (maps keep their copy).
    ModalDialog {
        id: deleteDialog
        objectName: "pitchTemplateDeleteDialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min((parent ? parent.width : 400) - 8 * Theme.grid, 400)
        title: qsTranslate("PitchTemplateEditor", "Delete template")
        onAccepted: fields.pitch.deleteDraft()
        Label {
            width: deleteDialog.availableWidth  // not parent.width: a binding loop on the dialog's implicit height
            wrapMode: Text.Wrap
            text: qsTranslate("PitchTemplateEditor", "Delete the template \"%1\" from the library? Maps that use it keep their copy.")
                .arg(fields.draft.name || "")
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        buttons: [
            DarkButton {
                objectName: "pitchTemplateDeleteConfirm"
                text: qsTranslate("PitchTemplateEditor", "Delete")
                primary: true
                onClicked: deleteDialog.accept()
            }
        ]
    }
}
