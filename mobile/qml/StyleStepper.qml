import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-119: a numeric field of the side panel (ui-mobile.md, "Side panel"): a 48 px row, the 14 px
// label at the left, then − , the value and + as 48 px targets. − and + step by `step` and do not
// repeat while held; a tap on the value types it with the keyboard (Enter or leaving applies it,
// a value outside the range goes back). "—" for mixed values or none.
Item {
    id: stepper
    property string label
    property var value: undefined   // number, undefined = mixed or none
    property real step: 1
    property real minimum: 0
    property real maximum: 9999
    property int decimals: 0
    // The value is whole minutes, shown and typed as hours.minutes ("8.15" = 8 h 15, "8.3" = 8 h 30);
    // a typed value goes to the owner, which rounds and limits it.
    property bool clock: false
    property color valueColor: Theme.darkText  // FEAT-082: red or green against a snapshot
    signal stepped(real delta)
    signal typed(real number)
    width: parent ? parent.width : 0
    implicitHeight: Theme.touchTarget
    opacity: enabled ? 1 : 0.4

    readonly property string shown: value === undefined || value === null ? "—"
        : clock ? Math.floor(value / 60) + "." + String(value % 60).padStart(2, "0")
        : Number(value).toFixed(decimals).replace(/\.0+$/, "")

    function clockMinutes(text) {  // NaN when not hours.minutes
        const parts = text.trim().replace(",", ".").split(".")
        if (parts.length > 2 || !/^\d*$/.test(parts[0]) || (parts.length === 2 && !/^\d{0,2}$/.test(parts[1]))) return NaN
        if (parts[0] === "" && (parts.length === 1 || parts[1] === "")) return NaN
        const minutes = parts.length === 2 && parts[1] !== "" ? Number(parts[1].padEnd(2, "0")) : 0
        return minutes < 60 ? Number(parts[0] || 0) * 60 + minutes : NaN
    }

    TipLabel {
        anchors.left: parent.left
        anchors.right: minus.left
        anchors.verticalCenter: parent.verticalCenter
        elide: Text.ElideRight
        text: stepper.label
        color: Theme.darkText
        font.pixelSize: Theme.bodySize
    }
    component StepButton: AbstractButton {
        id: button
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        Accessible.name: text
        contentItem: Label {
            text: button.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.darkText
            font.pixelSize: Theme.largeSize
        }
        background: Rectangle {
            anchors.centerIn: parent
            width: Theme.buttonHeight
            height: Theme.buttonHeight
            radius: Theme.controlRadius
            color: button.down ? Theme.darkDivider : Theme.darkSunken
        }
    }
    StepButton {
        id: minus
        objectName: "stepDown"
        anchors.right: field.left
        anchors.verticalCenter: parent.verticalCenter
        text: "−"
        onClicked: stepper.stepped(-stepper.step)
    }
    TextField {
        id: field
        objectName: "stepValue"
        anchors.right: plus.left
        anchors.verticalCenter: parent.verticalCenter
        width: 56
        height: Theme.fieldHeight
        padding: 0
        horizontalAlignment: Text.AlignHCenter
        inputMethodHints: stepper.clock || stepper.decimals > 0 || stepper.minimum < 0 ? Qt.ImhFormattedNumbersOnly : Qt.ImhDigitsOnly  // a minus for a lead
        color: field.activeFocus ? Theme.darkText : stepper.valueColor
        selectionColor: Theme.accent
        font.pixelSize: Theme.bodySize
        Binding on text { when: !field.activeFocus; value: stepper.shown }
        Accessible.name: stepper.label
        background: Rectangle {
            radius: Theme.controlRadius
            color: Theme.darkSunken
            border.width: 1
            border.color: field.activeFocus ? Theme.accent : Theme.darkDivider
        }
        onActiveFocusChanged: if (activeFocus) { text = stepper.shown === "—" ? "" : stepper.shown; selectAll() }
        function apply() {
            if (stepper.clock) {
                const minutes = stepper.clockMinutes(text)
                if (!isNaN(minutes)) stepper.typed(minutes)
                focus = false
                return
            }
            const number = Number(text.replace(",", "."))
            if (text.trim() !== "" && !isNaN(number) && number >= stepper.minimum && number <= stepper.maximum) stepper.typed(number)
            focus = false
        }
        onAccepted: apply()
        onEditingFinished: if (activeFocus) apply()
    }
    StepButton {
        id: plus
        objectName: "stepUp"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        text: "+"
        onClicked: stepper.stepped(stepper.step)
    }
}
