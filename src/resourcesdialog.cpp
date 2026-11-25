#include "resourcesdialog.h"
#include "help.h"

#include "ganttpeoplecharts.h"
#include "numberfield.h"
#include "peoplechartspanel.h"
#include "stylecontrols.h"
#include "toggleswitch.h"
#include "uitheme.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleValidator>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr int kSurfaceWidthPx = 760;
constexpr int kModalMarginPx = 16;  // ModalDialog's margin around the surface
constexpr int kAddSurfaceWidthPx = 420;  // as ModalDialog::askText
constexpr int kRemoveWidthPx = 32;
constexpr int kNumberWidthPx = 32;
constexpr int kCurrencyWidthPx = 104;
const QColor kErrorColor(0xe5, 0x48, 0x4d);  // as the spell checker's underline (ui-foundations.md)

constexpr int kCurrencyRole = Qt::UserRole + 3;  // the Currency cell's item: the person's currency text

// Cells edited with a plain field (texts at most kMaxTextLength); the cost with a number field
// in the locale's format and shown as money in the row's currency (FEAT-128); a cell with an error
// (its tooltip, set by updateValidity) has a red border.
class ResourceDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const override {
        auto *editor = new QLineEdit(parent);
        editor->setFrame(false);
        // On the field itself: the modal's QLineEdit style (radius 4, 30 px high) won over the
        // table's, and the field showed only its left border (user's picture, 2026-10-30).
        editor->setStyleSheet(UiTheme::qss(QStringLiteral(
            "QLineEdit { background:@surfaceSunken; color:@text; border:1px solid @accent; border-radius:0;"
            " min-height:0; padding:0 7px; }")));
        editor->setMaxLength(GanttResources::kMaxTextLength);
        if (index.column() == ResourcesDialog::HourlyCost) {
            auto *validator = new QDoubleValidator(0, double(GanttResources::kMaxHourlyCostCents) / 100.0, 2, editor);
            validator->setNotation(QDoubleValidator::StandardNotation);
            editor->setValidator(validator);
        }
        return editor;
    }
    // The whole cell, not the style's text area inside the item's padding.
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override {
        editor->setGeometry(option.rect);
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        auto *field = static_cast<QLineEdit *>(editor);
        if (index.column() == ResourcesDialog::HourlyCost) {
            const qint64 cents = index.data(Qt::EditRole).toLongLong();
            field->setText(cents ? QLocale().toString(double(cents) / 100.0, 'f', 2) : QString());
        } else {
            field->setText(index.data(Qt::EditRole).toString());
        }
        field->selectAll();
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        const QString text = static_cast<QLineEdit *>(editor)->text().trimmed();
        if (index.column() != ResourcesDialog::HourlyCost) {
            model->setData(index, text, Qt::EditRole);
            return;
        }
        model->setData(index, GanttResources::parseCost(text), Qt::EditRole);
    }
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if (index.column() != ResourcesDialog::HourlyCost) return;
        const QString currency = index.siblingAtColumn(ResourcesDialog::Currency).data(kCurrencyRole).toString();
        option->text = GanttResources::formatCost(index.data(Qt::EditRole).toLongLong(),
                                                  GanttCurrency::fromText(currency).normalized().displaySymbol());
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        if (index.data(Qt::ToolTipRole).toString().isEmpty()) return;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(kErrorColor, 1));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(option.rect).adjusted(2.5, 2.5, -2.5, -2.5), 4, 4);
        painter->restore();
    }
};

constexpr int kSavedRole = Qt::UserRole + 1;  // currency items saved on this computer: they have an ×
constexpr int kAddRole = Qt::UserRole + 2;    // the "+ Add currency..." item
constexpr int kRemoveIconPx = 12;
constexpr int kRemoveWidthInListPx = 24;

QRect currencyRemoveRect(const QRect &item) {
    return QRect(item.right() - kRemoveWidthInListPx, item.top(), kRemoveWidthInListPx, item.height());
}

