import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-119: a select of the side panel (ui-mobile.md, "Side panel"): a 48 px row, the 14 px label at
// the left, the current value at the right (its icon, as the desktop's icon selects, or its name) and
// a chevron; "—" for mixed values. A value set by hand looks as a derived one, as on the desktop
// (ui-foundations.md, "Controlli disattivati", 2026-11-05). A tap opens the choices
// (StyleChoiceMenu). `label` empty: the value fills the row (the font).
AbstractButton {
    id: row
    property string label
    property var choices: []          // [{value, name, group}]
    property int iconKind: -1         // MobileStyle.IconKind, -1 = names only
    property var value: undefined     // the current value; undefined = none or mixed
    property bool manual: false
    property bool mixed: false
    property string shownText: ""     // overrides the value's name (a size typed by hand)
    property bool presetPreview: false  // FEAT-020: the choices are the presets' preview pills
    property bool imageGrid: false      // FEAT-026: the choices carry an image (structure thumbnails)
    // The presets' previews: the first selected topic's branch colour, level and the theme's colours.
    property color presetColor: "#ff6b6b"
    property int presetDepth: 1
    property var themeColors: []
    // FEAT-072 B9: the selected topic's pill shown instead of the value's name (MobileStyle::topicPreview).
    property var topicPreview: ({})
    readonly property bool showsTopic: topicPreview.title !== undefined
    property var menu                 // the panel's StyleChoiceMenu
    signal picked(var value)
    signal removed(var value)  // FEAT-122 C1: the × of a choice marked `saved` (a saved currency)

    readonly property string valueName: {
        if (shownText !== "") return shownText
        if (mixed || value === undefined) return "—"
        for (const choice of choices) if (choice.value === value) return choice.name
        return "—"
    }
    readonly property bool showsIcon: iconKind >= 0 && !mixed && value !== undefined
    width: parent ? parent.width : 0
    implicitHeight: Theme.touchTarget
    focusPolicy: Qt.NoFocus
    Accessible.name: (label !== "" ? label + ": " : "") + valueName
    onClicked: menu.openFor(row)
    // A long press on the label shows its name (TipLabel) and does not open the choices; elsewhere it
    // opens them, as a tap.
    onPressAndHold: {
        if (caption.visible && caption.holdsAt(row, pressX, pressY)) caption.hold()
        else clicked()
    }
    onReleased: caption.release()
    onCanceled: caption.cancel()

    contentItem: Item {
        opacity: row.enabled ? 1 : 0.4
        TipLabel {
            id: caption
            button: row
            visible: row.label !== ""
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, parent.width / 2)
            elide: Text.ElideRight
            text: row.label
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        IconImage {
            id: chevron
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.smallIconSize
            height: Theme.smallIconSize
            source: "qrc:/assets/hugeicons/arrow-down.svg"
            color: Theme.darkTextMuted
            sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
        }
        StyleIcon {
            visible: row.showsIcon
            anchors.right: chevron.left
            anchors.rightMargin: 2 * Theme.grid
            anchors.verticalCenter: parent.verticalCenter
            width: 36
            height: 22
            kind: row.iconKind
            value: row.showsIcon ? row.value : 0
            ink: Theme.darkText
            muted: Theme.darkTextMuted
        }
        StyleIcon {
            objectName: "topicPreview"
            visible: row.showsTopic
            anchors.right: chevron.left
            anchors.rightMargin: 2 * Theme.grid
            anchors.left: caption.visible ? caption.right : parent.left
            anchors.leftMargin: caption.visible ? 3 * Theme.grid : 0
            anchors.verticalCenter: parent.verticalCenter
            height: 30
            topic: row.topicPreview
        }
        Label {
            visible: !row.showsIcon && !row.showsTopic
            anchors.right: chevron.left
            anchors.rightMargin: 2 * Theme.grid
            anchors.left: caption.visible ? caption.right : parent.left
            anchors.leftMargin: caption.visible ? 3 * Theme.grid : 0
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: caption.visible ? Text.AlignRight : Text.AlignLeft
            elide: Text.ElideRight
            text: row.valueName
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
    }
    background: Rectangle { color: row.down ? Theme.darkDivider : "transparent"; radius: Theme.controlRadius }
}
