#include "gantttaskpanel.h"

#include "branchpath.h"  // the map's end marks
#include "datepicker.h"
#include "floatingui.h"   // FloatingListPopup
#include "ganttcommands.h"
#include "ganttnamefield.h"
#include "ganttstylechoices.h"
#include "gantttaskedits.h"
#include "ganttview.h"     // GanttCanvas::typeName
#include "mindmapmodel.h"  // MindNode::kMaxPriority
#include "modaldialog.h"
#include "nodesketch.h"    // the Fill selects' pictures
#include "nodestyle.h"     // NodeStyle::Reading
#include "numberfield.h"
#include "segmentedcontrol.h"
#include "stylecontrols.h"  // ColorField
#include "toggleswitch.h"
#include "uitheme.h"

#include <QApplication>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QShowEvent>
#include <utility>
#include <QLabel>
#include <QMouseEvent>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>

namespace {

constexpr GanttDependency::Type kTypes[] = {GanttDependency::Type::FinishStart, GanttDependency::Type::StartStart,
                                            GanttDependency::Type::FinishFinish, GanttDependency::Type::StartFinish};
constexpr int kLinkLine = 220;   // a link's task name and its summary on one line, elided
constexpr int kLagWidth = 80;    // "-3650" between − and +

int side(bool predecessors) { return predecessors ? 1 : 0; }

// Whole numbers within the plan's limits; the unit is in the caption.
NumberField *numberField(int low, int high) {
    auto *field = new NumberField;
    field->setRange(low, high);
    return field;
}

// Scrolling the panel over a field scrolls the panel: only a focused field takes the wheel (a day
// stepped by the wheel moved a task's start without anyone noticing). Qt's selects and date fields
// default to WheelFocus, which would focus them on the wheel first.
template <typename Field>
class PanelWheel : public Field {
public:
    PanelWheel() { this->setFocusPolicy(Qt::StrongFocus); }

protected:
    void wheelEvent(QWheelEvent *event) override {
        if (this->hasFocus()) Field::wheelEvent(event);
        else event->ignore();  // on to the scroll area
    }
};

// The panel's selects: the arrow in the panel's text colour and, on a click, the app's select list
// (FloatingListPopup, as the sheet and zoom selects) in the card's colours, 4 px below the field as
// the calendar (user, 2026-10-28: Qt's own list did not match the app).
class PanelSelect final : public PanelWheel<QComboBox> {
public:
    DatePicker::Colors colors;  // set by GanttTaskPanel::setColors
    int gridColumns = 0;  // FEAT-080: > 0 = items with pictures in a grid menu, as the Style page's
    int gridNone = -1;    // the grid's item shown as a wide tile at the bottom ("None"), -1 = none

    // Link rows are rebuilt on every load: an open list must not call back into a deleted select.
    ~PanelSelect() override {
        if (m_popup) {
            m_popup->chosen = nullptr;
            m_popup->close();
        }
    }

    void showPopup() override {
        // As FloatingSelect: the press that closed the list does not open it again.
        if (m_popup) { m_popup->close(); return; }
        if (m_popupClosed.isValid() && m_popupClosed.elapsed() < 250) return;
        if (gridColumns > 0) {
            openIconGridMenu(this, gridColumns, {colors.card, colors.line, colors.text, colors.hover}, gridNone);
            return;
        }
        FloatingListPopup::Colors list{colors.card, colors.line, colors.text, colors.hover, colors.hover};
        list.fontPixelSize = UiTheme::fontPx(12);  // the panel's text
        // As wide as its longest item (padding of the items and of the list), never cut.
        QFont font = this->font();
        font.setPixelSize(list.fontPixelSize);
        const QFontMetrics metrics(font);
        int widest = 0;
        for (int i = 0; i < count(); ++i) widest = std::max(widest, metrics.horizontalAdvance(itemText(i)));
        auto *popup = new FloatingListPopup(this, std::max(width(), widest + 2 * (8 + FloatingUi::kPadding) + 8), list);
        m_popup = popup;
        QObject::connect(popup, &QObject::destroyed, this, [this] { m_popupClosed.start(); update(); });
        popup->chosen = [this](int index) {
            setCurrentIndex(index);
            emit activated(index);
        };
        const QRect global(mapToGlobal(QPoint(0, 0)), size());
        QPoint at(global.left(), global.bottom() + 1 + kGap);
        if (const QScreen *s = screen()) {
            const QRect available = s->availableGeometry();
            if (at.y() + popup->height() > available.bottom()) at.setY(global.top() - kGap - popup->height());
            at.setX(std::clamp(at.x(), available.left(), std::max(available.left(), available.right() - popup->width())));
        }
        popup->move(at);
        popup->show();
    }
    void hidePopup() override { if (m_popup) m_popup->close(); }

protected:
    void paintEvent(QPaintEvent *event) override {
        QComboBox::paintEvent(event);
        DateField::paintChevron(this, colors.text);
    }

private:
    static constexpr int kGap = 4;  // as DatePicker
    QPointer<FloatingListPopup> m_popup;
    QElapsedTimer m_popupClosed;
};

// A date field with the app's calendar (DatePicker).
class PanelDateField final : public PanelWheel<DateField> {
public:
    PanelDateField() {
        setKeyboardTracking(false);  // a typed date is applied when confirmed
        setDateRange(QDate(GanttPlan::kFirstYear, 1, 1), QDate(GanttPlan::kLastYear, 12, 31));
        setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
    }
};

// A section's title row: a click on it (not on its "+ Add") closes or opens the section.
class SectionTitleRow final : public QWidget {
public:
    std::function<void()> clicked;

protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && clicked) clicked();
    }
};

// FEAT-081: the Resources table (user, 2026-10-30): header 24 px, rows 28 px, × column last.
constexpr int kPeopleHeaderHeight = 24;
constexpr int kPeopleRowHeight = 28;
constexpr int kRemoveColumnWidth = 24;
constexpr int kResourceColumnCount = 3;  // first name, last name, role (hidden, moved); then % and ×
constexpr int kPercentColumn = kResourceColumnCount;  // FEAT-083: always shown, before the ×
constexpr int kRemoveColumn = kResourceColumnCount + 1;
constexpr int kPercentColumnWidth = 72;
const QStringList &resourceColumnKeys() {
    static const QStringList keys{QStringLiteral("first"), QStringLiteral("last"), QStringLiteral("role")};
    return keys;
}
const QString kResourceColumnsSetting = QStringLiteral("gantt/resourceColumns");
const QString kClosedSectionsSetting = QStringLiteral("gantt/closedSections");

QLabel *caption(const QString &text) {
    auto *label = new QLabel(text);
    label->setProperty("caption", true);
    return label;
}

// As the map's Style page (user, 2026-10-28; stylecontrols.cpp): controls 24 px high in a column of
// 148 px at the right (72 for the colour), 12 px between rows, sections with a 1 px line and 20 px
// above and below it.
constexpr int kFieldWidth = 148;
constexpr int kNarrowFieldWidth = 72;
// Tight enough that the panel fits without scrolling; only long link lists scroll (user, 2026-10-29).
constexpr int kRowGap = 6;
constexpr int kSectionGap = 10;

QFrame *divider() {
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("GanttPanelDivider"));
    line->setFixedHeight(1);
    return line;
}

// A grid of rows: the caption at the left, the control in the column at the right.
QGridLayout *fieldGrid() {
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(kRowGap);
    grid->setColumnStretch(0, 1);
    grid->setColumnMinimumWidth(1, kFieldWidth);
    return grid;
}

void addField(QGridLayout *grid, const QString &label, QWidget *field) {
    const int row = grid->rowCount();
    QLabel *text = caption(label);
    grid->addWidget(text, row, 0);
    grid->addWidget(field, row, 1, Qt::AlignLeft);
    field->setProperty("caption", QVariant::fromValue(text));
}

// A field and its caption, shown or hidden together (the grid row closes up).
void showRow(QWidget *field, bool shown) {
    field->setVisible(shown);
    if (auto *text = field->property("caption").value<QLabel *>()) text->setVisible(shown);
}

}  // namespace

void GanttTaskPanel::setPanelWidth(int width) {
    if (width == this->width() && minimumWidth() == width) return;
    setFixedWidth(width);
    for (QWidget *content : std::as_const(m_pageContents)) content->setMaximumWidth(width);
}