// The currency list: light text as every dark select, an × on the saved currencies.
class CurrencyItemDelegate final : public QStyledItemDelegate {
public:
    explicit CurrencyItemDelegate(QObject *parent) : QStyledItemDelegate(parent), m_base(this) {}
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        m_base.paint(painter, option, index);
        if (!index.data(kSavedRole).toBool()) return;
        static const QIcon remove(QStringLiteral(":/assets/hugeicons/window-close-light.svg"));
        const QRect box = currencyRemoveRect(option.rect);
        remove.paint(painter, QRect(box.center() - QPoint(kRemoveIconPx / 2, kRemoveIconPx / 2), QSize(kRemoveIconPx, kRemoveIconPx)));
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        return m_base.sizeHint(option, index) + QSize(kRemoveWidthInListPx, 0);
    }

private:
    LightComboItemDelegate m_base;
};

// FEAT-127: the modal's content, scrolling when the window is shorter than it (the charts under the
// people make it tall). Its size is the content's: QScrollArea's own hint is capped at a few lines.
class FitScrollArea final : public QScrollArea {
public:
    using QScrollArea::QScrollArea;
    QSize sizeHint() const override {
        const QSize content = widget() ? widget()->sizeHint() : QSize();
        return {content.width(), content.height()};
    }
    QSize minimumSizeHint() const override { return {0, 0}; }
};

QLabel *sectionTitle(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600;"));
    return label;
}

}  // namespace

