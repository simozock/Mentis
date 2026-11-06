#include "resourcesdialog.h"
#include "help.h"

#include "numberfield.h"
#include "stylecontrols.h"
#include "toggleswitch.h"
#include "uitheme.h"

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
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr int kSurfaceWidthPx = 760;
constexpr int kAddSurfaceWidthPx = 420;  // as ModalDialog::askText
constexpr int kRemoveWidthPx = 32;
constexpr int kNumberWidthPx = 32;
const QColor kErrorColor(0xe5, 0x48, 0x4d);  // as the spell checker's underline (ui-foundations.md)

// Cells edited with a plain field (texts at most kMaxTextLength); the cost with a number field
// in the locale's format and shown as money; a cell with an error (its tooltip, set by
// updateValidity) has a red border.
class ResourceDelegate final : public QStyledItemDelegate {
public:
    ResourceDelegate(std::function<QString()> currency, QObject *parent)
        : QStyledItemDelegate(parent), m_currency(std::move(currency)) {}

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
        bool ok = false;
        const double value = QLocale().toDouble(text, &ok);
        const qint64 cents = ok && std::isfinite(value) ? std::clamp<qint64>(std::llround(value * 100.0), 0, GanttResources::kMaxHourlyCostCents) : 0;
        model->setData(index, cents, Qt::EditRole);
    }
    QString displayText(const QVariant &value, const QLocale &locale) const override {
        if (value.typeId() == QMetaType::LongLong) return ResourcesDialog::formatCost(value.toLongLong(), m_currency());
        return QStyledItemDelegate::displayText(value, locale);
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

private:
    std::function<QString()> m_currency;
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

QLabel *sectionTitle(const QString &text, QWidget *parent) {
    auto *label = new QLabel(text, parent);
    label->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600;"));
    return label;
}

}  // namespace

QString ResourcesDialog::formatCost(qint64 cents, const QString &symbol) {
    return QLocale().toCurrencyString(double(cents) / 100.0, symbol, 2);
}

