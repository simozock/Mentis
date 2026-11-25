#include "ganttview.h"

#include "document.h"
#include "floatingui.h"
#include "ganttbaselinemenu.h"
#include "ganttcolumns.h"
#include "ganttcommands.h"
#include "gantttaskedits.h"
#include "ganttexport.h"
#include "ganttnamefield.h"
#include "ganttnames.h"
#include "gantttaskpanel.h"
#include "panelresizehandle.h"
#include "mindmapmodel.h"
#include "modaldialog.h"
#include "nodeitem.h"
#include "numberfield.h"
#include "shortcuts.h"
#include "stylecontrols.h"  // ColorPicker
#include "uitheme.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDateEdit>
#include <QDrag>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QLabel>
#include <QLineF>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStackedLayout>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

// The task panel's dragged width (user's request 2026-11-22); missing = GanttTaskPanel::kWidth.
const QString kPanelWidthSetting = QStringLiteral("gantt/panelWidth");

// Number (FEAT-024 numbering, read-only) is drawn before Name but stays after it in the model, so the
// tree (expand arrows, indentation) stays on Name, logical column 0.
enum Column { Name, Start, End, Duration, Progress, Priority, Person, Number, Extras, ColumnCount };

// Extras column (user, 2026-10-28): icons of the topic's web link and note, after Name.
enum Extra { NoExtra = 0, WebLinkExtra = 1, NoteExtra = 2 };
constexpr int kExtrasRole = Qt::UserRole + 1;  // Extra flags of the row
constexpr int kMilestoneRole = Qt::UserRole + 2;  // a milestone that is not a summary
constexpr int kNewRole = Qt::UserRole + 3;  // FEAT-082: a task not in the snapshot compared
constexpr int kNameFormatsRole = Qt::UserRole + 4;  // FEAT-124: the name's formats (GanttNames::ranges)
constexpr int kNameEditRole = Qt::UserRole + 5;     // FEAT-124: the name typed, {title, html}

constexpr int kExtraIcon = 16;
constexpr int kExtrasWidth = 8 + kExtraIcon + 8 + kExtraIcon + 8;

// The icons of a cell of the Extras column, left to right: web link, then note.
QList<QPair<Extra, QRect>> extraIcons(const QRect &cell, int flags) {
    QList<QPair<Extra, QRect>> icons;
    int x = cell.left() + 8;
    for (const Extra extra : {WebLinkExtra, NoteExtra}) {
        if (!(flags & extra)) continue;
        icons.append({extra, QRect(x, cell.top() + (cell.height() - kExtraIcon) / 2, kExtraIcon, kExtraIcon)});
        x += kExtraIcon + 8;
    }
    return icons;
}

// The table's column as the core's (GanttColumns keeps the choice and order, FEAT-078).
GanttColumns::Field fieldOf(int column) {
    using Field = GanttColumns::Field;
    switch (column) {
    case Number: return Field::Number;
    case Start: return Field::Start;
    case End: return Field::End;
    case Duration: return Field::Duration;
    case Progress: return Field::Progress;
    case Priority: return Field::Priority;
    case Person: return Field::People;
    default: return Field::Name;
    }
}

int columnOf(GanttColumns::Field field) {
    for (int column = Name; column < ColumnCount; ++column)
        if (column != Extras && fieldOf(column) == field) return column;
    return Name;
}

// The action buttons' icons in the page's text colour (light or dark page).
QIcon tintedIcon(const QIcon &base, const QColor &color, const QColor &disabled) {
    const auto tinted = [&base](const QColor &tint) {
        QPixmap pixmap = base.pixmap(QSize(FloatingUi::kIconSize, FloatingUi::kIconSize), 2.0);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), tint);
        return pixmap;
    };
    QIcon icon;
    icon.addPixmap(tinted(color), QIcon::Normal);
    icon.addPixmap(tinted(color), QIcon::Active);
    icon.addPixmap(tinted(disabled), QIcon::Disabled);
    return icon;
}

QString rgba(const QColor &color) {
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

QString shortDate(const QDate &date) { return QLocale().toString(date, QLocale::ShortFormat); }

// Pixels to scroll: the touchpad's own pixels, or notch steps (120 = one notch) of `step` pixels,
// fractions kept so high-resolution wheels scroll too.
int wheelPixels(const QWheelEvent *event, int step) {
    const QPoint pixels = event->pixelDelta();
    if (!pixels.isNull()) return pixels.y() != 0 ? pixels.y() : pixels.x();
    const QPoint angle = event->angleDelta();
    return int(std::round((angle.y() != 0 ? angle.y() : angle.x()) / 120.0 * step));
}

// One axis only: a touchpad's two fingers move both at once (the map's way, user 2026-10-30).
int wheelPixels(const QWheelEvent *event, int step, Qt::Orientation axis) {
    const auto along = [axis](const QPoint &delta) { return axis == Qt::Vertical ? delta.y() : delta.x(); };
    if (const QPoint pixels = event->pixelDelta(); !pixels.isNull()) return along(pixels);
    return int(std::round(along(event->angleDelta()) / 120.0 * step));
}

}  // namespace

// FEAT-082: the band above the chart's header, empty but while comparing: the legend of the
// comparison, left to right (user, 2026-10-30): the snapshot's grey bar with its name, the slip red
// (later) and green (earlier), the "New" pill. 12 px muted text, samples 16 x 4 px.
class GanttLegend final : public QWidget {
public:
    using QWidget::QWidget;
    QString baseline;  // the compared snapshot's name; empty = nothing drawn
    QColor text = GanttPalette::light().muted;
    QStringList labels() const { return {baseline, GanttView::tr("Later"), GanttView::tr("Earlier")}; }

protected:
    void paintEvent(QPaintEvent *) override {
        if (baseline.isEmpty()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont font = painter.font();
        font.setPixelSize(UiTheme::fontPx(12));
        painter.setFont(font);
        const QFontMetricsF metrics(font);
        const double middle = height() / 2.0;
        double x = 12;
        const QColor samples[] = {GanttCanvas::baselineColor(), GanttCanvas::criticalColor(), GanttCanvas::baselineEarlierColor()};
        const QStringList texts = labels();
        for (int i = 0; i < texts.size(); ++i) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(samples[i]);
            painter.drawRoundedRect(QRectF(x, middle - 2, 16, 4), 2, 2);
            x += 22;
            const QString label = metrics.elidedText(texts.at(i), Qt::ElideRight, i == 0 ? 200 : 120);
            painter.setPen(text);
            painter.drawText(QRectF(x, 0, metrics.horizontalAdvance(label) + 1, height()), Qt::AlignLeft | Qt::AlignVCenter, label);
            x += metrics.horizontalAdvance(label) + 16;
        }
        const QFont pillFont = GanttCanvas::newPillFont(font, UiTheme::fontPx(11));
        const QSizeF pill = GanttCanvas::newPillSize(pillFont);
        GanttCanvas::paintNewPill(&painter, QRectF(QPointF(x, middle - pill.height() / 2), pill), pillFont, UiTheme::color(UiTheme::Token::Accent));
    }
};

// ---------------------------------------------------------------------------------------------
// Table model: the plan as a tree, values from the schedule. Edits go through `edit`.

class GanttTableModel final : public QAbstractItemModel {
public:
    std::function<bool(const std::function<bool(GanttPlan &)> &)> edit;
    // id, new title, its formatted form (FEAT-124; empty: plain)
    std::function<bool(const QString &, const QString &, const QString &)> rename;
    std::function<const MindNode *(const QString &)> topicOf;  // FEAT-124: the row's topic
    QColor text = GanttPalette::light().text, muted = GanttPalette::light().muted;
    // FEAT-078: the topics' priority; FEAT-081: their people (ids and names, in the Resources list's
    // order, removed people left out), set with the plan; FEAT-024: their map number.
    struct Info { int priority = 0; QStringList people; QStringList names; QString number; int extras = NoExtra; };
    QHash<QString, Info> info;
    // FEAT-082: set before setPlan; Start, End and Duration that differ in red (later, longer) or
    // green, the snapshot's value in the tooltip.
    std::optional<GanttBaseline> baseline;
    GanttBaselineComparison comparison;
    QHash<QString, int> baselineIndex;  // task id -> index in baseline->tasks
    void setBaseline(const GanttBaseline *snapshot, const GanttBaselineComparison &compared) {
        baseline = snapshot ? std::optional<GanttBaseline>(*snapshot) : std::nullopt;
        comparison = compared;
        baselineIndex.clear();
        if (baseline)
            for (int i = 0; i < baseline->tasks.size(); ++i) baselineIndex.insert(baseline->tasks.at(i).id, i);
    }

    void setPlan(const GanttPlan &plan, const GanttSchedule &schedule) {
        beginResetModel();
        m_plan = plan;
        m_schedule = schedule;
        m_rows.clear();
        m_top.clear();
        QHash<QString, int> &index = m_index;
        index.clear();
        for (const GanttTask &task : plan.tasks()) {
            index.insert(task.id, m_rows.size());
            m_rows.append({task.id, -1, {}, 0});
        }
        for (int i = 0; i < m_rows.size(); ++i) {
            const int parent = index.value(plan.tasks().at(i).parentId, -1);
            QVector<int> &siblings = parent < 0 ? m_top : m_rows[parent].children;
            m_rows[i].parent = parent;
            m_rows[i].row = siblings.size();
            siblings.append(i);
        }
        // FEAT-124: the names' formats, read once per plan (the title's HTML is parsed here, not
        // when a row is painted).
        m_nameFormats.clear();
        if (topicOf)
            for (const GanttTask &task : plan.tasks())
                if (const MindNode *topic = topicOf(task.id); topic && !topic->titleHtml.isEmpty())
                    if (auto formats = GanttNames::ranges(topic, schedule.tasks.value(task.id).summary); !formats.isEmpty())
                        m_nameFormats.insert(task.id, formats);
        endResetModel();
    }
    const GanttPlan &plan() const { return m_plan; }
    const GanttSchedule &schedule() const { return m_schedule; }
    QString id(const QModelIndex &index) const {
        return index.isValid() ? m_rows.at(int(index.internalId()) - 1).id : QString();
    }
    // FEAT-002: the name being typed for one task (GanttView::previewName); an empty id: none.
    void setNamePreview(const QString &id, const QString &name) {
        const QString shown = id.isEmpty() ? QString() : GanttTask::shown(name);
        if (id == m_previewId && shown == m_previewName) return;
        const QString before = m_previewId;
        m_previewId = id;
        m_previewName = shown;
        for (const QString &row : {before, id})
            if (const QModelIndex cell = indexOf(row, Name); cell.isValid()) emit dataChanged(cell, cell);
    }
    QModelIndex indexOf(const QString &id, int column = 0) const {
        const int i = m_index.value(id, -1);
        return i < 0 ? QModelIndex() : createIndex(m_rows.at(i).row, column, quintptr(i + 1));
    }

    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override {
        const QVector<int> &siblings = parent.isValid() ? m_rows.at(int(parent.internalId()) - 1).children : m_top;
        if (row < 0 || row >= siblings.size() || column < 0 || column >= ColumnCount) return {};
        return createIndex(row, column, quintptr(siblings.at(row) + 1));
    }
    QModelIndex parent(const QModelIndex &child) const override {
        if (!child.isValid()) return {};
        const int parent = m_rows.at(int(child.internalId()) - 1).parent;
        return parent < 0 ? QModelIndex() : createIndex(m_rows.at(parent).row, 0, quintptr(parent + 1));
    }
    int rowCount(const QModelIndex &parent = {}) const override {
        if (parent.column() > 0) return 0;
        return parent.isValid() ? m_rows.at(int(parent.internalId()) - 1).children.size() : m_top.size();
    }
    int columnCount(const QModelIndex & = {}) const override { return ColumnCount; }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
        switch (section) {
        case Name: return GanttView::tr("Name");
        case Start: return GanttView::tr("Start");
        case End: return GanttView::tr("End");
        case Duration: return GanttView::tr("Duration");
        case Progress: return GanttView::tr("Progress");
        case Priority: return GanttView::tr("Priority");
        case Person: return GanttView::tr("People");
        case Number: return GanttView::tr("No.");
        default: return {};
        }
    }

    Qt::ItemFlags flags(const QModelIndex &index) const override {
        if (!index.isValid()) return {};
        Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        const bool leaf = m_rows.at(int(index.internalId()) - 1).children.isEmpty();
        const bool dated = m_schedule.tasks.value(id(index)).dated;
        // End needs a start; a summary's dates, duration and % come from its children; a milestone has
        // no end nor duration of its own. Priority and person are edited in the task panel; the number
        // comes from the map's numbering.
        const bool milestone = m_plan.tasks().at(int(index.internalId()) - 1).milestone;
        const bool length = index.column() == End || index.column() == Duration;
        if (index.column() == Name
            || (leaf && index.column() <= Progress && (index.column() != End || dated) && !(length && milestone)))
            flags |= Qt::ItemIsEditable;
        return flags;
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid()) return {};
        const QString taskId = id(index);
        const GanttTask *task = &m_plan.tasks().at(int(index.internalId()) - 1);
        const GanttScheduledTask scheduled = m_schedule.tasks.value(taskId);
        if (!task) return {};
        const QVariant duration = !scheduled.summary ? QVariant(task->duration)
                                  : scheduled.dated ? QVariant(m_plan.workingDaysBetween(scheduled.start, scheduled.end)) : QVariant();
        if (role == Qt::DisplayRole || role == Qt::EditRole) {
            const bool display = role == Qt::DisplayRole;
            switch (index.column()) {
            case Name: return taskId == m_previewId && display ? m_previewName : task->shownName();
            case Start: return display ? (scheduled.dated ? QVariant(shortDate(scheduled.start)) : QVariant()) : QVariant(scheduled.start);
            case End: return display ? (scheduled.dated ? QVariant(shortDate(scheduled.end)) : QVariant()) : QVariant(scheduled.end);
            case Duration: return duration;
            case Progress: return display ? QVariant(QStringLiteral("%1%").arg(scheduled.progress)) : QVariant(task->progress);
            case Priority: return GanttCommands::priorityName(info.value(taskId).priority);
            case Person: return info.value(taskId).names.join(QStringLiteral(", "));
            case Number: return info.value(taskId).number;
            default: return {};
            }
        }
        if (role == Qt::UserRole) return scheduled.summary;  // bold in GanttDelegate
        if (role == kExtrasRole) return info.value(taskId).extras;
        if (role == kNameFormatsRole)  // none on the name being typed: its formats are the field's
            return index.column() == Name && taskId != m_previewId ? QVariant::fromValue(m_nameFormats.value(taskId)) : QVariant();
        if (role == kMilestoneRole) return task->milestone && !scheduled.summary;
        // Grey: computed (summaries, number) or not in use (a milestone's kept duration, its end).
        const bool unused = task->milestone && (index.column() == End || index.column() == Duration);
        if (role == kNewRole) return baseline && comparison.tasks.value(taskId) == GanttBaselineComparison::Change::New;
        const bool compared = baseline && !unused && (index.column() == Start || index.column() == End || index.column() == Duration);
        if (compared && (role == Qt::ForegroundRole || role == Qt::ToolTipRole)) {
            if (const int at = baselineIndex.value(taskId, -1); at >= 0) {
                const GanttBaselineTask *then = &baseline->tasks.at(at);
                using Field = GanttCommands::BaselineField;
                const Field field = index.column() == Start ? Field::Start : index.column() == End ? Field::End : Field::Duration;
                const int difference = GanttCommands::baselineDifference(*then, scheduled, duration.isValid() ? duration.toInt() : -1, field);
                if (role == Qt::ToolTipRole) {
                    if (index.column() == Duration)
                        return then->dated ? GanttView::tr("Baseline: %n day(s)", nullptr, then->duration) : GanttView::tr("Baseline: —");
                    return GanttView::tr("Baseline: %1").arg(then->dated ? shortDate(index.column() == Start ? then->start : then->end)
                                                                          : QStringLiteral("—"));
                }
                if (difference != 0) return difference > 0 ? GanttCanvas::criticalColor() : GanttCanvas::baselineEarlierColor();
            }
        }
        if (role == Qt::ForegroundRole) return (scheduled.summary && index.column() != Name) || index.column() == Number || unused ? muted : text;
        if (role == Qt::ToolTipRole && index.column() == Name) return task->shownName();
        if (role == Qt::ToolTipRole && index.column() == Person) return info.value(taskId).names.join(QLatin1Char('\n'));
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override {
        if (role == kNameEditRole && index.column() == Name && rename) {
            const QVariantList typed = value.toList();
            return typed.size() == 2 && rename(id(index), typed.at(0).toString(), typed.at(1).toString());
        }
        if (role != Qt::EditRole || !edit || !(flags(index) & Qt::ItemIsEditable)) return false;
        const QString taskId = id(index);
        const QDate start = m_schedule.tasks.value(taskId).start;
        switch (index.column()) {
        case Start:  // a task placed by a dependency changes its lag (FEAT-074)
            return edit([&](GanttPlan &plan) { return plan.moveTo(taskId, value.toDate()); });
        case End: {
            const int duration = GanttCommands::durationTo(m_plan, start, value.toDate());
            return edit([&](GanttPlan &plan) { return plan.setDuration(taskId, duration); });
        }
        case Duration: {
            const int duration = std::clamp(value.toInt(), GanttPlan::kMinDuration, GanttPlan::kMaxDuration);
            return edit([&](GanttPlan &plan) { return plan.setDuration(taskId, duration); });
        }
        case Progress: {
            const int progress = std::clamp(value.toInt(), 0, GanttPlan::kMaxProgress);
            return edit([&](GanttPlan &plan) { return plan.setProgress(taskId, progress); });
        }
        default: return false;
        }
    }

