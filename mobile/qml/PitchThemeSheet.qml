import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-120 stage A: the desktop's Change theme menu (PitchPage::chooseTheme) in a dark modal titled
// "Change theme": the groups Basic and Illustrated under 12 px darkTextMuted headings, three cards
// in a row, each a small slide drawn in the theme (an illustrated one shows its cover) with its name
// under it, the sheet's theme ringed 2 px in darkTextMuted. A tap chooses it (a theme replaces the
// sheet's template, one Undo step) and closes. Stage B (FEAT-089): a Templates group follows, the
// library's templates (the map's own first when the library lacks it, "Name (this map)"), each its
// cover, then a New template card (plus icon) and Import template... across the row.
ModalDialog {
    id: sheet
    objectName: "pitchThemeSheet"
    property MobilePitch pitch: null
    readonly property int revision: pitch ? pitch.revision : -1
    readonly property int current: revision >= 0 && pitch && pitch.state().theme !== undefined ? pitch.state().theme : -1
    readonly property string currentTemplate: revision >= 0 && pitch ? (pitch.state().templateId || "") : ""
    property var templates: []
    onAboutToShow: templates = pitch ? pitch.templates() : []
    // The list's room, as MapTypeSheet's: never under the buttons on a short (landscape) page.
    readonly property real listRoom: (parent ? parent.height - 8 * Theme.grid : 600) - (header ? header.implicitHeight : 0)
                                     - (footer ? footer.implicitHeight : 0) - topPadding - bottomPadding - 2 * spacing

    title: qsTranslate("PitchPage", "Change theme")
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 480)
    height: Math.min(parent ? parent.height - 8 * Theme.grid : 0, implicitHeight)

    function openFor(pitch) {
        sheet.pitch = pitch
        sheet.open()
    }
    function themesIn(illustrated) {
        return sheet.pitch ? sheet.pitch.themes().filter(t => t.illustrated === illustrated) : []
    }

    component Group: Column {
        id: group
        property alias heading: heading.text
        property var themes: []
        property bool templateCards: false  // themes are templates() entries; New template follows
        width: parent ? parent.width : 0
        spacing: Theme.grid
        Label {
            id: heading
            width: parent.width
            color: Theme.darkTextMuted
            font.pixelSize: 12
        }
        Grid {
            id: cards
            columns: 3
            spacing: 2 * Theme.grid
            readonly property real cardWidth: (group.width - 2 * spacing) / 3
            Repeater {
                model: group.templateCards ? group.themes.concat([{ create: true }]) : group.themes
                AbstractButton {
                    id: card
                    required property var modelData
                    readonly property bool create: modelData.create === true
                    readonly property bool chosen: group.templateCards ? !create && modelData.id === sheet.currentTemplate
                                                                       : sheet.currentTemplate === "" && modelData.theme === sheet.current
                    objectName: create ? "pitchNewTemplateCard" : group.templateCards ? "pitchTemplateCard-" + modelData.id
                                                                                      : "pitchThemeCard" + modelData.theme
                    width: cards.cardWidth
                    height: thumbnail.height + name.implicitHeight + 3 * Theme.grid
                    focusPolicy: Qt.NoFocus
                    Accessible.name: create ? qsTranslate("PitchPage", "New template") : modelData.name
                    Accessible.checkable: true
                    Accessible.checked: chosen
                    onClicked: {
                        if (create) sheet.pitch.newTemplate()
                        else if (group.templateCards) sheet.pitch.chooseTemplate(modelData.id)
                        else sheet.pitch.chooseTheme(modelData.theme)
                        sheet.close()
                    }
                    background: Rectangle {
                        radius: 8
                        color: card.down ? Theme.darkDivider : "transparent"
                        border.width: 2
                        border.color: card.chosen ? Theme.darkTextMuted : "transparent"
                    }
                    contentItem: Column {
                        topPadding: Theme.grid
                        spacing: Theme.grid
                        Item {
                            id: thumbnail
                            x: Theme.grid
                            width: card.width - 2 * Theme.grid
                            height: Math.round(width * 9 / 16)
                            PitchSlideView {
                                anchors.fill: parent
                                visible: !card.create
                                pitch: sheet.pitch
                                theme: group.templateCards || card.create ? -1 : card.modelData.theme
                                templateId: group.templateCards && !card.create ? card.modelData.id : ""
                            }
                            // The desktop's New template card: the plus icon over its name.
                            IconImage {
                                anchors.centerIn: parent
                                visible: card.create
                                width: 24
                                height: 24
                                color: Theme.darkText
                                sourceSize: Qt.size(24 * Screen.devicePixelRatio, 24 * Screen.devicePixelRatio)
                                source: card.create ? "qrc:/assets/hugeicons/menu-plus.svg" : ""
                            }
                        }
                        Label {
                            id: name
                            width: card.width
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: card.create ? qsTranslate("PitchPage", "New template") : card.modelData.name
                            color: Theme.darkText
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
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
            spacing: 3 * Theme.grid
            Group {
                heading: qsTranslate("PitchPage", "Basic")
                themes: sheet.opened || sheet.visible ? sheet.themesIn(false) : []
            }
            Group {
                heading: qsTranslate("PitchPage", "Illustrated")
                themes: sheet.opened || sheet.visible ? sheet.themesIn(true) : []
            }
            Group {
                heading: qsTranslate("PitchPage", "Templates")
                templateCards: true
                themes: sheet.templates
            }
            DarkButton {
                objectName: "pitchImportTemplate"
                width: parent.width
                text: qsTranslate("PitchPage", "Import template...")
                onClicked: {
                    sheet.close()
                    importPicker.open()
                }
            }
        }
    }

    // Android's document picker (no filter: the system knows no type for .mentis-theme; a file that
    // is not a template is refused with the desktop's message).
    FileDialog {
        id: importPicker
        title: qsTranslate("PitchPage", "Import template")
        onAccepted: sheet.pitch.importTemplate(selectedFile)
    }

    buttons: [
        DarkButton {
            text: qsTranslate("ColorSheet", "Done")
            primary: true
            onClicked: sheet.close()
        }
    ]
}
