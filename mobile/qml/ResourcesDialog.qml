import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl
import Mentis.Mobile

// FEAT-122 C1: the desktop's Resources modal (FEAT-079) on the phone, a dark modal at the top of the
// page: People, each person a card (its number, Move up and Move down in place of
// the desktop's drag by the number, ×; First name, Last name, Email, Role, Hourly cost and its Currency (FEAT-128) as the modals'
// fields, a wrong one in `danger` with the desktop's "Required" / "Invalid email address" under it),
// "+ Add person"; then Working week (Saturday, Sunday, the day's length in hours.minutes). Save (disabled
// while a field is wrong) applies it all as one Undo step; the X and Back drop it. The currency's
// last choice, "+ Add currency…", opens the desktop's Add currency modal; a saved currency has an ×
// in the list that removes it from this device. Texts are the desktop's.
// A card is folded to its header (number, name and role) when the modal opens, so a long list fits;
// a tap on the header unfolds it, a new person's card is open (user's request 2026-11-23).
// FEAT-127: the people's charts, the desktop's figures and texts (MobileResources.charts): under the
// "+ Add person" button, beside the people when the window holds both; the unfolded card is the person
// shown (the last unfolded), all folded the summary. The chart button in the title hides them.
ModalDialog {
    id: dialog
    objectName: "resourcesDialog"
    required property MobileMap map
    required property StyleChoiceMenu menu
    property int ganttScale: 0  // FEAT-127: the Gantt's Days, Weeks, Months
    function text(source) { return qsTranslate("ResourcesDialog", source) }
    function chartText(source) { return qsTranslate("GanttPeopleCharts", source) }
    // The charts beside the people when the window holds both (FEAT-127, as the desktop's rule).
    readonly property real chartsWidth: 300
    readonly property real narrowWidth: 480
    readonly property bool chartsBeside: draft.chartsShown
                                         && (parent ? parent.width - 8 * Theme.grid : 0) >= narrowWidth + 4 * Theme.grid + chartsWidth

    title: dialog.text("Resources")
    closePolicy: Popup.CloseOnEscape
    x: parent ? (parent.width - width) / 2 : 0
    y: 4 * Theme.grid
    width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, chartsBeside ? narrowWidth + 4 * Theme.grid + chartsWidth : narrowWidth)
    height: Math.min(parent ? parent.height - 8 * Theme.grid : 0, implicitHeight)

    // The open cards, by row; kept in step with the rows' adds, moves and removals.
    property var openRows: []
    function setOpen(row, open) {
        const rows = openRows.slice()
        rows[row] = open
        openRows = rows
        // The person of the charts: the card just unfolded, else the last one still unfolded.
        if (open) draft.chartRow = row
        else if (draft.chartRow === row) draft.chartRow = rows.lastIndexOf(true)
    }

    titleButtons: [
        AbstractButton {
            id: chartsToggle
            objectName: "resourcesChartsToggle"
            width: Theme.touchTarget
            height: Theme.touchTarget
            focusPolicy: Qt.NoFocus
            checkable: true
            checked: draft.chartsShown
            onToggled: draft.chartsShown = checked
            Accessible.name: draft.chartsShown ? dialog.text("Hide charts") : dialog.text("Show charts")
            contentItem: Item {
                IconImage {
                    anchors.centerIn: parent
                    width: 16
                    height: 16
                    sourceSize: Qt.size(16 * Screen.devicePixelRatio, 16 * Screen.devicePixelRatio)
                    source: "qrc:/assets/hugeicons/chart-light.svg"
                    color: Theme.darkText
                }
            }
            background: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 28
                    height: 28
                    radius: Theme.controlRadius
                    color: chartsToggle.down ? Theme.darkDivider : chartsToggle.checked ? Theme.darkSunken : "transparent"
                }
            }
        }
    ]

    MobileResources {
        id: draft
        objectName: "resourcesModel"
        map: dialog.map
        scale: dialog.ganttScale
    }

    function openResources() {
        if (!draft.load()) return
        dialog.openRows = []
        body.contentY = 0
        dialog.open()
    }
    onClosed: Qt.inputMethod.hide()

    Flickable {
        id: body
        width: dialog.availableWidth
        // The room left above the buttons on a short page.
        height: Math.max(0, Math.min(Math.max(content.height, sideCharts.visible ? sideCharts.height : 0), (dialog.parent ? dialog.parent.height - 8 * Theme.grid : 600)
                                                     - (dialog.header ? dialog.header.implicitHeight : 0)
                                                     - (dialog.footer ? dialog.footer.implicitHeight : 0) - 2 * dialog.spacing))
        contentHeight: Math.max(content.height, sideCharts.visible ? sideCharts.height : 0)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        PeopleCharts {
            id: sideCharts
            objectName: "resourcesChartsBeside"
            visible: dialog.chartsBeside
            x: content.width + 4 * Theme.grid
            width: dialog.chartsWidth
        }
        Column {
            id: content
            width: dialog.chartsBeside ? body.width - dialog.chartsWidth - 4 * Theme.grid : body.width
            spacing: 2 * Theme.grid

            Heading { text: dialog.text("People") }
            Label {
                visible: draft.people.length === 0
                width: parent.width
                height: 3 * Theme.touchTarget  // the desktop keeps three rows' room for the empty list
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                text: dialog.text("No people yet")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.bodySize
            }
            Repeater {
                id: cards
                model: draft.people
                Rectangle {
                    id: card
                    required property var modelData
                    required property int index
                    readonly property var problems: draft.problems[index] || ({})
                    readonly property bool open: dialog.openRows[index] === true
                    readonly property bool wrong: Object.keys(problems).some(key => problems[key] === true)
                    readonly property string name: (firstField.text + " " + lastField.text).trim()
                    objectName: "resourcesPerson" + index
                    width: content.width
                    height: cardColumn.implicitHeight
                    radius: 6
                    color: Theme.darkSunken
                    Column {
                        id: cardColumn
                        width: parent.width
                        leftPadding: 2 * Theme.grid
                        rightPadding: 2 * Theme.grid
                        bottomPadding: card.open ? 2 * Theme.grid : 0
                        readonly property real inner: width - leftPadding - rightPadding
                        Item {
                            width: cardColumn.inner
                            height: Theme.touchTarget
                            // The header left of the buttons folds and unfolds the card.
                            AbstractButton {
                                id: toggle
                                objectName: "resourcesToggle"
                                anchors.left: parent.left
                                anchors.leftMargin: -2 * Theme.grid  // the target reaches the card's edge
                                anchors.right: buttons.left
                                height: parent.height
                                focusPolicy: Qt.NoFocus
                                Accessible.name: card.name !== "" ? card.name : String(card.index + 1)
                                Accessible.checkable: true
                                Accessible.checked: card.open
                                onClicked: dialog.setOpen(card.index, !card.open)
                                contentItem: Item {
                                    IconImage {
                                        id: chevron
                                        anchors.left: parent.left
                                        anchors.leftMargin: 2 * Theme.grid
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: Theme.smallIconSize
                                        height: Theme.smallIconSize
                                        rotation: card.open ? 0 : -90
                                        source: "qrc:/assets/hugeicons/arrow-down.svg"
                                        color: Theme.darkText
                                        sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
                                    }
                                    Label {
                                        id: number
                                        anchors.left: chevron.right
                                        anchors.leftMargin: 2 * Theme.grid
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: card.index + 1
                                        color: !card.open && card.wrong ? Theme.danger : Theme.darkTextMuted
                                        font.pixelSize: Theme.captionSize
                                    }
                                    Column {
                                        anchors.left: number.right
                                        anchors.leftMargin: 2 * Theme.grid
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        Label {
                                            objectName: "resourcesName"
                                            visible: text !== ""
                                            width: parent.width
                                            elide: Text.ElideRight
                                            text: card.name
                                            color: !card.open && card.wrong ? Theme.danger : Theme.darkText
                                            font.pixelSize: Theme.bodySize
                                        }
                                        Label {
                                            visible: !card.open && text !== ""
                                            width: parent.width
                                            elide: Text.ElideRight
                                            text: roleField.text
                                            color: Theme.darkTextMuted
                                            font.pixelSize: Theme.captionSize
                                        }
                                    }
                                }
                                background: Rectangle { color: toggle.down ? Theme.darkDivider : "transparent"; radius: 6 }
                            }
                            Row {
                                id: buttons
                                anchors.right: parent.right
                                anchors.rightMargin: -2 * Theme.grid  // the × target reaches the card's edge
                                IconTarget {
                                    objectName: "resourcesMoveUp"
                                    enabled: card.index > 0
                                    source: "qrc:/assets/hugeicons/arrow-up.svg"
                                    Accessible.name: qsTranslate("OutlinerView", "Move up")
                                    onClicked: dialog.movePerson(card.index, card.index - 1)
                                }
                                IconTarget {
                                    objectName: "resourcesMoveDown"
                                    enabled: card.index < draft.people.length - 1
                                    source: "qrc:/assets/hugeicons/arrow-down.svg"
                                    Accessible.name: qsTranslate("OutlinerView", "Move down")
                                    onClicked: dialog.movePerson(card.index, card.index + 1)
                                }
                                AbstractButton {
                                    id: remove
                                    objectName: "resourcesRemove"
                                    width: Theme.touchTarget
                                    height: Theme.touchTarget
                                    focusPolicy: Qt.NoFocus
                                    Accessible.name: dialog.text("Remove")
                                    onClicked: {
                                        const rows = dialog.openRows.slice()
                                        rows.splice(card.index, 1)
                                        dialog.openRows = rows
                                        const shown = draft.chartRow === card.index
                                        draft.removePerson(card.index)
                                        if (shown) draft.chartRow = rows.lastIndexOf(true)  // the last unfolded
                                    }
                                    contentItem: Label {
                                        text: "×"
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: Theme.darkTextMuted
                                        font.pixelSize: Theme.largeSize
                                    }
                                }
                            }
                        }
                        // The fields stay while folded: the header reads the name and role from them.
                        Column {
                            visible: card.open
                            width: cardColumn.inner
                            PersonField { id: firstField; owner: card; field: "firstName"; label: dialog.text("First name") }
                            PersonField { id: lastField; owner: card; field: "lastName"; label: dialog.text("Last name") }
                            PersonField {
                                owner: card
                                field: "email"
                                label: dialog.text("Email")
                                hints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
                            }
                            PersonField { id: roleField; owner: card; field: "role"; label: dialog.text("Role") }
                            PersonField {
                                owner: card
                                field: "cost"
                                label: dialog.text("Hourly cost")
                                hints: Qt.ImhFormattedNumbersOnly
                                suffix: draft.rowSymbols[card.index] || ""
                            }
                            // FEAT-128: the person's currency, under their cost.
                            StyleSelectRow {
                                objectName: "resourcesCurrency"
                                width: cardColumn.inner
                                label: dialog.text("Currency")
                                menu: dialog.menu
                                choices: draft.currencies.concat([{ value: "+", name: dialog.text("+ Add currency…"), group: "" }])
                                value: draft.rowCurrencies[card.index]
                                onPicked: value => {
                                    if (value === "+") addCurrencyDialog.openAdd(card.index)
                                    else draft.chooseCurrency(card.index, value)
                                }
                                onRemoved: value => draft.removeSavedCurrency(value)
                            }
                        }
                    }

                }
            }
            DarkButton {
                objectName: "resourcesAddPerson"
                width: parent.width
                text: dialog.text("+ Add person")
                onClicked: {
                    dialog.setOpen(draft.people.length, true)
                    draft.addPerson()
                    draft.chartRow = draft.people.length - 1
                    // The new card's first name, typed at once (the desktop edits it).
                    Qt.callLater(() => {
                        const added = cards.itemAt(cards.count - 1)
                        if (!added) return
                        body.contentY = Math.max(0, Math.min(body.contentHeight - body.height, added.y))
                        const first = findField(added, "resources_firstName")
                        if (first) first.forceActiveFocus()
                    })
                }
            }
            PeopleCharts {
                objectName: "resourcesChartsUnder"
                visible: draft.chartsShown && !dialog.chartsBeside
                width: parent.width
            }

            Heading { text: dialog.text("Working week") }
            DarkSwitch {
                objectName: "resourcesSaturday"
                width: parent.width
                text: dialog.text("Saturday is a working day")
                checked: draft.saturday
                onToggled: draft.saturday = checked
            }
            DarkSwitch {
                objectName: "resourcesSunday"
                width: parent.width
                text: dialog.text("Sunday is a working day")
                checked: draft.sunday
                onToggled: draft.sunday = checked
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: dialog.text("Length of a working day")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
            StyleStepper {  // hours.minutes, − and + by a quarter hour (user, 2026-11-19)
                objectName: "resourcesDayLength"
                label: dialog.text("h")
                clock: true
                value: draft.dayMinutes
                step: draft.minutesStep
                minimum: draft.minutesStep
                maximum: draft.maxDayMinutes
                onStepped: delta => draft.dayMinutes = draft.dayMinutes + delta
                onTyped: number => draft.dayMinutes = number
            }
        }
    }

    function movePerson(from, to) {
        const rows = openRows.slice()
        const moved = rows[from] === true
        rows[from] = rows[to] === true
        rows[to] = moved
        openRows = rows
        draft.movePerson(from, to)
    }

    function findField(item, name) {
        if (item.objectName === name) return item
        for (const child of item.children) {
            const found = findField(child, name)
            if (found) return found
        }
        return null
    }

    buttons: [
        DarkButton {
            objectName: "resourcesSave"
            text: qsTranslate("ResourcesDialog", "Save")
            primary: true
            enabled: draft.valid
            onClicked: {
                Qt.inputMethod.commit()  // the word being composed belongs to its field
                draft.save()
                dialog.close()
            }
        }
    ]

    // A field of a person's card: the label, the modals' field (danger border when wrong) and the
    // desktop's tooltip text under it.
    component PersonField: Column {
        id: personField
        required property string field
        required property string label
        property string suffix: ""
        property int hints: Qt.ImhNone
        required property var owner  // the person's card: its row, person and problems
        readonly property int row: owner ? owner.index : -1
        readonly property var person: owner ? owner.modelData : ({})
        readonly property var problems: owner ? owner.problems : ({})
        property alias text: input.text
        width: parent ? parent.width : 0
        readonly property bool wrong: problems[field] === true
        Item {
            width: parent.width
            height: Theme.touchTarget
            Label {
                anchors.left: parent.left
                anchors.right: input.left
                anchors.rightMargin: 2 * Theme.grid
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: personField.label
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            TextField {
                id: input
                objectName: "resources_" + personField.field
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.round(parent.width * 0.6)
                height: Theme.touchTarget
                text: personField.person[personField.field]
                maximumLength: 200
                inputMethodHints: personField.hints
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
                verticalAlignment: TextInput.AlignVCenter
                leftPadding: 2 * Theme.grid
                rightPadding: (personField.suffix !== "" ? suffixLabel.implicitWidth + Theme.grid : 0) + 2 * Theme.grid
                selectByMouse: true
                Accessible.name: personField.label
                onTextEdited: draft.setText(personField.row, personField.field, text)
                Label {
                    id: suffixLabel
                    visible: personField.suffix !== ""
                    anchors.right: parent.right
                    anchors.rightMargin: 2 * Theme.grid
                    anchors.verticalCenter: parent.verticalCenter
                    text: personField.suffix
                    color: Theme.darkTextMuted
                    font.pixelSize: Theme.bodySize
                }
                background: Item {
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: Theme.fieldHeight
                        radius: Theme.controlRadius
                        color: Theme.darkSurface
                        border.width: 1
                        border.color: personField.wrong ? Theme.danger : input.activeFocus ? Theme.accent : Theme.darkDivider
                    }
                }
            }
        }
        Label {
            visible: personField.wrong
            width: parent.width
            horizontalAlignment: Text.AlignRight
            bottomPadding: Theme.grid
            text: personField.field === "email" ? dialog.text("Invalid email address") : dialog.text("Required")
            color: Theme.danger
            font.pixelSize: Theme.captionSize
        }
    }

    // FEAT-127: the charts, read-only (docs/design/ui-mobile.md, "Resources modal").
    component PeopleCharts: Rectangle {
        id: chartsView
        readonly property var charts: draft.charts
        readonly property var person: charts.person ? charts.person : null
        readonly property real columnWidth: draft.scale === 0 ? 10 : draft.scale === 1 ? 20 : 28
        readonly property real loadHeight: 120
        property string tip: ""  // the tapped column's date, hours and %
        onPersonChanged: tip = ""
        height: chartsColumn.implicitHeight + 4 * Theme.grid
        radius: 6
        color: Theme.darkSunken
        Column {
            id: chartsColumn
            x: 2 * Theme.grid
            y: 2 * Theme.grid
            width: chartsView.width - 4 * Theme.grid
            spacing: Theme.grid
            Label {
                objectName: "resourcesChartsEmpty"
                visible: chartsView.charts.empty === true && !chartsView.person
                width: parent.width
                wrapMode: Text.Wrap
                text: chartsView.charts.emptyText || ""
                color: Theme.darkTextMuted
                font.pixelSize: Theme.bodySize
            }
            // The team summary.
            Column {
                objectName: "resourcesChartsSummary"
                visible: chartsView.charts.empty === false && !chartsView.person
                width: parent.width
                spacing: Theme.grid
                Row {
                    width: parent.width
                    spacing: 2 * Theme.grid
                    ChartFigure { caption: dialog.chartText("Planned cost"); value: chartsView.charts.cost || ""; objectName: "resourcesChartsCost" }
                    ChartFigure { caption: dialog.chartText("Person-days"); value: chartsView.charts.personDays || ""; objectName: "resourcesChartsPersonDays" }
                }
                ChartTitle { text: dialog.chartText("Days per person") }
                Repeater {
                    model: !chartsView.visible ? [] : chartsView.charts.days || []
                    Item {
                        required property var modelData
                        width: chartsColumn.width
                        height: 24
                        Label {
                            width: 96 - Theme.grid
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideRight
                            text: modelData.name
                            color: Theme.darkText
                            font.pixelSize: Theme.captionSize
                        }
                        Rectangle {
                            id: dayBar
                            x: 96
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(0, (parent.width - 96 - 40) * modelData.fraction)
                            height: 8
                            radius: 2
                            color: Theme.accent
                        }
                        Label {
                            x: dayBar.x + dayBar.width + 6
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.value
                            color: Theme.darkTextMuted
                            font.pixelSize: Theme.captionSize
                        }
                    }
                }
                ChartTitle { visible: (chartsView.charts.roleBars || []).length > 0; text: dialog.chartText("Cost by role") }
                // FEAT-128: a bar per currency, the currency above it when there are several.
                Repeater {
                    model: !chartsView.visible ? [] : chartsView.charts.roleBars || []
                    Column {
                        id: roleBar
                        required property var modelData
                        width: chartsColumn.width
                        spacing: Theme.grid
                        Label {
                            visible: roleBar.modelData.title !== ""
                            text: roleBar.modelData.title
                            color: Theme.darkTextMuted
                            font.pixelSize: Theme.captionSize
                        }
                        Row {
                            width: parent.width
                            height: 12
                            Repeater {
                                model: roleBar.modelData.roles
                                Rectangle {
                                    required property var modelData
                                    width: chartsColumn.width * modelData.fraction
                                    height: 12
                                    color: modelData.color
                                }
                            }
                        }
                        Repeater {
                            model: roleBar.modelData.roles
                            Item {
                                required property var modelData
                                width: chartsColumn.width
                                height: 20
                                Rectangle {
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 8
                                    height: 8
                                    radius: 2
                                    color: modelData.color
                                }
                                Label {
                                    x: 16
                                    width: parent.width - 16 - percent.width - Theme.grid
                                    anchors.verticalCenter: parent.verticalCenter
                                    elide: Text.ElideRight
                                    text: modelData.name
                                    color: Theme.darkText
                                    font.pixelSize: Theme.captionSize
                                }
                                Label {
                                    id: percent
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData.percent
                                    color: Theme.darkTextMuted
                                    font.pixelSize: Theme.captionSize
                                }
                            }
                        }
                    }
                }
            }
            // A person.
            Column {
                objectName: "resourcesChartsPerson"
                visible: !!chartsView.person
                width: parent.width
                spacing: Theme.grid
                Label {
                    objectName: "resourcesChartsName"
                    width: parent.width
                    elide: Text.ElideRight
                    text: chartsView.person ? chartsView.person.name : ""
                    color: Theme.darkText
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                Label {
                    objectName: "resourcesChartsLine"
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: chartsView.person ? chartsView.person.line : ""
                    color: Theme.darkTextMuted
                    font.pixelSize: Theme.captionSize
                }
                ChartTitle { text: dialog.chartText("Load") }
                Flickable {
                    id: loadView
                    objectName: "resourcesChartsLoad"
                    width: parent.width
                    height: chartsView.loadHeight
                    contentWidth: Math.max(width, loadRow.width)
                    contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true
                    Row {
                        id: loadRow
                        height: loadView.height
                        spacing: 3
                        Repeater {
                            model: chartsView.visible && chartsView.person ? chartsView.person.load : []
                            Rectangle {
                                required property var modelData
                                width: chartsView.columnWidth
                                height: Math.max(0, (loadView.height - 8) * modelData.height)
                                y: loadView.height - height
                                radius: 2
                                color: modelData.overloaded ? Theme.danger : Theme.accent
                                TapHandler { onTapped: chartsView.tip = modelData.tip }
                            }
                        }
                    }
                    Rectangle {  // the base line
                        y: loadView.height - 1
                        width: loadView.contentWidth
                        height: 1
                        color: Theme.darkDivider
                    }
                }
                Item {  // 100 %: a dashed line over the chart, as wide as the view (not the content)
                    width: parent.width
                    height: 0
                    Repeater {
                        model: chartsView.visible ? Math.ceil(loadView.width / 6) : 0
                        Rectangle {
                            required property int index
                            x: index * 6
                            y: Math.round(-Theme.grid - loadView.height + (loadView.height - (loadView.height - 8)
                                          * (chartsView.person ? chartsView.person.top : 1)))
                            width: 3
                            height: 1
                            color: Theme.darkTextMuted
                        }
                    }
                }
                Label {
                    objectName: "resourcesChartsTip"
                    visible: chartsView.tip !== ""
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: chartsView.tip
                    color: Theme.darkTextMuted
                    font.pixelSize: Theme.captionSize
                }
                ChartTitle { text: dialog.chartText("Progress of the tasks") }
                Rectangle {
                    width: parent.width
                    height: 8
                    radius: 4
                    color: Theme.darkDivider
                    Rectangle {
                        width: parent.width * (chartsView.person ? chartsView.person.progress : 0)
                        height: parent.height
                        radius: 4
                        color: Theme.accent
                    }
                }
                Label {
                    objectName: "resourcesChartsProgress"
                    width: parent.width
                    text: chartsView.person ? chartsView.person.progressLine : ""
                    color: Theme.darkTextMuted
                    font.pixelSize: Theme.captionSize
                }
            }
        }
    }

    component ChartTitle: Label {
        width: parent ? parent.width : 0
        topPadding: Theme.grid
        color: Theme.darkText
        font.pixelSize: Theme.bodySize
        font.weight: Font.DemiBold
    }

    component ChartFigure: Column {
        id: chartFigure
        required property string caption
        required property string value
        width: parent ? (parent.width - 2 * Theme.grid) / 2 : 0  // two side by side
        spacing: 2
        Label {
            width: parent.width
            elide: Text.ElideRight
            text: chartFigure.caption
            color: Theme.darkTextMuted
            font.pixelSize: Theme.captionSize
        }
        Label {
            objectName: chartFigure.objectName + "Value"
            width: parent.width
            elide: Text.ElideRight
            text: chartFigure.value
            color: Theme.darkText
            font.pixelSize: Theme.subtitleSize
            font.weight: Font.DemiBold
        }
    }

    component Heading: Label {
        width: parent ? parent.width : 0
        height: Theme.touchTarget
        verticalAlignment: Text.AlignVCenter
        color: Theme.darkText
        font.pixelSize: 13
        font.weight: Font.DemiBold
    }

    component IconTarget: AbstractButton {
        id: target
        property url source
        width: Theme.touchTarget
        height: Theme.touchTarget
        focusPolicy: Qt.NoFocus
        contentItem: Item {
            IconImage {
                anchors.centerIn: parent
                width: Theme.smallIconSize
                height: Theme.smallIconSize
                source: target.source
                color: Theme.darkText
                opacity: target.enabled ? 1 : 0.4
                sourceSize: Qt.size(Theme.smallIconSize * Screen.devicePixelRatio, Theme.smallIconSize * Screen.devicePixelRatio)
            }
        }
        background: Rectangle {
            anchors.centerIn: parent
            width: Theme.buttonHeight
            height: Theme.buttonHeight
            radius: Theme.controlRadius
            color: target.down ? Theme.darkDivider : "transparent"
        }
    }

    // The desktop's Add currency modal: the text, Code and Symbol side by side (no spaces), the note
    // when the currency is already there; Add saves it on this device and chooses it.
    ModalDialog {
        id: addCurrencyDialog
        objectName: "addCurrencyDialog"
        parent: Overlay.overlay
        closePolicy: Popup.CloseOnEscape
        x: parent ? (parent.width - width) / 2 : 0
        y: 4 * Theme.grid
        width: Math.min(parent ? parent.width - 8 * Theme.grid : 0, 420)
        title: dialog.text("Add currency")
        readonly property int check: draft.currencyCheck(codeField.text, symbolField.text)
        property int row: -1  // the person who gets the added currency
        function openAdd(person) {
            row = person
            codeField.text = ""
            symbolField.text = ""
            addCurrencyDialog.open()
            codeField.forceActiveFocus()
        }
        onAccepted: draft.addCurrency(row, codeField.text, symbolField.text)
        onClosed: Qt.inputMethod.hide()
        Column {
            width: addCurrencyDialog.availableWidth
            spacing: 2 * Theme.grid
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                text: dialog.text("It is saved on this computer, to use it in other documents too.")
                color: Theme.darkText
                font.pixelSize: Theme.bodySize
            }
            Row {
                width: parent.width
                spacing: 3 * Theme.grid
                CurrencyField {
                    id: codeField
                    objectName: "addCurrencyCode"
                    caption: dialog.text("Code")
                    placeholder: "RON"
                    length: 12
                }
                CurrencyField {
                    id: symbolField
                    objectName: "addCurrencySymbol"
                    caption: dialog.text("Symbol (optional)")
                    placeholder: "lei"
                    length: 5
                }
            }
            Label {
                objectName: "addCurrencyNote"
                opacity: addCurrencyDialog.check === 1 ? 1 : 0  // its room kept while typing
                width: parent.width
                wrapMode: Text.Wrap
                text: dialog.text("This currency is already in the list")
                color: Theme.darkTextMuted
                font.pixelSize: Theme.captionSize
            }
        }
        buttons: [
            DarkButton {
                objectName: "addCurrencyAdd"
                text: dialog.text("Add")
                primary: true
                enabled: addCurrencyDialog.check === 2
                onClicked: { Qt.inputMethod.commit(); addCurrencyDialog.accept() }
            }
        ]
    }

    component CurrencyField: Column {
        id: currencyField
        property alias text: field.text
        required property string caption
        required property string placeholder
        required property int length
        width: (addCurrencyDialog.availableWidth - 3 * Theme.grid) / 2
        spacing: Theme.grid
        function forceActiveFocus() { field.forceActiveFocus() }
        Label {
            width: parent.width
            elide: Text.ElideRight
            text: currencyField.caption
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
        }
        TextField {
            id: field
            objectName: currencyField.objectName + "Input"
            width: parent.width
            height: Theme.touchTarget
            maximumLength: currencyField.length
            placeholderText: currencyField.placeholder
            placeholderTextColor: Theme.darkTextMuted
            inputMethodHints: Qt.ImhNoPredictiveText
            // No spaces: the currency is stored as "code symbol" and read back at the last space.
            validator: RegularExpressionValidator { regularExpression: /\S*/ }
            color: Theme.darkText
            font.pixelSize: Theme.bodySize
            verticalAlignment: TextInput.AlignVCenter
            leftPadding: 2 * Theme.grid
            rightPadding: 2 * Theme.grid
            Accessible.name: currencyField.caption
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
    }
}