ResourcesDialog::ResourcesDialog(QWidget *parent, const GanttResources &resources, const GanttCalendar &calendar,
                                 const GanttPlan &plan, const GanttAssignments &assignments, GanttCanvas::Scale scale)
    : ModalDialog(parent, tr("Resources"), tr("Save"), kSurfaceWidthPx), m_calendar(calendar), m_plan(plan),
      m_assignments(assignments), m_scale(scale) {
    Help::setPage(this, QStringLiteral("gantt/people-and-costs.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(QStringLiteral("ResourcesDialog"));
    m_chartsShown = QSettings().value(QLatin1String(kChartsSetting), true).toBool();
    // The modal takes the main window's size: the wide surface must not stop it from narrowing, or the
    // charts would stay beside once there (the layout's minimum would hold the 1060 px).
    layout()->setSizeConstraint(QLayout::SetNoConstraint);

    // FEAT-127: People and Working week in a column, the charts beside it or in it.
    m_scroll = new FitScrollArea(this);
    m_scroll->setObjectName(QStringLiteral("ResourcesScroll"));
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setStyleSheet(QStringLiteral("QScrollArea { background:transparent; }"));
    m_scroll->viewport()->setAutoFillBackground(false);
    auto *body = new QWidget;
    m_body = new QHBoxLayout(body);
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(16);
    auto *left = new QWidget(body);
    auto *column = new QVBoxLayout(left);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(contentLayout()->spacing());
    m_body->addWidget(left, 1);

    // People; each person's currency in their row (FEAT-128).
    column->addWidget(sectionTitle(tr("People"), this));
    for (const GanttCurrency &used : resources.usedCurrencies()) m_openedCurrencies.append(used.text());

    m_table = new QTableWidget(0, ColumnCount, this);
    m_table->setObjectName(QStringLiteral("ResourcesTable"));
    m_table->setStyleSheet(UiTheme::qss(QStringLiteral(
        // padding:0 over the main window's "padding:5px 8px", which took 10 px from the rows and
        // cut the last one (user's picture, 2026-10-30).
        "QTableWidget { background:@surfaceSunken; color:@text; border:0; border-radius:4px; padding:0; font-size:14px;"
        " outline:0; }"
        "QTableWidget::item { padding:0 8px; border-bottom:1px solid @divider; }"
        "QTableWidget::item:selected { background:@surfaceRaised; color:@text; }"
        "QHeaderView::section { background:@surfaceSunken; color:@textMuted; border:0; border-bottom:1px solid @divider;"
        " padding:0 8px; font-size:12px; }"
        "QHeaderView::section:vertical { padding:0; }"
        // The numbers' column below the last person was the header's own light ground (manual
        // pictures, 2026-11-02).
        "QHeaderView { background:@surfaceSunken; border:0; }"
        "QTableCornerButton::section { background:@surfaceSunken; border:0; border-bottom:1px solid @divider; }"
        "QToolButton { background:transparent; border:0; border-radius:4px; }"
        "QToolButton:hover { background:@divider; }")));
    m_table->setHorizontalHeaderLabels({tr("First name"), tr("Last name"), tr("Email"), tr("Role"), tr("Hourly cost"), tr("Currency"), QString()});
    // The row's number before the first name; dragged, it moves the person (user, 2026-10-30).
    QHeaderView *numbers = m_table->verticalHeader();
    numbers->setDefaultSectionSize(kRowHeightPx);
    numbers->setSectionResizeMode(QHeaderView::Fixed);
    numbers->setDefaultAlignment(Qt::AlignCenter);
    numbers->setFixedWidth(kNumberWidthPx);
    numbers->setHighlightSections(false);
    numbers->setSectionsMovable(true);
    numbers->setToolTip(tr("Drag to move the person"));
    numbers->viewport()->setCursor(Qt::OpenHandCursor);
    connect(numbers, &QHeaderView::sectionMoved, this, [this, numbers](int, int from, int to) {
        // The header only moved its section: put it back and move the row itself, so the
        // numbers (the rows' own) stay in order.
        {
            const QSignalBlocker block(numbers);
            numbers->moveSection(to, from);
        }
        movePerson(from, to);
    });
    m_table->setShowGrid(false);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    // A click edits the cell (connected below); typing or F2 on the current cell too. Not on every
    // change of the current cell: the first cell went into editing when the table took the focus.
    m_table->setEditTriggers(QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    m_table->setTabKeyNavigation(true);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setItemDelegate(new ResourceDelegate(m_table));
    QHeaderView *header = m_table->horizontalHeader();
    header->setHighlightSections(false);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setFixedHeight(kHeaderHeightPx);
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(Email, QHeaderView::Stretch);
    // FEAT-128: narrower names and role make room for the currency; Email takes the rest.
    for (const auto &[column, width] : {std::pair{FirstName, 110}, {LastName, 110}, {Role, 120}, {HourlyCost, 100},
                                        {Currency, kCurrencyWidthPx}, {Remove, kRemoveWidthPx}})
        header->resizeSection(column, width);
    column->addWidget(m_table);

    m_empty = new QLabel(tr("No people yet"), this);
    m_empty->setObjectName(QStringLiteral("ResourcesEmpty"));
    m_empty->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_empty->setStyleSheet(UiTheme::qss(QStringLiteral("color:@textMuted;")));
    column->addWidget(m_empty);
    m_add = new QPushButton(tr("+ Add person"), this);
    m_add->setObjectName(QStringLiteral("ResourcesAdd"));
    m_add->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QPushButton { background:@surfaceRaised; color:@text; border:1px solid @divider; }"
        "QPushButton:hover { background:@divider; } QPushButton:pressed { background:@surfaceSunken; }")));
    m_add->setAutoDefault(false);
    column->addWidget(m_add, 0, Qt::AlignLeft);
    m_under = new QVBoxLayout;  // the charts when they do not fit beside
    m_under->setContentsMargins(0, 4, 0, 0);
    column->addLayout(m_under);

    // Working week of the sheet.
    column->addSpacing(4);
    column->addWidget(sectionTitle(tr("Working week"), this));
    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(16);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(2, 1);
    const auto addRow = [this, grid](const QString &text, QWidget *control) {
        const int row = grid->rowCount();
        grid->addWidget(new QLabel(text, this), row, 0);
        grid->addWidget(control, row, 1, Qt::AlignLeft);
        control->setAccessibleName(text);
    };
    m_saturday = new ToggleSwitch(this);
    m_saturday->setObjectName(QStringLiteral("ResourcesSaturday"));
    m_saturday->setChecked(calendar.workingDays.contains(Qt::Saturday));
    addRow(tr("Saturday is a working day"), m_saturday);
    m_sunday = new ToggleSwitch(this);
    m_sunday->setObjectName(QStringLiteral("ResourcesSunday"));
    m_sunday->setChecked(calendar.workingDays.contains(Qt::Sunday));
    addRow(tr("Sunday is a working day"), m_sunday);
    // One field in hours.minutes, − and + by a quarter hour (user, 2026-11-19: 8.15, 8.30, 7.45).
    auto *length = new QWidget(this);
    auto *lengthLayout = new QHBoxLayout(length);
    lengthLayout->setContentsMargins(0, 0, 0, 0);
    lengthLayout->setSpacing(6);
    m_length = new NumberField(this);
    m_length->setObjectName(QStringLiteral("ResourcesDayLength"));
    m_length->setAccessibleName(tr("Length of a working day"));
    m_length->setRange(GanttCalendar::kMinutesStep, GanttCalendar::kMaxMinutesPerDay);
    m_length->setStep(GanttCalendar::kMinutesStep);
    m_length->setClockFormat(true);
    m_length->setValue(calendar.minutesPerDay);
    m_length->setFixedWidth(96);
    lengthLayout->addWidget(m_length);
    lengthLayout->addWidget(new QLabel(tr("h"), this));
    addRow(tr("Length of a working day"), length);
    column->addLayout(grid);
    column->addStretch();

    m_chartsLine = new QWidget(body);
    m_chartsLine->setObjectName(QStringLiteral("ResourcesChartsDivider"));
    m_chartsLine->setFixedWidth(1);
    m_chartsLine->setAttribute(Qt::WA_StyledBackground);
    m_chartsLine->setStyleSheet(UiTheme::qss(QStringLiteral("background:@divider;")));
    m_body->addWidget(m_chartsLine);
    m_charts = new PeopleChartsPanel(body);
    m_scroll->setWidget(body);
    // setWidget turns the body's autoFillBackground on: it painted the window's light colour under
    // the light texts of People and Working week (BUG-058). The modal's surface shows through.
    body->setAutoFillBackground(false);
    contentLayout()->addWidget(m_scroll);

    m_chartsButton = new QToolButton;
    m_chartsButton->setObjectName(QStringLiteral("ResourcesChartsToggle"));
    m_chartsButton->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/chart-light.svg")));
    m_chartsButton->setIconSize(QSize(16, 16));
    m_chartsButton->setCheckable(true);
    m_chartsButton->setChecked(m_chartsShown);
    m_chartsButton->setFocusPolicy(Qt::NoFocus);
    m_chartsButton->setToolTip(m_chartsShown ? tr("Hide charts") : tr("Show charts"));
    m_chartsButton->setAccessibleName(m_chartsButton->toolTip());
    addTitleButton(m_chartsButton);
    connect(m_chartsButton, &QToolButton::toggled, this, &ResourcesDialog::setChartsShown);


    for (const GanttResource &person : resources.people) insertRow(m_table->rowCount(), person);
    updateRows();
    updateValidity();
    connect(m_add, &QPushButton::clicked, this, &ResourcesDialog::addPerson);
    connect(m_table, &QTableWidget::itemChanged, this, &ResourcesDialog::updateValidity);
    // FEAT-127: the charts follow every change of the copy; a person is chosen by a click on their
    // row (on their number again: the summary), the keyboard's current row, or none (empty part).
    connect(m_table, &QTableWidget::itemChanged, this, &ResourcesDialog::updateCharts);
    connect(m_saturday, &QAbstractButton::toggled, this, &ResourcesDialog::updateCharts);
    connect(m_sunday, &QAbstractButton::toggled, this, &ResourcesDialog::updateCharts);
    connect(m_length, &NumberField::valueChosen, this, &ResourcesDialog::updateCharts);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (!m_settingCurrent && row >= 0 && QApplication::mouseButtons() == Qt::NoButton) showChartRow(row);
    });
    m_table->viewport()->installEventFilter(this);
    numbers->viewport()->installEventFilter(this);
    updateChartsPlace();
    updateCharts();
    connect(m_table, &QTableWidget::clicked, m_table, [this](const QModelIndex &index) {
        if (index.column() != Remove && index.column() != Currency) m_table->edit(index);
    });
}