GanttTaskPanel::GanttTaskPanel(QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("GanttTaskPanel"));
    setFixedWidth(kWidth);
    setAttribute(Qt::WA_StyledBackground);
    auto *outer = new QVBoxLayout(this);
    // The pages stop above the card's radius: reaching the bottom they squared its corners (user's photo, 2026-10-29).
    outer->setContentsMargins(0, 12, 0, GanttColors::kCardRadius);
    outer->setSpacing(0);
    // FEAT-080: the pages' selector as the map's side panel (FEAT-017); no close X, the top bar's
    // button and Esc close the panel.
    for (const QString &name : QSettings().value(kClosedSectionsSetting).toStringList()) m_closedSections.insert(name);
    m_pageSelector = new SegmentedControl({tr("Task"), tr("Gantt")});
    m_pageSelector->setObjectName(QStringLiteral("GanttPanelPages"));
    auto *selectorRow = new QHBoxLayout;
    selectorRow->setContentsMargins(16, 0, 16, 0);
    selectorRow->addWidget(m_pageSelector);
    outer->addLayout(selectorRow);
    m_pages = new QStackedWidget;
    outer->addWidget(m_pages, 1);
    const auto scrollPage = [this](const QString &name) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *content = new QWidget;
        content->setObjectName(name);
        scroll->setWidget(content);
        scroll->viewport()->setAutoFillBackground(false);
        // Never wider than the panel: a link card added after the panel opened asked for more on the
        // first layout and pushed the fields and buttons out (seen on a picture of the panel).
        content->setMaximumWidth(kWidth);
        m_pageContents.append(content);
        m_pages->addWidget(scroll);
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(16, 12, 16, 16);
        layout->setSpacing(0);
        return layout;
    };
    QVBoxLayout *layout = scrollPage(QStringLiteral("GanttTaskPanelContent"));
    auto *noTask = new QLabel(tr("No task selected"));
    noTask->setObjectName(QStringLiteral("GanttPanelNoTask"));
    noTask->setProperty("muted", true);
    noTask->setAlignment(Qt::AlignCenter);
    m_pages->addWidget(noTask);
    QVBoxLayout *styleLayout = scrollPage(QStringLiteral("GanttStylePageContent"));
    connect(m_pageSelector, &SegmentedControl::segmentActivated, this, [this](int index) {
        // A field being edited is confirmed; the selector keeps the focus for its arrow keys.
        if (QWidget *focus = QApplication::focusWidget(); focus && m_pages->isAncestorOf(focus)) focus->clearFocus();
        showPage(index == 0 ? Page::Task : Page::Gantt);
    });

    // Sections as the Style page's: Task, Dates, Details (user, 2026-10-28), then the links.
    QGridLayout *taskGrid = fieldGrid();
    QGridLayout *datesGrid = fieldGrid();
    QGridLayout *detailsGrid = fieldGrid();
    m_name = new GanttNameField;  // FEAT-124: with the name's formats
    m_name->setFixedWidth(kFieldWidth);
    addField(taskGrid, tr("Name"), m_name);
    // On, the topic gets a checkbox (FEAT-048) and the task is a diamond on its start day; its
    // duration is kept for when it is turned off.
    m_milestone = new ToggleSwitch;
    m_milestone->setAccessibleName(tr("Milestone"));
    addField(taskGrid, tr("Milestone"), m_milestone);
    // The name on the bar: summaries show it by default, tasks do not (user, 2026-10-27).
    m_label = new ToggleSwitch;
    m_label->setAccessibleName(tr("Name on the bar"));
    addField(taskGrid, tr("Name on the bar"), m_label);
    QWidget *taskSection = section(QStringLiteral("GanttSectionTask"), tr("Task"), taskGrid);
    // No title above the sections: it repeated "Task" (user's picture, 2026-10-30); the section's
    // title counts the tasks when several are selected.
    m_title = taskSection->findChild<QLabel *>(QStringLiteral("SectionTitle"));
    layout->addWidget(taskSection);
    // FEAT-080: the task's own style (user, 2026-11-01): colour (the topic's branch colour, map
    // too), shape, fill and border, each with a × back to the sheet's. The pictures are the Gantt
    // page's (fillBarShapes).
    QGridLayout *styleGrid = fieldGrid();
    const auto styleRow = [styleGrid](const QString &label, const QList<QWidget *> &fields, const QString &resetTip) {
        auto *box = new QWidget;
        auto *boxLayout = new QHBoxLayout(box);
        boxLayout->setContentsMargins(0, 0, 0, 0);
        boxLayout->setSpacing(4);
        for (QWidget *field : fields) boxLayout->addWidget(field);
        auto *reset = new QToolButton;
        reset->setText(QStringLiteral("×"));
        reset->setToolTip(resetTip);
        boxLayout->addWidget(reset);
        boxLayout->addStretch();
        addField(styleGrid, label, box);
        return reset;
    };
    const auto ownSelect = [](const QString &name, int columns) {
        auto *select = new PanelSelect;
        select->gridColumns = columns;
        select->setObjectName(name);
        select->setFixedWidth(kNarrowFieldWidth);
        return select;
    };
    m_color = new ColorField;
    m_color->setAccessibleName(tr("Colour"));
    m_color->allowOpacity = false;
    m_color->setFixedWidth(kNarrowFieldWidth);
    m_defaultColor = styleRow(tr("Colour"), {m_color}, tr("Default colour"));
    m_ownBarShape = ownSelect(QStringLiteral("GanttTaskBarShape"), 2);
    m_ownSummaryShape = ownSelect(QStringLiteral("GanttTaskSummaryShape"), 4);
    m_ownMilestoneShape = ownSelect(QStringLiteral("GanttTaskMilestoneShape"), 4);
    m_defaultShape = styleRow(tr("Shape"), {m_ownBarShape, m_ownSummaryShape, m_ownMilestoneShape}, tr("Default shape"));
    m_ownFill = ownSelect(QStringLiteral("GanttTaskFill"), 2);
    m_defaultFill = styleRow(tr("Fill"), {m_ownFill}, tr("Default fill"));
    m_ownBorder = ownSelect(QStringLiteral("GanttTaskBorder"), 2);
    m_defaultBorder = styleRow(tr("Border"), {m_ownBorder}, tr("Default border"));
    // The border's own colour (user, 2026-11-22), × back to the sheet's.
    m_ownBorderColor = new ColorField;
    m_ownBorderColor->setObjectName(QStringLiteral("GanttTaskBorderColor"));
    m_ownBorderColor->setAccessibleName(tr("Border colour"));
    m_ownBorderColor->allowOpacity = false;
    m_ownBorderColor->setFixedWidth(kNarrowFieldWidth);
    m_defaultBorderColor = styleRow(tr("Border colour"), {m_ownBorderColor}, tr("Default border colour"));
    m_styleSection = section(QStringLiteral("GanttSectionStyle"), tr("Style"), styleGrid);
    layout->addWidget(m_styleSection);

    // Start: a date with a clear button, or "Add date" for a topic not planned yet.
    auto *startBox = new QWidget;
    startBox->setFixedWidth(kFieldWidth);
    auto *startLayout = new QHBoxLayout(startBox);
    startLayout->setContentsMargins(0, 0, 0, 0);
    startLayout->setSpacing(4);
    m_start = new PanelDateField;
    m_clearStart = new QToolButton;
    m_clearStart->setText(QStringLiteral("×"));
    m_clearStart->setToolTip(tr("Remove date"));
    m_addStart = new QPushButton(tr("Add date"));
    m_addStart->setCursor(Qt::PointingHandCursor);
    startLayout->addWidget(m_start, 1);
    startLayout->addWidget(m_clearStart);
    startLayout->addWidget(m_addStart, 1);
    addField(datesGrid, tr("Start"), startBox);
    m_duration = numberField(GanttPlan::kMinDuration, GanttPlan::kMaxDuration);
    m_duration->setFixedWidth(kFieldWidth);
    addField(datesGrid, tr("Duration (days)"), m_duration);
    m_end = new PanelDateField;
    m_end->setFixedWidth(kFieldWidth);
    addField(datesGrid, tr("End"), m_end);
    m_datesSection = section(QStringLiteral("GanttSectionDates"), tr("Dates"), datesGrid);
    layout->addWidget(m_datesSection);

    m_progress = numberField(0, GanttPlan::kMaxProgress);
    m_progress->setFixedWidth(kFieldWidth);
    addField(detailsGrid, tr("Progress (%)"), m_progress);
    m_priority = new PanelSelect;
    m_priority->setFixedWidth(kFieldWidth);
    m_priority->addItem(tr("None"));
    for (int p = 1; p <= MindNode::kMaxPriority; ++p) m_priority->addItem(GanttCommands::priorityName(p));
    addField(detailsGrid, tr("Priority"), m_priority);
    m_detailsSection = section(QStringLiteral("GanttSectionDetails"), tr("Details"), detailsGrid);
    layout->addWidget(m_detailsSection);

    // FEAT-081: the people assigned in a table, "+ Add" at the right of the title (user, 2026-10-30).
    m_addPerson = new QToolButton;
    m_addPerson->setObjectName(QStringLiteral("GanttAddPerson"));
    m_addPerson->setText(tr("+ Add"));
    m_addPerson->setCursor(Qt::PointingHandCursor);
    auto *people = new QVBoxLayout;
    people->setContentsMargins(0, 0, 0, 0);
    people->setSpacing(0);
    m_noPeople = new QLabel(tr("None"));
    m_noPeople->setProperty("muted", true);
    people->addWidget(m_noPeople);
    m_peopleTable = new QTableWidget(0, kRemoveColumn + 1);
    m_peopleTable->setObjectName(QStringLiteral("GanttPeopleTable"));
    m_peopleTable->setHorizontalHeaderLabels({tr("First name"), tr("Last name"), tr("Role"), QStringLiteral("%"), QString()});
    m_peopleTable->verticalHeader()->hide();
    m_peopleTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_peopleTable->verticalHeader()->setDefaultSectionSize(kPeopleRowHeight);
    m_peopleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_peopleTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_peopleTable->setFocusPolicy(Qt::NoFocus);
    m_peopleTable->setShowGrid(false);
    m_peopleTable->setFrameShape(QFrame::NoFrame);
    m_peopleTable->setWordWrap(false);
    m_peopleTable->setTextElideMode(Qt::ElideRight);
    // No scrolling of its own: it grows with its rows, the panel scrolls.
    m_peopleTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_peopleTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    QHeaderView *columns = m_peopleTable->horizontalHeader();
    columns->setFixedHeight(kPeopleHeaderHeight);
    columns->setHighlightSections(false);
    columns->setSectionsMovable(true);
    columns->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    columns->setMinimumSectionSize(kRemoveColumnWidth);
    for (int column = 0; column < kResourceColumnCount; ++column) columns->setSectionResizeMode(column, QHeaderView::Stretch);
    columns->setSectionResizeMode(kPercentColumn, QHeaderView::Fixed);
    columns->resizeSection(kPercentColumn, kPercentColumnWidth);
    columns->setSectionResizeMode(kRemoveColumn, QHeaderView::Fixed);
    columns->resizeSection(kRemoveColumn, kRemoveColumnWidth);
    columns->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(columns, &QHeaderView::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        menu.setStyleSheet(property("menuStyle").toString());
        fillResourceColumnMenu(&menu);
        menu.exec(m_peopleTable->horizontalHeader()->mapToGlobal(pos));
    });
    connect(columns, &QHeaderView::sectionMoved, this, [this] { resourceColumnMoved(); });
    restoreResourceColumns();
    people->addWidget(m_peopleTable);
    m_resourcesSection = section(QStringLiteral("GanttSectionResources"), tr("Resources"), people, m_addPerson);
    layout->addWidget(m_resourcesSection);

    // Predecessors and successors: one card per link that says what it means; "+ Add" at the right
    // of the section's title, as the Style page's selects next to Shape or Branch.
    m_linkSections = new QWidget;
    auto *sections = new QVBoxLayout(m_linkSections);
    sections->setContentsMargins(0, 0, 0, 0);
    sections->setSpacing(0);
    for (const bool predecessors : {true, false}) {
        QToolButton *&add = m_addLink[side(predecessors)];
        add = new QToolButton;
        add->setObjectName(predecessors ? QStringLiteral("GanttAddPredecessor") : QStringLiteral("GanttAddSuccessor"));
        add->setText(tr("+ Add"));
        add->setToolTip(predecessors ? tr("Add a task this one waits for") : tr("Add a task that waits for this one"));
        add->setCursor(Qt::PointingHandCursor);
        auto *list = new QVBoxLayout;
        list->setContentsMargins(0, 0, 0, 0);
        list->setSpacing(6);
        m_noLinks[side(predecessors)] = new QLabel(tr("None"));
        m_noLinks[side(predecessors)]->setProperty("muted", true);
        list->addWidget(m_noLinks[side(predecessors)]);
        m_linkLists[side(predecessors)] = new QVBoxLayout;
        m_linkLists[side(predecessors)]->setSpacing(6);
        list->addLayout(m_linkLists[side(predecessors)]);
        QWidget *box = section(predecessors ? QStringLiteral("GanttSectionPredecessors") : QStringLiteral("GanttSectionSuccessors"),
                               predecessors ? tr("Predecessors") : tr("Successors"), list, add);
        box->findChild<QLabel *>(QStringLiteral("SectionTitle"))
            ->setToolTip(predecessors ? tr("Tasks this one waits for") : tr("Tasks that wait for this one"));
        sections->addWidget(box);
        connect(add, &QToolButton::clicked, this, [this, predecessors] { showAddMenu(predecessors); });
    }
    layout->addWidget(m_linkSections);
    layout->addStretch();

    // FEAT-080: the Gantt page, styles of the whole chart, a section per kind (user, 2026-10-29):
    // tasks and summaries with their shape and colour, then the relations.
    // Pictures of the bars as the map's Border and Fill selects (user, 2026-10-29), drawn in setColors.
    const auto pictureSelect = [](const QString &name, int columns = 2) {
        auto *select = new PanelSelect;
        select->gridColumns = columns;
        select->setObjectName(name);
        select->setFixedWidth(kFieldWidth);
        return select;
    };
    m_barShape = pictureSelect(QStringLiteral("GanttBarShape"));
    m_summaryShape = pictureSelect(QStringLiteral("GanttSummaryShape"), 4);
    m_milestoneShape = pictureSelect(QStringLiteral("GanttMilestoneShape"), 4);
    m_fillStyle[int(GanttPlan::BarKind::Task)] = pictureSelect(QStringLiteral("GanttTaskFill"));
    m_fillStyle[int(GanttPlan::BarKind::Summary)] = pictureSelect(QStringLiteral("GanttSummaryFill"));
    m_fillStyle[int(GanttPlan::BarKind::Milestone)] = pictureSelect(QStringLiteral("GanttMilestoneFill"));
    m_borderStyle[int(GanttPlan::BarKind::Task)] = pictureSelect(QStringLiteral("GanttTaskBorder"));
    m_borderStyle[int(GanttPlan::BarKind::Summary)] = pictureSelect(QStringLiteral("GanttSummaryBorder"));
    m_borderStyle[int(GanttPlan::BarKind::Milestone)] = pictureSelect(QStringLiteral("GanttMilestoneBorder"));
    m_linkLine = pictureSelect(QStringLiteral("GanttLinkLine"), 3);
    m_linkEnd = pictureSelect(QStringLiteral("GanttLinkEnd"), 3);
    m_linkWidth = pictureSelect(QStringLiteral("GanttLinkWidth"), 4);
    m_linkEndSize = pictureSelect(QStringLiteral("GanttLinkEndSize"), 3);
    fillBarShapes(palette().color(QPalette::WindowText));  // again in the card's colour (setColors)
    // The sheet's colours (user, 2026-10-29): every bar without a colour chosen on its task; the
    // × goes back to the default peach or lilac, or the milestones' slate.
    const QString names[GanttPlan::kBarKinds]{tr("Tasks"), tr("Summaries"), tr("Milestones")};
    const QString sectionNames[GanttPlan::kBarKinds]{QStringLiteral("GanttSectionTasks"), QStringLiteral("GanttSectionSummaries"),
                                                 QStringLiteral("GanttSectionMilestones")};
    QComboBox *const shapes[GanttPlan::kBarKinds]{m_barShape, m_summaryShape, m_milestoneShape};
    // A colour field with a × shown when the sheet has its own colour.
    const auto colorRow = [](QGridLayout *grid, const QString &label, const QString &accessibleName, const QString &resetTip,
                             ColorField *&field, QToolButton *&reset) {
        auto *box = new QWidget;
        auto *row = new QHBoxLayout(box);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);
        field = new ColorField;
        field->setAccessibleName(accessibleName);
        field->allowOpacity = false;
        field->setFixedWidth(kNarrowFieldWidth);
        reset = new QToolButton;
        reset->setText(QStringLiteral("×"));
        reset->setToolTip(resetTip);
        reset->setAccessibleName(resetTip);
        reset->hide();
        row->addWidget(field);
        row->addWidget(reset);
        addField(grid, label, box);
    };
    for (int i = 0; i < GanttPlan::kBarKinds; ++i) {
        const auto kind = GanttPlan::BarKind(i);
        QGridLayout *grid = fieldGrid();
        addField(grid, tr("Shape"), shapes[i]);
        QComboBox *fill = m_fillStyle[i];
        addField(grid, tr("Fill"), fill);
        connect(fill, &QComboBox::activated, this, [this, kind, fill](int index) {
            if (setFillStyle) setFillStyle(kind, NodeFillStyle(fill->itemData(index).toInt()));
        });
        QComboBox *border = m_borderStyle[i];
        addField(grid, tr("Border"), border);
        connect(border, &QComboBox::activated, this, [this, kind, border](int index) {
            if (setBorderStyle) setBorderStyle(kind, GanttPlan::BorderStyle(border->itemData(index).toInt()));
        });
        // The border's colour (user, 2026-11-22); × back to automatic.
        colorRow(grid, tr("Border colour"), tr("Border colour"), tr("Default border colour"), m_sheetBorderColor[i], m_sheetBorderColorReset[i]);
        colorRow(grid, tr("Colour"), names[i], tr("Default colour"), m_sheetColor[i], m_sheetColorReset[i]);
        styleLayout->addWidget(section(sectionNames[i], names[i], grid));
        connect(m_sheetColor[i], &ColorField::colorChosen, this, [this, kind](const QColor &color) {
            if (setDefaultColor) setDefaultColor(kind, color);
        });
        connect(m_sheetColorReset[i], &QToolButton::clicked, this, [this, kind] {
            if (setDefaultColor) setDefaultColor(kind, std::nullopt);
        });
        connect(m_sheetBorderColor[i], &ColorField::colorChosen, this, [this, kind](const QColor &color) {
            if (setDefaultBorderColor) setDefaultBorderColor(kind, color);
        });
        connect(m_sheetBorderColorReset[i], &QToolButton::clicked, this, [this, kind] {
            if (setDefaultBorderColor) setDefaultBorderColor(kind, std::nullopt);
        });
    }
    // The arrows' look, the map relationships' Line selects (user, 2026-10-29): only the end has a mark.
    QGridLayout *relationsGrid = fieldGrid();
    addField(relationsGrid, tr("Line"), m_linkLine);
    addField(relationsGrid, tr("End"), m_linkEnd);
    addField(relationsGrid, tr("Thickness"), m_linkWidth);  // user, 2026-11-01
    addField(relationsGrid, tr("End size"), m_linkEndSize);
    // Their colour (user, 2026-11-22); × back to the view's grey.
    colorRow(relationsGrid, tr("Colour"), tr("Relations"), tr("Default colour"), m_linkColor, m_linkColorReset);
    connect(m_linkColor, &ColorField::colorChosen, this, [this](const QColor &color) {
        if (setLinkColor) setLinkColor(color);
    });
    connect(m_linkColorReset, &QToolButton::clicked, this, [this] {
        if (setLinkColor) setLinkColor(std::nullopt);
    });
    styleLayout->addWidget(section(QStringLiteral("GanttSectionRelations"), tr("Relations"), relationsGrid));
    styleLayout->addStretch();
    connect(m_barShape, &QComboBox::activated, this, [this](int index) {
        if (setBarShape) setBarShape(GanttPlan::BarShape(m_barShape->itemData(index).toInt()));
    });
    // FEAT-080: the Style section's own values, on every task shown.
    using Own = GanttPlan::OwnStyle;
    const auto setOwn = [this](Own part, const QString &key) {
        if (setTaskStyle) setTaskStyle(m_targets, part, key);
    };
    const auto value = [](QComboBox *select, int index) { return select->itemData(index).toInt(); };
    connect(m_ownBarShape, &QComboBox::activated, this, [this, setOwn, value](int index) {
        setOwn(Own::Shape, GanttPlan::styleKey(GanttPlan::BarShape(value(m_ownBarShape, index))));
    });
    connect(m_ownSummaryShape, &QComboBox::activated, this, [this, setOwn, value](int index) {
        setOwn(Own::Shape, GanttPlan::styleKey(GanttPlan::SummaryShape(value(m_ownSummaryShape, index))));
    });
    connect(m_ownMilestoneShape, &QComboBox::activated, this, [this, setOwn, value](int index) {
        setOwn(Own::Shape, GanttPlan::styleKey(GanttPlan::MilestoneShape(value(m_ownMilestoneShape, index))));
    });
    connect(m_ownFill, &QComboBox::activated, this, [this, setOwn, value](int index) {
        setOwn(Own::Fill, GanttPlan::styleKey(NodeFillStyle(value(m_ownFill, index))));
    });
    connect(m_ownBorder, &QComboBox::activated, this, [this, setOwn, value](int index) {
        setOwn(Own::Border, GanttPlan::styleKey(GanttPlan::BorderStyle(value(m_ownBorder, index))));
    });
    connect(m_defaultShape, &QToolButton::clicked, this, [setOwn] { setOwn(Own::Shape, QString()); });
    connect(m_defaultFill, &QToolButton::clicked, this, [setOwn] { setOwn(Own::Fill, QString()); });
    connect(m_defaultBorder, &QToolButton::clicked, this, [setOwn] { setOwn(Own::Border, QString()); });
    connect(m_summaryShape, &QComboBox::activated, this, [this](int index) {
        if (setSummaryShape) setSummaryShape(GanttPlan::SummaryShape(m_summaryShape->itemData(index).toInt()));
    });
    connect(m_milestoneShape, &QComboBox::activated, this, [this](int index) {
        if (setMilestoneShape) setMilestoneShape(GanttPlan::MilestoneShape(m_milestoneShape->itemData(index).toInt()));
    });
    connect(m_linkLine, &QComboBox::activated, this, [this](int index) {
        if (setLinkLine) setLinkLine(static_cast<GanttPlan::LinkLine>(m_linkLine->itemData(index).toInt()));
    });
    connect(m_linkEnd, &QComboBox::activated, this, [this](int index) {
        if (setLinkEnd) setLinkEnd(static_cast<BranchEnd>(m_linkEnd->itemData(index).toInt()));
    });
    connect(m_linkWidth, &QComboBox::activated, this, [this](int index) {
        if (setLinkWidth) setLinkWidth(GanttPlan::LinkWidth(m_linkWidth->itemData(index).toInt()));
    });
    connect(m_linkEndSize, &QComboBox::activated, this, [this](int index) {
        if (setLinkEndSize) setLinkEndSize(GanttPlan::LinkEndSize(m_linkEndSize->itemData(index).toInt()));
    });
    showPage(Page::Task);
    // Up/Down reach the focused widget first (a select would change value, the scroll area would
    // scroll): the panel watches whatever of its own takes the focus.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (now && isAncestorOf(now)) now->installEventFilter(this);  // installing again does not duplicate
    });
    // At the bottom, red and as wide as the panel (user's picture, 2026-10-29): deletes the
    // targets as Delete on the table does, topics on the map too.
    m_remove = new QPushButton(tr("Delete"));
    m_remove->setObjectName(QStringLiteral("GanttPanelRemove"));
    m_remove->setToolTip(tr("Delete the task and its topic"));
    m_remove->setCursor(Qt::PointingHandCursor);
    layout->addSpacing(16);
    layout->addWidget(m_remove);
    // Asked first (user, 2026-10-29), although Undo brings the tasks back.
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const GanttTask *task = m_plan.task(m_id);
        if (!task || !remove) return;
        const QString question = m_targets.size() == 1
                                     ? tr("Delete \"%1\" and its topic on the map?").arg(task->shownName())
                                     : tr("Delete %n task(s) and their topics on the map?", nullptr, int(m_targets.size()));
        if (ModalDialog::ask(window(), tr("Delete task"), question, tr("Delete")) == ModalDialog::Choice::Primary)
            remove(m_targets);
    });

    // Loads are signal-blocked: a change here is the user's typing.
    connect(m_name, &QTextEdit::textChanged, this, [this] {
        if (nameTyped && !m_id.isEmpty()) nameTyped(m_id, m_name->text());
    });
    connect(m_name, &GanttNameField::editingFinished, this, [this] {
        const GanttTask *task = m_plan.task(m_id);
        const MindNode *topic = topicOf ? topicOf(m_id) : nullptr;
        QString name, html;
        if (!task || !topic || !m_name->result(topic, &name, &html)) {
            if (topic) m_name->load(topic, m_plan.hasChildren(m_id));  // an empty name is refused
            return;
        }
        if (rename) rename(m_id, name, html);
    });
    // Every field below edits all the targets (the selected tasks) in one step (GanttTaskEdits).
    connect(m_progress, &NumberField::valueChosen, this, [this](double value) {
        editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setProgress(plan, m_targets, int(value)); });
    });
    connect(m_progress, &NumberField::valueStepped, this, [this](double delta) {  // different values
        editPlan([&](GanttPlan &plan) { return GanttTaskEdits::stepProgress(plan, m_targets, int(delta)); });
    });
    connect(m_priority, &QComboBox::activated, this, [this](int index) {
        if (setInfo) setInfo(m_targets, index);
    });
    connect(m_start, &QDateEdit::dateChanged, this, [this](const QDate &date) {
        editPlan([&](GanttPlan &plan) { return GanttTaskEdits::moveStart(plan, m_targets, date); });
    });
    connect(m_clearStart, &QToolButton::clicked, this, [this] {
        editPlan([&](GanttPlan &plan) { return GanttTaskEdits::clearStart(plan, m_targets); });
    });
    connect(m_addStart, &QPushButton::clicked, this, [this] {
        editPlan([&](GanttPlan &plan) { return GanttTaskEdits::addStart(plan, m_schedule, m_targets); });
    });
    connect(m_duration, &NumberField::valueChosen, this, [this](double value) {
        editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setDuration(plan, m_targets, int(value)); });
    });
    connect(m_duration, &NumberField::valueStepped, this, [this](double delta) {  // different values
        editPlan([&](GanttPlan &plan) { return GanttTaskEdits::stepDuration(plan, m_targets, int(delta)); });
    });
    connect(m_end, &QDateEdit::dateChanged, this, [this](const QDate &date) {
        editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setEnd(plan, m_schedule, m_targets, date); });
    });
    connect(m_milestone, &ToggleSwitch::toggled, this, [this](bool milestone) {
        if (!m_id.isEmpty() && setMilestone) setMilestone(m_targets, milestone);
    });
    connect(m_color, &ColorField::colorChosen, this, [this](const QColor &color) {
        if (setBarColor) setBarColor(m_targets, color);
    });
    connect(m_defaultColor, &QToolButton::clicked, this, [this] {
        if (setBarColor) setBarColor(m_targets, std::nullopt);
    });
    connect(m_ownBorderColor, &ColorField::colorChosen, this, [this](const QColor &color) {
        if (setTaskBorderColor) setTaskBorderColor(m_targets, color);
    });
    connect(m_defaultBorderColor, &QToolButton::clicked, this, [this] {
        if (setTaskBorderColor) setTaskBorderColor(m_targets, std::nullopt);
    });
    connect(m_label, &ToggleSwitch::toggled, this, [this](bool shown) {
        if (setLabel) setLabel(m_targets, shown);
    });
    connect(m_addPerson, &QToolButton::clicked, this, [this] {
        QMenu menu(this);
        menu.setStyleSheet(property("menuStyle").toString());
        fillPersonMenu(&menu);
        menu.exec(m_addPerson->mapToGlobal(QPoint(0, m_addPerson->height())));
    });
}

