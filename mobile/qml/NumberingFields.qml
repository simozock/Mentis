import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-024: the desktop's NumberingFields on the phone: the preset, then with a numbering its
// format, separator, separator at the end, first number, type and levels. The Style page edits the
// selected topics', the Map page the central topic's (the whole map's); one Undo step each.
Column {
    id: numbering
    required property MobileStyle style
    required property StyleChoiceMenu menu
    required property int revision
    // The central topic's numbering, the whole map's (the Map page); else the selected topics'.
    property bool sheet: false
    readonly property var state: revision >= 0 ? style.numbering(sheet) : ({})
    readonly property bool any: revision >= 0 && state.empty !== true
    width: parent ? parent.width : 0
    readonly property bool set: !state.empty && !state.mixed && state.format !== ""
    function choose(change) {
        const next = Object.assign({}, numbering.state)
        change(next)
        numbering.style.chooseNumbering(next, numbering.sheet)
    }
    StyleSelectRow {
        objectName: "numberingSelect"
        label: qsTranslate("NumberingFields", "Numbering")
        menu: numbering.menu
        choices: {
            const list = [{ value: -1, name: qsTranslate("NumberingFields", "None") }]
            const presets = numbering.style.numberingPresets(numbering.state.separator || ".")
            for (let i = 0; i < presets.length; ++i) list.push({ value: i, name: presets[i] })
            list.push({ value: -2, name: qsTranslate("NumberingFields", "Custom"), enabled: false })
            return list
        }
        value: numbering.state.empty || numbering.state.mixed ? undefined : numbering.state.preset
        mixed: numbering.state.mixed === true
        enabled: numbering.any
        onPicked: v => numbering.choose(n => { n.format = v < 0 ? "" : numbering.style.numberingPresets(n.separator || ".")[v] })
    }
    Column {
        width: parent.width
        visible: numbering.set
        Item {
            width: parent.width
            height: Theme.touchTarget
            TipLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTranslate("NumberingFields", "Format")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            TextField {
                id: formatField
                objectName: "numberingFormatField"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 120
                height: Theme.fieldHeight
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                inputMethodHints: Qt.ImhNoPredictiveText
                Binding on text { when: !formatField.activeFocus; value: numbering.state.format || "" }
                background: Rectangle {
                    radius: Theme.controlRadius
                    color: Theme.darkSunken
                    border.width: 1
                    border.color: formatField.activeFocus ? Theme.accent : Theme.darkDivider
                }
                function apply() {
                    const next = Object.assign({}, numbering.state, { format: formatField.text.trim() })
                    if (next.format !== numbering.state.format && next.format !== "" && numbering.style.numberingValid(next)) numbering.style.chooseNumbering(next, numbering.sheet)
                    formatField.focus = false
                }
                onAccepted: apply()
                onEditingFinished: if (activeFocus) apply()
            }
        }
        StyleSelectRow {
            objectName: "numberingSeparatorSelect"
            label: qsTranslate("NumberingFields", "Separator")
            menu: numbering.menu
            choices: numbering.style.numberingSeparators().map(s => ({ value: s, name: s }))
            value: numbering.state.separator
            onPicked: v => numbering.choose(n => { n.format = n.format.split(n.separator).join(v); n.separator = v })
        }
        DarkSwitch {
            objectName: "numberingTrailingSwitch"
            width: parent.width
            text: qsTranslate("NumberingFields", "Separator at the end")
            checked: numbering.state.trailingSeparator === true
            onClicked: numbering.choose(n => { n.trailingSeparator = checked })
        }
        StyleStepper {
            objectName: "numberingStartStepper"
            label: qsTranslate("NumberingFields", "First number")
            value: numbering.state.start
            minimum: 1
            maximum: numbering.style.numberingMaxStart()
            onStepped: delta => numbering.choose(n => { n.start = Math.max(minimum, Math.min(maximum, n.start + delta)) })
            onTyped: number => numbering.choose(n => { n.start = Math.round(number) })
        }
        StyleSelectRow {
            objectName: "numberingTypeSelect"
            label: qsTranslate("NumberingFields", "Type")
            menu: numbering.menu
            choices: [{ value: true, name: qsTranslate("NumberingFields", "Hierarchical") }, { value: false, name: qsTranslate("NumberingFields", "By level") }]
            value: numbering.state.hierarchical
            onPicked: v => numbering.choose(n => { n.hierarchical = v })
        }
        StyleSelectRow {
            objectName: "numberingLevelsSelect"
            label: qsTranslate("NumberingFields", "Levels")
            menu: numbering.menu
            choices: {
                const list = [{ value: 0, name: qsTranslate("NumberingFields", "All") }]
                for (let i = 1; i <= numbering.style.numberingMaxLevels(); ++i) list.push({ value: i, name: String(i) })
                return list
            }
            value: numbering.state.levels
            onPicked: v => numbering.choose(n => { n.levels = v })
        }
    }
}