void ResourcesDialog::setChartsShown(bool shown) {
    if (m_chartsButton->isChecked() != shown) {
        m_chartsButton->setChecked(shown);  // toggled() comes back here
        return;
    }
    m_chartsButton->setToolTip(shown ? tr("Hide charts") : tr("Show charts"));
    m_chartsButton->setAccessibleName(m_chartsButton->toolTip());
    if (shown == m_chartsShown) return;
    m_chartsShown = shown;
    QSettings().setValue(QLatin1String(kChartsSetting), shown);
    updateChartsPlace();
    updateCharts();
}

void ResourcesDialog::showChartRow(int row) {
    const int shown = row >= 0 && row < m_table->rowCount() ? row : -1;
    if (shown == m_chartRow) return;
    m_chartRow = shown;
    updateCharts();
}

void ResourcesDialog::updateCharts() {
    if (!m_chartsShown) return;  // hidden charts compute nothing
    GanttPlan plan = m_plan;
    plan.setCalendar(calendar());
    // The rows as typed (blank ones left out, as Save drops them); a new person gets an id of its
    // row, which no task has.
    QVector<GanttResource> people;
    int person = -1;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        if (!m_table->item(row, HourlyCost)) continue;  // being added
        GanttResource typed = rowPerson(row);
        if (typed.isEmpty()) continue;
        if (typed.id.isEmpty()) typed.id = QStringLiteral("row:%1").arg(row);
        if (row == m_chartRow) person = int(people.size());
        people.append(typed);
    }
    m_charts->setCharts(GanttPeopleCharts::compute(plan, m_assignments, people, m_scale), person);
    m_scroll->updateGeometry();
}