private:
    struct Row {
        QString id;
        int parent = -1;
        QVector<int> children;
        int row = 0;
    };
    GanttPlan m_plan;
    GanttSchedule m_schedule;
    QVector<Row> m_rows;
    QVector<int> m_top;
    QHash<QString, int> m_index;  // id -> m_rows
    QHash<QString, QList<QTextLayout::FormatRange>> m_nameFormats;
    QString m_previewId, m_previewName;  // setNamePreview, already as GanttTask::shown
};

namespace {

// Rows of GanttChart::kRowHeight; date fields with the app's calendar, the app's number fields in range.
class GanttDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    DatePicker::Colors pickerColors;  // set with the page's colours
    QIcon webLinkIcon, noteIcon;      // the Extras column's icons, in the page's text colour
    const ShortcutRegistry *shortcuts = nullptr;  // FEAT-124: the format keys in the name field
    std::function<void()> indent, outdent;        // Tab and Shift+Tab while a name is typed
    // FEAT-002: every state of the name being typed, for the bar and the panel (GanttView::previewName).
    std::function<void(const QModelIndex &, const QString &)> nameTyped;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (index.column() == Number) {  // 8 px from the card's edge, as the header's "N." (user, 2026-10-29)
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            const QString text = opt.text;
            opt.text.clear();
            QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
            painter->save();
            painter->setFont(opt.font);
            painter->setPen(opt.palette.color(QPalette::Text));
            painter->drawText(opt.rect.adjusted(8, 0, -4, 0), int(opt.displayAlignment), text);
            painter->restore();
            return;
        }
        const auto formats = index.column() == Name ? index.data(kNameFormatsRole).value<QList<QTextLayout::FormatRange>>()
                                                    : QList<QTextLayout::FormatRange>();
        const bool isNew = index.column() == Name && index.data(kNewRole).toBool();  // FEAT-082
        if (isNew || !formats.isEmpty()) {  // FEAT-124: the name drawn with its formats
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            const QString text = opt.text;
            opt.text.clear();
            QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
            const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget).adjusted(4, 0, -4, 0);
            const QFont pillFont = GanttCanvas::newPillFont(opt.font, UiTheme::fontPx(11));
            const QSizeF pill = isNew ? GanttCanvas::newPillSize(pillFont) : QSizeF();
            const double room = std::max(0.0, textRect.width() - (isNew ? pill.width() + 6 : 0.0));
            painter->save();
            painter->setFont(opt.font);
            // The row's text colour also when selected, as the style sheet's item:selected (the palette's
            // HighlightedText is the system's, white on the light selection).
            painter->setPen(opt.palette.color(QPalette::Text));
            const double width = GanttNames::paint(painter, QRectF(textRect.left(), textRect.top(), room, textRect.height()), text, formats);
            painter->restore();
            if (isNew)
                GanttCanvas::paintNewPill(painter, QRectF(QPointF(textRect.left() + width + 6, textRect.center().y() - pill.height() / 2 + 0.5), pill),
                                          pillFont, UiTheme::color(UiTheme::Token::Accent));
            return;
        }
        QStyledItemDelegate::paint(painter, option, index);
        if (index.column() != Extras) return;
        for (const auto &[extra, rect] : extraIcons(option.rect, index.data(kExtrasRole).toInt()))
            (extra == WebLinkExtra ? webLinkIcon : noteIcon).paint(painter, rect);
    }
    // Hover on an icon: "Link" or "Note" (user, 2026-10-28).
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option, const QModelIndex &index) override {
        if (event->type() != QEvent::ToolTip || index.column() != Extras) return QStyledItemDelegate::helpEvent(event, view, option, index);
        for (const auto &[extra, rect] : extraIcons(option.rect, index.data(kExtrasRole).toInt()))
            if (rect.contains(event->pos())) {
                QToolTip::showText(event->globalPos(), extra == WebLinkExtra ? GanttView::tr("Link") : GanttView::tr("Note"), view, rect);
                return true;
            }
        QToolTip::hideText();
        event->ignore();
        return true;
    }
    void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override {
        QStyledItemDelegate::initStyleOption(option, index);
        if (index.data(Qt::UserRole).toBool()) option->font.setBold(true);  // summaries
    }
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const int padding = index.column() == Number ? 8 + 4 : 0;  // as drawn in paint
        return {QStyledItemDelegate::sizeHint(option, index).width() + padding, GanttChart::kRowHeight};
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        if (index.column() == Start || index.column() == End) {
            auto *editor = new DateField(parent);
            editor->setDateRange(QDate(GanttPlan::kFirstYear, 1, 1), QDate(GanttPlan::kLastYear, 12, 31));
            editor->setDisplayFormat(QLocale().dateFormat(QLocale::ShortFormat));
            editor->setPickerColors(pickerColors);
            editor->setChevronColor(pickerColors.text);
            // A day picked in the calendar is the edit: applied and the editor closed.
            auto *self = const_cast<GanttDelegate *>(this);
            connect(editor, &DateField::picked, self, [self, editor] {
                emit self->commitData(editor);
                emit self->closeEditor(editor);
            });
            return editor;
        }
        if (index.column() == Duration || index.column() == Progress) {
            // The task panel's field with − and + (user, 2026-10-29); the value goes to the plan when
            // the editor closes (Enter, leaving it), one step however many clicks.
            auto *editor = new NumberField(parent);
            if (index.column() == Duration) editor->setRange(GanttPlan::kMinDuration, GanttPlan::kMaxDuration);
            else editor->setRange(0, GanttPlan::kMaxProgress);
            // A milestone's % is its checkbox (FEAT-048): 0 or 100.
            if (index.column() == Progress && index.data(kMilestoneRole).toBool()) editor->setStep(GanttPlan::kMaxProgress);
            return editor;
        }
        if (index.column() == Name) {  // FEAT-124: the name with its formats
            auto *editor = new GanttNameField(parent);
            editor->shortcuts = shortcuts;
            // Enter confirms (the delegate leaves Enter to a text edit).
            auto *self = const_cast<GanttDelegate *>(this);
            connect(editor, &GanttNameField::returnPressed, self, [self, editor] {
                emit self->commitData(editor);
                emit self->closeEditor(editor, QAbstractItemDelegate::NoHint);
            });
            return editor;
        }
        return QStyledItemDelegate::createEditor(parent, option, index);
    }
    // Tab and Shift+Tab in the name being typed: the name is confirmed and the task goes a level
    // down or up, as in the outliner's editor (the delegate would open the next cell instead).
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() == QEvent::KeyPress && qobject_cast<GanttNameField *>(object)) {
            const auto *key = static_cast<QKeyEvent *>(event);
            std::function<void()> callback;
            if (key->key() == Qt::Key_Backtab || (key->key() == Qt::Key_Tab && key->modifiers().testFlag(Qt::ShiftModifier))) callback = outdent;
            else if (key->key() == Qt::Key_Tab) callback = indent;
            if (callback) {
                auto *editor = static_cast<QWidget *>(object);
                emit commitData(editor);
                emit closeEditor(editor, QAbstractItemDelegate::NoHint);
                QTimer::singleShot(0, this, std::move(callback));
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }
    // A number field wider than its column (% is 52 px): − and + and the longest value fit, the
    // editor covers the next cell while it is open; in the last column it grows to the left, so the
    // table's edge does not cut off the + (user's photo, 2026-10-29).
    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto *field = qobject_cast<NumberField *>(editor);
        if (!field) return QStyledItemDelegate::updateEditorGeometry(editor, option, index);
        const int digits = field->fontMetrics().horizontalAdvance(QString::number(qint64(field->maximum())));
        QRect rect = option.rect;
        rect.setWidth(std::max(rect.width(), 2 * NumberField::kButtonPx + digits + 12));
        if (const QWidget *viewport = field->parentWidget(); viewport && rect.right() > viewport->width() - 1)
            rect.moveRight(std::max(rect.width() - 1, viewport->width() - 1));
        field->setGeometry(rect);
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const override {
        if (auto *name = qobject_cast<GanttNameField *>(editor)) {
            const auto *table = static_cast<const GanttTableModel *>(model);
            const MindNode *topic = table->topicOf ? table->topicOf(table->id(index)) : nullptr;
            QString title, html;
            if (name->result(topic, &title, &html)) model->setData(index, QVariantList{title, html}, kNameEditRole);
            return;
        }
        auto *field = qobject_cast<NumberField *>(editor);
        if (!field) return QStyledItemDelegate::setModelData(editor, model, index);
        // The text as typed (Enter reaches the delegate before the field confirms it), in range;
        // nothing valid typed: the field's value.
        bool typed = false;
        const double number = field->text().trimmed().replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&typed);
        const double value = std::clamp(typed ? number : field->value(), field->minimum(), field->maximum());
        model->setData(index, int(std::lround(value)));
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override {
        if (auto *name = qobject_cast<GanttNameField *>(editor)) {
            const auto *table = static_cast<const GanttTableModel *>(index.model());
            // Loaded once: the view calls this again on the cell's dataChanged, which the preview of
            // the typed name sends (it would put the old name back under the typing).
            if (name->property("loaded").toBool()) return;
            name->setProperty("loaded", true);
            const MindNode *topic = table->topicOf ? table->topicOf(table->id(index)) : nullptr;
            if (topic) name->load(topic, index.data(Qt::UserRole).toBool());
            name->selectAll();  // as a line edit opened by the delegate
            connect(name, &QTextEdit::textChanged, this, [this, name, cell = QPersistentModelIndex(index)] {
                if (nameTyped) nameTyped(cell, name->text());
            });
            return;
        }
        if (auto *field = qobject_cast<NumberField *>(editor)) {
            field->setValue(index.data(Qt::EditRole).toDouble());
            field->selectAll();  // typing replaces the number, as in a spin box
            return;
        }
        // A topic without a date opens on today.
        if (auto *date = qobject_cast<QDateEdit *>(editor); date && !index.data(Qt::EditRole).toDate().isValid()) {
            date->setDate(GanttToday::date());
            return;
        }
        QStyledItemDelegate::setEditorData(editor, index);
    }
};

