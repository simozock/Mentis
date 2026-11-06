#include "ganttbaselinemenu.h"

#include "floatingui.h"
#include "uitheme.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Rows of 28 px as the floating lists; names 13 px, dates 11 px muted; the compared row checked
// (surfaceRaised, a check before the name).
QString menuStyle() {
    return UiTheme::qss(QStringLiteral(
        "QLabel{color:@text;font-size:13px;background:transparent;}"
        "QLabel#GanttBaselineTitle{font-weight:600;}"
        "QLabel[muted=\"true\"]{color:@textMuted;font-size:11px;}"
        "QLineEdit{color:@text;background:@surfaceSunken;border:1px solid @divider;border-radius:4px;padding:0 8px;"
        "font-size:13px;min-height:26px;max-height:26px;selection-background-color:@accent;}"
        "QLineEdit:focus{border-color:@accent;}"
        "QPushButton{color:#ffffff;background:@accent;border:0;border-radius:4px;padding:0 12px;font-size:13px;"
        "font-weight:600;min-height:28px;max-height:28px;}"
        "QPushButton:disabled{color:@textMuted;background:@surfaceRaised;}"
        "QToolButton{color:@text;background:transparent;border:0;border-radius:4px;padding:0 8px;font-size:13px;"
        "min-height:28px;text-align:left;}"
        "QToolButton:hover{background:@divider;}"
        "QToolButton:checked{background:@surfaceRaised;}"
        "QToolButton#GanttBaselineRename,QToolButton#GanttBaselineRemove{color:@textMuted;padding:0;"
        "min-width:24px;max-width:24px;}"
        "QToolButton#GanttBaselineRename:hover,QToolButton#GanttBaselineRemove:hover{color:@text;}"
        "QToolButton#GanttBaselineRemoved{color:@textMuted;font-size:12px;}"
        "QFrame#GanttBaselineDivider{background:@divider;}"));
}

QFrame *divider() {
    auto *line = new QFrame;
    line->setObjectName(QStringLiteral("GanttBaselineDivider"));
    line->setFixedHeight(1);
    return line;
}

}  // namespace

GanttBaselineMenu::GanttBaselineMenu(QWidget *parent)
    : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint) {
    setObjectName(QStringLiteral("GanttBaselineMenu"));
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoMouseReplay);  // as GridMenuPopup: a click on the Baseline button only closes it
    setFixedWidth(kWidth);
    setStyleSheet(menuStyle());
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);
    auto *title = new QLabel(tr("New snapshot"));
    title->setObjectName(QStringLiteral("GanttBaselineTitle"));
    layout->addWidget(title);
    auto *createRow = new QHBoxLayout;
    createRow->setSpacing(8);
    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("GanttBaselineNameField"));
    m_name->setAccessibleName(tr("Snapshot name"));
    m_save = new QPushButton(tr("Save"));
    m_save->setObjectName(QStringLiteral("GanttBaselineSave"));
    createRow->addWidget(m_name, 1);
    createRow->addWidget(m_save);
    layout->addLayout(createRow);
    const auto create = [this] {
        if (m_save->isEnabled() && !m_name->text().trimmed().isEmpty()) emit createRequested(m_name->text());
    };
    connect(m_save, &QPushButton::clicked, this, create);
    connect(m_name, &QLineEdit::returnPressed, this, create);
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_save->setEnabled(m_name->isEnabled() && !text.trimmed().isEmpty());
    });
    layout->addWidget(divider());
    m_list = new QWidget;
    m_listLayout = new QVBoxLayout(m_list);
    m_listLayout->setContentsMargins(0, 0, 0, 0);
    m_listLayout->setSpacing(2);
    layout->addWidget(m_list);
}

void GanttBaselineMenu::setEntries(const QList<Entry> &entries, const QString &compared, const QStringList &removed,
                                   bool canCreate, const QString &defaultName) {
    if (compared != m_compared) m_removedOpen = false;
    m_entries = entries;
    m_compared = compared;
    m_removed = removed;
    m_name->setEnabled(canCreate);
    m_name->setText(defaultName);
    m_name->selectAll();
    m_save->setEnabled(canCreate && !defaultName.trimmed().isEmpty());
    rebuild();
}