QWidget *GanttTaskPanel::section(const QString &name, const QString &title, QLayout *content, QWidget *header) {
    auto *box = new QWidget;
    box->setObjectName(name);
    auto *boxLayout = new QVBoxLayout(box);
    boxLayout->setContentsMargins(0, kSectionGap, 0, 0);
    boxLayout->setSpacing(0);
    boxLayout->addWidget(divider());
    boxLayout->addSpacing(kSectionGap);
    // A click on the title closes or opens the section (user, 2026-10-30; before always open).
    auto *titleRow = new SectionTitleRow;
    titleRow->setCursor(Qt::PointingHandCursor);
    auto *chevron = new QLabel;
    chevron->setObjectName(QStringLiteral("SectionChevron"));
    chevron->setFixedWidth(14);
    auto *titleLabel = new QLabel(title);
    titleLabel->setObjectName(QStringLiteral("SectionTitle"));
    auto *headerLayout = new QHBoxLayout(titleRow);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(2);
    headerLayout->addWidget(chevron);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    if (header) headerLayout->addWidget(header);
    boxLayout->addWidget(titleRow);
    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, kRowGap, 0, 0);  // the gap closes with the section
    bodyLayout->setSpacing(0);
    bodyLayout->addLayout(content);
    boxLayout->addWidget(body);
    m_sectionParts.insert(name, {chevron, body});
    titleRow->clicked = [this, name] { setSectionOpen(name, !sectionOpen(name)); };
    chevron->setText(sectionOpen(name) ? QStringLiteral("▾") : QStringLiteral("▸"));
    body->setVisible(sectionOpen(name));
    return box;
}