constexpr char kTaskMime[] = "application/x-mentis-gantt-task";

// The table, with its editing state visible to the view. A row's name dragged moves its topic as in
// the outliner (user, 2026-10-29): before, inside or after the row under the cursor; a line or a ring
// shows where, so the rows stay aligned with the chart (the outliner opens a gap instead).
class GanttTableView final : public QTreeView {
public:
    using QTreeView::QTreeView;
    bool editing() const { return state() == EditingState; }
    std::function<void(const QModelIndex &)> movedByKey;  // the current row changed by a plain key
    std::function<bool(const QString &id, const QString &target, GanttView::DropPlace place)> moveTask;
    QColor rowLine;  // the items' bottom border, continued under the indentation
    QColor selectedRow;  // the selected items' background, continued under the indentation

protected:
    // The line and the selected background drawn here, not by the style sheet: any ::branch rule
    // hides the arrows (BUG-026).
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override {
        if (selectionModel()->isSelected(index)) painter->fillRect(rect, selectedRow);
        QTreeView::drawBranches(painter, rect, index);
        painter->fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), rowLine);
    }
    void mousePressEvent(QMouseEvent *event) override {
        const QPoint pos = event->position().toPoint();
        const QModelIndex index = indexAt(pos);
        // On the name, not on the arrow beside it; one row selected or none: several do not move
        // (as in the outliner).
        const bool onName = index.isValid() && index.column() == Name && visualRect(index).contains(pos);
        m_dragIndex = event->button() == Qt::LeftButton && onName && selectionModel()->selectedRows().size() <= 1
                          ? QPersistentModelIndex(index) : QPersistentModelIndex();
        m_dragStart = pos;
        QTreeView::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent *event) override {
        if (m_dragIndex.isValid() && event->buttons().testFlag(Qt::LeftButton) && !editing()) {
            if ((event->position().toPoint() - m_dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                const QModelIndex index = m_dragIndex;
                m_dragIndex = QPersistentModelIndex();
                dragRow(index);
            }
            event->accept();  // no rubber band selection from the name (outliner, 2026-10-13)
            return;
        }
        QTreeView::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override {
        m_dragIndex = QPersistentModelIndex();
        QTreeView::mouseReleaseEvent(event);
    }
    void dragEnterEvent(QDragEnterEvent *event) override {
        if (!event->mimeData()->hasFormat(kTaskMime)) { event->ignore(); return; }
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    void dragMoveEvent(QDragMoveEvent *event) override {
        const QPoint pos = event->position().toPoint();
        if (pos.y() < autoScrollMargin() || pos.y() > viewport()->height() - autoScrollMargin()) startAutoScroll();
        const QModelIndex index = indexAt(pos).siblingAtColumn(0);
        const QString moving = QString::fromUtf8(event->mimeData()->data(kTaskMime));
        if (!index.isValid() || !event->mimeData()->hasFormat(kTaskMime) || inside(index, moving)) {
            setDropHint({}, GanttView::DropPlace::Inside);
            event->ignore();
            return;
        }
        const QRect row = visualRect(index);
        const double relativeY = row.height() > 0 ? double(pos.y() - row.top()) / row.height() : 0.5;
        setDropHint(index, relativeY < 0.3 ? GanttView::DropPlace::Before
                           : relativeY > 0.7 ? GanttView::DropPlace::After : GanttView::DropPlace::Inside);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    void dragLeaveEvent(QDragLeaveEvent *event) override {
        stopAutoScroll();
        setDropHint({}, GanttView::DropPlace::Inside);
        QTreeView::dragLeaveEvent(event);
    }
    void dropEvent(QDropEvent *event) override {
        stopAutoScroll();
        const QString target = m_dropIndex.isValid() ? taskId(m_dropIndex) : QString();
        const GanttView::DropPlace place = m_dropPlace;
        setDropHint({}, GanttView::DropPlace::Inside);  // the table is rebuilt by the move
        const QString moving = QString::fromUtf8(event->mimeData()->data(kTaskMime));
        if (target.isEmpty() || !moveTask || !moveTask(moving, target, place)) { event->ignore(); return; }
        event->setDropAction(Qt::MoveAction);
        event->accept();
    }
    void paintEvent(QPaintEvent *event) override {
        QTreeView::paintEvent(event);
        if (!m_dropIndex.isValid()) return;
        QPainter painter(viewport());
        painter.setRenderHint(QPainter::Antialiasing);
        // The selection colour of Settings, as the outliner's drop line (user, 2026-11-18).
        const QColor accent = NodeItem::selectionStyle().selectedColor;
        const QRect row = visualRect(m_dropIndex);
        if (m_dropPlace == GanttView::DropPlace::Inside) {
            painter.setPen(QPen(accent, 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(QRectF(0, row.top(), viewport()->width(), row.height()).adjusted(1, 1, -1, -1), 4, 4);
        } else {
            // The line starts at the row's name: the level the topic lands on.
            const double y = m_dropPlace == GanttView::DropPlace::Before ? row.top() : row.bottom() + 1;
            painter.fillRect(QRectF(row.left(), y - 1, viewport()->width() - row.left(), 2), accent);
        }
    }

private:
    QString taskId(const QModelIndex &index) const { return static_cast<const GanttTableModel *>(model())->id(index); }
    // index is the moving row or below it: a topic cannot go into its own branch.
    bool inside(QModelIndex index, const QString &moving) const {
        for (; index.isValid(); index = index.parent())
            if (taskId(index) == moving) return true;
        return false;
    }
    void setDropHint(const QModelIndex &index, GanttView::DropPlace place) {
        if (m_dropIndex == index && m_dropPlace == place) return;
        m_dropIndex = index;
        m_dropPlace = place;
        viewport()->update();
    }
    // The name cell, half transparent, follows the cursor where it was grabbed (as the outliner's).
    void dragRow(const QModelIndex &index) {
        auto *mime = new QMimeData;
        mime->setData(kTaskMime, taskId(index).toUtf8());
        const QRect rect = visualRect(index);
        const QPixmap source = viewport()->grab(rect);
        QPixmap ghost(source.size());
        ghost.setDevicePixelRatio(source.devicePixelRatio());
        ghost.fill(Qt::transparent);
        {
            QPainter painter(&ghost);
            painter.setOpacity(0.5);
            painter.drawPixmap(0, 0, source);
        }
        auto *drag = new QDrag(this);
        drag->setMimeData(mime);
        drag->setPixmap(ghost);
        drag->setHotSpot(m_dragStart - rect.topLeft());
        drag->exec(Qt::MoveAction, Qt::MoveAction);
    }

    QPersistentModelIndex m_dragIndex;
    QPoint m_dragStart;
    QPersistentModelIndex m_dropIndex;
    GanttView::DropPlace m_dropPlace = GanttView::DropPlace::Inside;

protected:
    void keyPressEvent(QKeyEvent *event) override {
        const QModelIndex before = currentIndex();
        // FEAT-002 (user 2026-11-19): a printable key on the single selected task opens its name and
        // replaces it, as on the map (NodeItem::keyPressEvent); Enter keeps it, Esc the old name.
        // Shortcuts arrive earlier as ShortcutOverride; AltGr (Ctrl+Alt on Windows) still writes.
        const QString text = event->text();
        const Qt::KeyboardModifiers mods = event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        const bool printable = !text.isEmpty() && text.at(0).isPrint() && (!mods || mods == (Qt::ControlModifier | Qt::AltModifier));
        if (printable && !editing() && before.isValid() && selectionModel()->selectedRows().size() <= 1) {
            const QModelIndex name = before.siblingAtColumn(Name);
            setCurrentIndex(name);
            if (edit(name, QAbstractItemView::AllEditTriggers, event)) {
                if (QWidget *editor = indexWidget(name)) {
                    QApplication::sendEvent(editor, event);  // the whole name is selected: the key replaces it
                    return;
                }
            }
        }
        QTreeView::keyPressEvent(event);
        // Shift and Ctrl extend the selection, as with the mouse: the panel keeps its task.
        const QModelIndex after = currentIndex();
        if (movedByKey && after.isValid() && after != before && !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
            movedByKey(after);
    }
};

bool editing(const QTreeView *table) { return static_cast<const GanttTableView *>(table)->editing(); }

}  // namespace

// ---------------------------------------------------------------------------------------------
// Chart

GanttChart::GanttChart(QWidget *parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    viewport()->setMouseTracking(true);
    viewport()->setAutoFillBackground(false);
    // FEAT-035 part B: the bars are document content (the viewport too, as in MindMapView).
    setFont(UiTheme::documentFont());
    viewport()->setFont(UiTheme::documentFont());
    m_canvas.horizontalOffset = [this] { return horizontalScrollBar()->value(); };
    m_canvas.verticalOffset = [this] { return verticalOffset ? double(verticalOffset()) : 0.0; };
    m_canvas.textFont = viewport()->font();
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    updateRange();
}

void GanttChart::setZoomPercent(int percent, double anchorX) {
    percent = std::clamp(percent, kMinZoom, kMaxZoom);
    if (percent == m_canvas.zoomPercent()) return;
    const double day = (anchorX + horizontalScrollBar()->value()) / dayWidth();  // date under the anchor
    m_canvas.setZoomPercent(percent);
    updateScrollBars();
    horizontalScrollBar()->setValue(int(std::lround(day * dayWidth() - anchorX)));
    viewport()->update();
    emit zoomChanged(percent);
}

void GanttChart::setFirstDay(const QDate &day) { horizontalScrollBar()->setValue(m_canvas.offsetOfDay(day)); }

void GanttChart::scrollToDay(const QDate &day) { horizontalScrollBar()->setValue(m_canvas.offsetOfDay(day.addDays(-kOpenDaysBefore))); }

void GanttChart::setScale(Scale scale) {
    if (scale == m_canvas.scale()) return;
    // Keeps the date at the left edge in view.
    const QDate left = m_canvas.dateAt(0);
    m_canvas.setScale(scale);
    updateRange();
    horizontalScrollBar()->setValue(m_canvas.offsetOfDay(left));
    viewport()->update();
}

void GanttChart::setData(const GanttPlan &plan, const GanttSchedule &schedule, const QStringList &rows,
                         const QSet<QString> &selected, const QHash<QString, GanttBarLook> &looks) {
    m_canvas.setData(plan, schedule, rows, selected, looks);
    updateRange();
    viewport()->update();
}

void GanttChart::setBaseline(const std::optional<GanttBaseline> &baseline, const GanttBaselineComparison &comparison) {
    if (m_canvas.setBaseline(baseline, comparison)) viewport()->update();
}

// A new origin keeps the visible dates where they were.
void GanttChart::updateRange() {
    const int value = horizontalScrollBar()->value();
    const int shift = m_canvas.updateRange();
    updateScrollBars();  // the range first, so the kept position is not clamped
    if (shift != 0) horizontalScrollBar()->setValue(value + shift);
}

void GanttChart::updateScrollBars() {
    horizontalScrollBar()->setRange(0, std::max(0, m_canvas.contentWidth() - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setSingleStep(dayWidth());
}

void GanttChart::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

void GanttChart::paintEvent(QPaintEvent *) {
    QPainter painter(viewport());
    QFont header = UiTheme::interfaceFont();  // the timeline header is interface
    header.setPixelSize(UiTheme::fontPx(12));
    m_canvas.headerFont = header;
    m_canvas.textFont = viewport()->font();
    m_canvas.accent = UiTheme::color(UiTheme::Token::Accent);
    m_canvas.selectionRing = NodeItem::selectionStyle().selectedColor;
    m_canvas.paint(painter, viewport()->rect());
}

void GanttChart::keyPressEvent(QKeyEvent *event) {
    // Up/Down move between the tasks as in the table, not the scroll bar (user, 2026-10-29).
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && !(event->modifiers() & Qt::AltModifier)) {
        emit stepRequested(event->key(), event->modifiers());
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

// ---- mouse

void GanttChart::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const QPointF pos = event->position();
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    gesture.press = gesture.pos = pos;
    gesture.drag = Drag::None;
    gesture.id.clear();
    // The progress badge hangs under its bar, partly over the next row.
    if (const GanttTask *hovered = m_canvas.taskOf(gesture.hovered); hovered && !m_canvas.isMilestone(gesture.hovered)
        && m_canvas.progressBadge(gesture.hovered).adjusted(-2, -2, 2, 2).contains(pos)) {
        gesture.drag = Drag::Progress;
        gesture.id = gesture.hovered;
        return;
    }
    const int row = m_canvas.rowAt(pos.y());
    if (row < 0) return;
    const QString id = m_canvas.rows().at(row);
    emit taskClicked(id, event->modifiers());
    if (m_canvas.schedule().tasks.value(id).summary) return;
    const GanttTask *task = m_canvas.taskOf(id);
    const QRectF bar = m_canvas.barRect(id);
    constexpr double inset = GanttCanvas::kBarInset;
    if (!m_canvas.linkHandleAt(id, pos, &gesture.fromStart).isEmpty()) gesture.drag = Drag::Link;
    else if (task && !task->milestone && m_canvas.progressBadge(id).adjusted(-2, -2, 2, 2).contains(pos)) gesture.drag = Drag::Progress;
    else if (task && !task->milestone && m_canvas.onResizeGrip(bar, pos) && bar.adjusted(0, -inset, 0, inset).contains(QPointF(bar.center().x(), pos.y())))
        gesture.drag = Drag::Resize;
    else if (m_canvas.barAt(pos) == id) gesture.drag = Drag::Move;
    if (gesture.drag != Drag::None) gesture.id = id;
}

void GanttChart::mouseMoveEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    if (gesture.drag != Drag::None) {
        gesture.pos = pos;
        if (gesture.drag == Drag::Link) {
            const QString target = m_canvas.barAt(pos);
            const bool refused = !target.isEmpty() && target != gesture.id && !m_canvas.linkAllowed(m_canvas.linkFrom(target), m_canvas.linkTo(target));
            viewport()->setCursor(refused ? Qt::ForbiddenCursor : Qt::CrossCursor);
        }
        viewport()->update();
        return;
    }
    QString hovered = m_canvas.barAt(pos);
    // The circles and the badge stand outside the bar: they keep it hovered.
    if (hovered.isEmpty() && !gesture.hovered.isEmpty()
        && (!m_canvas.linkHandleAt(gesture.hovered, pos).isEmpty() || m_canvas.progressBadge(gesture.hovered).adjusted(-2, -2, 2, 2).contains(pos)))
        hovered = gesture.hovered;
    if (hovered != gesture.hovered) {
        gesture.hovered = hovered;
        viewport()->update();
    }
    Qt::CursorShape cursor = Qt::ArrowCursor;
    if (!hovered.isEmpty() && !m_canvas.schedule().tasks.value(hovered).summary) {
        const QRectF bar = m_canvas.barRect(hovered);
        const GanttTask *task = m_canvas.taskOf(hovered);
        if (!m_canvas.linkHandleAt(hovered, pos).isEmpty()) cursor = Qt::CrossCursor;
        else if (task && !task->milestone && m_canvas.progressBadge(hovered).adjusted(-2, -2, 2, 2).contains(pos)) cursor = Qt::SizeHorCursor;
        else if (task && !task->milestone && m_canvas.onResizeGrip(bar, pos)) cursor = Qt::SizeHorCursor;
        else cursor = Qt::OpenHandCursor;
    }
    viewport()->setCursor(cursor);
}

void GanttChart::mouseReleaseEvent(QMouseEvent *event) {
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    if (event->button() != Qt::LeftButton || gesture.drag == Drag::None) return;
    const std::function<bool(GanttPlan &)> change = m_canvas.gestureEdit(event->position());
    gesture.drag = Drag::None;
    gesture.id.clear();
    viewport()->unsetCursor();
    viewport()->update();
    if (change && edit) edit(change);
}

void GanttChart::wheelEvent(QWheelEvent *event) {
    // Ctrl+wheel: the zoom steps of the window's select, the date under the mouse kept.
    if (event->modifiers() & Qt::ControlModifier) {
        static constexpr int kSteps[] = {50, 75, 100, 125, 150, 200, 300};
        const int delta = event->angleDelta().y();
        const int zoom = zoomPercent();
        int next = zoom;
        if (delta > 0) {
            for (int step : kSteps)
                if (step > zoom) { next = step; break; }
        } else if (delta < 0) {
            for (int step : kSteps)
                if (step < zoom) next = step;
        }
        setZoomPercent(next, event->position().x());
        event->accept();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - wheelPixels(event, 3 * dayWidth()));
        event->accept();
        return;
    }
    event->ignore();  // the view scrolls the table, the chart follows
}

void GanttChart::contextMenuEvent(QContextMenuEvent *event) {
    const QPointF pos = event->pos();
    if (const int d = m_canvas.dependencyAt(pos); d >= 0 && m_canvas.barAt(pos).isEmpty()) {
        QMenu *menu = dependencyMenu(d, this);
        menu->exec(event->globalPos());
        delete menu;
        return;
    }
    if (const int row = m_canvas.rowAt(pos.y()); row >= 0) emit rowMenuRequested(m_canvas.rows().at(row), event->globalPos());
}

bool GanttChart::setDependency(const GanttDependency &dependency) {
    return edit && edit([&](GanttPlan &plan) { return plan.updateDependency(dependency); });
}

QMenu *GanttChart::dependencyMenu(int dependency, QWidget *parent) {
    auto *menu = new QMenu(parent);
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setStyleSheet(menuStyle);
    const GanttDependency link = m_canvas.plan().dependencies().at(dependency);
    for (const auto type : {GanttDependency::Type::FinishStart, GanttDependency::Type::StartStart,
                            GanttDependency::Type::FinishFinish, GanttDependency::Type::StartFinish}) {
        QAction *action = menu->addAction(GanttCanvas::typeName(type));
        action->setCheckable(true);
        action->setChecked(type == link.type);
        connect(action, &QAction::triggered, this, [this, link, type] {
            GanttDependency changed = link;
            changed.type = type;
            setDependency(changed);
        });
    }
    connect(menu->addAction(tr("Lag…")), &QAction::triggered, this, [this, link] {
        ModalDialog dialog(window(), tr("Lag"), tr("Apply"), 320);
        auto *caption = new QLabel(tr("Working days between the two tasks; negative = lead."));
        caption->setWordWrap(true);
        auto *days = new NumberField;  // the app's field with − and +; the unit is in the caption
        days->setRange(-GanttPlan::kMaxLag, GanttPlan::kMaxLag);
        days->setValue(link.lag);
        days->selectAll();
        dialog.contentLayout()->addWidget(caption);
        dialog.contentLayout()->addWidget(days);
        days->setFocus();
        if (dialog.exec() != QDialog::Accepted) return;
        GanttDependency changed = link;
        changed.lag = int(days->value());
        setDependency(changed);
    });
    menu->addSeparator();
    connect(menu->addAction(tr("Delete dependency")), &QAction::triggered, this, [this, link] {
        if (edit) edit([&](GanttPlan &plan) { return plan.removeDependency(link.from, link.to); });
    });
    return menu;
}

bool GanttChart::viewportEvent(QEvent *event) {
    if (event->type() == QEvent::ToolTip) {
        auto *help = static_cast<QHelpEvent *>(event);
        const QString id = m_canvas.barAt(help->pos());
        if (const int d = id.isEmpty() ? m_canvas.dependencyAt(help->pos()) : -1; d >= 0) {
            QToolTip::showText(help->globalPos(), m_canvas.dependencyLabel(d), viewport());
            return true;
        }
        if (const QString tip = m_canvas.taskTip(id); !tip.isEmpty()) QToolTip::showText(help->globalPos(), tip, viewport());
        else QToolTip::hideText();
        return true;
    }
    GanttCanvas::Gesture &gesture = m_canvas.gesture();
    if (event->type() == QEvent::Leave && gesture.drag == Drag::None && !gesture.hovered.isEmpty()) {
        gesture.hovered.clear();
        viewport()->update();
    }
    return QAbstractScrollArea::viewportEvent(event);
}
// ---------------------------------------------------------------------------------------------
// View

GanttView::GanttView(Document *document, ShortcutRegistry *shortcuts, QWidget *parent)
    : QWidget(parent), m_document(document), m_shortcuts(shortcuts), m_tableModel(new GanttTableModel) {
    m_tableModel->setParent(this);
    const auto edit = [this](const std::function<bool(GanttPlan &)> &change) { return editPlan(change); };
    m_tableModel->edit = edit;
    m_tableModel->rename = [this](const QString &id, const QString &name, const QString &html) { return renameTask(id, name, html); };
    m_tableModel->topicOf = [this](const QString &id) -> const MindNode * { return node(id); };

    m_table = new GanttTableView;
    m_table->setModel(m_tableModel);
    auto *delegate = new GanttDelegate(m_table);
    delegate->shortcuts = m_shortcuts;
    delegate->indent = [this] { indentSelected(); };
    delegate->outdent = [this] { outdentSelected(); };
    delegate->nameTyped = [this](const QModelIndex &cell, const QString &name) { previewName(m_tableModel->id(cell), name, false); };
    connect(delegate, &QAbstractItemDelegate::closeEditor, this, [this] { endNamePreview(); });
    m_table->setItemDelegate(delegate);
    m_table->setUniformRowHeights(true);
    m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);  // same height as the chart's
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    m_table->setIndentation(16);
    m_table->setFrameShape(QFrame::NoFrame);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->header()->setFixedHeight(GanttChart::kHeaderHeight);
    m_table->header()->setStretchLastSection(false);
    m_table->header()->setSectionResizeMode(Name, QHeaderView::Stretch);
    for (int column = Start; column <= Person; ++column) m_table->header()->resizeSection(column, GanttColumns::width(fieldOf(column)));
    m_table->installEventFilter(this);
    // FEAT-078: Name, Start, Duration by default; the header's menu shows the others (app setting).
    for (int column = Start; column <= Person; ++column)
        m_table->header()->setSectionHidden(column, !GanttColumns::isShown(fieldOf(column)));
    // FEAT-024: the map's numbers before the name, shown when the map is numbered (refresh).
    m_table->setTreePosition(Name);
    m_table->header()->moveSection(m_table->header()->visualIndex(Number), 0);
    m_table->header()->setSectionResizeMode(Number, QHeaderView::ResizeToContents);
    m_table->header()->setSectionHidden(Number, true);
    // Extras: after Name, shown while some topic has a web link or a note (refresh).
    m_table->header()->moveSection(m_table->header()->visualIndex(Extras), m_table->header()->visualIndex(Name) + 1);
    m_table->header()->setSectionResizeMode(Extras, QHeaderView::Fixed);
    m_table->header()->resizeSection(Extras, kExtrasWidth);
    m_table->header()->setSectionHidden(Extras, true);
    // Column order (user, 2026-10-29): the movable columns after No., Name, Extras, as last dragged.
    int place = m_table->header()->visualIndex(Extras) + 1;
    for (const GanttColumns::Field field : GanttColumns::order())
        if (const int column = columnOf(field); m_table->header()->visualIndex(column) >= place)
            m_table->header()->moveSection(m_table->header()->visualIndex(column), place++);
    m_table->header()->setSectionsMovable(true);
    m_table->header()->setFirstSectionMovable(false);
    connect(m_table->header(), &QHeaderView::sectionMoved, this, &GanttView::columnMoved);
    m_table->viewport()->setMouseTracking(true);
    m_table->viewport()->installEventFilter(this);
    m_numberHidden = !GanttColumns::isShown(GanttColumns::Field::Number);
    m_table->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table->header(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showColumnMenu(m_table->header()->mapToGlobal(pos)); });
    fitColumns();
    connect(m_table->header(), &QHeaderView::sectionResized, this, [this](int column, int, int size) {
        // A movable column's dragged width is an app setting, shared with the phone (user's
        // request 2026-11-23); a hidden section reports 0 and is not a choice.
        if (column >= Start && column <= Person && size > 0) {
            if (size < GanttColumns::kMinWidth) {
                m_table->header()->resizeSection(column, GanttColumns::kMinWidth);  // calls back here
                return;
            }
            GanttColumns::setWidth(fieldOf(column), size);
        }
        if (column != Name) fitColumns();
    });

    m_chart = new GanttChart;
    m_chart->edit = edit;
    m_chart->setCriticalShown(QSettings().value(QStringLiteral("gantt/critical"), true).toBool());
    m_chart->verticalOffset = [this] { return m_table->verticalScrollBar()->value(); };
    m_chart->viewport()->installEventFilter(this);
    m_chart->setFrameShape(QFrame::NoFrame);
    m_chart->viewport()->setAutoFillBackground(false);
    m_table->viewport()->setAutoFillBackground(false);

    // FEAT-078: the view's actions above the columns (user, 2026-10-27); the chart has an empty
    // band of the same height so the rows stay aligned.
    // Table and chart are rounded cards (FEAT-078; user, 2026-10-28): the card paints the
    // background, what is inside is transparent so the corners show.
    auto *tablePane = new QFrame;
    tablePane->setObjectName(QStringLiteral("GanttCard"));
    auto *tableLayout = new QVBoxLayout(tablePane);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->setSpacing(0);
    m_actionsRow = new QWidget;
    m_actionsRow->setObjectName(QStringLiteral("GanttActions"));
    m_actionsRow->setFixedHeight(kActionsHeight);
    auto *actionsLayout = new QHBoxLayout(m_actionsRow);
    actionsLayout->setContentsMargins(8, 4, 8, 4);
    actionsLayout->setSpacing(FloatingUi::kSpacing);
    const auto addAction = [&](const char *name, const char *icon, const std::function<void()> &run) {
        auto *button = new QToolButton;
        button->setObjectName(QLatin1String(name));
        button->setProperty("baseIcon", QIcon(QStringLiteral(":/assets/hugeicons/%1.svg").arg(QLatin1String(icon))));
        FloatingUi::setupIconButton(button);
        connect(button, &QToolButton::clicked, this, run);
        actionsLayout->addWidget(button);
        m_actionButtons.append(button);
    };
    addAction("ganttAddTask", "sibling", [this] { addTask(false); });
    addAction("ganttAddSubtask", "child", [this] { addTask(true); });
    addAction("ganttOutdent", "indent-left", [this] { outdentSelected(); });
    addAction("ganttIndent", "indent-right", [this] { indentSelected(); });
    addAction("ganttChain", "relation", [this] { chainSelected(); });
    addAction("ganttToday", "clock", [this] { m_chart->scrollToToday(); });
    actionsLayout->addStretch();
    tableLayout->addWidget(m_actionsRow);
    tableLayout->addWidget(m_table);
    auto *chartPane = new QFrame;
    chartPane->setObjectName(QStringLiteral("GanttCard"));
    auto *chartLayout = new QVBoxLayout(chartPane);
    chartLayout->setContentsMargins(0, 0, 0, 0);
    chartLayout->setSpacing(0);
    m_legend = new GanttLegend;
    m_legend->setObjectName(QStringLiteral("GanttBaselineLegend"));
    m_legend->setFixedHeight(kActionsHeight);
    chartLayout->addWidget(m_legend);
    chartLayout->addWidget(m_chart);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(GanttColors::kCardGap);  // the page shows between the cards
    splitter->setObjectName(QStringLiteral("GanttSplitter"));
    splitter->addWidget(tablePane);
    splitter->addWidget(chartPane);
    splitter->setSizes({440, 760});
    splitter->setStretchFactor(1, 1);

    m_empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(m_empty);
    emptyLayout->addStretch();
    auto *label = new QLabel(tr("No tasks yet"));
    label->setAlignment(Qt::AlignCenter);
    label->setObjectName(QStringLiteral("GanttEmptyLabel"));
    emptyLayout->addWidget(label);
    emptyLayout->addSpacing(12);
    m_addFirst = new QPushButton(tr("Add task"));
    m_addFirst->setCursor(Qt::PointingHandCursor);
    m_addFirst->setStyleSheet(QStringLiteral(
        "QPushButton{background:#7047eb;color:white;border:0;border-radius:6px;padding:6px 16px;font-size:14px;font-weight:600;}"));
    emptyLayout->addWidget(m_addFirst, 0, Qt::AlignHCenter);
    emptyLayout->addStretch();

    // The stack sits in a box layout: QStackedLayout ignores contents margins, and the window gives
    // the view margins to stay clear of its floating bars.
    // FEAT-080: the panel stays beside an empty plan too (its Gantt page, "No task selected").
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(GanttColors::kCardGap);  // the panel is the third card
    auto *pages = new QWidget;
    m_stack = new QStackedLayout(pages);
    // FEAT-078: the task panel at the left of the table, opened by the top bar's button (FEAT-080).
    m_panel = new GanttTaskPanel;
    m_panel->hide();
    m_panel->edit = edit;
    m_panel->rename = [this](const QString &id, const QString &name, const QString &html) { return renameTask(id, name, html); };
    m_panel->topicOf = [this](const QString &id) -> const MindNode * { return node(id); };
    m_panel->nameField()->shortcuts = m_shortcuts;  // FEAT-124: the format keys
    m_panel->nameTyped = [this](const QString &id, const QString &name) { previewName(id, name, true); };
    connect(m_panel->nameField(), &GanttNameField::editingFinished, this, [this] { endNamePreview(); });
    m_panel->setInfo = [this](const QStringList &ids, std::optional<int> priority) {
        QList<MindNode *> topics;
        for (const QString &id : ids) topics.append(node(id));
        return m_document->setTaskInfo(topics, priority);
    };
    m_panel->setPeople = [this](const QStringList &ids, const QStringList &assign, const QStringList &unassign) {
        QList<MindNode *> topics;
        for (const QString &id : ids) topics.append(node(id));
        return m_document->setTaskInfo(topics, std::nullopt, assign, unassign);
    };
    m_panel->setPercent = [this](const QStringList &ids, const QString &person, int percent) {
        QList<MindNode *> topics;
        for (const QString &id : ids) topics.append(node(id));
        return m_document->setPeoplePercent(topics, person, percent);
    };
    m_panel->openResources = [this] { emit resourcesRequested(); };
    m_panel->setLabel = [this](const QStringList &ids, bool shown) { return setNameOnBar(shown, ids); };
    m_panel->setMilestone = [this](const QStringList &ids, bool milestone) {
        QList<MindNode *> topics;
        for (const QString &id : ids) topics.append(node(id));
        if (!GanttCommands::setMilestone(*m_document, topics, milestone)) return false;
        finishMapChange();  // the topics grow by their checkbox: the map is laid out again, one step
        return true;
    };
    m_panel->setBarColor = [this](const QStringList &ids, const std::optional<QColor> &color) {
        QList<MindNode *> topics;
        for (const QString &id : ids)
            if (MindNode *topic = node(id)) topics.append(topic);
        MindMapModel *model = m_document->model();
        if (!(color ? model->setStyle(topics, StyleProperty::BranchColor, *color) : model->clearStyle(topics, StyleProperty::BranchColor)))
            return false;
        m_document->markChanged();
        finishMapChange();
        return true;
    };
    m_panel->remove = [this](const QStringList &ids) { return removeTasks(ids); };  // the panel shows no task
    m_panel->setBarShape = [this](GanttPlan::BarShape shape) {
        return editPlan([shape](GanttPlan &plan) { return plan.setBarShape(shape); });
    };
    m_panel->setSummaryShape = [this](GanttPlan::SummaryShape shape) {
        return editPlan([shape](GanttPlan &plan) { return plan.setSummaryShape(shape); });
    };
    m_panel->setTaskStyle = [this](const QStringList &ids, GanttPlan::OwnStyle part, const QString &key) {  // one step for them all
        return editPlan([ids, part, key](GanttPlan &plan) { return GanttTaskEdits::setOwnStyle(plan, ids, part, key); });
    };
    m_panel->setTaskBorderColor = [this](const QStringList &ids, const std::optional<QColor> &color) {
        return editPlan([ids, color](GanttPlan &plan) { return GanttTaskEdits::setBorderColor(plan, ids, color.value_or(QColor())); });
    };
    m_panel->setLinkLine = [this](GanttPlan::LinkLine line) {
        return editPlan([line](GanttPlan &plan) { return plan.setLinkLine(line); });
    };
    m_panel->setLinkEnd = [this](BranchEnd end) {
        return editPlan([end](GanttPlan &plan) { return plan.setLinkEnd(end); });
    };
    m_panel->setLinkWidth = [this](GanttPlan::LinkWidth width) {
        return editPlan([width](GanttPlan &plan) { return plan.setLinkWidth(width); });
    };
    m_panel->setLinkColor = [this](const std::optional<QColor> &color) {
        return editPlan([color](GanttPlan &plan) { return plan.setLinkColor(color.value_or(QColor())); });
    };
    m_panel->setLinkEndSize = [this](GanttPlan::LinkEndSize size) {
        return editPlan([size](GanttPlan &plan) { return plan.setLinkEndSize(size); });
    };
    m_panel->setMilestoneShape = [this](GanttPlan::MilestoneShape shape) {
        return editPlan([shape](GanttPlan &plan) { return plan.setMilestoneShape(shape); });
    };
    m_panel->setBorderStyle = [this](GanttPlan::BarKind kind, GanttPlan::BorderStyle style) {
        return editPlan([kind, style](GanttPlan &plan) { return plan.setBorderStyle(kind, style); });
    };
    m_panel->setFillStyle = [this](GanttPlan::BarKind kind, NodeFillStyle style) {
        return editPlan([kind, style](GanttPlan &plan) { return plan.setFillStyle(kind, style); });
    };
    m_panel->setDefaultColor = [this](GanttPlan::BarKind kind, const std::optional<QColor> &color) {
        return editPlan([kind, color](GanttPlan &plan) { return plan.setDefaultColor(kind, color.value_or(QColor())); });
    };
    m_panel->setDefaultBorderColor = [this](GanttPlan::BarKind kind, const std::optional<QColor> &color) {
        return editPlan([kind, color](GanttPlan &plan) { return plan.setDefaultBorderColor(kind, color.value_or(QColor())); });
    };
    const auto themeColors = [this] { return m_document->model()->activePalette; };
    m_panel->colorField()->themeColors = themeColors;
    m_panel->ownBorderColorField()->themeColors = themeColors;
    m_panel->linkColorField()->themeColors = themeColors;
    for (int kind = 0; kind < GanttPlan::kBarKinds; ++kind) {
        m_panel->defaultColorField(GanttPlan::BarKind(kind))->themeColors = themeColors;
        m_panel->defaultBorderColorField(GanttPlan::BarKind(kind))->themeColors = themeColors;
    }
    connect(m_panel, &GanttTaskPanel::closeRequested, this, &GanttView::closePanel);
    outer->addWidget(m_panel);
    outer->addWidget(pages, 1);
    // The panel's right edge is dragged (user's request 2026-11-22): the gap to the table and 4 px inside.
    auto *panelHandle = new PanelResizeHandle(m_panel, Qt::RightEdge, 4, GanttColors::kCardGap);
    panelHandle->resizeTo = [this](int wanted) {
        m_panel->setPanelWidth(panelWidthFor(wanted));
        return m_panel->width();
    };
    panelHandle->finished = [this](int width) {
        m_panelDraggedWidth = width;
        QSettings().setValue(kPanelWidthSetting, width);
    };
    m_panelDraggedWidth = QSettings().value(kPanelWidthSetting, GanttTaskPanel::kWidth).toInt();
    applyPanelWidth();
    m_stack->addWidget(splitter);
    m_stack->addWidget(m_empty);

    applyColors();
    connect(m_addFirst, &QPushButton::clicked, this, [this] { addTask(); });
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, m_chart->viewport(), qOverload<>(&QWidget::update));
    connect(m_table->verticalScrollBar(), &QScrollBar::rangeChanged, m_chart->viewport(), qOverload<>(&QWidget::update));
    connect(m_table, &QTreeView::collapsed, this, [this](const QModelIndex &index) {
        m_collapsed.insert(m_tableModel->id(index));
        updateChart();
    });
    connect(m_table, &QTreeView::expanded, this, [this](const QModelIndex &index) {
        m_collapsed.remove(m_tableModel->id(index));
        updateChart();
    });
    // Order in which the tasks were selected: "Link in a chain" follows it.
    connect(m_table->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            [this](const QItemSelection &selected, const QItemSelection &deselected) {
        if (m_refreshing) return;  // refresh() puts back the order and redraws once at its end
        for (const QModelIndex &index : deselected.indexes())
            if (index.column() == 0) m_selectionOrder.removeAll(m_tableModel->id(index));
        QSet<QString> added;
        for (const QModelIndex &index : selected.indexes())
            if (index.column() == 0 && !m_selectionOrder.contains(m_tableModel->id(index))) added.insert(m_tableModel->id(index));
        // Rows selected together (Shift) join top to bottom, as clicked one by one (user, 2026-10-29).
        for (QModelIndex index = m_tableModel->index(0, 0); index.isValid() && !added.isEmpty(); index = m_table->indexBelow(index))
            if (added.remove(m_tableModel->id(index))) m_selectionOrder.append(m_tableModel->id(index));
        m_selectionOrder.append(QStringList(added.cbegin(), added.cend()));  // rows hidden in a collapsed summary
        updateChart();
        updateActions();
        if (!m_panel->isHidden()) loadPanel();  // the tasks its edits apply to
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this, [this] {
        if (!m_refreshing) updateActions();
    });
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        const QModelIndex index = m_table->indexAt(pos);
        if (!index.isValid()) return;
        if (!m_table->selectionModel()->isRowSelected(index.row(), index.parent())) selectTask(m_tableModel->id(index));
        showRowMenu(m_table->viewport()->mapToGlobal(pos));
    });
    connect(m_chart, &GanttChart::taskClicked, this, [this](const QString &id, Qt::KeyboardModifiers modifiers) {
        m_table->setFocus();
        // Shift+click selects the rows from the last row clicked without Shift, as in the table;
        // Ctrl+Shift adds them to the selection.
        if (modifiers & Qt::ShiftModifier) {
            selectRange(m_rangeAnchor.isEmpty() ? currentId() : m_rangeAnchor, id, modifiers & Qt::ControlModifier);
            return;
        }
        m_rangeAnchor = id;
        const QModelIndex index = m_tableModel->indexOf(id);
        const auto flags = modifiers & Qt::ControlModifier ? QItemSelectionModel::Toggle : QItemSelectionModel::ClearAndSelect;
        m_table->selectionModel()->setCurrentIndex(index, flags | QItemSelectionModel::Rows);
        if (!(modifiers & Qt::ControlModifier)) showInPanel(id);
    });
    m_table->setAcceptDrops(true);
    static_cast<GanttTableView *>(m_table)->moveTask = [this](const QString &id, const QString &target, DropPlace place) {
        return moveTask(id, target, place);
    };
    // Up/Down in the chart or the panel move in the table, so the open panel follows (user, 2026-10-29).
    const auto step = [this](int key, Qt::KeyboardModifiers modifiers) {
        m_table->setFocus();  // a field being edited in the panel is confirmed on focus loss
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        QCoreApplication::sendEvent(m_table, &press);
    };
    connect(m_chart, &GanttChart::stepRequested, this, step);
    connect(m_panel, &GanttTaskPanel::stepRequested, this, step);
    // The arrow keys move the open panel to the new current row, as a plain click.
    static_cast<GanttTableView *>(m_table)->movedByKey = [this](const QModelIndex &index) {
        m_rangeAnchor = m_tableModel->id(index);
        if (!m_panel->isHidden()) openPanel(m_rangeAnchor);  // the page shown stays
    };
    // A plain click on a row shows it in the open panel; Ctrl/Shift+click only selects (the table
    // selects the range itself).
    connect(m_table, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        const Qt::KeyboardModifiers modifiers = QApplication::keyboardModifiers();
        if (!(modifiers & Qt::ShiftModifier)) m_rangeAnchor = m_tableModel->id(index);
        if (!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier))) showInPanel(m_tableModel->id(index));
    });
    connect(m_chart, &GanttChart::rowMenuRequested, this, [this](const QString &id, const QPoint &globalPos) {
        if (!selectedIds().contains(id)) selectTask(id);
        showRowMenu(globalPos);
    });
    // FEAT-081: names in the People column and the panel follow the Resources list.
    connect(document, &Document::resourcesChanged, this, [this] {
        m_stale = true;
        if (!isVisible()) return;
        if (editing(m_table)) refreshLater();
        else refresh();
    });
    connect(document->model(), &MindMapModel::ganttChanged, this, [this] {
        m_stale = true;
        if (!isVisible()) return;
        // A value committed from an open editor: the table is rebuilt once the editor has closed,
        // so Qt gives the focus back to the table (a rebuild now would drop the focused editor).
        if (editing(m_table)) refreshLater();
        else refresh();
    });
    // The rows are the map's topics: any change of the map (also from the map, the outliner or
    // Undo) rebuilds the view once, after the change is complete.
    MindMapModel *model = document->model();
    for (const auto signal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(model, signal, this, &GanttView::refreshLater);
    connect(model, &QAbstractItemModel::rowsMoved, this, &GanttView::refreshLater);
    connect(model, &QAbstractItemModel::modelReset, this, &GanttView::refreshLater);
    connect(model, &QAbstractItemModel::dataChanged, this, &GanttView::refreshLater);
    connect(model, &MindMapModel::stylesChanged, this, &GanttView::refreshLater);  // branch colours
    connect(document, &Document::nodesChanged, this, &GanttView::refreshLater);
    connect(document, &Document::restored, this, &GanttView::refreshLater);
}