void GanttBaselineMenu::rebuild() {
    while (QLayoutItem *item = m_listLayout->takeAt(0)) {
        if (QWidget *widget = item->widget()) widget->deleteLater();
        delete item;
    }
    if (m_entries.isEmpty()) {
        auto *empty = new QLabel(tr("No snapshots yet"));
        empty->setObjectName(QStringLiteral("GanttBaselineEmpty"));
        empty->setProperty("muted", true);
        m_listLayout->addWidget(empty);
        adjustSize();
        return;
    }
    // "None" turns the comparison off; then the snapshots, newest first.
    auto *none = new QToolButton;
    none->setObjectName(QStringLiteral("GanttBaselineNone"));
    none->setText(tr("None"));
    none->setCheckable(true);
    none->setChecked(m_compared.isEmpty());
    none->setToolButtonStyle(Qt::ToolButtonTextOnly);
    none->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(none, &QToolButton::clicked, this, [this] { emit compareRequested({}); });
    m_listLayout->addWidget(none);
    const QLocale locale;
    for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) {
        const Entry entry = *it;
        auto *row = new QWidget;
        row->setObjectName(QStringLiteral("GanttBaselineRow"));
        row->setProperty("baselineId", entry.id);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(2);
        auto *name = new QToolButton;
        name->setObjectName(QStringLiteral("GanttBaselineName"));
        const bool checked = entry.id == m_compared;
        name->setText((checked ? QStringLiteral("✓  ") : QString()) + entry.name);
        name->setCheckable(true);
        name->setChecked(checked);
        name->setToolTip(entry.name);
        name->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        // A click compares with it; on the compared one it stops comparing.
        connect(name, &QToolButton::clicked, this, [this, id = entry.id] { emit compareRequested(id == m_compared ? QString() : id); });
        rowLayout->addWidget(name, 1);
        auto *taken = new QLabel(locale.toString(entry.taken, QLocale::ShortFormat));
        taken->setProperty("muted", true);
        rowLayout->addWidget(taken);
        auto *rename = new QToolButton;
        rename->setObjectName(QStringLiteral("GanttBaselineRename"));
        rename->setText(QStringLiteral("✎"));
        rename->setToolTip(tr("Rename"));
        rename->setAccessibleName(tr("Rename"));
        connect(rename, &QToolButton::clicked, this, [this, id = entry.id] { startRename(id); });
        rowLayout->addWidget(rename);
        auto *remove = new QToolButton;
        remove->setObjectName(QStringLiteral("GanttBaselineRemove"));
        remove->setText(QStringLiteral("×"));
        remove->setToolTip(tr("Delete snapshot"));
        remove->setAccessibleName(tr("Delete snapshot"));
        connect(remove, &QToolButton::clicked, this, [this, id = entry.id] { emit removeRequested(id); });
        rowLayout->addWidget(remove);
        m_listLayout->addWidget(row);
        // Under the compared snapshot, the tasks removed since, a click shows their names.
        if (checked && !m_removed.isEmpty()) {
            auto *removed = new QToolButton;
            removed->setObjectName(QStringLiteral("GanttBaselineRemoved"));
            removed->setText((m_removedOpen ? QStringLiteral("▾ ") : QStringLiteral("▸ "))
                             + tr("%n task(s) removed", nullptr, int(m_removed.size())));
            removed->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            connect(removed, &QToolButton::clicked, this, [this] {
                m_removedOpen = !m_removedOpen;
                rebuild();
            });
            m_listLayout->addWidget(removed);
            if (m_removedOpen)
                for (const QString &task : std::as_const(m_removed)) {
                    auto *label = new QLabel(task);
                    label->setObjectName(QStringLiteral("GanttBaselineRemovedTask"));
                    label->setProperty("muted", true);
                    label->setContentsMargins(24, 0, 8, 0);
                    m_listLayout->addWidget(label);
                }
        }
    }
    adjustSize();
}

// The name becomes a field: Enter or leaving it renames (an empty name keeps the old one), Esc cancels.
void GanttBaselineMenu::startRename(const QString &id) {
    for (QWidget *row : m_list->findChildren<QWidget *>(QStringLiteral("GanttBaselineRow"))) {
        if (row->property("baselineId").toString() != id) continue;
        auto *name = row->findChild<QToolButton *>(QStringLiteral("GanttBaselineName"));
        auto it = std::find_if(m_entries.cbegin(), m_entries.cend(), [&id](const Entry &entry) { return entry.id == id; });
        if (!name || it == m_entries.cend()) return;
        auto *field = new QLineEdit(it->name);
        field->setObjectName(QStringLiteral("GanttBaselineRenameField"));
        field->setAccessibleName(tr("Snapshot name"));
        static_cast<QHBoxLayout *>(row->layout())->replaceWidget(name, field);
        name->hide();
        field->selectAll();
        field->setFocus();
        connect(field, &QLineEdit::editingFinished, this, [this, id, field] {
            if (field->property("finished").toBool()) return;
            field->setProperty("finished", true);
            const QString text = field->text();
            if (text.trimmed().isEmpty()) rebuild();
            else emit renameRequested(id, text);
        });
        field->installEventFilter(this);
        return;
    }
}

bool GanttBaselineMenu::eventFilter(QObject *watched, QEvent *event) {
    // Esc in the rename field: the old name back, the popover stays open.
    if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape
        && watched->objectName() == QLatin1StringView("GanttBaselineRenameField")) {
        watched->setProperty("finished", true);
        rebuild();
        return true;
    }
    return QFrame::eventFilter(watched, event);
}

void GanttBaselineMenu::popupBelow(QWidget *anchor) {
    adjustSize();
    QPoint pos = anchor->mapToGlobal(QPoint(anchor->width() / 2 - width() / 2, anchor->height() + 4));
    if (const QScreen *screen = anchor->screen()) {
        const QRect area = screen->availableGeometry();
        pos.setX(std::clamp(pos.x(), area.left() + 4, std::max(area.left() + 4, area.right() - width() - 4)));
    }
    move(pos);
    show();
    m_name->setFocus();
    m_name->selectAll();
}

void GanttBaselineMenu::paintEvent(QPaintEvent *) {
    const FloatingListPopup::Colors colors = FloatingListPopup::themed();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(colors.border, 1));
    painter.setBrush(colors.surface);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}