void GanttTaskPanel::showTypedName(const QString &id, const QString &name) {
    if (id != m_id || m_targets.size() != 1) return;
    const QSignalBlocker blocker(m_name);
    m_name->setText(name);
}

void GanttTaskPanel::reloadName() {
    if (m_id.isEmpty() || m_targets.size() != 1) return;
    const QSignalBlocker blocker(m_name);
    if (const MindNode *topic = topicOf ? topicOf(m_id) : nullptr) m_name->load(topic, m_plan.hasChildren(m_id));
}

void GanttTaskPanel::showBarColor(const QColor &color, bool manual) {
    NodeStyle::Reading reading;
    reading.empty = false;
    reading.mixed = !color.isValid();
    reading.value = color;
    reading.allManual = manual;
    m_color->setReading(reading);
    m_defaultColor->setVisible(manual);
}

void GanttTaskPanel::showBorderColor(const QColor &color, bool own) {
    NodeStyle::Reading reading;
    reading.empty = false;
    reading.mixed = !color.isValid();
    reading.value = color;
    reading.allManual = own;
    m_ownBorderColor->setReading(reading);
    m_defaultBorderColor->setVisible(own);
}

bool GanttTaskPanel::editPlan(const std::function<bool(GanttPlan &)> &change) {
    return !m_id.isEmpty() && edit && edit(change);
}