GanttColors GanttColors::light() { return {GanttPalette::light()}; }

// The outliner's dark page (MainWindow darkOutlineColors): the app's dark tokens, lines in white.
GanttColors GanttColors::dark() {
    using UiTheme::Token;
    return {{UiTheme::color(Token::SurfaceSunken), UiTheme::color(Token::Text), UiTheme::color(Token::TextMuted),
            QColor(255, 255, 255, 18), QColor(255, 255, 255, 34), QColor(255, 255, 255, 8),
            QColor(255, 255, 255, 22), QColor(Qt::white), UiTheme::color(Token::Surface), UiTheme::color(Token::Surface),
            QColor(255, 255, 255, 18)}};
}

void GanttView::setCriticalShown(bool shown) {
    m_chart->setCriticalShown(shown);
    QSettings().setValue(QStringLiteral("gantt/critical"), shown);
}

DatePicker::Colors GanttColors::picker() const {
    return {card, text, muted, line, selected, UiTheme::color(UiTheme::Token::Accent)};
}

void GanttView::setDark(bool dark) {
    const GanttColors colors = dark ? GanttColors::dark() : GanttColors::light();
    if (colors.page == m_colors.page) return;
    m_colors = colors;
    applyColors();
}

void GanttView::applyColors() {
    const GanttColors &c = m_colors;
    // The table's selection is opaque (the selection over the page): a translucent one lets the
    // style's own highlight show through in the branch column.
    const auto opaque = [&c](const QColor &over) {
        const double a = over.alphaF();
        const auto mix = [a](int top, int under) { return int(top * a + under * (1 - a)); };
        return QColor(mix(over.red(), c.card.red()), mix(over.green(), c.card.green()), mix(over.blue(), c.card.blue()));
    };
    const QColor selected = opaque(c.selected);
    // Thin scroll bars on the cards, as the window's (transparent track, rounded handle).
    const QString scrollBars = QStringLiteral(
        "QScrollBar:horizontal{background:transparent;height:10px;margin:0 %2px 2px %2px;}"
        "QScrollBar::handle:horizontal{background:%1;border-radius:4px;min-width:32px;margin:2px 0;}"
        "QScrollBar::add-line,QScrollBar::sub-line{width:0;height:0;}"
        "QScrollBar::add-page,QScrollBar::sub-page{background:transparent;}")
        .arg(rgba(c.line)).arg(GanttColors::kCardRadius);
    m_table->setStyleSheet(QStringLiteral(
        "QTreeView{background:transparent;color:%1;font-size:13px;outline:0;}"
        "QTreeView::item{border-bottom:1px solid %2;}"
        "QTreeView::item:selected{background:%3;color:%1;}"
        "QHeaderView{background:transparent;}"
        // The editors (date, number, name) as the task panel's fields (DateField paints the chevron,
        // NumberField its − and +).
        "QDateEdit,QLineEdit,GanttNameField{color:%1;background:%6;border:1px solid %7;border-radius:4px;padding:0 4px;font-size:12px;"
        "selection-background-color:%3;selection-color:%1;}"
        "QDateEdit::drop-down{border:0;width:24px;}QDateEdit::down-arrow{image:none;width:0;height:0;}"
        "QHeaderView::section{background:transparent;color:%4;font-size:12px;border:0;border-bottom:1px solid %5;padding-left:8px;}")
        .arg(c.text.name(), rgba(c.grid), selected.name(), c.muted.name(), rgba(c.line), c.card.name(),
             UiTheme::color(UiTheme::Token::Accent).name()) + scrollBars);
    m_chart->setStyleSheet(scrollBars);
    QPalette palette = m_table->palette();  // branch arrows of the summaries
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Text, c.text);
    m_table->setPalette(palette);
    static_cast<GanttTableView *>(m_table)->rowLine = c.grid;
    static_cast<GanttTableView *>(m_table)->selectedRow = selected;
    m_tableModel->text = c.text;
    auto *delegate = static_cast<GanttDelegate *>(m_table->itemDelegate());
    delegate->pickerColors = c.picker();
    delegate->webLinkIcon = tintedIcon(QIcon(QStringLiteral(":/assets/hugeicons/link-web.svg")), c.text, c.muted);
    delegate->noteIcon = tintedIcon(QIcon(QStringLiteral(":/assets/hugeicons/note.svg")), c.text, c.muted);
    m_tableModel->muted = c.muted;
    m_empty->setStyleSheet(QStringLiteral("QWidget{background:%1;} QLabel#GanttEmptyLabel{color:%2;font-size:14px;}")
                               .arg(c.page.name(), c.muted.name()));
    if (auto *splitter = findChild<QSplitter *>(QStringLiteral("GanttSplitter")))
        splitter->setStyleSheet(QStringLiteral("QSplitter::handle{background:transparent;}"
                                               "QFrame#GanttCard{background:%1;border-radius:%2px;}")
                                    .arg(c.card.name()).arg(GanttColors::kCardRadius));
    m_chart->setColors(c);
    m_panel->setColors(c);
    m_table->viewport()->update();
    m_actionsRow->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:0;border-radius:6px;padding:0;}"
        "QToolButton:hover{background:%1;}QToolButton:pressed{background:%2;}")
        .arg(rgba(c.selected), rgba(c.line)));
    QColor disabled = c.muted;
    disabled.setAlphaF(0.4);
    for (QToolButton *button : std::as_const(m_actionButtons))
        button->setIcon(tintedIcon(button->property("baseIcon").value<QIcon>(), c.text, disabled));
    // The view's margins (below the top bars, around the table and chart) are the page too: the
    // window's page stack behind it is white (user's photo, 2026-10-27).
    QPalette own = QWidget::palette();
    own.setColor(QPalette::Window, c.page);
    setPalette(own);
    setAutoFillBackground(true);
}