ResourcesDialog::ResourcesDialog(QWidget *parent, const GanttResources &resources, const GanttCalendar &calendar)
    : ModalDialog(parent, tr("Resources"), tr("Save"), kSurfaceWidthPx), m_calendar(calendar) {
    Help::setPage(this, QStringLiteral("gantt/people-and-costs.md"));  // FEAT-092: F1 and "?" open it
    setObjectName(QStringLiteral("ResourcesDialog"));

    // People, with the currency next to the title.
    auto *peopleRow = new QHBoxLayout;
    peopleRow->setContentsMargins(0, 0, 0, 0);
    peopleRow->addWidget(sectionTitle(tr("People"), this));
    peopleRow->addStretch();
    auto *currencyLabel = new QLabel(tr("Currency"), this);
    peopleRow->addWidget(currencyLabel);
    m_currency = makeDarkSelect(this);
    m_currency->setObjectName(QStringLiteral("ResourcesCurrency"));
    m_currency->setAccessibleName(tr("Currency"));
    // Listed, saved on this computer, or added with the last item (user, 2026-10-30; it was typed
    // in the select, with a "+" after it to save it).
    m_currency->setToolTip(tr("Choose a currency, or add one at the end of the list"));
    for (const QString &code : GanttResources::currencies()) {
        GanttResources listed;
        listed.currency = code;
        listed.currencySymbol = GanttResources::knownSymbol(code);
        m_currency->addItem(listed.currencyText());
    }
    m_currency->addItem(tr("+ Add currency…"));
    m_currency->setItemData(addCurrencyItem(), true, kAddRole);
    for (const QString &text : GanttResources::savedCurrencies()) insertCurrencyItem(text, true);
    // The document's currency, also one neither listed nor saved here (another person's file).
    m_documentCurrency = resources.normalized().currencyText();
    m_shownCurrency = m_documentCurrency;
    if (findCurrencyItem(m_shownCurrency) < 0) insertCurrencyItem(m_shownCurrency, false);
    m_currency->setCurrentIndex(findCurrencyItem(m_shownCurrency));
    m_currency->setItemDelegate(new CurrencyItemDelegate(m_currency));
    // Installed after the list's own filter, so it runs first: a click on the × does not choose.
    m_currency->view()->viewport()->installEventFilter(this);
    m_currency->setFixedWidth(120);
    peopleRow->addWidget(m_currency);
    contentLayout()->addLayout(peopleRow);

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
    m_table->setHorizontalHeaderLabels({tr("First name"), tr("Last name"), tr("Email"), tr("Role"), tr("Hourly cost"), QString()});
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
    m_table->setItemDelegate(new ResourceDelegate([this] { return currency().displaySymbol(); }, m_table));
    QHeaderView *header = m_table->horizontalHeader();
    header->setHighlightSections(false);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setFixedHeight(kHeaderHeightPx);
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(Email, QHeaderView::Stretch);
    for (const auto &[column, width] : {std::pair{FirstName, 120}, {LastName, 120}, {Role, 130}, {HourlyCost, 120}, {Remove, kRemoveWidthPx}})
        header->resizeSection(column, width);
    contentLayout()->addWidget(m_table);

    m_empty = new QLabel(tr("No people yet"), this);
    m_empty->setObjectName(QStringLiteral("ResourcesEmpty"));
    m_empty->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_empty->setStyleSheet(UiTheme::qss(QStringLiteral("color:@textMuted;")));
    contentLayout()->addWidget(m_empty);
    m_add = new QPushButton(tr("+ Add person"), this);
    m_add->setObjectName(QStringLiteral("ResourcesAdd"));
    m_add->setStyleSheet(UiTheme::qss(QStringLiteral(
        "QPushButton { background:@surfaceRaised; color:@text; border:1px solid @divider; }"
        "QPushButton:hover { background:@divider; } QPushButton:pressed { background:@surfaceSunken; }")));
    m_add->setAutoDefault(false);
    contentLayout()->addWidget(m_add, 0, Qt::AlignLeft);

    // Working week of the sheet.
    contentLayout()->addSpacing(4);
    contentLayout()->addWidget(sectionTitle(tr("Working week"), this));
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
    // Hours and quarter hours (user, 2026-10-29: 8 h 15, 8 h 30).
    auto *length = new QWidget(this);
    auto *lengthLayout = new QHBoxLayout(length);
    lengthLayout->setContentsMargins(0, 0, 0, 0);
    lengthLayout->setSpacing(6);
    m_hours = new NumberField(this);
    m_hours->setObjectName(QStringLiteral("ResourcesHours"));
    m_hours->setAccessibleName(tr("Hours"));
    m_hours->setRange(0, GanttCalendar::kMaxMinutesPerDay / 60);
    m_hours->setValue(calendar.minutesPerDay / 60);
    m_hours->setFixedWidth(96);
    m_minutes = makeDarkSelect(this);
    m_minutes->setObjectName(QStringLiteral("ResourcesMinutes"));
    m_minutes->setAccessibleName(tr("Minutes"));
    for (int minutes = 0; minutes < 60; minutes += GanttCalendar::kMinutesStep)
        m_minutes->addItem(QStringLiteral("%1").arg(minutes, 2, 10, QLatin1Char('0')), minutes);
    m_minutes->setCurrentIndex(std::max(0, m_minutes->findData(calendar.minutesPerDay % 60)));
    lengthLayout->addWidget(m_hours);
    lengthLayout->addWidget(new QLabel(tr("h"), this));
    lengthLayout->addWidget(m_minutes);
    lengthLayout->addWidget(new QLabel(tr("min"), this));
    addRow(tr("Length of a working day"), length);
    contentLayout()->addLayout(grid);

    for (const GanttResource &person : resources.people) insertRow(m_table->rowCount(), person);
    updateRows();
    updateValidity();
    connect(m_add, &QPushButton::clicked, this, &ResourcesDialog::addPerson);
    connect(m_table, &QTableWidget::itemChanged, this, &ResourcesDialog::updateValidity);
    connect(m_table, &QTableWidget::clicked, m_table, [this](const QModelIndex &index) {
        if (index.column() != Remove) m_table->edit(index);
    });
    connect(m_currency, &QComboBox::currentIndexChanged, this, [this](int item) {
        if (item < 0) return;
        if (m_currency->itemData(item, kAddRole).toBool()) {
            // Back to the shown currency, and the modal once the list has closed (ui-components.md, "Menu").
            QTimer::singleShot(0, this, [this] {
                m_currency->setCurrentIndex(findCurrencyItem(m_shownCurrency));
                addCurrency();
            });
            return;
        }
        m_shownCurrency = m_currency->itemText(item);
        m_table->viewport()->update();
    });
}