void GanttTaskPanel::load(const QString &id, const GanttPlan &plan, const GanttSchedule &schedule, int priority,
                          const QHash<QString, QStringList> &people, bool labelled, const QStringList &targets) {
    const GanttTask *task = plan.task(id);
    if (!task) return;
    const QStringList wanted = targets.isEmpty() ? QStringList{id} : targets;
    const bool other = id != m_id || wanted != m_targets;
    m_id = id;
    m_targets = wanted;
    m_plan = plan;
    m_schedule = schedule;
    m_priorityValue = priority;
    m_assigned = people;
    showPage(page());  // the task's fields in place of "No task selected"
    const GanttScheduledTask scheduled = schedule.tasks.value(id);
    const bool summary = plan.hasChildren(id);
    // A field being edited keeps its text, unless another task is shown.
    const QWidget *focus = QApplication::focusWidget();
    const auto keep = [&](QWidget *field) { return !other && focus && (focus == field || field->isAncestorOf(focus)); };
    const auto set = [&](auto *field, const auto &apply) {
        if (keep(field)) return;
        const QSignalBlocker blocker(field);
        apply(field);
    };
    set(m_name, [&](GanttNameField *f) {
        if (const MindNode *topic = topicOf ? topicOf(id) : nullptr) f->load(topic, summary);
        else f->setText(task->shownName());
    });
    set(m_progress, [&](NumberField *f) { f->setValue(summary ? scheduled.progress : task->progress); });
    set(m_priority, [&](QComboBox *f) { f->setCurrentIndex(std::clamp(priority, 0, MindNode::kMaxPriority)); });
    set(m_start, [&](QDateEdit *f) { if (scheduled.dated) f->setDate(scheduled.start); });
    const int days = summary ? (scheduled.dated ? plan.workingDaysBetween(scheduled.start, scheduled.end) : 0) : task->duration;
    set(m_duration, [&](NumberField *f) { f->setValue(days); });
    set(m_end, [&](QDateEdit *f) { if (scheduled.dated) f->setDate(scheduled.end); });
    set(m_label, [&](ToggleSwitch *f) { f->setChecked(labelled); });
    set(m_milestone, [&](ToggleSwitch *f) { f->setChecked(task->milestone && !summary); });
    // Only the fields of the task's kind (user, 2026-10-29): a milestone has a start, no duration
    // nor end; a summary's dates come from its children, so no dates, milestone switch nor person,
    // and its % is read-only (as in the table).
    const bool milestone = task->milestone && !summary;
    showRow(m_milestone, !summary);
    showRow(m_start->parentWidget(), !summary);
    showRow(m_duration, !summary && !milestone);
    showRow(m_end, !summary && !milestone);
    // Nor priority and progress (user, 2026-10-30), the whole Details section: the summary's %
    // comes from its children.
    m_detailsSection->setVisible(!summary);
    m_resourcesSection->setVisible(!summary);
    loadPeople();
    m_datesSection->setVisible(!summary);
    m_progress->setEnabled(!summary);
    m_progress->setStep(milestone ? GanttPlan::kMaxProgress : 1);  // a checkbox: empty or checked
    m_duration->setEnabled(true);
    m_milestone->setEnabled(true);
    m_start->setVisible(scheduled.dated);
    m_start->setEnabled(true);
    m_clearStart->setVisible(task->start.isValid());
    m_addStart->setVisible(!scheduled.dated);
    m_end->setEnabled(scheduled.dated);
    m_linkSections->setVisible(!summary);
    if (!summary) {
        loadLinks(true);
        loadLinks(false);
    }
    m_title->setText(tr("Task"));
    m_name->setEnabled(true);
    m_progress->setStepsEachItem(false);
    m_duration->setStepsEachItem(false);
    if (m_targets.size() > 1) loadSeveral(!other);
    loadStyle();
}

void GanttTaskPanel::loadStyle() {
    // The values the tasks shown share, empty when they differ; × when one of them has its own. The
    // shape only for tasks of one kind (GanttTaskEdits::ownStyle).
    using Kind = GanttPlan::BarKind;
    const GanttTaskEdits::OwnStyle style = GanttTaskEdits::ownStyle(m_plan, m_targets);
    const auto show = [](QComboBox *select, int value) {
        const QSignalBlocker blocker(select);
        select->setCurrentIndex(value < 0 ? -1 : select->findData(value));
    };
    const bool oneKind = style.kind >= 0;
    showRow(m_defaultShape->parentWidget(), oneKind);
    if (oneKind) {
        const auto kind = Kind(style.kind);
        QComboBox *select = kind == Kind::Task ? m_ownBarShape : kind == Kind::Summary ? m_ownSummaryShape : m_ownMilestoneShape;
        for (QComboBox *other : {m_ownBarShape, m_ownSummaryShape, m_ownMilestoneShape}) other->setVisible(other == select);
        show(select, style.shape);
        m_defaultShape->setVisible(style.ownShape);
    }
    show(m_ownFill, style.fill);
    m_defaultFill->setVisible(style.ownFill);
    show(m_ownBorder, style.border);
    m_defaultBorder->setVisible(style.ownBorder);
}

// Several tasks: a value they share, or none ("—"); a field is enabled when one of them can change.
void GanttTaskPanel::loadSeveral(bool same) {
    const GanttTaskEdits::Several several = GanttTaskEdits::several(m_plan, m_schedule, m_targets);
    const QStringList &leaves = several.leaves;
    const bool dated = several.dated, undated = several.undated, stored = several.stored, lengths = several.lengths;
    // A field being edited keeps its text while the same tasks are shown.
    const QWidget *focus = QApplication::focusWidget();
    const auto shared = [&](NumberField *field, const QList<int> &values) {
        field->setStepsEachItem(true);
        if (same && focus && (focus == field || field->isAncestorOf(focus))) return;
        const QSignalBlocker blocker(field);
        if (!values.isEmpty() && values.count(values.first()) == values.size()) field->setValue(values.first());
        else field->clearValue();
    };
    shared(m_progress, several.progress);
    shared(m_duration, several.durations);
    m_progress->setStep(1);
    if (m_priorityValue < 0) {
        const QSignalBlocker blocker(m_priority);
        m_priority->setCurrentIndex(-1);
    }
    {
        const QSignalBlocker blocker(m_milestone);
        m_milestone->setChecked(several.allMilestones);
    }
    m_title->setText(tr("%n task(s)", nullptr, int(m_targets.size())));
    m_name->setEnabled(false);  // a name, and the links, are one task's
    // A field is hidden when no selected task has it: summaries only, or milestones and summaries.
    showRow(m_milestone, !leaves.isEmpty());
    showRow(m_start->parentWidget(), !leaves.isEmpty());
    showRow(m_duration, !several.durations.isEmpty());
    showRow(m_end, !several.durations.isEmpty());
    m_resourcesSection->setVisible(!leaves.isEmpty());
    m_datesSection->setVisible(!leaves.isEmpty());
    m_detailsSection->setVisible(!leaves.isEmpty());  // priority set on the tasks, not the summaries
    m_progress->setEnabled(!leaves.isEmpty());
    m_duration->setEnabled(true);
    m_milestone->setEnabled(true);
    // Start and End show the shown task's dates, or the first dated task's when it has none.
    if (!m_schedule.tasks.value(m_id).dated && several.firstStart.isValid()) {
        const QSignalBlocker startBlocker(m_start), endBlocker(m_end);
        m_start->setDate(several.firstStart);
        m_end->setDate(several.firstEnd);
    }
    m_start->setVisible(dated);
    m_start->setEnabled(dated);
    m_clearStart->setVisible(stored);
    m_addStart->setVisible(!dated && undated);
    m_end->setEnabled(lengths);
    m_linkSections->setVisible(false);
}

QList<GanttDependency> GanttTaskPanel::links(bool predecessors) const { return GanttTaskEdits::links(m_plan, m_id, predecessors); }

QStringList GanttTaskPanel::linkCandidates(bool predecessors) const { return GanttTaskEdits::linkCandidates(m_plan, m_id, predecessors); }

void GanttTaskPanel::setSectionOpen(const QString &name, bool open) {
    const auto parts = m_sectionParts.value(name);
    if (!parts.first || sectionOpen(name) == open) return;
    if (open) m_closedSections.remove(name);
    else m_closedSections.insert(name);
    parts.first->setText(open ? QStringLiteral("▾") : QStringLiteral("▸"));
    parts.second->setVisible(open);
    QStringList closed(m_closedSections.cbegin(), m_closedSections.cend());
    closed.sort();
    QSettings().setValue(kClosedSectionsSetting, closed);
}

bool GanttTaskPanel::addLink(bool predecessors, const QString &other) {
    setSectionOpen(predecessors ? QStringLiteral("GanttSectionPredecessors") : QStringLiteral("GanttSectionSuccessors"), true);
    const GanttDependency link = GanttTaskEdits::linkWith(m_id, predecessors, other);
    return editPlan([&](GanttPlan &plan) { return plan.addDependency(link); });
}

QString GanttTaskPanel::parentName(const QString &id) const {
    const GanttTask *task = m_plan.task(id);
    const GanttTask *parent = task ? m_plan.task(task->parentId) : nullptr;
    return parent ? parent->shownName() : QString();
}

void GanttTaskPanel::showAddMenu(bool predecessors) {
    QMenu menu(this);
    menu.setStyleSheet(property("menuStyle").toString());
    // Topics often share a name ("Subtopic 1"): the summary they belong to tells them apart.
    for (const QString &id : linkCandidates(predecessors)) {
        const QString parent = parentName(id);
        const QString name = m_plan.task(id)->shownName();
        QAction *action = menu.addAction(parent.isEmpty() ? name : tr("%1 · in %2").arg(name, parent));
        connect(action, &QAction::triggered, this, [this, predecessors, id] { addLink(predecessors, id); });
    }
    if (menu.isEmpty()) menu.addAction(tr("No other task can be linked"))->setEnabled(false);
    QToolButton *button = m_addLink[side(predecessors)];
    menu.exec(button->mapToGlobal(QPoint(0, button->height())));
}

void GanttTaskPanel::setResources(const QVector<GanttResource> &people) {
    if (people == m_resources) return;
    m_resources = people;
    loadPeople();
}

QStringList GanttTaskPanel::peopleTargets() const {
    QStringList result;
    for (const QString &id : m_targets)
        if (m_plan.task(id) && !m_plan.hasChildren(id)) result.append(id);
    return result;
}