void GanttView::setMenuStyle(const QString &styleSheet) {
    m_menuStyle = styleSheet;
    m_chart->menuStyle = styleSheet;
    m_panel->setProperty("menuStyle", styleSheet);
}

bool GanttView::setNameOnBar(bool shown, const QStringList &ids) {
    QList<MindNode *> topics;
    for (const QString &id : ids.isEmpty() ? selectedIds() : ids) topics.append(node(id));
    return GanttCommands::setNameOnBar(*m_document, topics, shown);
}

void GanttView::selectRange(const QString &from, const QString &to, bool add) {
    const QModelIndex last = m_tableModel->indexOf(to);
    if (!last.isValid()) return;
    // The rows shown, top to bottom (collapsed children left out).
    QStringList shown;
    for (QModelIndex index = m_tableModel->index(0, 0); index.isValid(); index = m_table->indexBelow(index)) shown.append(m_tableModel->id(index));
    QItemSelection selection;
    for (const QString &id : GanttCommands::range(shown, from, to, m_tableModel->indexOf(from).isValid()))
        selection.select(m_tableModel->indexOf(id), m_tableModel->indexOf(id, ColumnCount - 1));
    const auto flags = (add ? QItemSelectionModel::Select : QItemSelectionModel::ClearAndSelect) | QItemSelectionModel::Rows;
    m_table->selectionModel()->select(selection, flags);
    m_table->selectionModel()->setCurrentIndex(last, QItemSelectionModel::NoUpdate);
}