void ResourcesDialog::updateChartsPlace() {
    const bool beside = m_chartsShown && width() - 2 * kModalMarginPx >= kWideSurfaceWidthPx;
    // Resizing the window calls this at every step: nothing to move while the place stays.
    if (beside == m_beside && m_charts->isVisibleTo(this) == m_chartsShown && m_placed) return;
    m_placed = true;
    m_beside = beside;
    m_body->removeWidget(m_charts);
    m_under->removeWidget(m_charts);
    if (m_beside) {
        m_body->addWidget(m_charts);
        m_charts->setFixedWidth(PeopleChartsPanel::kWidthPx);
    } else {
        m_under->addWidget(m_charts);
        m_charts->setMinimumWidth(0);
        m_charts->setMaximumWidth(QWIDGETSIZE_MAX);
    }
    m_chartsLine->setVisible(m_beside);
    m_charts->setVisible(m_chartsShown);
    const int surface = m_beside ? kWideSurfaceWidthPx : kSurfaceWidthPx;
    setSurfaceWidth(surface);
    m_charts->fitTexts(m_beside ? PeopleChartsPanel::kWidthPx : surface - 2 * kPaddingPx);
    m_scroll->updateGeometry();
}

void ResourcesDialog::resizeEvent(QResizeEvent *event) {
    ModalDialog::resizeEvent(event);
    updateChartsPlace();
}

void ResourcesDialog::showEvent(QShowEvent *event) {
    ModalDialog::showEvent(event);  // wrapped texts sized for the whole surface: the charts' after it
    m_placed = false;
    updateChartsPlace();
}

QComboBox *ResourcesDialog::currencySelect(int row) const {
    QWidget *cell = row >= 0 && row < m_table->rowCount() ? m_table->cellWidget(row, Currency) : nullptr;
    return cell ? cell->findChild<QComboBox *>() : nullptr;
}

int ResourcesDialog::rowOfCell(const QWidget *child, int column) const {
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (QWidget *cell = m_table->cellWidget(row, column); cell && (cell == child || cell->isAncestorOf(child))) return row;
    return -1;
}

void ResourcesDialog::fillCurrencySelect(QComboBox *select, const QString &current) const {
    const QSignalBlocker block(select);
    select->clear();
    QStringList texts;
    for (const QString &code : GanttResources::currencies()) texts.append(GanttCurrency{code, GanttResources::knownSymbol(code)}.text());
    const QStringList saved = GanttResources::savedCurrencies();
    texts.append(saved);
    // Neither listed nor saved here (another computer's file, or a saved one removed): offered
    // without ×, as long as the modal is open.
    QStringList others = m_openedCurrencies;
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (QTableWidgetItem *item = m_table->item(row, Currency)) others.append(item->data(kCurrencyRole).toString());
    others.append(current);
    for (const QString &text : std::as_const(others))
        if (!text.isEmpty() && !texts.contains(text)) texts.append(text);
    for (const QString &text : std::as_const(texts)) {
        select->addItem(text);
        if (saved.contains(text)) {
            select->setItemData(select->count() - 1, true, kSavedRole);
            select->setItemData(select->count() - 1, tr("Saved on this computer: the × removes it"), Qt::ToolTipRole);
        }
    }
    select->addItem(tr("+ Add currency…"));
    select->setItemData(select->count() - 1, true, kAddRole);
    select->setCurrentIndex(std::max<int>(0, select->findText(current)));
}

void ResourcesDialog::refreshCurrencySelects() {
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (QComboBox *select = currencySelect(row)) fillCurrencySelect(select, m_table->item(row, Currency)->data(kCurrencyRole).toString());
}