void GanttTaskPanel::loadPeople() {
    // Removing the rows deletes their × later (Qt's index widgets): one may be the button clicked.
    m_peopleTable->setRowCount(0);
    m_peopleRows.clear();
    m_partialPeople.clear();
    const QStringList targets = peopleTargets();
    for (const GanttResource &person : std::as_const(m_resources)) {
        const auto count = std::count_if(targets.cbegin(), targets.cend(), [&](const QString &id) { return m_assigned.value(id).contains(person.id); });
        if (!count) continue;
        const bool partial = count < targets.size();
        const int row = m_peopleTable->rowCount();
        m_peopleTable->insertRow(row);
        const QString texts[kResourceColumnCount] = {person.firstName, person.lastName, person.role};
        for (int column = 0; column < kResourceColumnCount; ++column) {
            auto *item = new QTableWidgetItem(texts[column]);
            QStringList tip;
            if (!texts[column].isEmpty()) tip.append(texts[column]);
            if (partial) tip.append(tr("Only on some of the tasks"));
            item->setToolTip(tip.join(QLatin1Char('\n')));
            if (const QColor color = partial ? m_mutedText : m_rowText; color.isValid()) item->setForeground(color);
            m_peopleTable->setItem(row, column, item);
        }
        auto *remove = new QToolButton;
        remove->setObjectName(QStringLiteral("GanttPersonRemove"));
        remove->setText(QStringLiteral("×"));
        remove->setToolTip(tr("Remove from the task"));
        remove->setCursor(Qt::PointingHandCursor);
        const QString id = person.id;
        connect(remove, &QToolButton::clicked, this, [this, id] {
            if (setPeople) setPeople(peopleTargets(), {}, {id});
        });
        m_peopleTable->setCellWidget(row, kRemoveColumn, remove);
        // FEAT-083: the person's share of the task(s); "—" when the tasks differ.
        auto *percent = new NumberField;
        percent->setObjectName(QStringLiteral("GanttPersonPercent"));
        percent->setRange(MindNode::kMinPercent, MindNode::kMaxPercent);
        percent->setDecimals(0);
        percent->setStep(MindNode::kPercentStep);
        percent->setPlaceholderText(QStringLiteral("—"));
        percent->setAccessibleName(tr("Share of the task (%)"));
        percent->setToolTip(tr("Share of the task (%)"));
        int shared = -1;
        for (const QString &target : targets) {
            if (!m_assigned.value(target).contains(id)) continue;
            const int value = m_percents.value(target).value(id, MindNode::kMaxPercent);
            shared = shared < 0 || shared == value ? value : 0;
        }
        if (shared > 0) percent->setValue(shared);
        else percent->clearValue();
        connect(percent, &NumberField::valueChosen, this, [this, id](double value) {
            if (setPercent) setPercent(peopleTargets(), id, int(value));
        });
        m_peopleTable->setCellWidget(row, kPercentColumn, percent);
        m_peopleRows.append(id);
        if (partial) m_partialPeople.insert(id);
    }
    const int rows = m_peopleTable->rowCount();
    m_noPeople->setVisible(rows == 0);
    m_peopleTable->setVisible(rows > 0);
    m_peopleTable->setFixedHeight(kPeopleHeaderHeight + rows * kPeopleRowHeight);
    const bool everyone = !m_resources.isEmpty() && personCandidates().isEmpty();
    m_addPerson->setEnabled(!everyone);
    m_addPerson->setToolTip(everyone ? tr("Everyone is already assigned") : tr("Assign a person"));
}

QToolButton *GanttTaskPanel::personRemove(const QString &id) const {
    const int row = int(m_peopleRows.indexOf(id));
    return row < 0 ? nullptr : static_cast<QToolButton *>(m_peopleTable->cellWidget(row, kRemoveColumn));
}

NumberField *GanttTaskPanel::personPercent(const QString &id) const {
    const int row = int(m_peopleRows.indexOf(id));
    return row < 0 ? nullptr : static_cast<NumberField *>(m_peopleTable->cellWidget(row, kPercentColumn));
}

QStringList GanttTaskPanel::resourceColumns() const {
    const QHeaderView *header = m_peopleTable->horizontalHeader();
    QStringList keys;
    for (int visual = 0; visual < header->count(); ++visual)
        if (const int column = header->logicalIndex(visual); column < kResourceColumnCount && !header->isSectionHidden(column))
            keys.append(resourceColumnKeys().at(column));
    return keys;
}

void GanttTaskPanel::restoreResourceColumns() {
    QStringList keys;
    for (const QString &key : QSettings().value(kResourceColumnsSetting, resourceColumnKeys()).toStringList())
        if (resourceColumnKeys().contains(key) && !keys.contains(key)) keys.append(key);
    if (keys.isEmpty()) keys = resourceColumnKeys();  // one column at least
    QHeaderView *header = m_peopleTable->horizontalHeader();
    const QSignalBlocker blocker(header);
    for (int place = 0; place < keys.size(); ++place)
        header->moveSection(header->visualIndex(int(resourceColumnKeys().indexOf(keys.at(place)))), place);
    header->moveSection(header->visualIndex(kPercentColumn), header->count() - 1);
    header->moveSection(header->visualIndex(kRemoveColumn), header->count() - 1);
    for (int column = 0; column < kResourceColumnCount; ++column)
        header->setSectionHidden(column, !keys.contains(resourceColumnKeys().at(column)));
}

void GanttTaskPanel::resourceColumnMoved() {
    QHeaderView *header = m_peopleTable->horizontalHeader();
    if (header->visualIndex(kPercentColumn) != header->count() - 2 || header->visualIndex(kRemoveColumn) != header->count() - 1) {
        const QSignalBlocker blocker(header);  // % and × stay last
        header->moveSection(header->visualIndex(kPercentColumn), header->count() - 1);
        header->moveSection(header->visualIndex(kRemoveColumn), header->count() - 1);
    }
    QSettings().setValue(kResourceColumnsSetting, resourceColumns());
}

bool GanttTaskPanel::setResourceColumnShown(const QString &key, bool shown) {
    const int column = int(resourceColumnKeys().indexOf(key));
    QHeaderView *header = m_peopleTable->horizontalHeader();
    if (column < 0 || header->isSectionHidden(column) != shown) return false;
    if (!shown && resourceColumns().size() == 1) return false;  // the last one stays
    header->setSectionHidden(column, !shown);
    QSettings().setValue(kResourceColumnsSetting, resourceColumns());
    return true;
}

void GanttTaskPanel::fillResourceColumnMenu(QMenu *menu) {
    const QStringList shown = resourceColumns();
    for (int column = 0; column < kResourceColumnCount; ++column) {
        const QString key = resourceColumnKeys().at(column);
        QAction *action = menu->addAction(m_peopleTable->horizontalHeaderItem(column)->text());
        action->setCheckable(true);
        action->setChecked(shown.contains(key));
        action->setEnabled(!(shown.size() == 1 && shown.contains(key)));
        connect(action, &QAction::toggled, this, [this, key](bool on) { setResourceColumnShown(key, on); });
    }
}

QStringList GanttTaskPanel::personCandidates() const {
    const QStringList targets = peopleTargets();
    QStringList result;
    for (const GanttResource &person : m_resources)
        if (std::any_of(targets.cbegin(), targets.cend(), [&](const QString &id) { return !m_assigned.value(id).contains(person.id); }))
            result.append(person.id);
    return result;
}

void GanttTaskPanel::fillPersonMenu(QMenu *menu) {
    if (m_resources.isEmpty()) {
        menu->addAction(tr("No people yet"))->setEnabled(false);
        // Queued: the Resources modal opens once the menu has closed.
        connect(menu->addAction(tr("Add people…")), &QAction::triggered, this, [this] {
            QMetaObject::invokeMethod(this, [this] { if (openResources) openResources(); }, Qt::QueuedConnection);
        });
        return;
    }
    const QStringList candidates = personCandidates();
    for (const GanttResource &person : std::as_const(m_resources)) {
        if (!candidates.contains(person.id)) continue;
        const QString name = person.displayName().isEmpty() ? person.email : person.displayName();
        // Two people may share a name: the role tells them apart.
        QAction *action = menu->addAction(person.role.isEmpty() ? name : tr("%1 — %2").arg(name, person.role));
        const QString id = person.id;
        connect(action, &QAction::triggered, this, [this, id] {
            setSectionOpen(QStringLiteral("GanttSectionResources"), true);
            if (setPeople) setPeople(peopleTargets(), {id}, {});
        });
    }
}

QString GanttTaskPanel::sentence(const GanttDependency &link, bool predecessors, const QString &otherName) {
    QString text;
    // From the task shown: its predecessors say when it may start or finish; its successors say
    // when they may.
    if (predecessors) {
        switch (link.type) {
        case GanttDependency::Type::FinishStart: text = tr("Starts after «%1» ends"); break;
        case GanttDependency::Type::StartStart: text = tr("Starts after «%1» starts"); break;
        case GanttDependency::Type::FinishFinish: text = tr("Ends after «%1» ends"); break;
        case GanttDependency::Type::StartFinish: text = tr("Ends after «%1» starts"); break;
        }
    } else {
        switch (link.type) {
        case GanttDependency::Type::FinishStart: text = tr("«%1» starts after this one ends"); break;
        case GanttDependency::Type::StartStart: text = tr("«%1» starts after this one starts"); break;
        case GanttDependency::Type::FinishFinish: text = tr("«%1» ends after this one ends"); break;
        case GanttDependency::Type::StartFinish: text = tr("«%1» ends after this one starts"); break;
        }
    }
    text = text.arg(otherName);
    if (link.lag > 0) text += tr(", %n day(s) later", nullptr, link.lag);
    else if (link.lag < 0) text += tr(", %n day(s) earlier", nullptr, -link.lag);
    return text + QLatin1Char('.');
}