void GanttView::openPanel(const QString &id) {
    const QString shown = m_tableModel->plan().task(id) ? id : QString();
    if (!m_panel->isHidden() && m_panel->taskId() != shown) m_panel->commitPendingEdit();
    m_panelId = shown;
    const bool opened = !!m_panel->isHidden();
    m_panel->show();
    loadPanel();
    if (opened) emit panelShownChanged(true);
}

void GanttView::togglePanel() {
    if (!m_panel->isHidden()) closePanel();
    else openPanel(selectedIds().contains(currentId()) ? currentId() : QString());  // a current row not selected: none
}

void GanttView::showInPanel(const QString &id) {
    if (!!m_panel->isHidden()) return;
    m_panel->showPage(GanttTaskPanel::Page::Task);
    openPanel(id);
}

void GanttView::closePanel() {
    const bool closing = !m_panel->isHidden();
    m_panel->commitPendingEdit();
    m_panel->hide();
    m_panelId.clear();
    focusContent();  // the table, or "Add task" beside an empty plan
    if (closing) emit panelShownChanged(false);
}

void GanttView::previewName(const QString &id, const QString &name, bool fromPanel) {
    if (id.isEmpty()) return;
    m_namePreviewId = id;
    m_chart->setNamePreview(id, name);
    m_tableModel->setNamePreview(id, name);
    if (!fromPanel) m_panel->showTypedName(id, name);
}

void GanttView::endNamePreview() {
    if (m_namePreviewId.isEmpty()) return;
    m_namePreviewId.clear();
    m_chart->setNamePreview({}, {});
    m_tableModel->setNamePreview({}, {});
    m_panel->reloadName();  // the name kept, or the old one after Esc
}

void GanttView::loadPanel() {
    m_panel->showBarShape(m_tableModel->plan().barShape());
    m_panel->showSummaryShape(m_tableModel->plan().summaryShape());
    m_panel->showLinkStyle(m_tableModel->plan());
    m_panel->showMilestoneShape(m_tableModel->plan().milestoneShape());
    for (int i = 0; i < GanttPlan::kBarKinds; ++i) {
        const auto kind = GanttPlan::BarKind(i);
        m_panel->showFillStyle(kind, m_tableModel->plan().fillStyle(kind));
        m_panel->showBorderStyle(kind, m_tableModel->plan().borderStyle(kind));
    }
    for (int i = 0; i < GanttPlan::kBarKinds; ++i) {
        const auto kind = GanttPlan::BarKind(i);
        m_panel->showDefaultColor(kind, GanttCanvas::defaultBarColor(m_tableModel->plan(), kind), m_tableModel->plan().defaultColor(kind).isValid());
        m_panel->showDefaultBorderColor(kind, GanttCanvas::defaultOutlineColor(m_tableModel->plan(), kind),
                                        m_tableModel->plan().defaultBorderColor(kind).isValid());
    }
    if (m_panelId.isEmpty()) {
        m_panel->clear();
        return;
    }
    const GanttPlan &plan = m_tableModel->plan();
    if (!plan.task(m_panelId)) {  // the task was deleted: the panel stays open without it
        m_panelId.clear();
        m_panel->clear();
        return;
    }
    // With several tasks selected, the panel's among them, its edits apply to all of them.
    QStringList targets{m_panelId};
    if (const QStringList selected = selectedIds(); selected.contains(m_panelId))
        for (const QString &id : selected)
            if (id != m_panelId && plan.task(id)) targets.append(id);
    const auto info = m_tableModel->info.value(m_panelId);
    int priority = info.priority;
    QHash<QString, QStringList> people;
    QHash<QString, QHash<QString, int>> percents;  // FEAT-083
    for (const QString &id : std::as_const(targets)) {
        const auto other = m_tableModel->info.value(id);
        if (other.priority != info.priority) priority = -1;
        people.insert(id, other.people);
        if (const MindNode *topic = node(id); topic && !topic->ganttPercent.isEmpty()) percents.insert(id, topic->ganttPercent);
    }
    m_panel->setResources(m_document->resources().people);
    m_panel->setPercents(percents);
    m_panel->load(m_panelId, plan, m_tableModel->schedule(), priority, people, m_chart->showsName(m_panelId), targets);
    // The colour the bars show: chosen by hand, or the sheet's default (as the chart).
    // The border's the same way: the task's own, the sheet's or the automatic one (GanttCanvas::outlineColor).
    QColor color, borderColor;
    bool manual = false;
    for (int i = 0; i < targets.size(); ++i) {
        const MindNode *topic = node(targets.at(i));
        const bool own = topic && NodeStyle::hasManualValue(topic, StyleProperty::BranchColor);
        const QColor bar = own ? topic->branchColor : GanttCanvas::defaultBarColor(plan, plan.barKind(targets.at(i)));
        const QColor outline = GanttCanvas::outlineColor(plan, targets.at(i), bar, bar);
        manual = manual || own;
        if (i == 0) {
            color = bar;
            borderColor = outline;
        }
        if (bar != color) color = QColor();
        if (outline != borderColor) borderColor = QColor();
    }
    m_panel->showBarColor(color, manual);
    m_panel->showBorderColor(borderColor, GanttTaskEdits::ownStyle(plan, targets).ownBorderColor);
}

