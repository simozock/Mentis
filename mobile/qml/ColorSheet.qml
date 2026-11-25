import QtQuick
import QtQuick.Controls
import Mentis.Mobile

// FEAT-119: the colour sheet (ui-mobile.md, "Colour sheet"): the desktop's ColorPicker in a dark
// modal: the grid of colours, the hex code (Enter applies it), the opacity 0-100 %, the preview and
// the custom colour panel (saturation and value area, hue bar); then the theme's colours and the
// recently used ones. Swatches 28 px in 48 px cells. A swatch or the hex code picks and closes; the
// opacity and the custom panel pick and stay open, as on the desktop. From a fully transparent
// colour (no fill, no border) the colour picked starts opaque (BUG-011).
ModalDialog {
    id: sheet
    objectName: "colorSheet"
    required property MobileStyle style
    property int property: -1
    property color current: "transparent"
    property bool mixed: false
    property int opacityPercent: 100
    // FEAT-032: a colour for a caller (the theme editor), not a style: opaque, no opacity field.
    property var callback: null
    property bool withOpacityField: true
    // The custom panel's colour while it is dragged (hue 0-1, saturation and value 0-1).
    property real hue: 0
    property real saturation: 0
    property real brightness: 1

    readonly property int swatchSize: 28
    readonly property int cellSize: Theme.touchTarget

    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 480)
    height: Math.min(parent ? parent.height - 8 * Theme.grid : 0, implicitHeight)

    function openFor(property, title, reading) {
        sheet.callback = null
        sheet.withOpacityField = true
        sheet.property = property
        sheet.title = title
        sheet.mixed = reading.mixed || reading.empty
        sheet.current = sheet.mixed ? "transparent" : reading.value
        sheet.opacityPercent = !sheet.mixed && sheet.current.a > 0 ? Math.round(sheet.current.a * 100) : 100
        custom.checked = false
        syncCustom()
        hexField.text = sheet.mixed ? "" : sheet.hexOf(sheet.current)
        recent.model = sheet.style.recentColors()
        sheet.open()
    }
    // The desktop's ColorPicker opened without opacity: the colour picked goes to callback.
    function openWith(title, color, callback) {
        sheet.callback = callback
        sheet.withOpacityField = false
        sheet.property = -1
        sheet.title = title
        sheet.mixed = !color || color === ""
        sheet.current = sheet.mixed ? "transparent" : color
        sheet.opacityPercent = 100
        custom.checked = false
        syncCustom()
        hexField.text = sheet.mixed ? "" : sheet.hexOf(sheet.current)
        recent.model = sheet.style.recentColors()
        sheet.open()
    }
    function hexOf(color) {
        const hex = c => ("0" + Math.round(c * 255).toString(16)).slice(-2)
        return ("#" + hex(color.r) + hex(color.g) + hex(color.b)).toUpperCase()
    }
    function withOpacity(color) { return Qt.rgba(color.r, color.g, color.b, sheet.opacityPercent / 100) }
    function pick(color, close) {
        sheet.current = color
        sheet.mixed = false
        hexField.text = sheet.hexOf(color)
        if (sheet.callback) {
            const opaque = Qt.rgba(color.r, color.g, color.b, 1)
            sheet.current = opaque
            sheet.style.rememberColor(opaque)
            sheet.callback(opaque)
        } else {
            sheet.style.chooseColor(sheet.property, color)
        }
        if (close) sheet.close()
    }
    function syncCustom() {
        const base = sheet.mixed ? Qt.color("white") : sheet.current
        sheet.hue = Math.max(0, base.hsvHue)
        sheet.saturation = base.hsvSaturation
        sheet.brightness = base.hsvValue
    }
    function customColor() { return sheet.withOpacity(Qt.hsva(sheet.hue, sheet.saturation, sheet.brightness, 1)) }

    component Swatch: AbstractButton {
        id: swatch
        required property color swatchColor
        property bool chosen: false
        width: sheet.cellSize
        height: sheet.cellSize
        focusPolicy: Qt.NoFocus
        Accessible.name: sheet.hexOf(swatchColor)
        background: Item {}
        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                width: sheet.swatchSize + 6
                height: width
                radius: Theme.controlRadius + 3
                color: "transparent"
                border.width: 2
                border.color: Theme.darkText
                visible: swatch.chosen
            }
            Rectangle {
                anchors.centerIn: parent
                width: sheet.swatchSize
                height: sheet.swatchSize
                radius: Theme.controlRadius
                color: swatch.swatchColor
                border.width: 1
                border.color: Theme.darkDivider
                opacity: swatch.down ? 0.7 : 1
            }
        }
    }
    component Caption: Label {
        color: Theme.darkText
        font.pixelSize: Theme.captionSize
        font.weight: Font.DemiBold
        topPadding: 2 * Theme.grid
    }

    Flickable {
        width: sheet.availableWidth
        height: Math.min(column.implicitHeight, sheet.parent ? sheet.parent.height - 64 * Theme.grid : column.implicitHeight)
        implicitHeight: column.implicitHeight
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: column
            width: parent.width
            spacing: Theme.grid
            Flow {
                objectName: "colorGrid"
                width: parent.width
                Repeater {
                    model: sheet.style.gridColors()
                    Swatch {
                        required property string modelData
                        swatchColor: modelData
                        // The same hue counts, the opacity is apart (ColorPicker).
                        chosen: !sheet.mixed && sheet.hexOf(sheet.current) === sheet.hexOf(Qt.color(modelData))
                        onClicked: sheet.pick(sheet.withOpacity(Qt.color(modelData)), true)
                    }
                }
            }
            Row {
                spacing: 2 * Theme.grid
                height: Theme.touchTarget
                TextField {
                    id: hexField
                    objectName: "hexField"
                    anchors.verticalCenter: parent.verticalCenter
                    width: 112
                    height: Theme.fieldHeight
                    placeholderText: "#RRGGBB"
                    placeholderTextColor: Theme.darkTextMuted
                    color: Theme.darkText
                    font.pixelSize: Theme.bodySize
                    inputMethodHints: Qt.ImhNoPredictiveText
                    validator: RegularExpressionValidator { regularExpression: /#?[0-9A-Fa-f]{0,6}/ }
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: Theme.darkSunken
                        border.width: 1
                        border.color: hexField.activeFocus ? Theme.accent : Theme.darkDivider
                    }
                    onAccepted: {
                        const text = hexField.text.startsWith("#") ? hexField.text : "#" + hexField.text
                        if (text.length === 7) sheet.pick(sheet.withOpacity(Qt.color(text)), true)
                    }
                }
                TextField {
                    id: opacityField
                    objectName: "opacityField"
                    visible: sheet.withOpacityField
                    anchors.verticalCenter: parent.verticalCenter
                    width: 72
                    height: Theme.fieldHeight
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.darkText
                    font.pixelSize: Theme.bodySize
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: RegularExpressionValidator { regularExpression: /\s*\d{0,3}\s*%?\s*/ }
                    Binding on text { when: !opacityField.activeFocus; value: sheet.opacityPercent + " %" }
                    Accessible.name: qsTranslate("ColorPicker", "Opacity (0-100 %)")
                    background: Rectangle {
                        radius: Theme.controlRadius
                        color: Theme.darkSunken
                        border.width: 1
                        border.color: opacityField.activeFocus ? Theme.accent : Theme.darkDivider
                    }
                    function apply() {
                        const value = parseInt(opacityField.text.replace("%", "").trim())
                        sheet.opacityPercent = isNaN(value) ? 100 : Math.max(0, Math.min(100, value))
                        if (!sheet.mixed && Math.round(sheet.current.a * 100) !== sheet.opacityPercent) sheet.pick(sheet.withOpacity(sheet.current), false)
                        opacityField.focus = false
                    }
                    onAccepted: apply()
                    onEditingFinished: if (activeFocus) apply()
                }
                Rectangle {
                    objectName: "colorPreview"
                    anchors.verticalCenter: parent.verticalCenter
                    width: sheet.swatchSize
                    height: sheet.swatchSize
                    radius: Theme.controlRadius
                    color: sheet.mixed ? "transparent" : sheet.current
                    border.width: 1
                    border.color: Theme.darkDivider
                }
                AbstractButton {
                    // The desktop's rainbow button: opens the custom colour panel.
                    id: custom
                    objectName: "customColorButton"
                    width: Theme.touchTarget
                    height: Theme.touchTarget
                    checkable: true
                    focusPolicy: Qt.NoFocus
                    Accessible.name: qsTr("Custom color")
                    onToggled: if (checked) sheet.syncCustom()
                    background: Item {}
                    contentItem: Item {
                        Rectangle {
                            anchors.centerIn: parent
                            width: sheet.swatchSize
                            height: sheet.swatchSize
                            radius: sheet.swatchSize / 2
                            border.width: custom.checked ? 2 : 0
                            border.color: Theme.darkText
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: "#ff0000" }
                                GradientStop { position: 0.33; color: "#00ff00" }
                                GradientStop { position: 0.66; color: "#0000ff" }
                                GradientStop { position: 1.0; color: "#ff00ff" }
                            }
                        }
                    }
                }
            }
            // The custom colour panel: saturation across and value down at the hue, then the hue bar.
            Column {
                visible: custom.checked
                width: parent.width
                spacing: 2 * Theme.grid
                Item {
                    id: area
                    objectName: "saturationValueArea"
                    width: parent.width
                    height: 128
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.controlRadius
                        color: Qt.hsva(sheet.hue, 1, 1, 1)
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.controlRadius
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0; color: "white" }
                            GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
                        }
                    }
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.controlRadius
                        gradient: Gradient {
                            GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0) }
                            GradientStop { position: 1; color: "black" }
                        }
                    }
                    Rectangle {
                        x: sheet.saturation * area.width - width / 2
                        y: (1 - sheet.brightness) * area.height - height / 2
                        width: 14
                        height: 14
                        radius: 7
                        color: "transparent"
                        border.width: 2
                        border.color: "white"
                    }
                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        function at(mouse) {
                            sheet.saturation = Math.max(0, Math.min(1, mouse.x / width))
                            sheet.brightness = 1 - Math.max(0, Math.min(1, mouse.y / height))
                            sheet.current = sheet.customColor()
                            sheet.mixed = false
                            hexField.text = sheet.hexOf(sheet.current)
                        }
                        onPressed: mouse => at(mouse)
                        onPositionChanged: mouse => at(mouse)
                        onReleased: sheet.pick(sheet.customColor(), false)
                    }
                }
                Item {
                    id: hueBar
                    objectName: "hueBar"
                    width: parent.width
                    height: Theme.touchTarget
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: 16
                        radius: 8
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: "#ff0000" }
                            GradientStop { position: 0.167; color: "#ffff00" }
                            GradientStop { position: 0.333; color: "#00ff00" }
                            GradientStop { position: 0.5; color: "#00ffff" }
                            GradientStop { position: 0.667; color: "#0000ff" }
                            GradientStop { position: 0.833; color: "#ff00ff" }
                            GradientStop { position: 1.0; color: "#ff0000" }
                        }
                    }
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        x: sheet.hue * hueBar.width - width / 2
                        width: 18
                        height: 18
                        radius: 9
                        color: Qt.hsva(sheet.hue, 1, 1, 1)
                        border.width: 2
                        border.color: "white"
                    }
                    MouseArea {
                        anchors.fill: parent
                        preventStealing: true
                        function at(mouse) {
                            sheet.hue = Math.max(0, Math.min(1, mouse.x / width))
                            // A grey has no hue: the bar gives it some colour (ColorPicker's hueBar).
                            sheet.saturation = Math.max(sheet.saturation, 1 / 255)
                            sheet.brightness = Math.max(sheet.brightness, 1 / 255)
                            sheet.current = sheet.customColor()
                            sheet.mixed = false
                            hexField.text = sheet.hexOf(sheet.current)
                        }
                        onPressed: mouse => at(mouse)
                        onPositionChanged: mouse => at(mouse)
                        onReleased: sheet.pick(sheet.customColor(), false)
                    }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.darkDivider }
            Caption { text: qsTranslate("ColorPicker", "Current theme") }
            Flow {
                objectName: "themeColors"
                width: parent.width
                Repeater {
                    model: sheet.style.themeColors()
                    Swatch {
                        required property string modelData
                        swatchColor: modelData
                        chosen: !sheet.mixed && Qt.colorEqual(sheet.current, modelData)
                        // The theme's and the recent colours apply as they are, opacity included.
                        onClicked: sheet.pick(Qt.color(modelData), true)
                    }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.darkDivider }
            Caption { text: qsTranslate("ColorPicker", "Recently used") }
            Label {
                visible: recent.count === 0
                text: qsTranslate("ColorPicker", "No recent colors")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
            Flow {
                objectName: "recentColors"
                width: parent.width
                Repeater {
                    id: recent
                    Swatch {
                        required property string modelData
                        swatchColor: modelData
                        chosen: !sheet.mixed && Qt.colorEqual(sheet.current, modelData)
                        onClicked: sheet.pick(Qt.color(modelData), true)
                    }
                }
            }
        }
    }

    buttons: [
        DarkButton {
            text: qsTr("Done")
            primary: true
            onClicked: sheet.close()
        }
    ]
}