GanttTaskPanel::LinkRow GanttTaskPanel::makeLinkRow(bool predecessors, const QString &other) {
    LinkRow row;
    auto *card = new QFrame;
    card->setObjectName(QStringLiteral("GanttLink"));
    row.row = card;
    // Two lines (user, 2026-10-28): the other task and its summary with the x, then the
    // type and the lag. What the link means is the card's tooltip.
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(8, 4, 2, 6);
    layout->setSpacing(2);
    auto *top = new QHBoxLayout;
    top->setSpacing(6);
    row.name = new QLabel;
    row.name->setObjectName(QStringLiteral("GanttLinkName"));
    row.parent = new QLabel;
    row.parent->setObjectName(QStringLiteral("GanttLinkParent"));
    row.remove = new QToolButton;
    row.remove->setText(QStringLiteral("×"));
    row.remove->setToolTip(tr("Delete dependency"));
    top->addWidget(row.name);
    top->addWidget(row.parent);
    top->addStretch();
    top->addWidget(row.remove);
    layout->addLayout(top);
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 0, 6, 0);
    bottom->setSpacing(6);
    auto *type = new PanelSelect;
    type->colors = m_listColors;
    row.type = type;
    for (const auto type : kTypes) row.type->addItem(GanttCanvas::typeName(type), int(type));
    // The select shows its whole value: the longest name, its padding and the arrow.
    QFont typeFont = row.type->font();
    typeFont.setPixelSize(UiTheme::fontPx(12));
    int widest = 0;
    for (int i = 0; i < row.type->count(); ++i) widest = std::max(widest, QFontMetrics(typeFont).horizontalAdvance(row.type->itemText(i)));
    row.type->setMinimumWidth(widest + 2 * 8 + 24 + 4);
    row.lag = numberField(-GanttPlan::kMaxLag, GanttPlan::kMaxLag);
    row.lag->setFixedWidth(kLagWidth);
    const QString lagTip = tr("Lag in working days; negative = lead");
    row.lag->setToolTip(lagTip);
    QLabel *lag = caption(tr("Lag"));
    lag->setToolTip(lagTip);
    bottom->addWidget(row.type, 1);
    bottom->addWidget(lag);
    bottom->addWidget(row.lag);
    layout->addLayout(bottom);
    const auto current = [this, predecessors, other] {
        const GanttDependency wanted = GanttTaskEdits::linkWith(m_id, predecessors, other);
        for (const GanttDependency &link : m_plan.dependencies())
            if (link.from == wanted.from && link.to == wanted.to) return link;
        return wanted;
    };
    connect(row.type, &QComboBox::activated, this, [this, current, type = row.type] {
        GanttDependency link = current();
        link.type = GanttDependency::Type(type->currentData().toInt());
        editPlan([&](GanttPlan &plan) { return plan.updateDependency(link); });
    });
    connect(row.lag, &NumberField::valueChosen, this, [this, current](double lag) {
        GanttDependency link = current();
        link.lag = int(lag);
        editPlan([&](GanttPlan &plan) { return plan.updateDependency(link); });
    });
    connect(row.remove, &QToolButton::clicked, this, [this, current] {
        const GanttDependency link = current();
        editPlan([&](GanttPlan &plan) { return plan.removeDependency(link.from, link.to); });
    });
    return row;
}

void GanttTaskPanel::loadLinks(bool predecessors) {
    QHash<QString, LinkRow> &rows = m_rows[side(predecessors)];
    const QList<GanttDependency> list = links(predecessors);
    QStringList others;
    for (const GanttDependency &link : list) others.append(predecessors ? link.from : link.to);
    // Rows of links that are gone go away later: one of them may be sending the signal now.
    for (auto it = rows.begin(); it != rows.end();) {
        if (others.contains(it.key())) { ++it; continue; }
        it->row->hide();
        it->row->deleteLater();
        it = rows.erase(it);
    }
    const QWidget *focus = QApplication::focusWidget();
    for (int i = 0; i < list.size(); ++i) {
        const QString &other = others.at(i);
        if (!rows.contains(other)) rows.insert(other, makeLinkRow(predecessors, other));
        LinkRow &row = rows[other];
        m_linkLists[side(predecessors)]->insertWidget(i, row.row);  // the plan's order
        row.row->show();
        const QString name = m_plan.task(other)->shownName();
        const QString parent = parentName(other);
        const QString inParent = parent.isEmpty() ? QString() : tr("in %1").arg(parent);
        // One line: a short name leaves its room to the summary, the summary takes at most half.
        QFont bold = row.name->font();  // the style sheet's weight is applied only when shown
        bold.setPixelSize(UiTheme::fontPx(12));
        bold.setWeight(QFont::DemiBold);
        QFont regular = row.parent->font();
        regular.setPixelSize(UiTheme::fontPx(12));
        const QFontMetrics nameMetrics(bold), parentMetrics(regular);
        const int nameWidth = nameMetrics.horizontalAdvance(name);
        const int parentRoom = inParent.isEmpty() ? 0 : std::min(parentMetrics.horizontalAdvance(inParent), kLinkLine / 2) + 6;
        const int nameRoom = kLinkLine - parentRoom;
        row.name->setText(nameMetrics.elidedText(name, Qt::ElideRight, nameRoom));
        row.parent->setText(parentMetrics.elidedText(inParent, Qt::ElideRight, kLinkLine - std::min(nameWidth, nameRoom) - 6));
        row.parent->setVisible(!parent.isEmpty());
        row.sentence = sentence(list.at(i), predecessors, name);
        // The full names and the meaning, for the names cut and for who wonders what the link does.
        QStringList tip{name};
        if (!inParent.isEmpty()) tip.append(inParent);
        tip.append(row.sentence);
        row.row->setToolTip(tip.join(QLatin1Char('\n')));
        const QSignalBlocker typeBlocker(row.type), lagBlocker(row.lag);
        row.type->setCurrentIndex(row.type->findData(int(list.at(i).type)));
        if (focus != row.lag) row.lag->setValue(list.at(i).lag);
    }
    m_noLinks[side(predecessors)]->setVisible(list.isEmpty());
    m_addLink[side(predecessors)]->setEnabled(!linkCandidates(predecessors).isEmpty());
}

QComboBox *GanttTaskPanel::linkType(bool predecessors, const QString &other) const {
    return m_rows[side(predecessors)].value(other).type;
}

NumberField *GanttTaskPanel::linkLag(bool predecessors, const QString &other) const {
    return m_rows[side(predecessors)].value(other).lag;
}

QString GanttTaskPanel::linkSentence(bool predecessors, const QString &other) const {
    const LinkRow row = m_rows[side(predecessors)].value(other);
    return row.sentence;
}

QToolButton *GanttTaskPanel::linkRemove(bool predecessors, const QString &other) const {
    return m_rows[side(predecessors)].value(other).remove;
}

void GanttTaskPanel::setColors(const GanttColors &c) {
    // The colour field paints itself: the same thin border as the other fields (user, 2026-10-28).
    for (ColorField *field : {m_color, m_ownBorderColor, m_sheetColor[0], m_sheetColor[1], m_sheetColor[2], m_sheetBorderColor[0],
                              m_sheetBorderColor[1], m_sheetBorderColor[2], m_linkColor}) {
        field->borderColor = c.line;
        field->hoverBorderColor = c.muted;
        field->mixedFill = c.card;
        field->mixedText = c.text;
        field->update();
    }
    // Switches: the track when off is the selected rows' colour (#e3e5e6 light, surfaceRaised-like dark).
    for (ToggleSwitch *toggle : findChildren<ToggleSwitch *>()) {
        toggle->offTrack = c.selected;
        toggle->update();
    }
    // The third card of the Gantt (FEAT-078; user 2026-10-28): light or dark with the page,
    // fields with a thin border and 12 px text, captions muted, link cards on the page colour.
    const auto rgba = [](const QColor &color) {
        return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
    };
    const QString accent = UiTheme::color(UiTheme::Token::Accent).name();
    // Captions in dark halfway between text and muted: textMuted on the dark card hardly read
    // (user's picture, 2026-10-30).
    const bool dark = c.card.lightness() < 128;
    const QColor caption = dark ? QColor((c.text.red() + c.muted.red()) / 2, (c.text.green() + c.muted.green()) / 2,
                                         (c.text.blue() + c.muted.blue()) / 2)
                                : c.muted;
    const QString styleSheet = QStringLiteral(
        "#GanttTaskPanel{background:@card;border-radius:@radiuspx;}"
        "#GanttTaskPanelContent,QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;}"
        // The pages on the card: the workspace's "QStackedWidget{background:#ffffff}" reached them,
        // white in dark (user's picture, 2026-10-30).
        "QStackedWidget{background:transparent;}"
        "QFrame#GanttPanelDivider{background:@line;}"
        "QLabel{color:@text;font-size:12px;background:transparent;}"
        "QLabel[caption=\"true\"]{font-size:13px;}"
        "QLabel[caption=\"true\"],QLabel[muted=\"true\"]{color:@caption;}"
        "QLabel#SectionTitle{font-size:13px;font-weight:600;}"
        "QLabel#GanttLinkName{font-weight:600;}"
        "QLabel#GanttLinkParent{color:@caption;}"
        "QLineEdit,GanttNameField,QDateEdit,QComboBox{color:@text;background:transparent;border:1px solid @line;border-radius:4px;"
        "padding:0 8px;font-size:12px;min-height:22px;max-height:22px;selection-background-color:@selected;selection-color:@text;}"
        "QLineEdit,GanttNameField{placeholder-text-color:@muted;}"
        "QLineEdit:focus,GanttNameField:focus,QDateEdit:focus,QComboBox:focus{border-color:@accent;}"
        "QLineEdit:disabled,GanttNameField:disabled,QDateEdit:disabled,QComboBox:disabled{color:@muted;}"
        "QComboBox:hover{background:@selected;}"
        "QDateEdit::drop-down,QComboBox::drop-down{border:0;width:24px;}"
        "QDateEdit::down-arrow,QComboBox::down-arrow{image:none;width:0;height:0;}"
        "QToolButton{color:@text;background:transparent;border:0;border-radius:4px;padding:0 6px;font-size:12px;min-height:24px;}"
        "QToolButton:hover{background:@selected;}QToolButton:disabled{color:@muted;}"
        "QPushButton{color:@text;background:transparent;border:1px solid @line;border-radius:4px;padding:0 8px;"
        "font-size:12px;min-height:22px;max-height:22px;}"
        "QPushButton:hover{background:@selected;}"
        "QPushButton#GanttPanelRemove{color:#e5484d;border-color:#e5484d;font-size:13px;min-height:30px;max-height:30px;}"
        "QPushButton#GanttPanelRemove:hover{background:rgba(229,72,77,0.1);}"
        "QFrame#GanttLink{background:@page;border-radius:6px;}"
        "QLabel#SectionChevron{color:@caption;font-size:14px;}"
        // FEAT-081: padding 0, or the main window's "QTableWidget{padding:5px 8px}" takes room from the rows.
        "QTableWidget#GanttPeopleTable{background:transparent;border:0;padding:0;font-size:12px;}"
        "QTableWidget#GanttPeopleTable::item{padding:0 4px;border:0;border-bottom:1px solid @line;}"
        "QTableWidget#GanttPeopleTable QHeaderView{background:transparent;border:0;}"
        "QTableWidget#GanttPeopleTable QHeaderView::section{background:transparent;color:@caption;font-size:12px;"
        "border:0;border-bottom:1px solid @line;padding:0 4px;}"
        "QToolButton#GanttPersonRemove{min-height:20px;max-height:20px;padding:0;margin:0 2px;}"
        // The side panel's thin bar (sidepanel.cpp): with it shown the captions keep their 104 px.
        "QScrollBar:vertical{background:transparent;width:8px;margin:0;}"
        "QScrollBar::handle:vertical{background:@line;border-radius:3px;min-height:24px;}"
        "QScrollBar::handle:vertical:hover{background:@muted;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}"
        "QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}")
        .replace(QStringLiteral("@card"), c.card.name()).replace(QStringLiteral("@page"), c.page.name())
        .replace(QStringLiteral("@line"), rgba(c.line)).replace(QStringLiteral("@text"), c.text.name())
        .replace(QStringLiteral("@caption"), caption.name()).replace(QStringLiteral("@muted"), c.muted.name()).replace(QStringLiteral("@selected"), rgba(c.selected))
        .replace(QStringLiteral("@accent"), accent).replace(QStringLiteral("@radius"), QString::number(GanttColors::kCardRadius));
    m_pendingStyleSheet = styleSheet;
    if (isVisible()) applyPendingStyle();
    // The selector's track on the page colour, the active segment on the card's (as the map's, light or dark).
    m_pageSelector->setColors({c.page, c.line, c.card, c.selected, c.text, c.muted});
    fillBarShapes(c.text);
    m_listColors = c.picker();
    // The rows' colours on the items: a colour in the style sheet would cover the muted rows'.
    m_rowText = c.text;
    m_mutedText = caption;
    loadPeople();  // partial rows in the new muted colour
    // Every select of the panel is a PanelSelect (priority, link types).
    for (QComboBox *field : findChildren<QComboBox *>()) {
        static_cast<PanelSelect *>(field)->colors = m_listColors;
        field->update();
    }
    for (DateField *field : {m_start, m_end}) {
        field->setChevronColor(c.text);
        field->setPickerColors(m_listColors);
    }
    update();
}