void GanttView::fitColumns() {
    // The splitter stops where every shown column header is whole (user's photo), also after a
    // column is widened.
    int width = kNameMinWidth;
    for (int column = Start; column < ColumnCount; ++column)
        if (!m_table->header()->isSectionHidden(column)) width += m_table->header()->sectionSize(column);
    m_table->setMinimumWidth(width + 2 * m_table->frameWidth());
}

void GanttView::columnMoved() {
    if (m_movingColumns) return;  // the moves below are not the user's (the table still follows them)
    QHeaderView *header = m_table->header();
    m_movingColumns = true;
    int place = 0;
    for (int fixed : {int(Number), int(Name), int(Extras)}) {
        if (header->visualIndex(fixed) != place) header->moveSection(header->visualIndex(fixed), place);
        ++place;
    }
    m_movingColumns = false;
    QVector<GanttColumns::Field> order;
    for (int visual = place; visual < header->count(); ++visual)
        if (const int column = header->logicalIndex(visual); column >= Start && column <= Person) order.append(fieldOf(column));
    GanttColumns::setOrder(order);
}

void GanttView::setColumnShown(int column, bool shown) {
    if (column == Number) {
        if (!m_numbered || m_table->header()->isSectionHidden(Number) != shown) return;
        m_numberHidden = !shown;
        m_table->header()->setSectionHidden(Number, !shown);
        GanttColumns::setShown(GanttColumns::Field::Number, shown);
        fitColumns();
        return;
    }
    if (column <= Name || column > Person || m_table->header()->isSectionHidden(column) != shown) return;
    m_table->header()->setSectionHidden(column, !shown);
    fitColumns();
    GanttColumns::setShown(fieldOf(column), shown);
}

void GanttView::showColumnMenu(const QPoint &globalPos) {
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(m_menuStyle);
    for (int column : {int(Number), int(Start), int(End), int(Duration), int(Progress), int(Priority), int(Person)}) {
        QAction *action = menu.addAction(m_tableModel->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
        action->setCheckable(true);
        action->setChecked(!m_table->header()->isSectionHidden(column));
        action->setEnabled(column != Number || m_numbered);  // the number exists only on a numbered map
        connect(action, &QAction::toggled, this, [this, column](bool on) { setColumnShown(column, on); });
    }
    menu.exec(globalPos);
}

void GanttView::updateActions() {
    const auto button = [this](const char *name) { return m_actionsRow->findChild<QToolButton *>(QLatin1String(name)); };
    const MindNode *topic = node(currentId());
    const bool hasPrevious = topic && topic->parent && !topic->parent->children.empty() && topic->parent->children.front().get() != topic;
    button("ganttAddSubtask")->setEnabled(topic);
    button("ganttIndent")->setEnabled(hasPrevious);
    button("ganttOutdent")->setEnabled(topic && topic->parent && topic->parent->parent);  // a main topic stays one
    const bool unlinks = chainUnlinks(m_tableModel->plan());
    button("ganttChain")->setEnabled(unlinks || chainIds(m_tableModel->plan()).size() > 1);
    // Tooltips with the current keys (the user may change them in the settings).
    const auto tip = [this](const QString &text, const char *shortcut) {
        const QList<QKeySequence> keys = m_shortcuts && shortcut ? m_shortcuts->shortcuts(QLatin1String(shortcut)) : QList<QKeySequence>();
        return keys.isEmpty() ? text : tr("%1 (%2)").arg(text, keys.first().toString(QKeySequence::NativeText));
    };
    button("ganttAddTask")->setToolTip(tip(tr("Add task"), "ganttAddTask"));
    button("ganttAddSubtask")->setToolTip(tip(tr("Add subtask"), nullptr));
    button("ganttOutdent")->setToolTip(tip(tr("Level up"), "ganttOutdent"));
    button("ganttIndent")->setToolTip(tip(tr("Level down"), "ganttIndent"));
    button("ganttChain")->setToolTip(tip(unlinks ? tr("Remove links") : tr("Link in a chain"), "ganttChain"));
    button("ganttToday")->setToolTip(tr("Go to today"));
}

bool GanttView::editPlan(const std::function<bool(GanttPlan &)> &edit) {
    return m_document->editGantt(edit);
}

void GanttView::refreshLater() {
    m_stale = true;
    if (!isVisible() || m_refreshQueued) return;
    m_refreshQueued = true;
    QMetaObject::invokeMethod(this, [this] {
        m_refreshQueued = false;
        if (m_stale && isVisible()) refresh();
    }, Qt::QueuedConnection);
}

bool GanttView::showsEmptyState() const { return m_stack->currentWidget() == m_empty; }

void GanttView::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // Another sheet: its plan now, so it opens on its own day (switching sheets does not mark the view stale).
    if (m_stale || openedSheetKey() != m_openedSheet) refresh();
}

void GanttView::refresh() {
    m_stale = false;
    const QStringList selected = selectedIds();
    const QStringList order = m_selectionOrder;
    const QString current = currentId();
    const int scroll = m_table->verticalScrollBar()->value();
    const QWidget *focus = QApplication::focusWidget();
    const bool hadFocus = focus && (focus == m_table || m_table->isAncestorOf(focus));
    const GanttPlan plan = m_document->model()->gantt();
    m_tableModel->info.clear();
    bool numbered = false, extras = false;
    // The topics by id in one walk: nodeById searches the tree, once per task it cost O(n^2)
    // (1,000 tasks: about 6 ms of a 50 ms refresh).
    QHash<QString, const MindNode *> topics;
    topics.reserve(plan.tasks().size());
    std::function<void(const MindNode *)> collect = [&](const MindNode *topic) {
        topics.insert(topic->id, topic);
        for (const auto &child : topic->children) collect(child.get());
    };
    for (const auto &child : m_document->model()->root()->children) collect(child.get());
    const QVector<GanttResource> &resources = m_document->resources().people;
    for (const GanttTask &task : plan.tasks()) {
        const MindNode *topic = topics.value(task.id);
        if (!topic) continue;
        const int flags = (topic->webLink.isEmpty() ? 0 : WebLinkExtra) | (topic->notes.trimmed().isEmpty() ? 0 : NoteExtra);
        QStringList people, names;  // FEAT-081: in the list's order; ids of removed people left out
        if (!topic->ganttPeople.isEmpty())
            for (const GanttResource &person : resources)
                if (topic->ganttPeople.contains(person.id)) {
                    people.append(person.id);
                    names.append(person.displayName().isEmpty() ? person.email : person.displayName());
                }
        if (topic->ganttPriority || !people.isEmpty() || !topic->number.isEmpty() || flags) {
            m_tableModel->info.insert(task.id, {topic->ganttPriority, people, names, topic->number, flags});
            numbered = numbered || !topic->number.isEmpty();
            extras = extras || flags;
        }
    }
    const GanttSchedule schedule = plan.schedule();
    // FEAT-082: the compared snapshot, dropped when gone (removed, Undo of its creation) or on
    // another sheet or file.
    const GanttBaseline *baseline = m_baselineId.isEmpty() ? nullptr : plan.baseline(m_baselineId);
    if (!m_baselineId.isEmpty() && (!baseline || openedSheetKey() != m_baselineSheet)) {
        baseline = nullptr;
        setComparedId({});
    }
    m_tableModel->setBaseline(baseline, baseline ? plan.compare(schedule, *baseline) : GanttBaselineComparison());
    m_tableModel->setPlan(plan, schedule);
    if (m_table->header()->isSectionHidden(Extras) == extras) {
        m_table->header()->setSectionHidden(Extras, !extras);
        fitColumns();
    }
    m_numbered = numbered;  // a numbered map shows the column unless the user hid it
    if (const bool show = numbered && !m_numberHidden; m_table->header()->isSectionHidden(Number) == show) {
        m_table->header()->setSectionHidden(Number, !show);
        fitColumns();
    }
    m_collapsed.removeIf([&plan](const QString &id) { return !plan.hasChildren(id); });
    m_refreshing = true;
    {
        const QSignalBlocker blocker(m_table);  // expanded/collapsed would rebuild the chart each time
        m_table->expandAll();
        for (const QString &id : std::as_const(m_collapsed)) m_table->collapse(m_tableModel->indexOf(id));
    }
    QItemSelection selection;
    for (const QString &id : selected)
        if (const QModelIndex index = m_tableModel->indexOf(id); index.isValid())
            selection.select(index, m_tableModel->indexOf(id, ColumnCount - 1));
    m_table->selectionModel()->select(selection, QItemSelectionModel::ClearAndSelect);
    m_selectionOrder = order;
    m_selectionOrder.removeIf([&plan](const QString &id) { return !plan.task(id); });
    if (const QModelIndex index = m_tableModel->indexOf(current); index.isValid())
        m_table->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    m_refreshing = false;
    // The numbers' column fits its contents only now, with the rows filled and expanded; Qt 6.8 did
    // not always say so with sectionResized, and the splitter's stop was a few pixels short (CI).
    if (!m_table->header()->isSectionHidden(Number)) fitColumns();
    m_table->verticalScrollBar()->setValue(scroll);
    m_stack->setCurrentWidget(plan.tasks().isEmpty() ? m_empty : m_stack->widget(0));
    updateChart();
    updateActions();
    loadPanel();
    updateBaselineMenu();
    if (hadFocus) focusContent();
}

void GanttView::setComparedId(const QString &id) {
    if (id == m_baselineId) return;
    m_baselineId = id;
    m_baselineSheet = id.isEmpty() ? QString() : openedSheetKey();
    emit baselineCompared(!id.isEmpty());
}

QString GanttView::addBaseline(const QString &name) {
    QString id;
    if (!editPlan([&](GanttPlan &plan) {
            id = plan.addBaseline(name, QDateTime::currentDateTime());
            return !id.isEmpty();
        }))
        return {};
    refresh();
    return id;
}

bool GanttView::renameBaseline(const QString &id, const QString &name) {
    const bool renamed = editPlan([&](GanttPlan &plan) { return plan.renameBaseline(id, name); });
    refresh();  // also when refused: the menu shows the old name again
    return renamed;
}

bool GanttView::removeBaseline(const QString &id) {
    if (!editPlan([&](GanttPlan &plan) { return plan.removeBaseline(id); })) return false;
    refresh();  // the comparison stops with it
    return true;
}

void GanttView::compareBaseline(const QString &id) {
    if (!id.isEmpty() && !m_tableModel->plan().baseline(id)) return;
    setComparedId(id);
    refresh();
}

QString GanttView::nextBaselineName() const { return GanttCommands::nextBaselineName(m_tableModel->plan()); }

void GanttView::showBaselineMenu(QWidget *anchor) {
    if (!m_baselineMenu) {
        m_baselineMenu = new GanttBaselineMenu(this);
        connect(m_baselineMenu, &GanttBaselineMenu::createRequested, this, [this](const QString &name) { addBaseline(name); });
        connect(m_baselineMenu, &GanttBaselineMenu::compareRequested, this, &GanttView::compareBaseline);
        connect(m_baselineMenu, &GanttBaselineMenu::renameRequested, this, [this](const QString &id, const QString &name) { renameBaseline(id, name); });
        connect(m_baselineMenu, &GanttBaselineMenu::removeRequested, this, [this](const QString &id) { removeBaseline(id); });
    }
    if (m_stale) refresh();
    updateBaselineMenu();
    m_baselineMenu->popupBelow(anchor);
}

GanttExportSource GanttView::exportSource() {
    if (m_stale) refresh();
    // The shown columns, in the header's order; the Extras icons are left out.
    using Field = GanttExportSource::Field;
    QVector<Field> fields;
    QHeaderView *header = m_table->header();
    for (int visual = 0; visual < header->count(); ++visual) {
        const int column = header->logicalIndex(visual);
        if (header->isSectionHidden(column) || column == Extras) continue;
        fields.append(fieldOf(column));
    }
    GanttExportSource source = GanttExportSource::fromDocument(*m_document, fields, m_tableModel->baseline ? m_tableModel->baseline->id : QString(),
                                                               m_chart->criticalShown(), m_chart->scale());
    source.chartFont = UiTheme::documentFont();
    source.headerFont = UiTheme::interfaceFont();  // as the view's time header
    source.headerFont.setPixelSize(UiTheme::fontPx(12));
    source.accent = UiTheme::color(UiTheme::Token::Accent);
    return source;
}

void GanttView::updateBaselineMenu() {
    if (!m_baselineMenu) return;
    const GanttPlan &plan = m_tableModel->plan();
    QList<GanttBaselineMenu::Entry> entries;
    for (const GanttBaseline &baseline : plan.baselines()) entries.append({baseline.id, baseline.name, baseline.taken});
    m_baselineMenu->setEntries(entries, m_baselineId, m_tableModel->comparison.removed, !plan.tasks().isEmpty(), nextBaselineName());
}

void GanttView::focusContent() {
    if (showsEmptyState()) m_addFirst->setFocus();
    else m_table->setFocus();
}