void ResourcesDialog::insertCurrencyItem(const QString &text, bool saved) {
    const int item = addCurrencyItem();
    m_currency->insertItem(item, text);
    setCurrencyItemSaved(item, saved);
}

void ResourcesDialog::setCurrencyItemSaved(int item, bool saved) {
    m_currency->setItemData(item, saved, kSavedRole);
    m_currency->setItemData(item, saved ? tr("Saved on this computer: the × removes it") : QVariant(), Qt::ToolTipRole);
}

int ResourcesDialog::findCurrencyItem(const QString &text) const {
    for (int item = 0; item < addCurrencyItem(); ++item)
        if (m_currency->itemText(item) == text) return item;
    return -1;
}

void ResourcesDialog::addCurrency() {
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
    QLineEdit *code = field(0, tr("Code"), QStringLiteral("AddCurrencyCode"), QStringLiteral("RON"), GanttResources::kMaxCurrencyLength);
    QLineEdit *symbol = field(1, tr("Symbol (optional)"), QStringLiteral("AddCurrencySymbol"), QStringLiteral("lei"),
                              GanttResources::kMaxSymbolLength);
    dialog.contentLayout()->addLayout(fields);
    auto *note = new QLabel(tr("This currency is already in the list"));
    note->setObjectName(QStringLiteral("AddCurrencyNote"));
    note->setStyleSheet(UiTheme::qss(QStringLiteral("color:@textMuted;")));
    QSizePolicy keepRoom = note->sizePolicy();  // shown and hidden while typing: the modal keeps its height
    keepRoom.setRetainSizeWhenHidden(true);
    note->setSizePolicy(keepRoom);
    dialog.contentLayout()->addWidget(note);
    const auto typed = [code, symbol] {
        GanttResources result;
        result.currency = code->text();
        result.currencySymbol = symbol->text();
        return result.normalized();
    };
    const auto update = [&dialog, code, note, typed] {
        const bool known = !code->text().isEmpty() && !typed().canSaveCurrency();
        note->setVisible(known);
        dialog.acceptButton()->setEnabled(!code->text().isEmpty() && !known);
    };
    connect(code, &QLineEdit::textChanged, &dialog, update);
    connect(symbol, &QLineEdit::textChanged, &dialog, update);
    update();
    code->setFocus();
    if (dialog.exec() != QDialog::Accepted || !typed().canSaveCurrency()) return;

    // Saved at once, also if the Resources modal is then cancelled.
    const GanttResources added = typed();
    added.saveCurrency();
    // The list as saved: past the limit the oldest is gone from the computer.
    const QStringList saved = GanttResources::savedCurrencies();
    for (int item = addCurrencyItem() - 1; item >= 0; --item)
        if (m_currency->itemData(item, kSavedRole).toBool() && !saved.contains(m_currency->itemText(item))) m_currency->removeItem(item);
    // The document's own row of the same currency becomes the saved one. Not removed: when chosen,
    // the choice would move to the next row, "+ Add currency...", and open this modal again.
    if (const int item = findCurrencyItem(added.currencyText()); item >= 0) setCurrencyItemSaved(item, true);
    else insertCurrencyItem(added.currencyText(), true);
    m_currency->setCurrentIndex(findCurrencyItem(added.currencyText()));
}

void ResourcesDialog::removeSavedCurrency(int item) {
    if (!m_currency->itemData(item, kSavedRole).toBool()) return;
    const QString text = m_currency->itemText(item);
    GanttResources::removeSavedCurrency(text);
    // The document keeps its own currency: its row stays, without the ×. Another row goes at once
    // (user, 2026-10-30: a removed currency chosen in the modal stayed until it was opened again).
    if (text == m_documentCurrency) {
        setCurrencyItemSaved(item, false);
        return;
    }
    // Chosen: back to the document's currency first, or removing the row would move the choice
    // to the next one, possibly "+ Add currency...".
    if (item == m_currency->currentIndex()) m_currency->setCurrentIndex(findCurrencyItem(m_documentCurrency));
    m_currency->removeItem(findCurrencyItem(text));
}