void GanttTaskPanel::fillBarShapes(const QColor &text) {
    // The pictures in the card's text colour, in the Gantt page's selects and in the Task page's
    // Style section (which may show none). Choices and pictures are the core's (shared with the phone).
    using List = GanttStyleChoices::List;
    const auto fill = [&text](QComboBox *select, List list, bool keepNone) {
        const int current = keepNone ? select->currentIndex() : std::max(0, select->currentIndex());
        select->clear();
        for (const StyleChoices::Choice &choice : GanttStyleChoices::choices(list))
            addIconSelectItem(select, choice.value, choice.name, [list, value = choice.value, text](QPainter &painter, const QRectF &box) {
                GanttStyleChoices::drawIcon(list, value, painter, box, text);
            });
        select->setCurrentIndex(current);
        static_cast<PanelSelect *>(select)->gridNone = select->findData(GanttStyleChoices::noneValue(list));
    };
    fill(m_barShape, List::BarShape, false);
    fill(m_ownBarShape, List::BarShape, true);
    fill(m_summaryShape, List::SummaryShape, false);
    fill(m_ownSummaryShape, List::SummaryShape, true);
    fill(m_milestoneShape, List::MilestoneShape, false);
    fill(m_ownMilestoneShape, List::MilestoneShape, true);
    for (QComboBox *select : {m_fillStyle[0], m_fillStyle[1], m_fillStyle[2]}) fill(select, List::Fill, false);
    fill(m_ownFill, List::Fill, true);
    for (QComboBox *select : {m_borderStyle[0], m_borderStyle[1], m_borderStyle[2]}) fill(select, List::Border, false);
    fill(m_ownBorder, List::Border, true);
    fill(m_linkLine, List::LinkLine, false);
    // The filled arrow before the end is first shown, as the chart's default.
    if (m_linkEnd->currentIndex() < 0) {
        fill(m_linkEnd, List::LinkEnd, false);
        m_linkEnd->setCurrentIndex(m_linkEnd->findData(int(BranchEnd::FilledArrow)));
    } else {
        fill(m_linkEnd, List::LinkEnd, false);
    }
    fill(m_linkWidth, List::LinkWidth, false);
    fill(m_linkEndSize, List::LinkEndSize, false);
}

void GanttTaskPanel::showLinkStyle(const GanttPlan &plan) {
    m_linkLine->setCurrentIndex(m_linkLine->findData(int(plan.linkLine())));
    m_linkEnd->setCurrentIndex(m_linkEnd->findData(int(plan.linkEnd())));
    m_linkWidth->setCurrentIndex(m_linkWidth->findData(int(plan.linkWidth())));
    m_linkEndSize->setCurrentIndex(m_linkEndSize->findData(int(plan.linkEndSize())));
    NodeStyle::Reading reading;
    reading.empty = false;
    reading.value = GanttCanvas::linkColor(plan);
    reading.allManual = plan.linkColor().isValid();
    m_linkColor->setReading(reading);
    m_linkColorReset->setVisible(reading.allManual);
}

GanttTaskPanel::Page GanttTaskPanel::page() const {
    return m_pageSelector->currentIndex() == 1 ? Page::Gantt : Page::Task;
}

void GanttTaskPanel::showPage(Page page) {
    m_pageSelector->setCurrentIndex(page == Page::Gantt ? 1 : 0);
    m_pages->setCurrentIndex(page == Page::Gantt ? 2 : m_id.isEmpty() ? 1 : 0);
}

void GanttTaskPanel::showBarShape(GanttPlan::BarShape shape) {
    m_barShape->setCurrentIndex(m_barShape->findData(int(shape)));
}

void GanttTaskPanel::showFillStyle(GanttPlan::BarKind kind, NodeFillStyle style) {
    m_fillStyle[int(kind)]->setCurrentIndex(m_fillStyle[int(kind)]->findData(int(style)));
}

void GanttTaskPanel::showBorderStyle(GanttPlan::BarKind kind, GanttPlan::BorderStyle style) {
    if (QComboBox *select = m_borderStyle[int(kind)]) select->setCurrentIndex(select->findData(int(style)));
}

void GanttTaskPanel::showMilestoneShape(GanttPlan::MilestoneShape shape) {
    m_milestoneShape->setCurrentIndex(m_milestoneShape->findData(int(shape)));
}

void GanttTaskPanel::showDefaultColor(GanttPlan::BarKind kind, const QColor &shown, bool chosen) {
    NodeStyle::Reading reading;
    reading.empty = false;
    reading.value = shown;
    reading.allManual = chosen;
    m_sheetColor[int(kind)]->setReading(reading);
    m_sheetColorReset[int(kind)]->setVisible(chosen);
}

void GanttTaskPanel::showDefaultBorderColor(GanttPlan::BarKind kind, const QColor &shown, bool chosen) {
    NodeStyle::Reading reading;
    reading.empty = false;
    reading.value = shown;
    reading.allManual = chosen;
    m_sheetBorderColor[int(kind)]->setReading(reading);
    m_sheetBorderColorReset[int(kind)]->setVisible(chosen);
}

void GanttTaskPanel::showSummaryShape(GanttPlan::SummaryShape shape) {
    m_summaryShape->setCurrentIndex(m_summaryShape->findData(int(shape)));
}

void GanttTaskPanel::clear() {
    if (!m_id.isEmpty()) commitPendingEdit();  // only a task's field; the Shape select keeps the focus
    m_id.clear();
    m_targets.clear();
    showPage(page());
}

bool GanttTaskPanel::showsNoTask() const { return m_pages->currentIndex() == 1; }

void GanttTaskPanel::commitPendingEdit() {
    QWidget *focus = QApplication::focusWidget();
    if (focus && isAncestorOf(focus)) focus->clearFocus();  // line edits confirm on focus loss
}

bool GanttTaskPanel::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress && watched->isWidgetType() && isAncestorOf(static_cast<QWidget *>(watched))) {
        const auto *key = static_cast<QKeyEvent *>(event);
        // Number and date fields keep Up/Down for their value; Alt+Down opens a select or a calendar.
        if ((key->key() == Qt::Key_Up || key->key() == Qt::Key_Down) && !(key->modifiers() & Qt::AltModifier)
            && !qobject_cast<QAbstractSpinBox *>(watched) && !dynamic_cast<NumberField *>(watched)) {
            emit stepRequested(key->key(), key->modifiers());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void GanttTaskPanel::applyPendingStyle() {
    if (m_pendingStyleSheet.isEmpty()) return;
    setStyleSheet(std::exchange(m_pendingStyleSheet, QString()));
}

// Before the first paint: the children are already shown, the new sheet re-styles them once.
void GanttTaskPanel::showEvent(QShowEvent *event) {
    applyPendingStyle();
    QWidget::showEvent(event);
}

void GanttTaskPanel::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        emit closeRequested();
        return;
    }
    QWidget::keyPressEvent(event);
}