void GanttView::updateChart() {
    QStringList rows;
    for (QModelIndex index = m_tableModel->index(0, 0); index.isValid(); index = m_table->indexBelow(index))
        rows.append(m_tableModel->id(index));
    const QStringList selected = selectedIds();
    // Default colours (user, 2026-10-27): tasks peach, summaries lilac, so a summary
    // reads apart. A branch colour chosen by hand on the topic (here with Colour…, or on the map)
    // colours that bar alone; the inherited colours of the map do not.
    // The name on the bar: summaries yes, tasks no, unless the topic says otherwise (user, 2026-10-27).
    const GanttCommands::BarLooks bars = GanttCommands::barLooks(m_document->model()->root());
    m_chart->setLabelled(bars.labelled);
    m_chart->setBaseline(m_tableModel->baseline, m_tableModel->comparison);
    auto *legend = static_cast<GanttLegend *>(m_legend);
    legend->baseline = m_tableModel->baseline ? m_tableModel->baseline->name : QString();
    legend->text = m_colors.muted;
    legend->update();
    m_chart->setData(m_tableModel->plan(), m_tableModel->schedule(), rows, QSet<QString>(selected.cbegin(), selected.cend()), bars.looks);
    scrollIfOpened();
}

// Shown on another sheet (or file): its opening day near the left edge; the same sheet keeps its
// scroll.
QString GanttView::openedSheetKey() const {
    return QString::number(m_document->documentGeneration()) + QLatin1Char('/') + m_document->currentSheetId();
}

void GanttView::scrollIfOpened() {
    if (!isVisible() || openedSheetKey() == m_openedSheet) return;
    m_openedSheet = openedSheetKey();
    m_chart->scrollToOpeningDay();
}

QStringList GanttView::selectedIds() const {
    QStringList ids;
    for (const QModelIndex &index : m_table->selectionModel()->selectedRows()) ids.append(m_tableModel->id(index));
    return ids;
}

QString GanttView::currentId() const { return m_tableModel->id(m_table->currentIndex()); }

void GanttView::selectTask(const QString &id) {
    const QModelIndex index = m_tableModel->indexOf(id);
    if (!index.isValid()) return;
    m_table->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    m_table->scrollTo(index);
}

void GanttView::commitPendingEdit() {
    m_panel->commitPendingEdit();
    if (!editing(m_table)) return;
    // The open editor is the visible child of the table's viewport (a date field's line edit is one
    // level below); committing it works like Enter. Found without the focus, which another window
    // may hold (the window not active, or tests running at the same time).
    QWidget *editor = nullptr;
    for (QWidget *child : m_table->viewport()->findChildren<QWidget *>(Qt::FindDirectChildrenOnly))
        if (child->isVisible() && !child->isWindow()) editor = child;
    if (!editor) return;
    QAbstractItemDelegate *delegate = m_table->itemDelegate();
    emit delegate->commitData(editor);
    emit delegate->closeEditor(editor, QAbstractItemDelegate::NoHint);
}

MindNode *GanttView::node(const QString &id) const { return m_document->model()->nodeById(id); }

void GanttView::finishMapChange() {
    if (afterMapChange) afterMapChange();
    else m_document->flushPendingStep();
}

QString GanttView::addTask(bool child) {
    // A row is a topic: a sibling after the selected one, its child, or a main topic.
    MindNode *anchor = node(currentId());
    MindNode *created = GanttCommands::addTask(*m_document, anchor, child);
    if (!created) return {};
    const QString id = created->id;
    if (anchor && child) m_collapsed.remove(anchor->id);
    finishMapChange();
    refresh();  // the table must hold the new row before its name opens
    selectTask(id);
    m_table->setFocus();
    m_table->edit(m_tableModel->indexOf(id, Name));
    return id;
}

// FEAT-124: with its formatted form (empty: plain).
bool GanttView::renameTask(const QString &id, const QString &name, const QString &html) {
    MindNode *topic = node(id);
    if (!topic || !m_document->setFormattedTitle(topic, name, html)) return false;
    finishMapChange();
    return true;
}

bool GanttView::indentSelected() {
    const QString id = currentId();
    MindNode *topic = node(id);
    if (!GanttCommands::indent(*m_document, topic)) return false;
    if (topic->parent) m_collapsed.remove(topic->parent->id);
    finishMapChange();
    refresh();
    selectTask(id);
    return true;
}

bool GanttView::outdentSelected() {
    const QString id = currentId();
    MindNode *topic = node(id);
    if (!GanttCommands::outdent(*m_document, topic)) return false;
    finishMapChange();
    refresh();
    selectTask(id);
    return true;
}

bool GanttView::moveTask(const QString &id, const QString &target, DropPlace place) {
    MindNode *anchor = node(target);
    if (!GanttCommands::move(*m_document, node(id), anchor, place)) return false;
    if (place == DropPlace::Inside) m_collapsed.remove(anchor->id);
    finishMapChange();
    refresh();
    selectTask(id);
    return true;
}

bool GanttView::removeSelected() {
    return removeTasks(selectedIds());
}

bool GanttView::removeTasks(const QStringList &ids) {
    QList<MindNode *> topics;
    for (const QString &id : ids)
        if (MindNode *topic = node(id)) topics.append(topic);
    if (topics.isEmpty() || !m_document->removeNodes(topics)) return false;
    finishMapChange();
    refresh();
    return true;
}

QStringList GanttView::chainIds(const GanttPlan &plan) const { return GanttCommands::chainIds(plan, m_selectionOrder, selectedIds()); }

GanttPlan GanttView::currentPlan() const { return m_document->model()->gantt(); }

bool GanttView::chainUnlinks(const GanttPlan &plan) const { return GanttCommands::chainUnlinks(plan, chainIds(plan)); }

bool GanttView::chainSelected() {
    const GanttPlan current = m_document->model()->gantt();  // gantt() builds a new plan: keep it alive
    const QStringList ids = chainIds(current);
    const bool unlink = GanttCommands::chainUnlinks(current, ids);
    if (!unlink && ids.size() < 2) return false;
    return editPlan([&](GanttPlan &plan) { return GanttCommands::chain(plan, ids, unlink); });
}

void GanttView::chooseColor(const QPoint &globalPos) {
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) return;
    const GanttPlan plan = m_document->model()->gantt();  // gantt() builds a new plan: keep it alive
    const GanttTask *first = plan.task(ids.first());
    ColorPicker::Options options;
    const MindNode *firstTopic = node(ids.first());
    options.current = first && firstTopic && NodeStyle::hasManualValue(firstTopic, StyleProperty::BranchColor) ? first->color
                      : GanttCanvas::defaultBarColor(plan, plan.barKind(ids.first()));
    options.themeColors = m_document->model()->activePalette;
    options.allowOpacity = false;
    // The bar's colour is the topic's branch colour: choosing it here changes the map too.
    ColorPicker::exec(QRect(globalPos, QSize(1, 1)), ColorPicker::Placement::Above, options, [this, ids](const QColor &color) {
        QList<MindNode *> topics;
        for (const QString &id : ids)
            if (MindNode *topic = node(id)) topics.append(topic);
        if (!m_document->model()->setStyle(topics, StyleProperty::BranchColor, color)) return;
        m_document->markChanged();
        finishMapChange();
    });
}

void GanttView::showRowMenu(const QPoint &globalPos) {
    QMenu menu(this);
    menu.setWindowFlags(menu.windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu.setAttribute(Qt::WA_TranslucentBackground);
    menu.setStyleSheet(m_menuStyle + QStringLiteral(" QMenu::item{padding-left:12px;}"));
    QAction *add = menu.addAction(tr("Add task"));
    QAction *subtask = menu.addAction(tr("Add subtask"));
    const bool unlinks = chainUnlinks(m_tableModel->plan());
    QAction *chain = menu.addAction(unlinks ? tr("Remove links") : tr("Link in a chain"));
    chain->setEnabled(unlinks || chainIds(m_tableModel->plan()).size() > 1);
    QAction *color = menu.addAction(tr("Colour…"));
    // Back to the default peach or lilac: the colour chosen by hand is removed (map too).
    QAction *defaultColor = menu.addAction(tr("Default colour"));
    // The name on the bar, checked as the first selected bar shows it.
    QAction *label = menu.addAction(tr("Name on the bar"));
    label->setCheckable(true);
    // Back to a topic without a date (no bar), like before it was planned.
    QAction *undate = menu.addAction(tr("Remove date"));
    const GanttPlan plan = m_document->model()->gantt();
    const QStringList selected = selectedIds();
    QList<MindNode *> coloured;
    for (const QString &id : selected)
        if (MindNode *topic = node(id); topic && NodeStyle::hasManualValue(topic, StyleProperty::BranchColor)) coloured.append(topic);
    defaultColor->setEnabled(!coloured.isEmpty());
    label->setChecked(!selected.isEmpty() && m_chart->showsName(selected.first()));
    undate->setEnabled(std::any_of(selected.cbegin(), selected.cend(), [&plan](const QString &id) {
        const GanttTask *task = plan.task(id);
        return task && task->start.isValid();
    }));
    menu.addSeparator();
    // FEAT-085: the keys are MainWindow's, shown here only.
    QAction *copyStyleItem = menu.addAction(tr("Copy style"));
    QAction *pasteStyleItem = menu.addAction(tr("Paste style"));
    copyStyleItem->setEnabled(selected.size() == 1 && copyStyle);
    pasteStyleItem->setEnabled(!selected.isEmpty() && pasteStyle && canPasteStyle && canPasteStyle());
    if (m_shortcuts)
        for (const auto &[item, id] : {std::pair{copyStyleItem, "copyStyle"}, std::pair{pasteStyleItem, "pasteStyle"}}) {
            item->setShortcuts(m_shortcuts->shortcuts(QLatin1String(id)));
            item->setShortcutVisibleInContextMenu(true);
        }
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Delete"));
    remove->setShortcut(QKeySequence(QKeySequence::Delete));
    remove->setShortcutVisibleInContextMenu(true);
    QAction *chosen = menu.exec(globalPos);
    if (chosen == add) addTask(false);
    else if (chosen == subtask) addTask(true);
    else if (chosen == chain) chainSelected();
    else if (chosen == color) chooseColor(globalPos);
    else if (chosen == label) setNameOnBar(label->isChecked());
    else if (chosen == defaultColor && m_document->model()->clearStyle(coloured, StyleProperty::BranchColor)) {
        m_document->markChanged();
        finishMapChange();
    }
    else if (chosen == undate)
        editPlan([&selected](GanttPlan &edited) {
            bool changed = false;
            for (const QString &id : selected) changed = edited.setStart(id, QDate()) || changed;
            return changed;
        });
    else if (chosen == copyStyleItem) copyStyle();
    else if (chosen == pasteStyleItem) pasteStyle();
    else if (chosen == remove) removeSelected();
}

int GanttView::panelWidthFor(int wanted) const {
    return std::clamp(wanted, int(GanttTaskPanel::kWidth), std::max(int(GanttTaskPanel::kWidth), width() / 2));
}

void GanttView::applyPanelWidth() {
    m_panel->setPanelWidth(panelWidthFor(m_panelDraggedWidth));
}

void GanttView::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    applyPanelWidth();  // a narrower view narrows a dragged panel
}

bool GanttView::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_table && event->type() == QEvent::KeyPress && !editing(m_table) && m_shortcuts) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (m_shortcuts->matches(QStringLiteral("ganttAddTask"), key)) { addTask(); return true; }
        if (m_shortcuts->matches(QStringLiteral("ganttIndent"), key)) { indentSelected(); return true; }
        if (m_shortcuts->matches(QStringLiteral("ganttOutdent"), key)) { outdentSelected(); return true; }
        if (m_shortcuts->matches(QStringLiteral("ganttDelete"), key)) { removeSelected(); return true; }
        if (m_shortcuts->matches(QStringLiteral("ganttChain"), key)) { chainSelected(); return true; }
    }
    // The Extras icons: a click opens the web page or the note (user, 2026-10-28), without selecting.
    if (watched == m_table->viewport()) {
        const auto iconAt = [this](const QPoint &pos) -> QPair<Extra, QRect> {
            const QModelIndex index = m_table->indexAt(pos);
            if (index.column() != Extras) return {NoExtra, {}};
            for (const auto &icon : extraIcons(m_table->visualRect(index), index.data(kExtrasRole).toInt()))
                if (icon.second.contains(pos)) return icon;
            return {NoExtra, {}};
        };
        switch (event->type()) {
        case QEvent::MouseMove:
            if (iconAt(static_cast<QMouseEvent *>(event)->position().toPoint()).first != NoExtra)
                m_table->viewport()->setCursor(Qt::PointingHandCursor);
            else m_table->viewport()->unsetCursor();
            break;
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick: {
            auto *mouse = static_cast<QMouseEvent *>(event);
            const QPoint pos = mouse->position().toPoint();
            const auto [extra, rect] = iconAt(pos);
            if (extra == NoExtra || mouse->button() != Qt::LeftButton) break;
            const QString id = m_tableModel->id(m_table->indexAt(pos));
            if (extra == WebLinkExtra) emit webLinkRequested(id);
            else emit noteRequested(id, QRect(m_table->viewport()->mapToGlobal(rect.topLeft()), rect.size()));
            return true;
        }
        case QEvent::MouseButtonRelease:
            if (iconAt(static_cast<QMouseEvent *>(event)->position().toPoint()).first != NoExtra) return true;
            break;
        default: break;
        }
    }
    // The wheel over the chart scrolls the rows of the table, sideways (touchpad, tilt wheel) the
    // days of the chart; Shift+wheel and Ctrl+wheel (zoom) are the chart's own.
    if (watched == m_chart->viewport() && event->type() == QEvent::Wheel) {
        auto *wheel = static_cast<QWheelEvent *>(event);
        if (!(wheel->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))) {
            QScrollBar *rows = m_table->verticalScrollBar();
            rows->setValue(rows->value() - wheelPixels(wheel, 3 * GanttChart::kRowHeight, Qt::Vertical));
            QScrollBar *days = m_chart->horizontalScrollBar();
            days->setValue(days->value() - wheelPixels(wheel, 3 * m_chart->dayWidth(), Qt::Horizontal));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}