void ResourcesDialog::setRowCurrency(int row, const QString &text) {
    QTableWidgetItem *item = m_table->item(row, Currency);
    if (!item) return;
    // Listed later in every select (as the other people's): refilled first.
    const bool known = currencySelect(row)->findText(text) >= 0;
    item->setData(kCurrencyRole, text);  // itemChanged: validity and charts
    if (!known) refreshCurrencySelects();
    else if (QComboBox *select = currencySelect(row)) {
        const QSignalBlocker block(select);
        select->setCurrentIndex(select->findText(text));
    }
    m_table->viewport()->update();  // the cost shown with the new symbol
}

void ResourcesDialog::addCurrency(int row) {
    if (!currencySelect(row)) return;
    ModalDialog dialog(this, tr("Add currency"), tr("Add"), kAddSurfaceWidthPx);
    dialog.setObjectName(QStringLiteral("AddCurrencyDialog"));
    auto *intro = new QLabel(tr("It is saved on this computer, to use it in other documents too."));
    intro->setWordWrap(true);
    dialog.contentLayout()->addWidget(intro);
    auto *fields = new QGridLayout;
    fields->setHorizontalSpacing(12);
    fields->setVerticalSpacing(4);
    // No spaces: the currency is stored as "code symbol" and read back at the last space.
    const auto field = [&dialog, fields](int column, const QString &label, const QString &name, const QString &example, int length) {
        fields->addWidget(new QLabel(label), 0, column);
        auto *edit = new QLineEdit;
        edit->setObjectName(name);
        edit->setPlaceholderText(example);
        edit->setMaxLength(length);
        edit->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("\\S*")), edit));
        fields->addWidget(edit, 1, column);
        return edit;
    };
    QLineEdit *code = field(0, tr("Code"), QStringLiteral("AddCurrencyCode"), QStringLiteral("RON"), GanttCurrency::kMaxCodeLength);
    QLineEdit *symbol = field(1, tr("Symbol (optional)"), QStringLiteral("AddCurrencySymbol"), QStringLiteral("lei"),
                              GanttCurrency::kMaxSymbolLength);
    dialog.contentLayout()->addLayout(fields);
    auto *note = new QLabel(tr("This currency is already in the list"));
    note->setObjectName(QStringLiteral("AddCurrencyNote"));
    note->setStyleSheet(UiTheme::qss(QStringLiteral("color:@textMuted;")));
    QSizePolicy keepRoom = note->sizePolicy();  // shown and hidden while typing: the modal keeps its height
    keepRoom.setRetainSizeWhenHidden(true);
    note->setSizePolicy(keepRoom);
    dialog.contentLayout()->addWidget(note);
    const auto typed = [code, symbol] { return GanttCurrency{code->text(), symbol->text()}.normalized(); };
    const auto update = [&dialog, code, note, typed] {
        const bool known = !code->text().isEmpty() && !typed().canSave();
        note->setVisible(known);
        dialog.acceptButton()->setEnabled(!code->text().isEmpty() && !known);
    };
    connect(code, &QLineEdit::textChanged, &dialog, update);
    connect(symbol, &QLineEdit::textChanged, &dialog, update);
    update();
    code->setFocus();
    if (dialog.exec() != QDialog::Accepted || !typed().canSave()) return;

    // Saved at once, also if the Resources modal is then cancelled; every select lists it (past the
    // limit the oldest saved one is gone from the computer).
    const GanttCurrency added = typed();
    added.save();
    refreshCurrencySelects();
    setRowCurrency(row, added.text());
}

void ResourcesDialog::removeSavedCurrency(int row, int item) {
    QComboBox *select = currencySelect(row);
    if (!select || !select->itemData(item, kSavedRole).toBool()) return;
    // People using it keep it: their selects still offer it, without the ×.
    GanttResources::removeSavedCurrency(select->itemText(item));
    refreshCurrencySelects();
}

