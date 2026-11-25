import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-120 stage A: the side panel's Pitch page, the desktop's Pitch tab in its order: "View in Pitch
// mode", the preview of the topic's slide (as wide as the page, at most 16:9 tall, radius 8), Change
// theme, Aspect ratio; then for the topic Topic slide, List slide, Delivery and Layout (the ten
// icons in 48 px cells, the current one on darkRaised). No topic selected: the central topic's
// rows; several: the rows disabled. Every choice is one Undo step (MobilePitch). The texts are the
// desktop's (context PitchPage). Stage B: with a template on the sheet Edit template follows Change
// theme; while a template is edited its fields (PitchTemplateFields) replace the rows below the
// preview, which shows the draft.
Column {
    id: page
    required property MobilePitch pitch
    required property StyleChoiceMenu menu
    required property PitchThemeSheet themeSheet
    required property ColorSheet colorSheet  // stage B: the template's colours

    readonly property int revision: pitch.revision
    readonly property var pitchState: revision >= 0 ? pitch.state() : ({})
    readonly property bool open: pitchState.open === true
    readonly property bool topicRows: open && pitchState.topic === true
    readonly property bool editing: revision >= 0 && pitch.editing

    spacing: 0
    topPadding: 2 * Theme.grid
    bottomPadding: 4 * Theme.grid

    function named(names) {
        const list = []
        for (let i = 0; i < names.length; ++i) list.push({ value: i, name: names[i] })
        return list
    }

    component Caption: TipLabel {
        width: parent ? parent.width : 0
        topPadding: 2 * Theme.grid
        color: Theme.darkTextMuted
        font.pixelSize: 12
    }
    component Divider: Rectangle {
        width: parent ? parent.width : 0
        height: 1
        color: Theme.darkDivider
    }

    TipLabel {
        width: parent.width
        height: Theme.touchTarget
        verticalAlignment: Text.AlignVCenter
        text: qsTranslate("PitchPage", "View in Pitch mode")
        color: Theme.darkText
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }
    PitchSlideView {
        id: preview
        objectName: "pitchPreview"
        pitch: page.pitch
        width: parent.width
        // PitchPreview::fitHeight: as wide as the page, at most 16:9 tall (a tall slide sits in bands).
        height: Math.max(90, Math.round(width / Math.max(16 / 9, ratio)))
    }
    Item { width: 1; height: 2 * Theme.grid }
    PitchTemplateFields {
        objectName: "pitchTemplateFields"
        visible: page.editing
        width: parent.width
        pitch: page.pitch
        menu: page.menu
        colorSheet: page.colorSheet
    }
    // The sheet's and the topic's rows, hidden while a template is edited (PitchPage::startEditing).
    Column {
        visible: !page.editing
        width: parent.width
        DarkButton {
            objectName: "pitchThemeButton"
            width: parent.width
            text: qsTranslate("PitchPage", "Change theme")
            enabled: page.open
            onClicked: page.themeSheet.openFor(page.pitch)
        }
        Item { width: 1; height: Theme.grid; visible: editButton.visible }
        DarkButton {
            id: editButton
            objectName: "pitchEditTemplateButton"
            width: parent.width
            visible: page.pitchState.hasTemplate === true
            text: qsTranslate("PitchPage", "Edit template")
            onClicked: page.pitch.editTemplate()
        }
        StyleSelectRow {
            objectName: "pitchAspectSelect"
            label: qsTranslate("PitchPage", "Aspect ratio")
            menu: page.menu
            choices: page.named(page.revision >= 0 ? page.pitch.aspectNames() : [])
            value: page.pitchState.aspect
            enabled: page.open
            onPicked: v => page.pitch.chooseAspect(v)
        }
        Divider {}
        StyleSelectRow {
            objectName: "pitchTopicSlideSelect"
            label: qsTranslate("PitchPage", "Topic slide")
            menu: page.menu
            choices: page.named(page.pitchState.topicSlideNames || [])
            value: page.pitchState.topicSlide
            enabled: page.topicRows
            onPicked: v => page.pitch.chooseTopic(MobilePitch.TopicSlide, v)
        }
        Divider {}
        StyleSelectRow {
            objectName: "pitchListSlideSelect"
            label: qsTranslate("PitchPage", "List slide")
            menu: page.menu
            choices: page.named(page.pitchState.listSlideNames || [])
            value: page.pitchState.listSlide
            enabled: page.topicRows
            onPicked: v => page.pitch.chooseTopic(MobilePitch.ListSlide, v)
        }
        Caption { text: qsTranslate("PitchPage", "Delivery"); opacity: page.topicRows ? 1 : 0.4 }
        StyleSelectRow {
            objectName: "pitchDeliverySelect"
            menu: page.menu
            choices: page.named(page.revision >= 0 ? page.pitch.deliveryNames() : [])
            value: page.pitchState.delivery
            enabled: page.topicRows
            onPicked: v => page.pitch.chooseTopic(MobilePitch.Delivery, v)
        }
        Caption { text: qsTranslate("PitchPage", "Layout"); opacity: page.topicRows ? 1 : 0.4 }
        Item { width: 1; height: Theme.grid }
        // The desktop's two rows of five, as many 48 px cells per row as fit.
        Rectangle {
            id: track
            objectName: "pitchLayoutTrack"
            readonly property int columns: Math.max(1, Math.min(10, Math.floor((width - 2 * Theme.grid) / Theme.touchTarget)))
            width: parent.width
            height: layouts.height + 2 * Theme.grid
            radius: 8
            color: Theme.darkSurface
            border.width: 1
            border.color: Theme.darkDivider
            opacity: page.topicRows ? 1 : 0.4
            Grid {
                id: layouts
                x: (track.width - width) / 2
                y: Theme.grid
                columns: track.columns >= 10 ? 10 : (track.columns >= 5 ? 5 : track.columns)
                Repeater {
                    model: page.revision >= 0 ? page.pitch.layoutNames() : []
                    AbstractButton {
                        id: cell
                        required property string modelData
                        required property int index
                        readonly property bool current: page.pitchState.layout === index
                        objectName: "pitchLayout" + index
                        width: Theme.touchTarget
                        height: Theme.touchTarget
                        enabled: page.topicRows
                        focusPolicy: Qt.NoFocus
                        Accessible.name: modelData
                        Accessible.checkable: true
                        Accessible.checked: current
                        onClicked: page.pitch.chooseTopic(MobilePitch.Layout, index)
                        background: Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: Theme.controlRadius
                            color: cell.current ? Theme.darkRaised : (cell.down ? Theme.darkDivider : "transparent")
                        }
                        contentItem: Item {
                            PitchLayoutIcon {
                                anchors.centerIn: parent
                                width: 24
                                height: 24
                                layout: cell.index
                                color: Theme.darkText
                            }
                        }
                    }
                }
            }
        }
    }
}