bool ResourcesDialog::eventFilter(QObject *watched, QEvent *event) {
    const QEvent::Type type = event->type();
    if (watched == m_currency->view()->viewport()
        && (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick)) {
        const QPoint position = static_cast<QMouseEvent *>(event)->position().toPoint();
        const QModelIndex index = m_currency->view()->indexAt(position);
        if (index.data(kSavedRole).toBool() && currencyRemoveRect(m_currency->view()->visualRect(index)).contains(position)) {
            if (type == QEvent::MouseButtonRelease) {
                removeSavedCurrency(index.row());
                // The list is one row shorter: opened again at its new size.
                m_currency->hidePopup();
                m_currency->showPopup();
            }
            return true;
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
    insertRow(m_table->rowCount(), {});
    updateRows();
    const int row = m_table->rowCount() - 1;
    m_table->setFocus();
    m_table->setCurrentCell(row, FirstName);
    m_table->scrollToItem(m_table->item(row, FirstName));
    m_table->editItem(m_table->item(row, FirstName));
}

GanttResource ResourcesDialog::rowPerson(int row) const {
    const auto text = [this, row](int column) { return m_table->item(row, column)->text(); };
    return {m_table->item(row, FirstName)->data(Qt::UserRole).toString(), text(FirstName), text(LastName),
            text(Email), text(Role), m_table->item(row, HourlyCost)->data(Qt::EditRole).toLongLong()};
}

void ResourcesDialog::movePerson(int from, int to) {
    const int rows = m_table->rowCount();
    if (from == to || from < 0 || to < 0 || from >= rows || to >= rows) return;
    const int column = std::max(m_table->currentColumn(), 0);
    m_table->setCurrentItem(nullptr);  // an open editor commits its text first
    const GanttResource person = rowPerson(from);
    m_table->removeRow(from);
    insertRow(to, person);
    m_table->setCurrentCell(to, column);
    updateValidity();
}

void ResourcesDialog::removeRow(QWidget *button) {
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QWidget *cell = m_table->cellWidget(row, Remove);
        if (!cell || !cell->isAncestorOf(button)) continue;
        // The button is sending the signal: its cell is deleted later by removeRow.
        m_table->removeRow(row);
        break;
    }
    updateRows();
    updateValidity();
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
}

void ResourcesDialog::updateValidity() {
    bool valid = true;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        QTableWidgetItem *cost = m_table->item(row, HourlyCost);
        if (!cost) continue;  // being added
        const auto text = [this, row](int column) { return m_table->item(row, column)->text().trimmed(); };
        // A person has first name, last name and role (user, 2026-10-30); an empty row is dropped.
        const bool blank = text(FirstName).isEmpty() && text(LastName).isEmpty() && text(Email).isEmpty()
                           && text(Role).isEmpty() && cost->data(Qt::EditRole).toLongLong() == 0;
        for (const int column : {FirstName, LastName, Email, Role}) {
            QString tip;
            if (column == Email) {
                if (!GanttResources::isValidEmail(text(Email))) tip = tr("Invalid email address");
            } else if (!blank && text(column).isEmpty()) {
                tip = tr("Required");
            }
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

GanttResources ResourcesDialog::currency() const {
    GanttResources result;
    result.setCurrencyText(m_shownCurrency);
    return result.normalized();
}

GanttResources ResourcesDialog::resources() const {
    GanttResources result = currency();
    for (int row = 0; row < m_table->rowCount(); ++row) result.people.append(rowPerson(row));
    return result.normalized();
}

GanttCalendar ResourcesDialog::calendar() const {
    GanttCalendar result = m_calendar;
    for (const auto &[day, on] : {std::pair{int(Qt::Saturday), m_saturday->isChecked()}, {int(Qt::Sunday), m_sunday->isChecked()}}) {
        if (on) result.workingDays.insert(day);
        else result.workingDays.remove(day);
    }
    // At least a quarter hour, at most 24 h.
    result.minutesPerDay = std::clamp(int(m_hours->value()) * 60 + m_minutes->currentData().toInt(), GanttCalendar::kMinutesStep,
                                      GanttCalendar::kMaxMinutesPerDay);
    return result;
}