bool ResourcesDialog::eventFilter(QObject *watched, QEvent *event) {
    const QEvent::Type type = event->type();
    if (type == QEvent::MouseButtonPress && watched == m_table->viewport()) {
        const QModelIndex index = m_table->indexAt(static_cast<QMouseEvent *>(event)->position().toPoint());
        showChartRow(index.isValid() ? index.row() : -1);
    } else if (type == QEvent::MouseButtonPress && watched == m_table->verticalHeader()->viewport()) {
        const int row = m_table->verticalHeader()->logicalIndexAt(static_cast<QMouseEvent *>(event)->position().toPoint());
        if (row >= 0) showChartRow(row == m_chartRow ? -1 : row);
    }
    if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick) {
        // A row's currency list.
        int row = -1;
        for (int at = 0; at < m_table->rowCount() && row < 0; ++at)
            if (QComboBox *select = currencySelect(at); select && watched == select->view()->viewport()) row = at;
        if (QComboBox *select = currencySelect(row)) {
            const QPoint position = static_cast<QMouseEvent *>(event)->position().toPoint();
            const QModelIndex index = select->view()->indexAt(position);
            if (index.data(kSavedRole).toBool() && currencyRemoveRect(select->view()->visualRect(index)).contains(position)) {
                if (type == QEvent::MouseButtonRelease) {
                    select->hidePopup();
                    removeSavedCurrency(row, index.row());
                    select->showPopup();  // one row shorter: opened again at its new size
                }
                return true;
            }
        }
    }
    return ModalDialog::eventFilter(watched, event);
}

void ResourcesDialog::insertRow(int row, const GanttResource &person) {
    const QSignalBlocker block(m_table);
    m_table->insertRow(row);
    const QString texts[] = {person.firstName, person.lastName, person.email, person.role};
    for (int column = FirstName; column <= Role; ++column) m_table->setItem(row, column, new QTableWidgetItem(texts[column]));
    m_table->item(row, FirstName)->setData(Qt::UserRole, person.id);
    auto *cost = new QTableWidgetItem;
    cost->setData(Qt::EditRole, person.hourlyCostCents);
    cost->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->setItem(row, HourlyCost, cost);
    auto *currencyItem = new QTableWidgetItem;
    currencyItem->setFlags(Qt::ItemIsEnabled);
    currencyItem->setData(kCurrencyRole, person.currency.normalized().text());
    m_table->setItem(row, Currency, currencyItem);
    QComboBox *select = makeDarkSelect();
    select->setObjectName(QStringLiteral("ResourcesCurrency"));
    select->setAccessibleName(tr("Currency"));
    select->setToolTip(tr("Choose a currency, or add one at the end of the list"));
    select->setItemDelegate(new CurrencyItemDelegate(select));
    // Installed after the list's own filter, so it runs first: a click on the × does not choose.
    select->view()->viewport()->installEventFilter(this);
    connect(select, &QComboBox::currentIndexChanged, this, [this, select](int item) {
        const int at = rowOfCell(select, Currency);
        if (item < 0 || at < 0) return;
        if (select->itemData(item, kAddRole).toBool()) {
            // Back to the row's currency, and the modal once the list has closed (ui-components.md, "Menu").
            // The select as context: dropped if its row goes first.
            QTimer::singleShot(0, select, [this, select] {
                const int now = rowOfCell(select, Currency);
                if (now < 0) return;
                setRowCurrency(now, m_table->item(now, Currency)->data(kCurrencyRole).toString());
                addCurrency(now);
            });
            return;
        }
        setRowCurrency(at, select->itemText(item));
    });
    auto *currencyCell = new QWidget;
    auto *currencyLayout = new QHBoxLayout(currencyCell);
    currencyLayout->setContentsMargins(4, 0, 4, 0);
    currencyLayout->addWidget(select);
    m_table->setCellWidget(row, Currency, currencyCell);
    fillCurrencySelect(select, person.currency.normalized().text());
    auto *spacer = new QTableWidgetItem;
    spacer->setFlags(Qt::ItemIsEnabled);
    m_table->setItem(row, Remove, spacer);
    auto *remove = new QToolButton;
    remove->setObjectName(QStringLiteral("ResourcesRemove"));
    remove->setIcon(QIcon(QStringLiteral(":/assets/hugeicons/window-close-light.svg")));
    remove->setIconSize(QSize(14, 14));
    remove->setFixedSize(24, 24);
    remove->setToolTip(tr("Remove"));
    remove->setAccessibleName(tr("Remove"));
    remove->setFocusPolicy(Qt::NoFocus);
    connect(remove, &QToolButton::clicked, this, [this, remove] { removeRow(remove); });
    auto *cell = new QWidget;
    auto *layout = new QHBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(remove, 0, Qt::AlignCenter);
    m_table->setCellWidget(row, Remove, cell);
}

void ResourcesDialog::addPerson() {
    // FEAT-128: the last person's currency, EUR for the first.
    GanttResource person;
    if (const int last = m_table->rowCount() - 1; last >= 0) person.currency = rowPerson(last).currency.normalized();
    insertRow(m_table->rowCount(), person);
    updateRows();
    const int row = m_table->rowCount() - 1;
    m_settingCurrent = true;
    m_table->setFocus();
    m_table->setCurrentCell(row, FirstName);
    m_settingCurrent = false;
    m_table->scrollToItem(m_table->item(row, FirstName));
    m_table->editItem(m_table->item(row, FirstName));
}

GanttResource ResourcesDialog::rowPerson(int row) const {
    const auto text = [this, row](int column) { return m_table->item(row, column)->text(); };
    return {m_table->item(row, FirstName)->data(Qt::UserRole).toString(), text(FirstName), text(LastName),
            text(Email), text(Role), m_table->item(row, HourlyCost)->data(Qt::EditRole).toLongLong(),
            GanttCurrency::fromText(m_table->item(row, Currency)->data(kCurrencyRole).toString())};
}

void ResourcesDialog::movePerson(int from, int to) {
    const int rows = m_table->rowCount();
    if (from == to || from < 0 || to < 0 || from >= rows || to >= rows) return;
    const int column = std::max(m_table->currentColumn(), 0);
    m_settingCurrent = true;
    m_table->setCurrentItem(nullptr);  // an open editor commits its text first
    const GanttResource person = rowPerson(from);
    m_table->removeRow(from);
    insertRow(to, person);
    m_table->setCurrentCell(to, column);
    m_settingCurrent = false;
    // The person shown follows the move.
    if (m_chartRow == from) m_chartRow = to;
    else if (from < m_chartRow && m_chartRow <= to) --m_chartRow;
    else if (to <= m_chartRow && m_chartRow < from) ++m_chartRow;
    updateValidity();
    updateCharts();
}

void ResourcesDialog::removeRow(QWidget *button) {
    if (const int row = rowOfCell(button, Remove); row >= 0) {
        // The button is sending the signal: its cell is deleted later by removeRow.
        m_settingCurrent = true;
        m_table->removeRow(row);
        m_settingCurrent = false;
        if (m_chartRow == row) m_chartRow = -1;
        else if (m_chartRow > row) --m_chartRow;
    }
    updateRows();
    updateValidity();
    updateCharts();
}

void ResourcesDialog::updateRows() {
    const int rows = m_table->rowCount();
    m_empty->setVisible(rows == 0);
    m_table->setVisible(rows > 0);
    // At least kMinVisibleRows whole rows (user, 2026-10-30: one row was cut at the bottom); the
    // empty text takes the same room, so the modal keeps its height when the first person is added.
    const int height = kHeaderHeightPx + std::clamp(rows, kMinVisibleRows, kVisibleRows) * kRowHeightPx;
    m_table->setFixedHeight(height);
    m_empty->setFixedHeight(height);
    m_scroll->updateGeometry();
}

void ResourcesDialog::updateValidity() {
    bool valid = true;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *cost = m_table->item(row, HourlyCost);
        if (!cost) continue;  // being added
        const GanttResourceProblems problems = GanttResources::problems(rowPerson(row));
        for (const auto &[column, wrong] : {std::pair{int(FirstName), problems.firstName}, {int(LastName), problems.lastName},
                                            {int(Email), problems.email}, {int(Role), problems.role}}) {
            const QString tip = !wrong ? QString() : column == Email ? tr("Invalid email address") : tr("Required");
            QTableWidgetItem *item = m_table->item(row, column);
            if (item->toolTip() != tip) {
                const QSignalBlocker block(m_table);
                item->setToolTip(tip);
            }
            valid = valid && tip.isEmpty();
        }
    }
    acceptButton()->setEnabled(valid);
}

GanttResources ResourcesDialog::resources() const {
    GanttResources result;
    for (int row = 0; row < m_table->rowCount(); ++row) result.people.append(rowPerson(row));
    return result.normalized();
}

GanttCalendar ResourcesDialog::calendar() const {
    return m_calendar.withWorkingWeek(m_saturday->isChecked(), m_sunday->isChecked(), int(m_length->value()));
}
