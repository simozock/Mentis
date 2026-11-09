#include "ganttview.h"

#include "branchpath.h"  // the map's end marks (FEAT-080)
#include "document.h"
#include "floatingui.h"
#include "ganttbaselinemenu.h"
#include "ganttexport.h"
#include "gantttaskpanel.h"
#include "mindmapmodel.h"
#include "modaldialog.h"
#include "nodeshape.h"  // the map's node outlines for the milestones (FEAT-080)
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
#include <QToolButton>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// Colours of the view (docs/design/ui-window.md, "Gantt"). Bars, arrows, grid, selection and today
// measured on the reference Gantt guide (2026-10-27, user's choice); critical red added with FEAT-075.
const QColor kCritical(0xe5, 0x48, 0x4d);
const QColor kTaskBar(0xf9, 0xbd, 0x89);
const QColor kTaskText(0x87, 0x4c, 0x18);
const QColor kSummaryBar(0xaa, 0xaf, 0xfa);
const QColor kMilestoneBar(0x64, 0x74, 0x8b);  // FEAT-080: the milestones' own colour (slate, to confirm)
const QColor kSummaryText(0x40, 0x08, 0xba);
const QColor kArrow(0x9e, 0xa3, 0xa8);
// Grid and selection from the user's second reference picture (2026-10-28): the lines are translucent,
// #ecedf0 on white and still visible on a selected row (reference #cdced0 there); selected rows #e3e5e6;
// the selected bar has a thin blue ring 2 px outside it (measured on a scaled picture).
const QColor kGrid(10, 30, 70, 20);
const QColor kSelected(0xe3, 0xe5, 0xe6);
const QColor kSelectedBar(0x41, 0x88, 0xc9);
const QColor kToday(0xe0, 0x78, 0x4c);    // estimated: the reference is a GIF with reduced colours
const QColor kBadge(0x1f, 0x23, 0x26);    // progress badge
const QColor kText(0x1f, 0x29, 0x37);
const QColor kMuted(0x6b, 0x72, 0x80);
// Non-working days and header lines measured on the reference Gantt (user's picture, 2026-10-28): days
// #fafbfc on white (translucent, so a selected row stays a shade darker there), lines #f0f2f4.
const QColor kShade(10, 40, 80, 5);  // = #fafbfc on white
const QColor kHeaderLine(0xf0, 0xf2, 0xf4);
const QColor kLine(15, 23, 42, 28);      // borders of the panel's fields and cards
constexpr double kBarInset = 4;           // bar = row height - 8
// FEAT-082: while comparing with a snapshot the bar gives 7 px to the thin bar under it (4 px high,
// 4 px under the bar), grey at the snapshot's dates; the slip of the end on it red (later) or green.
constexpr double kBaselineRoom = 7;
constexpr double kBaselineMark = 4;
const QColor kBaselineSame(0x9a, 0xa2, 0xac);
const QColor kBaselineEarlier(0x30, 0xa4, 0x6c);
// 18 x 14 px diamond, a little wider than tall (user's picture, 2026-10-29); under 20 wide, so
// an arrow running down between milestones of next days (28 px) passes clear of the tips.
constexpr double kMilestoneHalfWidth = 9;
constexpr double kMilestoneHalfHeight = 7;
constexpr double kHandleRadius = 4;
constexpr double kRoundedBarRadius = 4;  // FEAT-080, user's picture (2026-10-29)
// Summary bracket (FEAT-080, user's picture, 2026-10-29): 16 px tall, centred in the bar's place;
// band 10 px, tips 8 px wide hanging 6 px under it. The other thin summary shapes share the place.
constexpr double kBracketHeight = 16;
// Group frame (FEAT-080, user, 2026-11-01; sizes proposed by Claude): 6 px out of the summary's days
// (clear of the critical halo and the selection ring) and 2 px under the last subtask's bar, 3 px
// more for every summary level inside it; corners 6 px.
constexpr double kFrameGap = 6;
constexpr double kFrameBelow = 2;
constexpr double kFrameStep = 3;
constexpr double kFrameRadius = 6;
constexpr double kFrameTint = 0.15;

// Border the fill's hue, more saturated and a little darker when they match: vivid, not a muddy
// darker shade (user's pictures, 2026-10-29: light blue outlined in blue, peach in coral).
QColor intense(const QColor &color) {
    const QColor hsv = color.toHsv();
    return QColor::fromHsvF(hsv.hsvHueF(), std::min(1.0, hsv.hsvSaturationF() * 2.4), hsv.valueF() * 0.88);
}
constexpr double kEdgeGrip = 5;
constexpr int kDragThreshold = 3;

// A bar's colour without a branch colour chosen by hand on its topic: the sheet's (FEAT-080), or
// The default peach and lilac.
QColor defaultBarColor(const GanttPlan &plan, GanttPlan::BarKind kind) {
    if (const QColor sheet = plan.defaultColor(kind); sheet.isValid()) return sheet;
    switch (kind) {
    case GanttPlan::BarKind::Summary: return kSummaryBar;
    case GanttPlan::BarKind::Milestone: return kMilestoneBar;
    case GanttPlan::BarKind::Task: break;
    }
    return kTaskBar;
}

// Text on a bar of that colour: white on dark colours, otherwise a dark shade (as a hand colour).
QColor barTextColor(const QColor &bar) {
    return NodeStyle::isDarkBackground(bar) ? QColor(Qt::white) : bar.darker(300);
}

// Number (FEAT-024 numbering, read-only) is drawn before Name but stays after it in the model, so the
// tree (expand arrows, indentation) stays on Name, logical column 0.
enum Column { Name, Start, End, Duration, Progress, Priority, Person, Number, Extras, ColumnCount };

// Extras column (user, 2026-10-28): icons of the topic's web link and note, after Name.
enum Extra { NoExtra = 0, WebLinkExtra = 1, NoteExtra = 2 };
constexpr int kExtrasRole = Qt::UserRole + 1;  // Extra flags of the row
constexpr int kMilestoneRole = Qt::UserRole + 2;  // a milestone that is not a summary
constexpr int kNewRole = Qt::UserRole + 3;  // FEAT-082: a task not in the snapshot compared

// FEAT-082: the "New" pill of the tasks not in the compared snapshot (table and chart).
QSizeF newPillSize(const QFont &font) {
    return {QFontMetricsF(font).horizontalAdvance(GanttView::tr("New")) + 12, 16};
}
void paintNewPill(QPainter *painter, const QRectF &rect, const QFont &font) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(UiTheme::color(UiTheme::Token::Accent));
    painter->drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter->drawText(rect, Qt::AlignCenter, GanttView::tr("New"));
    painter->restore();
}
QFont newPillFont(QFont font, int pixels = 11) {
    font.setPixelSize(pixels);
    font.setBold(false);
    font.setWeight(QFont::DemiBold);
    return font;
}
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

// Names of the optional columns in the setting "gantt/columns" (FEAT-078).
QString columnKey(int column) {
    switch (column) {
    case Start: return QStringLiteral("start");
    case End: return QStringLiteral("end");
    case Duration: return QStringLiteral("duration");
    case Progress: return QStringLiteral("progress");
    case Priority: return QStringLiteral("priority");
    case Person: return QStringLiteral("person");
    default: return {};
    }
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
    QColor text = kMuted;
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
        const QColor samples[] = {kBaselineSame, kCritical, kBaselineEarlier};
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
        const QFont pillFont = newPillFont(font, UiTheme::fontPx(11));
        const QSizeF pill = newPillSize(pillFont);
        paintNewPill(&painter, QRectF(QPointF(x, middle - pill.height() / 2), pill), pillFont);
    }
};

// ---------------------------------------------------------------------------------------------
// Table model: the plan as a tree, values from the schedule. Edits go through `edit`.

class GanttTableModel final : public QAbstractItemModel {
public:
    std::function<bool(const std::function<bool(GanttPlan &)> &)> edit;
    std::function<bool(const QString &, const QString &)> rename;  // id, new title
    QColor text = kText, muted = kMuted;
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
        endResetModel();
    }
    const GanttPlan &plan() const { return m_plan; }
    const GanttSchedule &schedule() const { return m_schedule; }
    QString id(const QModelIndex &index) const {
        return index.isValid() ? m_rows.at(int(index.internalId()) - 1).id : QString();
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
        case Progress: return QStringLiteral("%");
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
            case Name: return task->name;
            case Start: return display ? (scheduled.dated ? QVariant(shortDate(scheduled.start)) : QVariant()) : QVariant(scheduled.start);
            case End: return display ? (scheduled.dated ? QVariant(shortDate(scheduled.end)) : QVariant()) : QVariant(scheduled.end);
            case Duration: return duration;
            case Progress: return display ? QVariant(QStringLiteral("%1%").arg(scheduled.progress)) : QVariant(task->progress);
            case Priority: return GanttTaskPanel::priorityName(info.value(taskId).priority);
            case Person: return info.value(taskId).names.join(QStringLiteral(", "));
            case Number: return info.value(taskId).number;
            default: return {};
            }
        }
        if (role == Qt::UserRole) return scheduled.summary;  // bold in GanttDelegate
        if (role == kExtrasRole) return info.value(taskId).extras;
        if (role == kMilestoneRole) return task->milestone && !scheduled.summary;
        // Grey: computed (summaries, number) or not in use (a milestone's kept duration, its end).
        const bool unused = task->milestone && (index.column() == End || index.column() == Duration);
        if (role == kNewRole) return baseline && comparison.tasks.value(taskId) == GanttBaselineComparison::Change::New;
        const bool compared = baseline && !unused && (index.column() == Start || index.column() == End || index.column() == Duration);
        if (compared && (role == Qt::ForegroundRole || role == Qt::ToolTipRole)) {
            if (const int at = baselineIndex.value(taskId, -1); at >= 0) {
                const GanttBaselineTask *then = &baseline->tasks.at(at);
                int difference = 0;  // > 0: later or longer
                if (index.column() == Duration) {
                    if (then->dated && duration.isValid()) difference = duration.toInt() - then->duration;
                } else if (then->dated && scheduled.dated) {
                    const QDate now = index.column() == Start ? scheduled.start : scheduled.end;
                    difference = int((index.column() == Start ? then->start : then->end).daysTo(now));
                }
                if (role == Qt::ToolTipRole) {
                    if (index.column() == Duration)
                        return then->dated ? GanttView::tr("Baseline: %n day(s)", nullptr, then->duration) : GanttView::tr("Baseline: —");
                    return GanttView::tr("Baseline: %1").arg(then->dated ? shortDate(index.column() == Start ? then->start : then->end)
                                                                          : QStringLiteral("—"));
                }
                if (difference != 0) return difference > 0 ? kCritical : kBaselineEarlier;
            }
        }
        if (role == Qt::ForegroundRole) return (scheduled.summary && index.column() != Name) || index.column() == Number || unused ? muted : text;
        if (role == Qt::ToolTipRole && index.column() == Name) return task->name;
        if (role == Qt::ToolTipRole && index.column() == Person) return info.value(taskId).names.join(QLatin1Char('\n'));
        return {};
    }

    bool setData(const QModelIndex &index, const QVariant &value, int role) override {
        if (role != Qt::EditRole || !edit || !(flags(index) & Qt::ItemIsEditable)) return false;
        const QString taskId = id(index);
        const QDate start = m_schedule.tasks.value(taskId).start;
        switch (index.column()) {
        case Name: {  // the topic's title (FEAT-075: rows are topics)
            // Unchanged text (spaces aside) keeps the title as it is, with its line breaks and format.
            const QString name = value.toString().simplified();
            if (name.isEmpty() || !rename || name == m_plan.task(taskId)->name.simplified()) return false;
            return rename(taskId, name);
        }
        case Start:  // a task placed by a dependency changes its lag (FEAT-074)
            return edit([&](GanttPlan &plan) { return plan.moveTo(taskId, value.toDate()); });
        case End: {
            const int duration = std::clamp(m_plan.workingDaysBetween(start, value.toDate()), 1, GanttPlan::kMaxDuration);
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
};

namespace {

// Rows of GanttChart::kRowHeight; date fields with the app's calendar, the app's number fields in range.
class GanttDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    DatePicker::Colors pickerColors;  // set with the page's colours
    QIcon webLinkIcon, noteIcon;      // the Extras column's icons, in the page's text colour

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
        if (index.column() == Name && index.data(kNewRole).toBool()) {  // FEAT-082
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            const QString text = opt.text;
            opt.text.clear();
            QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
            style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
            const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget).adjusted(4, 0, -4, 0);
            const QFont pillFont = newPillFont(opt.font, UiTheme::fontPx(11));
            const QSizeF pill = newPillSize(pillFont);
            const QString shown = QFontMetrics(opt.font).elidedText(text, Qt::ElideRight, std::max(0, textRect.width() - int(pill.width()) - 6));
            painter->save();
            painter->setFont(opt.font);
            painter->setPen(opt.palette.color(opt.state & QStyle::State_Selected ? QPalette::HighlightedText : QPalette::Text));
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, shown);
            painter->restore();
            const double left = textRect.left() + QFontMetricsF(opt.font).horizontalAdvance(shown) + 6;
            paintNewPill(painter, QRectF(QPointF(left, textRect.center().y() - pill.height() / 2 + 0.5), pill), pillFont);
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
        return QStyledItemDelegate::createEditor(parent, option, index);
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
        const QColor accent = UiTheme::color(UiTheme::Token::Accent);
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
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, viewport(), qOverload<>(&QWidget::update));
    updateRange();
}

int GanttChart::dayWidth() const {
    int base = 28;
    if (m_scale == Scale::Weeks) base = 12;
    else if (m_scale == Scale::Months) base = 4;
    return std::max(2, int(std::lround(base * m_zoom / 100.0)));
}

void GanttChart::setZoomPercent(int percent, double anchorX) {
    percent = std::clamp(percent, kMinZoom, kMaxZoom);
    if (percent == m_zoom) return;
    const double day = (anchorX + horizontalScrollBar()->value()) / dayWidth();  // date under the anchor
    m_zoom = percent;
    updateScrollBars();
    horizontalScrollBar()->setValue(int(std::lround(day * dayWidth() - anchorX)));
    viewport()->update();
    emit zoomChanged(m_zoom);
}

QDate GanttChart::openingDay() const {
    const QDate today = GanttToday::date();
    if (m_schedule.end.isValid() && m_schedule.end < today) return m_schedule.end;
    if (m_schedule.start.isValid() && m_schedule.start > today) return m_schedule.start;
    return today;
}

void GanttChart::setFirstDay(const QDate &day) {
    horizontalScrollBar()->setValue(int(m_origin.daysTo(day)) * dayWidth());
}

void GanttChart::scrollToDay(const QDate &day) {
    horizontalScrollBar()->setValue(int(m_origin.daysTo(day.addDays(-kOpenDaysBefore))) * dayWidth());
}

QString GanttChart::barLabel(const QString &name, const QFont &font, double width) {
    const QString label = QFontMetricsF(font).elidedText(name, Qt::ElideRight, width);
    return label == QStringLiteral("…") ? QString() : label;
}

void GanttChart::setColors(const GanttColors &colors) {
    m_colors = colors;
    viewport()->update();
}

void GanttChart::setScale(Scale scale) {
    if (scale == m_scale) return;
    // Keeps the date at the left edge in view.
    const QDate left = dateAt(0);
    m_scale = scale;
    updateRange();
    horizontalScrollBar()->setValue(int(m_origin.daysTo(left)) * dayWidth());
    viewport()->update();
}

void GanttChart::setData(const GanttPlan &plan, const GanttSchedule &schedule, const QStringList &rows,
                         const QSet<QString> &selected, const QHash<QString, GanttBarLook> &looks) {
    m_looks = looks;
    m_plan = plan;
    m_schedule = schedule;
    m_rows = rows;
    m_rowOf.clear();
    for (int i = 0; i < rows.size(); ++i) m_rowOf.insert(rows.at(i), i);
    m_taskIndex.clear();
    for (int i = 0; i < plan.tasks().size(); ++i) m_taskIndex.insert(plan.tasks().at(i).id, i);
    m_sketches.removeIf([this](const auto &entry) { return !m_taskIndex.contains(entry.key()); });  // deleted tasks
    m_selected = selected;
    if (!m_rowOf.contains(m_hovered)) m_hovered.clear();
    updateFrames();
    updateRange();
    viewport()->update();
}

// From two weeks before the first task (or today) to two months after the last, aligned to a Monday
// (a month's first day in Months). A new origin keeps the visible dates where they were.
void GanttChart::updateRange() {
    const QDate today = GanttToday::date();
    QDate first = m_schedule.start.isValid() ? std::min(m_schedule.start, today) : today;
    QDate last = m_schedule.end.isValid() ? std::max(m_schedule.end, today) : today;
    first = first.addDays(-14);
    first = m_scale == Scale::Months ? QDate(first.year(), first.month(), 1) : first.addDays(1 - first.dayOfWeek());
    const int shift = m_origin.isValid() ? int(first.daysTo(m_origin)) * dayWidth() : 0;
    const int value = horizontalScrollBar()->value();
    m_origin = first;
    m_last = last.addDays(60);
    updateScrollBars();  // the range first, so the kept position is not clamped
    if (shift != 0) horizontalScrollBar()->setValue(value + shift);
}

void GanttChart::updateScrollBars() {
    const int width = int(m_origin.daysTo(m_last) + 1) * dayWidth();
    horizontalScrollBar()->setRange(0, std::max(0, width - viewport()->width()));
    horizontalScrollBar()->setPageStep(viewport()->width());
    horizontalScrollBar()->setSingleStep(dayWidth());
}

void GanttChart::resizeEvent(QResizeEvent *event) {
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBars();
}

double GanttChart::x(const QDate &date) const {
    return double(m_origin.daysTo(date)) * dayWidth() - horizontalScrollBar()->value();
}

QDate GanttChart::dateAt(double xPos) const {
    return m_origin.addDays(qint64(std::floor((xPos + horizontalScrollBar()->value()) / dayWidth())));
}

double GanttChart::rowTop(int row) const {
    return kHeaderHeight + double(row) * kRowHeight - (verticalOffset ? verticalOffset() : 0);
}

int GanttChart::rowAt(double y) const {
    if (y < kHeaderHeight) return -1;
    const int row = int(std::floor((y - kHeaderHeight + (verticalOffset ? verticalOffset() : 0)) / kRowHeight));
    return row >= 0 && row < m_rows.size() ? row : -1;
}

const GanttTask *GanttChart::taskOf(const QString &id) const {
    const int index = m_taskIndex.value(id, -1);
    return index < 0 ? nullptr : &m_plan.tasks().at(index);
}

QRectF GanttChart::barRect(const QString &id) const {
    const int row = m_rowOf.value(id, -1);
    const auto scheduled = m_schedule.tasks.constFind(id);
    if (row < 0 || scheduled == m_schedule.tasks.cend() || !scheduled->dated) return {};  // undated: no bar
    const double top = rowTop(row);
    const double room = m_baseline ? kBaselineRoom : 0;  // FEAT-082: the thin bar's room under the bar
    if (isMilestone(id)) {
        const double center = x(scheduled->start) + dayWidth() / 2.0;  // in the middle of its day (user, 2026-10-29)
        return {center - kMilestoneHalfWidth, top + (kRowHeight - room) / 2.0 - kMilestoneHalfHeight,
                2 * kMilestoneHalfWidth, 2 * kMilestoneHalfHeight};
    }
    const double left = x(scheduled->start), right = x(scheduled->end) + dayWidth();
    return {left, top + kBarInset, right - left, kRowHeight - 2 * kBarInset - room};
}

void GanttChart::setBaseline(const std::optional<GanttBaseline> &baseline, const GanttBaselineComparison &comparison) {
    m_comparison = comparison;  // a hash copy: shared data, no deep copy
    if (baseline == m_baseline) return;  // updateChart runs on every selection change
    m_baseline = baseline;
    m_baselineIndex.clear();
    if (m_baseline)
        for (int i = 0; i < m_baseline->tasks.size(); ++i) m_baselineIndex.insert(m_baseline->tasks.at(i).id, i);
    viewport()->update();
}

const GanttBaselineTask *GanttChart::baselineTask(const QString &id) const {
    const int index = m_baselineIndex.value(id, -1);
    return index < 0 ? nullptr : &m_baseline->tasks.at(index);
}

// The snapshot's bar at its dates, with the height of the current kind of bar (a milestone's box
// when the snapshot had a milestone).
QRectF GanttChart::baselineRect(const QString &id) const {
    const GanttBaselineTask *then = m_baseline ? baselineTask(id) : nullptr;
    const int row = m_rowOf.value(id, -1);
    if (!then || !then->dated || row < 0) return {};
    const double top = rowTop(row);
    if (then->milestone) {
        const double center = x(then->start) + dayWidth() / 2.0;
        return {center - kMilestoneHalfWidth, top + (kRowHeight - kBaselineRoom) / 2.0 - kMilestoneHalfHeight,
                2 * kMilestoneHalfWidth, 2 * kMilestoneHalfHeight};
    }
    const double left = x(then->start), right = x(then->end) + dayWidth();
    return {left, top + kBarInset, right - left, kRowHeight - 2 * kBarInset - kBaselineRoom};
}

QRectF GanttChart::baselineMarkRect(const QString &id) const {
    const QRectF place = baselineRect(id);
    if (place.isEmpty()) return {};
    // 4 px under the bar: clear of the critical halo (3 px out, 2 px wide).
    const double top = rowTop(m_rowOf.value(id)) + kRowHeight - 3 - kBaselineMark;
    if (baselineTask(id)->milestone) return {place.center().x() - 4, top - 2, 8, 8};
    return {place.left(), top, place.width(), kBaselineMark};
}

QRectF GanttChart::slipRect(const QString &id) const {
    const GanttBaselineTask *then = m_baseline ? baselineTask(id) : nullptr;
    const GanttScheduledTask now = m_schedule.tasks.value(id);
    const QRectF mark = baselineMarkRect(id);
    if (!then || !now.dated || mark.isEmpty() || then->end == now.end) return {};
    // The end of a bar is its last day's right edge, a milestone's the middle of its day.
    const int dw = dayWidth();
    const double was = then->milestone ? x(then->start) + dw / 2.0 : x(then->end) + dw;
    const double is = isMilestone(id) ? x(now.start) + dw / 2.0 : x(now.end) + dw;
    const double middle = mark.center().y();
    return {QPointF(std::min(was, is), middle - kBaselineMark / 2), QPointF(std::max(was, is), middle + kBaselineMark / 2)};
}

QColor GanttChart::baselineMarkColor(const QString &id) const {
    switch (m_comparison.tasks.value(id)) {
    case GanttBaselineComparison::Change::Later: return kCritical;
    case GanttBaselineComparison::Change::Earlier: return kBaselineEarlier;
    default: return kBaselineSame;
    }
}

QRectF GanttChart::newPillRect(const QString &id) const {
    if (!m_baseline || m_comparison.tasks.value(id) != GanttBaselineComparison::Change::New) return {};
    const QRectF bar = barRect(id);
    if (bar.isEmpty()) return {};
    const QSizeF size = newPillSize(newPillFont(viewport()->font()));
    // Before the bar, clear of the start's link circle.
    return {QPointF(bar.left() - 14 - size.width(), bar.center().y() - size.height() / 2), size};
}

void GanttChart::paintBaseline(QPainter &painter, const QString &id, const QColor &border) {
    if (!m_baseline) return;
    if (const QRectF pill = newPillRect(id); !pill.isEmpty()) {
        paintNewPill(&painter, pill, newPillFont(viewport()->font()));
        return;
    }
    const QRectF place = baselineRect(id);
    if (place.isEmpty()) return;
    // Readability (user, 2026-10-30): the snapshot's bar as a dashed outline only where the current
    // bar is not (over it, it mixed with the hand-drawn fills); the thin bar grey at the snapshot's
    // dates, and only the slip in colour, so the colour is where the change is and as long as it.
    QColor line = border;
    line.setAlphaF(0.8);
    painter.save();
    if (const QRectF bar = barRect(id); !bar.isEmpty()) {
        // The viewport with a hole of the bar grown 2 px (clear of its border): an odd-even path,
        // no boolean operation at every paint.
        QPainterPath outside;
        outside.setFillRule(Qt::OddEvenFill);
        outside.addRect(viewport()->rect());
        outside.addPath(barShape(id, bar.adjusted(-2, -2, 2, 2), isMilestone(id)));
        painter.setClipPath(outside, Qt::IntersectClip);
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(line, 1, Qt::DashLine));
    painter.drawPath(barShape(id, place, baselineTask(id)->milestone));
    painter.restore();
    painter.save();
    const QRectF mark = baselineMarkRect(id);
    painter.setPen(Qt::NoPen);
    const bool milestone = baselineTask(id)->milestone;
    painter.setBrush(kBaselineSame);
    if (!milestone) painter.drawRoundedRect(mark, 2, 2);
    if (const QRectF slip = slipRect(id); !slip.isEmpty()) {
        painter.setBrush(baselineMarkColor(id));
        painter.drawRoundedRect(slip, 2, 2);
    }
    if (milestone) {  // the snapshot's day over the slip that leaves it
        painter.setBrush(kBaselineSame);
        painter.drawPath(milestonePath(GanttPlan::MilestoneShape::Diamond, mark));
    }
    painter.restore();
}

QPainterPath GanttChart::barShape(const QString &id, const QRectF &box, bool milestone) const {
    QPainterPath shape;
    if (milestone) return milestonePath(milestoneShapeOf(id), box);
    if (m_schedule.tasks.value(id).summary && summaryShapeOf(id) != GanttPlan::SummaryShape::Bar)
        return summaryPath(summaryShapeOf(id), summaryPlace(id, box));
    return taskPath(barShapeOf(id), box);
}

GanttPlan::BarShape GanttChart::barShapeOf(const QString &id) const {
    const GanttTask *task = taskOf(id);
    return task ? m_plan.barShape(*task) : m_plan.barShape();
}

GanttPlan::MilestoneShape GanttChart::milestoneShapeOf(const QString &id) const {
    const GanttTask *task = taskOf(id);
    return task ? m_plan.milestoneShape(*task) : m_plan.milestoneShape();
}

GanttPlan::SummaryShape GanttChart::summaryShapeOf(const QString &id) const {
    const GanttTask *task = taskOf(id);
    return task ? m_plan.summaryShape(*task) : m_plan.summaryShape();
}

double GanttChart::barTip(GanttPlan::BarShape shape, const QRectF &box) {
    switch (shape) {
    case GanttPlan::BarShape::Arrow: return std::min(box.height() / 3, box.width() / 2);
    case GanttPlan::BarShape::Hexagon:
    case GanttPlan::BarShape::Parallelogram: return std::min(box.height() / 3, box.width() / 4);
    default: return 0;
    }
}

QPainterPath GanttChart::taskPath(GanttPlan::BarShape shape, const QRectF &box, qreal grow) {
    const QRectF outer = box.adjusted(-grow, -grow, grow, grow);
    QPainterPath path;
    switch (shape) {
    case GanttPlan::BarShape::Pill:
    case GanttPlan::BarShape::Rounded: {  // FEAT-080: a pill, or a rectangle with 4 px corners (at most half a narrow bar's width)
        double radius = std::min(box.height(), box.width()) / 2;
        if (shape == GanttPlan::BarShape::Rounded) radius = std::min(radius, kRoundedBarRadius);
        path.addRoundedRect(outer, radius + grow, radius + grow);
        break;
    }
    case GanttPlan::BarShape::Rectangle:
        path.addRect(outer);
        break;
    case GanttPlan::BarShape::Arrow:
    case GanttPlan::BarShape::Hexagon: {
        // Grown points keep the angle: their length grows with the height.
        const double tip = barTip(shape, box) * outer.height() / box.height();
        const double start = shape == GanttPlan::BarShape::Hexagon ? tip : 0, middle = outer.center().y();
        QPolygonF points{QPointF(outer.left() + start, outer.top()), QPointF(outer.right() - tip, outer.top()),
                         QPointF(outer.right(), middle), QPointF(outer.right() - tip, outer.bottom()),
                         QPointF(outer.left() + start, outer.bottom())};
        if (start > 0) points << QPointF(outer.left(), middle);
        path.addPolygon(points);
        path.closeSubpath();
        break;
    }
    case GanttPlan::BarShape::Leaf: {  // the top-left and bottom-right corners round
        const double radius = std::min(box.height() / 2, box.width() / 4) + grow;
        path.moveTo(outer.left(), outer.bottom());
        path.lineTo(outer.left(), outer.top() + radius);
        path.arcTo(QRectF(outer.left(), outer.top(), 2 * radius, 2 * radius), 180, -90);
        path.lineTo(outer.right(), outer.top());
        path.lineTo(outer.right(), outer.bottom() - radius);
        path.arcTo(QRectF(outer.right() - 2 * radius, outer.bottom() - 2 * radius, 2 * radius, 2 * radius), 0, -90);
        path.closeSubpath();
        break;
    }
    case GanttPlan::BarShape::Ticket: {  // a half-circle notch in each end; grown, the notch gets smaller
        const double notch = std::max(0.0, std::min(box.height() / 4, box.width() / 4) - grow), middle = outer.center().y();
        path.moveTo(outer.topLeft());
        path.lineTo(outer.topRight());
        path.lineTo(outer.right(), middle - notch);
        path.arcTo(QRectF(outer.right() - notch, middle - notch, 2 * notch, 2 * notch), 90, 180);
        path.lineTo(outer.bottomRight());
        path.lineTo(outer.bottomLeft());
        path.lineTo(outer.left(), middle + notch);
        path.arcTo(QRectF(outer.left() - notch, middle - notch, 2 * notch, 2 * notch), 270, 180);
        path.closeSubpath();
        break;
    }
    case GanttPlan::BarShape::Parallelogram: {  // leaning right; grown, the slant keeps its angle
        const double slant = barTip(shape, box) * outer.height() / box.height();
        path.addPolygon(QPolygonF{QPointF(outer.left() + slant, outer.top()), outer.topRight(), QPointF(outer.right() - slant, outer.bottom()),
                                  outer.bottomLeft()});
        path.closeSubpath();
        break;
    }
    }
    return path;
}

QPointF GanttChart::linkHandle(const QString &id) const {
    const QRectF bar = barRect(id);
    return bar.isEmpty() ? QPointF() : QPointF(bar.right() + 8, bar.center().y());
}

QPointF GanttChart::linkHandleAtStart(const QString &id) const {
    const QRectF bar = barRect(id);
    return bar.isEmpty() ? QPointF() : QPointF(bar.left() - 8, bar.center().y());
}

bool GanttChart::isMilestone(const QString &id) const {
    const GanttTask *task = taskOf(id);
    // A task that became a summary is drawn as one: its children give its dates.
    return task && task->milestone && !m_schedule.tasks.value(id).summary;
}

QRectF GanttChart::progressBadge(const QString &id, int progress) const {
    const QRectF bar = barRect(id);
    const GanttTask *task = taskOf(id);
    if (bar.isEmpty() || !task || isMilestone(id)) return {};
    if (progress < 0) progress = task->progress;
    QFont font = viewport()->font();
    font.setPixelSize(10);
    font.setBold(true);
    const double width = std::max(14.0, QFontMetricsF(font).horizontalAdvance(QString::number(progress)) + 8);
    const double edge = bar.left() + bar.width() * progress / 100.0;
    return {edge - width / 2, bar.bottom() - 4, width, 14};
}

QPointF GanttChart::progressHandle(const QString &id) const { return progressBadge(id).center(); }

// The link circle under pos (end or start) of the bar under the mouse; empty if none.
QString GanttChart::linkHandleAt(const QString &id, const QPointF &pos, bool *atStart) const {
    if (id.isEmpty() || m_schedule.tasks.value(id).summary) return {};
    const bool end = QLineF(pos, linkHandle(id)).length() <= kHandleRadius + 3;
    const bool start = QLineF(pos, linkHandleAtStart(id)).length() <= kHandleRadius + 3;
    if (!end && !start) return {};
    if (atStart) *atStart = start && !end;
    return id;
}

// Right edge of the bar; on a short bar (Weeks, Months) only its outer side, so the bar still moves.
bool GanttChart::onResizeGrip(const QRectF &bar, const QPointF &pos) const {
    if (std::abs(pos.x() - bar.right()) > kEdgeGrip) return false;
    return bar.width() >= 3 * kEdgeGrip || pos.x() >= bar.right();
}

QString GanttChart::barAt(const QPointF &pos) const {
    const int row = rowAt(pos.y());
    if (row < 0) return {};
    const QString id = m_rows.at(row);
    const QRectF bar = barRect(id).adjusted(-kEdgeGrip, -kBarInset, kEdgeGrip, kBarInset);
    return bar.contains(pos) ? id : QString();
}

QString GanttChart::linkFrom(const QString &target) const { return m_linkFromStart ? target : m_dragId; }
QString GanttChart::linkTo(const QString &target) const { return m_linkFromStart ? m_dragId : target; }

bool GanttChart::linkAllowed(const QString &from, const QString &to) const {
    GanttPlan copy = m_plan;
    GanttDependency dependency;
    dependency.from = from;
    dependency.to = to;
    return copy.addDependency(dependency);
}

// ---- painting

void GanttChart::paintEvent(QPaintEvent *) {
    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing);
    const QRect area = viewport()->rect();
    // The chart is the bottom of its card (FEAT-078): nothing under it, bottom corners rounded. A
    // rounded clip path sent every fill and line through an antialiased mask (2026-10-29, 1,000
    // tasks at 1150 x 850: 4-6 ms of the frame, 1 ms with a clip rectangle); the page covers the
    // corners at the end instead.
    painter.setClipRect(area);
    const int dw = dayWidth();
    const QDate firstDay = dateAt(0), lastDay = dateAt(area.width());

    // Selected rows, weekends and non-working days, and the grid under everything else.
    painter.save();
    painter.setClipRect(QRect(0, kHeaderHeight, area.width(), area.height() - kHeaderHeight), Qt::IntersectClip);
    for (const QString &id : m_selected)
        if (const int row = m_rowOf.value(id, -1); row >= 0)
            painter.fillRect(QRectF(0, rowTop(row), area.width(), kRowHeight), m_colors.selected);
    if (m_scale != Scale::Months)
        for (QDate day = firstDay; day <= lastDay; day = day.addDays(1))
            if (day.dayOfWeek() >= Qt::Saturday || !m_plan.calendar().isWorkingDay(day))
                painter.fillRect(QRectF(x(day), kHeaderHeight, dw, area.height() - kHeaderHeight), m_colors.shade);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(m_colors.grid);
    // One line per day (Days), week (Weeks) or month (Months), one under each row.
    for (QDate day = firstDay; day <= lastDay.addDays(1); day = day.addDays(1)) {
        const bool line = m_scale == Scale::Days || (m_scale == Scale::Weeks && day.dayOfWeek() == 1) || (m_scale == Scale::Months && day.day() == 1);
        if (line) painter.drawLine(QPointF(std::floor(x(day)), kHeaderHeight), QPointF(std::floor(x(day)), area.height()));
    }
    for (int row = std::max(0, rowAt(kHeaderHeight)); row < m_rows.size() && rowTop(row) < area.height(); ++row) {
        const double bottom = std::floor(rowTop(row) + kRowHeight) - 1;
        painter.drawLine(QPointF(0, bottom), QPointF(area.width(), bottom));
    }
    painter.setRenderHint(QPainter::Antialiasing);
    const double today = x(GanttToday::date()) + dw / 2.0;
    painter.setPen(QPen(kToday, 1.5));
    painter.drawLine(QPointF(today, kHeaderHeight), QPointF(today, area.height()));
    const int firstRow = std::max(0, rowAt(kHeaderHeight));
    if (!m_frames.isEmpty()) paintFrames(painter, firstRow, rowAt(area.height() - 1) < 0 ? int(m_rows.size()) - 1 : rowAt(area.height() - 1));
    paintArrows(painter);
    for (int row = firstRow; row < m_rows.size() && rowTop(row) < area.height(); ++row) paintTask(painter, m_rows.at(row), row);
    if (m_drag == Drag::Link) {
        const QString target = barAt(m_dragPos);
        painter.setPen(QPen(target.isEmpty() || target == m_dragId || linkAllowed(linkFrom(target), linkTo(target)) ? kArrow : kCritical, 1.5, Qt::DashLine));
        painter.drawLine(m_linkFromStart ? linkHandleAtStart(m_dragId) : linkHandle(m_dragId), m_dragPos);
    }
    painter.restore();
    paintHeader(painter, area);
    if (m_printing) return;  // FEAT-083: a page, not the card on the window's page
    const qreal r = GanttColors::kCardRadius;
    for (const bool left : {true, false}) {  // the page outside the card's bottom corners
        QPainterPath outside;
        outside.addRect(QRectF(left ? 0 : area.width() - r, area.height() - r, r, r));
        QPainterPath round;
        round.addRoundedRect(QRectF(left ? 0 : area.width() - 2 * r, area.height() - 2 * r, 2 * r, 2 * r), r, r);
        painter.fillPath(outside.subtracted(round), m_colors.page);
    }
}

// Month names with a capital, also where the language writes them lower case (user, 2026-10-29).
QString GanttChart::monthName(int month, QLocale::FormatType format) const {
    QString name = m_names.standaloneMonthName(month, format);
    if (!name.isEmpty()) name[0] = name.at(0).toUpper();
    return name;
}

void GanttChart::paintHeader(QPainter &painter, const QRect &area) {
    painter.fillRect(QRect(0, 0, area.width(), kHeaderHeight), m_colors.card);
    painter.setPen(m_colors.headerLine);
    painter.drawLine(QPointF(0, kHeaderHeight - 0.5), QPointF(area.width(), kHeaderHeight - 0.5));
    painter.drawLine(QPointF(0, kHeaderHeight / 2.0 - 0.5), QPointF(area.width(), kHeaderHeight / 2.0 - 0.5));
    QFont font = UiTheme::interfaceFont();  // the timeline header is interface
    font.setPixelSize(UiTheme::fontPx(12));
    painter.setFont(font);
    const int dw = dayWidth();
    const QDate firstDay = dateAt(0).addDays(-1), lastDay = dateAt(area.width());
    const double half = kHeaderHeight / 2.0;
    // Upper line: months (Days, Weeks) or years (Months), bold, the label kept at the left edge
    // while its period is in view.
    font.setBold(true);
    painter.setFont(font);
    const auto upper = [&](const QDate &start, const QDate &next, const QString &label) {
        const double left = x(start), right = x(next);
        painter.setPen(m_colors.headerLine);
        painter.drawLine(QPointF(left, 0), QPointF(left, half));
        painter.setPen(m_colors.text);
        const double textLeft = std::max(left, 0.0) + 6;
        painter.drawText(QRectF(textLeft, 0, std::max(0.0, right - textLeft), half), Qt::AlignLeft | Qt::AlignVCenter, label);
    };
    if (m_scale == Scale::Months) {
        for (QDate year(firstDay.year(), 1, 1); year <= lastDay; year = year.addYears(1))
            upper(year, year.addYears(1), QString::number(year.year()));
    } else {
        for (QDate month(firstDay.year(), firstDay.month(), 1); month <= lastDay; month = month.addMonths(1))
            upper(month, month.addMonths(1), monthName(month.month(), QLocale::LongFormat) + QLatin1Char(' ') + QString::number(month.year()));
    }
    font.setBold(false);
    painter.setFont(font);
    // Lower line: days, ISO weeks or months, in the text colour; non-working days shaded as in the
    // chart.
    const auto lower = [&](const QDate &start, int days, const QString &label) {
        const double left = x(start);
        if (m_scale == Scale::Days && (start.dayOfWeek() >= Qt::Saturday || !m_plan.calendar().isWorkingDay(start)))
            painter.fillRect(QRectF(left, half, dw, half - 1), m_colors.shade);
        painter.setPen(m_colors.headerLine);
        painter.drawLine(QPointF(left, half), QPointF(left, kHeaderHeight));
        painter.setPen(m_colors.text);
        painter.drawText(QRectF(left, half, double(days) * dw, half), Qt::AlignCenter, label);
    };
    switch (m_scale) {
    case Scale::Days:
        for (QDate day = firstDay; day <= lastDay; day = day.addDays(1)) lower(day, 1, QString::number(day.day()));
        break;
    case Scale::Weeks:
        for (QDate week = firstDay.addDays(1 - firstDay.dayOfWeek()); week <= lastDay; week = week.addDays(7))
            lower(week, 7, tr("W%1").arg(week.weekNumber()));
        break;
    case Scale::Months:
        for (QDate month(firstDay.year(), firstDay.month(), 1); month <= lastDay; month = month.addMonths(1))
            lower(month, month.daysInMonth(), monthName(month.month(), QLocale::ShortFormat));
        break;
    }
}

GanttBarPaint GanttChart::barPaint(const GanttPlan &plan, const QString &id, const GanttBarLook *look, bool summary) {
    const GanttPlan::BarKind kind = plan.barKind(id);
    const QColor fallback = defaultBarColor(plan, kind);
    GanttBarPaint paint;
    paint.fill = look ? look->fill : fallback;
    paint.border = look ? look->border : fallback;
    paint.text = look                                   ? look->text
                 : plan.defaultColor(kind).isValid()    ? barTextColor(fallback)
                 : summary                              ? kSummaryText
                                                        : kTaskText;
    // Done part: halfway from the fill to the branch colour (a light fill takes the branch's tint);
    // when they are the same, a darker shade; on a topic without fill, the branch colour, light.
    paint.done = paint.border;
    if (paint.fill.alpha() == 0) paint.done.setAlphaF(0.3);
    else if (paint.fill.rgb() == paint.border.rgb()) paint.done = paint.fill.darker(125);
    else paint.done = QColor((paint.fill.red() + paint.border.red()) / 2, (paint.fill.green() + paint.border.green()) / 2,
                             (paint.fill.blue() + paint.border.blue()) / 2);
    return paint;
}

QColor GanttChart::criticalColor() { return kCritical; }
QColor GanttChart::baselineColor() { return kBaselineSame; }
QColor GanttChart::todayColor() { return kToday; }

void GanttChart::paintTask(QPainter &painter, const QString &id, int row) {
    const GanttTask *task = taskOf(id);
    const GanttScheduledTask scheduled = m_schedule.tasks.value(id);
    QRectF bar = barRect(id);
    // Undated: no bar, and no name either, even with "Name on the bar" (user, 2026-10-29: it was
    // drawn at the chart's left edge as a milestone's).
    if (!task) return;
    if (bar.isEmpty()) {  // FEAT-082: the snapshot's side of a task that lost its dates
        paintBaseline(painter, id, defaultBarColor(m_plan, m_plan.barKind(id)));
        return;
    }
    int progress = scheduled.progress;
    // Preview of the drag in progress (the plan changes at release).
    if (id == m_dragId) {
        const double dx = m_dragPos.x() - m_pressPos.x();
        const int dw = dayWidth();
        if (m_drag == Drag::Move) {
            QDate start = scheduled.start.addDays(qint64(std::round(dx / dw)));
            for (int i = 0; i < 7 && !m_plan.calendar().isWorkingDay(start); ++i) start = start.addDays(1);
            bar.translate(double(scheduled.start.daysTo(start)) * dw, 0);
        }
        else if (m_drag == Drag::Resize) bar.setRight(std::max(bar.left() + dw, bar.right() + std::round(dx / dw) * dw));
        else if (m_drag == Drag::Progress) progress = std::clamp(int(std::round((m_dragPos.x() - bar.left()) / bar.width() * 100)), 0, 100);
    }
    // Bars as pills with the name inside: peach tasks and lilac summaries, or the branch
    // colour chosen by hand on the topic (GanttView::updateChart).
    const auto look = m_looks.constFind(id);
    const GanttPlan::BarKind kind = m_plan.barKind(id);
    const GanttBarPaint colors = barPaint(m_plan, id, look != m_looks.cend() ? &*look : nullptr, scheduled.summary);
    const QColor &fill = colors.fill, &border = colors.border, &text = colors.text, &done = colors.done;
    const bool filled = fill.alpha() > 0;
    // FEAT-080: the sheet's border pattern for this kind of bar.
    // FEAT-080: the task's own border and fill, or the sheet's for its kind.
    const GanttPlan::BorderStyle borderStyle = m_plan.borderStyle(*task, kind);
    const QPen outline = borderPen(borderStyle, filled && fill.rgb() == border.rgb() ? intense(fill) : border, 1.5);
    const bool handDrawn = isHandDrawn(borderStyle);
    // FEAT-080: the sheet's fill for this kind; with no fill the done part stays solid, so it shows.
    const NodeFillStyle fillStyle = m_plan.fillStyle(*task, kind);
    const NodeFillStyle doneStyle = fillStyle == NodeFillStyle::Nessuno ? NodeFillStyle::Pieno : fillStyle;
    const QColor body = filled ? fill : m_colors.card;  // no fill on the topic: the card over the grid
    // The done part: the shape filled again in the done colour, up to the progress (the strokes'
    // ends reach out of the shape: a full bar covers them too).
    const auto fillDone = [&](const QPainterPath &shape, const QRectF &box) {
        if (progress <= 0) return;
        constexpr qreal reach = NodeSketch::kPatternReach;
        const qreal right = progress >= GanttPlan::kMaxProgress ? box.right() + reach : box.left() + box.width() * progress / 100.0;
        painter.save();
        painter.setClipRect(QRectF(QPointF(box.left() - reach, box.top() - reach), QPointF(right, box.bottom() + reach)), Qt::IntersectClip);
        fillShape(painter, id, shape, doneStyle, done);
        painter.restore();
    };
    // The critical path is a red halo just outside the bar, so it reads on red branches too; not on
    // summaries (user, 2026-10-28: only the tasks).
    const QPen critical(kCritical, 2);
    QFont font = painter.font();
    font.setPixelSize(12);
    font.setWeight(QFont::Medium);
    painter.setFont(font);
    if (kind == GanttPlan::BarKind::Milestone) {
        const GanttPlan::MilestoneShape shape = milestoneShapeOf(id);
        painter.setBrush(Qt::NoBrush);
        if (m_selected.contains(id)) {
            painter.setPen(QPen(kSelectedBar, 1.5));
            painter.drawPath(milestonePath(shape, bar, 2));
        } else if (scheduled.critical && criticalPainted()) {
            painter.setPen(critical);
            painter.drawPath(milestonePath(shape, bar, 3));
        }
        // Done (its checkbox ticked): the colour of a bar's done part (user, 2026-10-29).
        const QPainterPath path = milestonePath(shape, bar);
        if (progress >= GanttPlan::kMaxProgress) fillShape(painter, id, path, doneStyle, done);
        else fillShape(painter, id, path, fillStyle, body);
        painter.setBrush(Qt::NoBrush);
        strokeBorder(painter, id, path, outline, handDrawn);
        if (m_labelled.contains(id)) {
            painter.setPen(m_colors.text);
            painter.drawText(QRectF(bar.right() + 6, rowTop(row), 600, kRowHeight), Qt::AlignLeft | Qt::AlignVCenter, task->name);
        }
    } else if (const GanttPlan::SummaryShape summaryShape = summaryShapeOf(id); scheduled.summary && summaryShape != GanttPlan::SummaryShape::Bar) {
        const QRectF place = summaryPlace(id, bar);
        const QPainterPath shape = summaryPath(summaryShape, place);
        if (m_selected.contains(id))  // under the shape: 1.5 px of blue around its outline
            painter.strokePath(shape, QPen(kSelectedBar, 4.5, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        fillShape(painter, id, shape, fillStyle, body);
        fillDone(shape, place);
        QPen pen = outline;
        if (!handDrawn) {  // the shape's sharp tips (a hand-drawn line keeps its round ends)
            pen.setCapStyle(Qt::FlatCap);
            pen.setJoinStyle(Qt::MiterJoin);
        }
        strokeBorder(painter, id, shape, pen, handDrawn);
        if (summaryShape == GanttPlan::SummaryShape::Frame) {  // the name inside the header, as on a bar
            const QString name = m_labelled.contains(id) ? barLabel(task->name, font, place.width() - 16) : QString();
            if (!name.isEmpty()) {
                painter.setPen(text);
                painter.drawText(place.adjusted(8, 0, -8, 0), Qt::AlignCenter, name);
            }
        } else if (m_labelled.contains(id)) {  // too thin for text: the name after it, as a milestone's
            painter.setPen(m_colors.text);
            painter.drawText(QRectF(place.right() + 6, rowTop(row), 600, kRowHeight), Qt::AlignLeft | Qt::AlignVCenter, task->name);
        }
    } else {
        const GanttPlan::BarShape barShape = barShapeOf(id);
        const QPainterPath shape = taskPath(barShape, bar);
        fillShape(painter, id, shape, fillStyle, body);
        fillDone(shape, bar);
        strokeBorder(painter, id, shape, outline, handDrawn);
        if (m_selected.contains(id))  // the selected bar: a blue ring in place of the critical halo
            painter.strokePath(taskPath(barShape, bar, 2), QPen(kSelectedBar, 1.5));
        else if (scheduled.critical && !scheduled.summary && criticalPainted())
            painter.strokePath(taskPath(barShape, bar, 3), critical);
        // Out of the points: on the Arrow's end, on both of the Hexagon's; half the Parallelogram's slant.
        const double tip = barTip(barShape, bar);
        const double start = barShape == GanttPlan::BarShape::Hexagon ? tip : barShape == GanttPlan::BarShape::Parallelogram ? tip / 2 : 0;
        const double end = barShape == GanttPlan::BarShape::Parallelogram ? tip / 2 : tip;
        const QRectF room = bar.adjusted(8 + start, 0, -8 - end, 0);
        const QString name = m_labelled.contains(id) ? barLabel(task->name, font, room.width()) : QString();
        if (!name.isEmpty()) {
            painter.setPen(text);
            painter.drawText(room, Qt::AlignCenter, name);
        }
    }

    // FEAT-082: over the bar, so the snapshot's outline shows inside a longer bar too.
    paintBaseline(painter, id, filled && fill.rgb() == border.rgb() ? intense(fill) : border);
    // The bar under the mouse: link circles at both ends, progress badge with the percentage.
    const bool hovered = id == m_hovered && !scheduled.summary && m_drag == Drag::None;
    if (hovered) {
        painter.setPen(QPen(kArrow, 1.5));
        painter.setBrush(m_colors.card);
        painter.drawEllipse(linkHandle(id), kHandleRadius, kHandleRadius);
        painter.drawEllipse(linkHandleAtStart(id), kHandleRadius, kHandleRadius);
    }
    if ((hovered || (id == m_dragId && m_drag == Drag::Progress)) && !isMilestone(id)) {
        const QRectF badge = progressBadge(id, progress);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_colors.badge);
        painter.drawRoundedRect(badge, badge.height() / 2, badge.height() / 2);
        QFont small = font;
        small.setPixelSize(10);
        small.setBold(true);
        painter.setFont(small);
        painter.setPen(m_colors.badgeText);
        painter.drawText(badge, Qt::AlignCenter, QString::number(progress));
    }
}

bool GanttChart::isHandDrawn(GanttPlan::BorderStyle style) {
    return style == GanttPlan::BorderStyle::HandDrawnSolid || style == GanttPlan::BorderStyle::HandDrawnDashed;
}

void GanttChart::strokeBorder(QPainter &painter, const QString &id, const QPainterPath &shape, const QPen &pen, bool handDrawn) {
    if (pen.style() == Qt::NoPen) return;
    if (!handDrawn) {
        painter.strokePath(shape, pen);
        return;
    }
    // The map's hand-drawn border (NodeSketch::roughOutline, seed from the id: the same stroke at
    // every repaint), kept with the fill's strokes and rebuilt only when the outline changes.
    const QPointF origin = shape.boundingRect().topLeft();
    const QPainterPath local = shape.translated(-origin);
    Sketch &sketch = m_sketches[id];
    if (sketch.borderOutline != local) {
        sketch.borderOutline = local;
        sketch.border = NodeSketch::roughOutline(local, NodeSketch::seedFor(id));
    }
    painter.save();
    painter.translate(origin);
    painter.strokePath(sketch.border, pen);
    painter.restore();
}

void GanttChart::fillShape(QPainter &painter, const QString &id, const QPainterPath &shape, NodeFillStyle style, const QColor &color) {
    if (style == NodeFillStyle::Nessuno) return;
    if (style == NodeFillStyle::Pieno) {
        painter.fillPath(shape, color);
        return;
    }
    const QPointF origin = shape.boundingRect().topLeft();
    const QPainterPath local = shape.translated(-origin);
    Sketch &sketch = m_sketches[id];
    if (sketch.style != style || sketch.outline != local) {
        const quint64 seed = NodeSketch::seedFor(id);  // the same strokes at every repaint
        sketch.outline = local;
        sketch.style = style;
        sketch.area = {};
        sketch.pattern = {};
        if (style == NodeFillStyle::PienoManoLibera) sketch.area = NodeSketch::roughArea(local, seed);
        else sketch.pattern = NodeSketch::pattern(style, local, seed);
    }
    painter.save();
    painter.translate(origin);
    if (style == NodeFillStyle::PienoManoLibera) {
        painter.fillPath(sketch.area, color);
    } else {  // opaque strokes: where they cross they do not get darker (as on the map)
        painter.setPen(QPen(NodeSketch::opaqueOver(color, m_colors.card), sketch.pattern.penWidth, Qt::SolidLine, Qt::FlatCap));
        painter.drawLines(sketch.pattern.lines);
    }
    painter.restore();
}

QPainterPath GanttChart::milestonePath(GanttPlan::MilestoneShape shape, const QRectF &box, qreal grow) {
    using Shape = GanttPlan::MilestoneShape;
    QPainterPath path;
    if (shape == Shape::Diamond) {
        const QRectF r = box.adjusted(-grow, -grow, grow, grow);
        const QPointF c = r.center();
        path.addPolygon(QPolygonF{{c.x(), r.top()}, {r.right(), c.y()}, {c.x(), r.bottom()}, {r.left(), c.y()}});
        path.closeSubpath();
        return path;
    }
    const qreal side = box.height() + 2 * grow;
    const QRectF square(box.center().x() - side / 2, box.center().y() - side / 2, side, side);
    switch (shape) {
    case Shape::Triangle:  // the tip down on its day
        path.addPolygon(QPolygonF{square.topLeft(), square.topRight(), {square.center().x(), square.bottom()}});
        path.closeSubpath();
        return path;
    case Shape::Circle: return NodeShapes::outline(NodeShape::Cerchio, square);
    case Shape::Star: return NodeShapes::outline(NodeShape::Stella, square);
    case Shape::Hexagon: return NodeShapes::outline(NodeShape::Esagono, square);
    case Shape::Heart: return NodeShapes::outline(NodeShape::Cuore, square);
    case Shape::Flag: return NodeShapes::outline(NodeShape::Bandiera, square);
    case Shape::Gem: return NodeShapes::outline(NodeShape::Gemma, square);
    case Shape::Diamond: break;
    }
    return path;
}

QPen GanttChart::borderPen(GanttPlan::BorderStyle style, const QColor &color, qreal width) {
    switch (style) {
    case GanttPlan::BorderStyle::Dashed: return QPen(color, width, Qt::DashLine, Qt::FlatCap);
    // The map's hand-drawn borders: round caps and joins.
    case GanttPlan::BorderStyle::HandDrawnSolid: return QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    case GanttPlan::BorderStyle::HandDrawnDashed: return QPen(color, width, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin);
    case GanttPlan::BorderStyle::Dotted: return QPen(color, width, Qt::DotLine, Qt::FlatCap);
    case GanttPlan::BorderStyle::None: return QPen(Qt::NoPen);
    case GanttPlan::BorderStyle::Solid: break;
    }
    return QPen(color, width);
}

Qt::PenStyle GanttChart::linkPenStyle(GanttPlan::LinkLine line) {
    switch (line) {
    case GanttPlan::LinkLine::Dashed: return Qt::DashLine;
    case GanttPlan::LinkLine::Dotted: return Qt::DotLine;
    case GanttPlan::LinkLine::Solid: break;
    }
    return Qt::SolidLine;
}

qreal GanttChart::linkWidthPx(GanttPlan::LinkWidth width) {
    switch (width) {
    case GanttPlan::LinkWidth::Medium: return 2;
    case GanttPlan::LinkWidth::Bold: return 3;
    case GanttPlan::LinkWidth::ExtraBold: return 4;  // more covers the 24 px bars
    case GanttPlan::LinkWidth::Thin: break;
    }
    return 1.25;
}

qreal GanttChart::linkEndSizePx(GanttPlan::LinkEndSize size) {
    switch (size) {
    case GanttPlan::LinkEndSize::Medium: return 9;
    case GanttPlan::LinkEndSize::Large: return 12;
    case GanttPlan::LinkEndSize::Small: break;
    }
    return BranchPath::kMinEndMarkPx;  // 6, the map's smallest mark
}

// Elbow line from the predecessor's side to the successor's side named by the type, with the head.
QPolygonF GanttChart::arrowPath(int d) const {
    const GanttDependency &dependency = m_plan.dependencies().at(d);
    const QRectF from = barRect(dependency.from), to = barRect(dependency.to);
    if (from.isEmpty() || to.isEmpty()) return {};
    using Type = GanttDependency::Type;
    const bool fromEnd = dependency.type == Type::FinishStart || dependency.type == Type::FinishFinish;
    const bool toStart = dependency.type == Type::FinishStart || dependency.type == Type::StartStart;
    const QPointF a(fromEnd ? from.right() : from.left(), from.center().y());
    const QPointF b(toStart ? to.left() : to.right(), to.center().y());
    // The last segment is 8 px, longer for a large end mark so the mark does not cover the turn.
    const double stub = std::max(8.0, linkEndSizePx(m_plan.linkEndSize()) + 2);
    const double out = fromEnd ? 8 : -8, in = toStart ? -stub : stub;
    const QPointF a1(a.x() + out, a.y()), b1(b.x() + in, b.y());
    QPolygonF path;
    path << a;
    if (out * in > 0) {
        const double side = out > 0 ? std::max(a1.x(), b1.x()) : std::min(a1.x(), b1.x());
        path << QPointF(side, a.y()) << QPointF(side, b.y()) << b;
        return path;
    }
    // Rows apart: the turn goes in the gap just after the predecessor's row and the line runs down
    // beside the successor, so the arrows of one predecessor do not share a line
    // (user, 2026-10-28); beside the predecessor only when that crosses fewer bars of the rows in
    // between. Adjacent rows: down beside the predecessor, straight or turning just before the
    // successor's row.
    const bool straight = (out > 0 && a1.x() <= b1.x()) || (out < 0 && a1.x() >= b1.x());
    const double step = a.y() < b.y() ? kRowHeight / 2.0 : -kRowHeight / 2.0;
    const double nearB = b.y() - step, nearA = a.y() + step;
    const int fromRow = m_rowOf.value(dependency.from), toRow = m_rowOf.value(dependency.to);
    if (std::abs(toRow - fromRow) > 1 && barsCrossed(b1.x(), fromRow, toRow) <= barsCrossed(a1.x(), fromRow, toRow))
        path << a1 << QPointF(a1.x(), nearA) << QPointF(b1.x(), nearA) << b1;
    else if (straight)
        path << a1 << QPointF(a1.x(), b.y());
    else
        path << a1 << QPointF(a1.x(), nearB) << QPointF(b1.x(), nearB) << b1;
    path << b;
    return path;
}

// Bars of the rows strictly between rowA and rowB (either order) that a vertical line at x crosses.
int GanttChart::barsCrossed(double x, int rowA, int rowB) const {
    int crossed = 0;
    for (int row = std::min(rowA, rowB) + 1; row < std::max(rowA, rowB); ++row) {
        const QRectF bar = barRect(m_rows.at(row));
        if (!bar.isEmpty() && bar.left() <= x && x <= bar.right()) ++crossed;
    }
    return crossed;
}

void GanttChart::paintArrows(QPainter &painter) {
    // Only arrows that can cross the visible rows: an end in view, or one above and one below.
    const int first = std::max(0, rowAt(kHeaderHeight)), last = first + viewport()->height() / kRowHeight + 1;
    for (int d = 0; d < m_plan.dependencies().size(); ++d) {
        if (m_schedule.ignoredDependencies.contains(d)) continue;
        const int from = m_rowOf.value(m_plan.dependencies().at(d).from, -1), to = m_rowOf.value(m_plan.dependencies().at(d).to, -1);
        if (from < 0 || to < 0 || (from < first && to < first) || (from > last && to > last)) continue;
        const QPolygonF path = arrowPath(d);
        if (path.size() < 2) continue;
        const bool critical = criticalPainted() && m_schedule.criticalDependencies.contains(d);
        const QColor color = critical ? kCritical : kArrow;
        const qreal width = linkWidthPx(m_plan.linkWidth()) + (critical ? 0.75 : 0);
        const QPointF tip = path.last(), before = path.at(path.size() - 2);
        const double direction = tip.x() >= before.x() ? 1 : -1;
        const BranchEnd end = m_plan.linkEnd();
        const qreal size = linkEndSizePx(m_plan.linkEndSize());
        // The line stops at the base of a filled mark and half its width before a bare or open
        // tip, so a thick line's cap does not show past the mark.
        const bool filled = end != BranchEnd::None && !BranchPath::endIsStroke(end);
        QPolygonF line = path;
        line.last().rx() -= direction * (filled ? size : width / 2);
        // FEAT-080: the sheet's pattern (flat caps keep the dots apart) and end mark.
        const Qt::PenStyle style = linkPenStyle(m_plan.linkLine());
        painter.setPen(QPen(color, width, style, style == Qt::SolidLine ? Qt::SquareCap : Qt::FlatCap));
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(line);
        if (end == BranchEnd::FilledArrow) {  // the chart's own triangle, 6 x 8 px when small
            const qreal half = size * 2 / 3;
            QPolygonF head;
            head << tip << QPointF(tip.x() - size * direction, tip.y() - half) << QPointF(tip.x() - size * direction, tip.y() + half);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            painter.drawPolygon(head);
        } else if (end != BranchEnd::None) {  // the map's marks, size long (endMark: 3 x its line width)
            const bool stroke = BranchPath::endIsStroke(end);
            painter.setPen(stroke ? QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin) : QPen(Qt::NoPen));
            painter.setBrush(stroke ? QBrush() : QBrush(color));
            painter.drawPath(BranchPath::endMark(end, tip, QPointF(direction, 0), size / 3));
        }
    }
}

void GanttChart::keyPressEvent(QKeyEvent *event) {
    // Up/Down move between the tasks as in the table, not the scroll bar (user, 2026-10-29).
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && !(event->modifiers() & Qt::AltModifier)) {
        emit stepRequested(event->key(), event->modifiers());
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

int GanttChart::dependencyAt(const QPointF &pos) const {
    for (int d = 0; d < m_plan.dependencies().size(); ++d) {
        const QPolygonF path = arrowPath(d);
        for (int i = 1; i < path.size(); ++i) {
            const QRectF segment = QRectF(path.at(i - 1), path.at(i)).normalized().adjusted(-4, -4, 4, 4);
            if (segment.contains(pos)) return d;
        }
    }
    return -1;
}

// ---- mouse

void GanttChart::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    const QPointF pos = event->position();
    m_pressPos = m_dragPos = pos;
    m_drag = Drag::None;
    m_dragId.clear();
    // The progress badge hangs under its bar, partly over the next row.
    if (const GanttTask *hovered = taskOf(m_hovered); hovered && !isMilestone(m_hovered)
        && progressBadge(m_hovered).adjusted(-2, -2, 2, 2).contains(pos)) {
        m_drag = Drag::Progress;
        m_dragId = m_hovered;
        return;
    }
    const int row = rowAt(pos.y());
    if (row < 0) return;
    const QString id = m_rows.at(row);
    emit taskClicked(id, event->modifiers());
    const GanttScheduledTask scheduled = m_schedule.tasks.value(id);
    if (scheduled.summary) return;
    const GanttTask *task = taskOf(id);
    const QRectF bar = barRect(id);
    if (!linkHandleAt(id, pos, &m_linkFromStart).isEmpty()) m_drag = Drag::Link;
    else if (task && !task->milestone && progressBadge(id).adjusted(-2, -2, 2, 2).contains(pos)) m_drag = Drag::Progress;
    else if (task && !task->milestone && onResizeGrip(bar, pos) && bar.adjusted(0, -kBarInset, 0, kBarInset).contains(QPointF(bar.center().x(), pos.y()))) m_drag = Drag::Resize;
    else if (barAt(pos) == id) m_drag = Drag::Move;
    if (m_drag != Drag::None) m_dragId = id;
}

void GanttChart::mouseMoveEvent(QMouseEvent *event) {
    const QPointF pos = event->position();
    if (m_drag != Drag::None) {
        m_dragPos = pos;
        if (m_drag == Drag::Link) {
            const QString target = barAt(pos);
            viewport()->setCursor(!target.isEmpty() && target != m_dragId && !linkAllowed(linkFrom(target), linkTo(target)) ? Qt::ForbiddenCursor : Qt::CrossCursor);
        }
        viewport()->update();
        return;
    }
    QString hovered = barAt(pos);
    // The circles and the badge stand outside the bar: they keep it hovered.
    if (hovered.isEmpty() && !m_hovered.isEmpty()
        && (!linkHandleAt(m_hovered, pos).isEmpty() || progressBadge(m_hovered).adjusted(-2, -2, 2, 2).contains(pos)))
        hovered = m_hovered;
    if (hovered != m_hovered) {
        m_hovered = hovered;
        viewport()->update();
    }
    Qt::CursorShape cursor = Qt::ArrowCursor;
    if (!hovered.isEmpty() && !m_schedule.tasks.value(hovered).summary) {
        const QRectF bar = barRect(hovered);
        const GanttTask *task = taskOf(hovered);
        if (!linkHandleAt(hovered, pos).isEmpty()) cursor = Qt::CrossCursor;
        else if (task && !task->milestone && progressBadge(hovered).adjusted(-2, -2, 2, 2).contains(pos)) cursor = Qt::SizeHorCursor;
        else if (task && !task->milestone && onResizeGrip(bar, pos)) cursor = Qt::SizeHorCursor;
        else cursor = Qt::OpenHandCursor;
    }
    viewport()->setCursor(cursor);
}

void GanttChart::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || m_drag == Drag::None) return;
    finishDrag(event->position());
}

void GanttChart::finishDrag(const QPointF &pos) {
    const Drag drag = m_drag;
    const QString id = m_dragId;
    m_drag = Drag::None;
    m_dragId.clear();
    viewport()->unsetCursor();
    viewport()->update();
    const double dx = pos.x() - m_pressPos.x();
    if (!edit || (drag != Drag::Link && std::abs(dx) < kDragThreshold)) return;
    const GanttScheduledTask scheduled = m_schedule.tasks.value(id);
    switch (drag) {
    case Drag::Move: {
        const int days = int(std::round(dx / dayWidth()));
        // A task placed by a dependency changes that dependency's lag (FEAT-074); another dependency
        // may still hold it where it is: then nothing changes and no step is recorded.
        if (days != 0)
            edit([&](GanttPlan &plan) {
                return plan.moveTo(id, scheduled.start.addDays(days)) && plan.schedule().tasks.value(id).start != scheduled.start;
            });
        break;
    }
    case Drag::Resize: {
        const QDate end = scheduled.end.addDays(qint64(std::round(dx / dayWidth())));
        const int duration = std::clamp(m_plan.workingDaysBetween(scheduled.start, end), 1, GanttPlan::kMaxDuration);
        edit([&](GanttPlan &plan) { return plan.setDuration(id, duration); });
        break;
    }
    case Drag::Progress: {
        const QRectF bar = barRect(id);
        const int progress = std::clamp(int(std::round((pos.x() - bar.left()) / bar.width() * 100)), 0, 100);
        edit([&](GanttPlan &plan) { return plan.setProgress(id, progress); });
        break;
    }
    case Drag::Link: {
        const QString target = barAt(pos);
        if (target.isEmpty() || target == id) break;
        GanttDependency dependency;  // from the end circle: id comes first; from the start circle: target
        dependency.from = m_linkFromStart ? target : id;
        dependency.to = m_linkFromStart ? id : target;
        edit([&](GanttPlan &plan) { return plan.addDependency(dependency); });
        break;
    }
    case Drag::None: break;
    }
}

void GanttChart::wheelEvent(QWheelEvent *event) {
    // Ctrl+wheel: the zoom steps of the window's select, the date under the mouse kept.
    if (event->modifiers() & Qt::ControlModifier) {
        static constexpr int kSteps[] = {50, 75, 100, 125, 150, 200, 300};
        const int delta = event->angleDelta().y();
        int next = m_zoom;
        if (delta > 0) {
            for (int step : kSteps)
                if (step > m_zoom) { next = step; break; }
        } else if (delta < 0) {
            for (int step : kSteps)
                if (step < m_zoom) next = step;
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
    if (const int d = dependencyAt(pos); d >= 0 && barAt(pos).isEmpty()) {
        QMenu *menu = dependencyMenu(d, this);
        menu->exec(event->globalPos());
        delete menu;
        return;
    }
    if (const int row = rowAt(pos.y()); row >= 0) emit rowMenuRequested(m_rows.at(row), event->globalPos());
}

QRectF GanttChart::summaryPlace(const QString &id, const QRectF &box) const {
    if (summaryShapeOf(id) != GanttPlan::SummaryShape::Frame)
        return {box.left(), box.center().y() - kBracketHeight / 2, box.width(), kBracketHeight};
    // The group frame's header fills its row from the bars' top (user, 2026-11-01), so its name fits.
    const int row = m_rowOf.value(id, -1);
    const double reach = kFrameGap + kFrameStep * m_frames.value(row).levels;
    const double top = row < 0 ? box.top() : rowTop(row) + kBarInset, bottom = row < 0 ? box.bottom() : rowTop(row) + kRowHeight;
    return {box.left() - reach, top, box.width() + 2 * reach, bottom - top};
}

void GanttChart::updateFrames() {
    m_frames.clear();
    // The rows are in the table's order: the rows shown of a summary's subtasks follow it. open holds
    // the summaries whose rows are still going on, outermost first; levels counts the group frames
    // inside each (a summary of another shape passes on its own).
    QHash<int, SummaryFrame> spans;
    QList<int> open;
    const auto close = [this, &open, &spans](int last) {
        const int row = open.takeLast();
        const SummaryFrame span{last, spans.value(row).levels};
        const bool frame = summaryShapeOf(m_rows.at(row)) == GanttPlan::SummaryShape::Frame;
        if (frame) m_frames.insert(row, span);
        if (!open.isEmpty()) {
            int &outer = spans[open.last()].levels;
            outer = std::max(outer, span.levels + (frame ? 1 : 0));
        }
    };
    for (int row = 0; row < m_rows.size(); ++row) {
        const QString &id = m_rows.at(row);
        const GanttTask *task = taskOf(id);
        const QString parent = task ? task->parentId : QString();
        while (!open.isEmpty() && m_rows.at(open.last()) != parent) close(row - 1);
        if (m_schedule.tasks.value(id).summary) {
            open.append(row);
            spans.insert(row, {row, 0});
        }
    }
    while (!open.isEmpty()) close(int(m_rows.size()) - 1);
}

void GanttChart::paintFrames(QPainter &painter, int firstRow, int lastRow) {
    painter.save();
    for (auto it = m_frames.cbegin(); it != m_frames.cend(); ++it) {
        const int row = it.key();
        const SummaryFrame &frame = it.value();
        if (frame.last <= row || frame.last < firstRow || row > lastRow) continue;  // the header only, or out of view
        const QString &id = m_rows.at(row);
        const QRectF bar = barRect(id);
        if (bar.isEmpty()) continue;
        const QRectF header = summaryPlace(id, bar);
        // Under the last subtask's bar, at most to the next row's bar.
        const double lastRowBottom = rowTop(frame.last) + kRowHeight;
        const double bottom = std::min(lastRowBottom - kBarInset + kFrameBelow + kFrameStep * frame.levels, lastRowBottom + kBarInset - 1);
        const auto look = m_looks.constFind(id);
        const GanttBarPaint colors = barPaint(m_plan, id, look != m_looks.cend() ? &*look : nullptr, true);
        QColor tint = colors.fill.alpha() > 0 ? colors.fill : colors.border;
        tint.setAlphaF(kFrameTint);
        const QColor line = m_selected.contains(id) ? kSelectedBar
                            : colors.fill.alpha() > 0 && colors.fill.rgb() == colors.border.rgb() ? intense(colors.fill)
                                                                                                 : colors.border;
        painter.setPen(QPen(line, 1.5, Qt::DashLine, Qt::FlatCap));
        painter.setBrush(tint);
        painter.drawRoundedRect(QRectF(header.topLeft(), QPointF(header.right(), bottom)), kFrameRadius, kFrameRadius);
    }
    painter.restore();
}

QPainterPath GanttChart::summaryPath(GanttPlan::SummaryShape shape, const QRectF &rect) {
    // Sizes of the 16 px place (FEAT-080), scaled to rect; the ends shrink on short summaries so
    // the shape never goes past its days.
    const double unit = rect.height() / 16, width = rect.width();
    const double left = rect.left(), right = rect.right(), top = rect.top(), bottom = rect.bottom();
    const double middle = rect.center().x(), centre = rect.center().y();
    QPainterPath path;
    const auto polygon = [&path](std::initializer_list<QPointF> points) {
        path.addPolygon(QPolygonF(QList<QPointF>(points)));
        path.closeSubpath();
    };
    switch (shape) {
    case GanttPlan::SummaryShape::Bar:
        path.addRect(rect);
        break;
    case GanttPlan::SummaryShape::Bracket: {
        const double band = 10 * unit, tip = std::min(8 * unit, width / 2);
        polygon({rect.topLeft(), rect.topRight(), rect.bottomRight(), {right - tip, top + band}, {left + tip, top + band}, rect.bottomLeft()});
        break;
    }
    case GanttPlan::SummaryShape::Brace: {  // an overbrace: the tip up in the middle, the ends curling down
        const double curl = std::min(6 * unit, width / 4), band = std::min(4 * unit, curl);
        const double upper = top + 6 * unit, lower = upper + band;
        path.moveTo(left, bottom);
        path.quadTo(left, upper, left + curl, upper);
        path.lineTo(middle - curl, upper);
        path.quadTo(middle, upper, middle, top);
        path.quadTo(middle, upper, middle + curl, upper);
        path.lineTo(right - curl, upper);
        path.quadTo(right, upper, right, bottom);
        path.lineTo(right - band, bottom);
        path.quadTo(right - band, lower, right - curl, lower);
        path.lineTo(middle + curl, lower);
        path.quadTo(middle, lower, middle, top + 4 * unit);
        path.quadTo(middle, lower, middle - curl, lower);
        path.lineTo(left + curl, lower);
        path.quadTo(left + band, lower, left + band, bottom);
        path.closeSubpath();
        break;
    }
    case GanttPlan::SummaryShape::IBeam: {  // a band between two bars as tall as the place
        const double cap = std::min(3 * unit, width / 4), half = 3 * unit;
        polygon({{left, top}, {left + cap, top}, {left + cap, centre - half}, {right - cap, centre - half}, {right - cap, top}, {right, top},
                 {right, bottom}, {right - cap, bottom}, {right - cap, centre + half}, {left + cap, centre + half}, {left + cap, bottom}, {left, bottom}});
        break;
    }
    case GanttPlan::SummaryShape::Hook: {  // the bracket with round ends
        const double corner = std::min(8 * unit, width / 4), band = std::min(4 * unit, corner);
        path.moveTo(left, bottom);
        path.lineTo(left, top + corner);
        path.quadTo(left, top, left + corner, top);
        path.lineTo(right - corner, top);
        path.quadTo(right, top, right, top + corner);
        path.lineTo(right, bottom);
        path.lineTo(right - band, bottom);
        path.lineTo(right - band, top + corner);
        path.quadTo(right - band, top + band, right - corner, top + band);
        path.lineTo(left + corner, top + band);
        path.quadTo(left + band, top + band, left + band, top + corner);
        path.lineTo(left + band, bottom);
        path.closeSubpath();
        break;
    }
    case GanttPlan::SummaryShape::Dimension: {  // a band with an arrowhead at each end
        const double head = std::min(10 * unit, width / 2), half = 3 * unit;
        polygon({{left, centre}, {left + head, top}, {left + head, centre - half}, {right - head, centre - half}, {right - head, top}, {right, centre},
                 {right - head, bottom}, {right - head, centre + half}, {left + head, centre + half}, {left + head, bottom}});
        break;
    }
    case GanttPlan::SummaryShape::Arrow: {  // pointed at the end, notched at the start
        const double point = std::min(8 * unit, width / 2), notch = std::min(8 * unit, width / 4);
        polygon({rect.topLeft(), {right - point, top}, {right, centre}, {right - point, bottom}, rect.bottomLeft(), {left + notch, centre}});
        break;
    }
    case GanttPlan::SummaryShape::Rail: {  // a band between two pins, one outline for the border
        const double radius = std::min(5 * unit, width / 4), half = 2 * unit;
        QPainterPath band, pins;
        band.addRect(QRectF(left + radius, centre - half, std::max(0.0, width - 2 * radius), 2 * half));
        pins.addEllipse(QPointF(left + radius, centre), radius, radius);
        pins.addEllipse(QPointF(right - radius, centre), radius, radius);
        path = band.united(pins);
        break;
    }
    case GanttPlan::SummaryShape::Frame: {  // the frame's header: rounded at the top, as the frame
        const double radius = std::min({kFrameRadius, width / 2, rect.height() / 2});
        path.moveTo(left, bottom);
        path.lineTo(left, top + radius);
        path.arcTo(QRectF(left, top, 2 * radius, 2 * radius), 180, -90);
        path.lineTo(right - radius, top);
        path.arcTo(QRectF(right - 2 * radius, top, 2 * radius, 2 * radius), 90, -90);
        path.lineTo(right, bottom);
        path.closeSubpath();
        break;
    }
    }
    return path;
}

QString GanttChart::typeName(GanttDependency::Type type) {
    switch (type) {
    case GanttDependency::Type::StartStart: return tr("Start to start");
    case GanttDependency::Type::FinishFinish: return tr("Finish to finish");
    case GanttDependency::Type::StartFinish: return tr("Start to finish");
    case GanttDependency::Type::FinishStart: break;
    }
    return tr("Finish to start");
}

QString GanttChart::dependencyLabel(int dependency) const {
    const GanttDependency &link = m_plan.dependencies().at(dependency);
    if (link.lag == 0) return typeName(link.type);
    if (link.lag > 0) return tr("%1, %n day(s) later", nullptr, link.lag).arg(typeName(link.type));
    return tr("%1, %n day(s) earlier", nullptr, -link.lag).arg(typeName(link.type));
}

bool GanttChart::setDependency(const GanttDependency &dependency) {
    return edit && edit([&](GanttPlan &plan) { return plan.updateDependency(dependency); });
}

QMenu *GanttChart::dependencyMenu(int dependency, QWidget *parent) {
    auto *menu = new QMenu(parent);
    menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setStyleSheet(menuStyle);
    const GanttDependency link = m_plan.dependencies().at(dependency);
    for (const auto type : {GanttDependency::Type::FinishStart, GanttDependency::Type::StartStart,
                            GanttDependency::Type::FinishFinish, GanttDependency::Type::StartFinish}) {
        QAction *action = menu->addAction(typeName(type));
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
        const QString id = barAt(help->pos());
        const GanttTask *task = taskOf(id);
        if (const int d = id.isEmpty() ? dependencyAt(help->pos()) : -1; d >= 0) {
            QToolTip::showText(help->globalPos(), dependencyLabel(d), viewport());
            return true;
        }
        if (!task) {
            QToolTip::hideText();
            return true;
        }
        const GanttScheduledTask scheduled = m_schedule.tasks.value(id);
        const int days = scheduled.summary ? m_plan.workingDaysBetween(scheduled.start, scheduled.end) : task->length();
        QToolTip::showText(help->globalPos(),
                           tr("%1\n%2 – %3\n%n working day(s)", nullptr, days).arg(task->name, shortDate(scheduled.start), shortDate(scheduled.end))
                               + QLatin1Char('\n') + tr("Float: %n day(s)", nullptr, std::max(0, scheduled.totalFloat)),
                           viewport());
        return true;
    }
    if (event->type() == QEvent::Leave && m_drag == Drag::None && !m_hovered.isEmpty()) {
        m_hovered.clear();
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
    m_tableModel->rename = [this](const QString &id, const QString &name) { return renameTask(id, name); };

    m_table = new GanttTableView;
    m_table->setModel(m_tableModel);
    m_table->setItemDelegate(new GanttDelegate(m_table));
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
    for (int column : {Start, End}) m_table->header()->resizeSection(column, 96);
    m_table->header()->resizeSection(Duration, 72);
    m_table->header()->resizeSection(Progress, 52);
    m_table->header()->resizeSection(Priority, 96);  // "Molto bassa" whole
    m_table->header()->resizeSection(Person, 112);
    m_table->installEventFilter(this);
    // FEAT-078: Name, Start, Duration by default; the header's menu shows the others (app setting).
    const QStringList shown = QSettings().value(QStringLiteral("gantt/columns"), QStringList{QStringLiteral("start"), QStringLiteral("duration")}).toStringList();
    for (int column = Start; column <= Person; ++column)
        m_table->header()->setSectionHidden(column, !shown.contains(columnKey(column)));
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
    const QStringList order = QSettings().value(QStringLiteral("gantt/columnOrder")).toStringList();
    int place = m_table->header()->visualIndex(Extras) + 1;
    for (const QString &key : order) {
        for (int column = Start; column <= Person; ++column)
            if (columnKey(column) == key && m_table->header()->visualIndex(column) >= place)
                m_table->header()->moveSection(m_table->header()->visualIndex(column), place++);
    }
    m_table->header()->setSectionsMovable(true);
    m_table->header()->setFirstSectionMovable(false);
    connect(m_table->header(), &QHeaderView::sectionMoved, this, &GanttView::columnMoved);
    m_table->viewport()->setMouseTracking(true);
    m_table->viewport()->installEventFilter(this);
    m_numberHidden = QSettings().value(QStringLiteral("gantt/numberHidden"), false).toBool();
    m_table->header()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table->header(), &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) { showColumnMenu(m_table->header()->mapToGlobal(pos)); });
    fitColumns();
    connect(m_table->header(), &QHeaderView::sectionResized, this, [this](int column) {
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
    m_panel->rename = [this](const QString &id, const QString &name) { return renameTask(id, name); };
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
    // Summaries left out; on, a checkbox already checked stays checked.
    m_panel->setMilestone = [this](const QStringList &ids, bool milestone) {
        QList<MindNode *> topics;
        for (const QString &id : ids)
            if (MindNode *topic = node(id); topic && topic->children.empty() && (!milestone || topic->task == MindNode::Task::None))
                topics.append(topic);
        if (!m_document->setTask(topics, milestone ? MindNode::Task::Open : MindNode::Task::None)) return false;
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
        return editPlan([ids, part, key](GanttPlan &plan) {
            bool changed = false;
            for (const QString &id : ids) changed = plan.setOwnStyle(id, part, key) || changed;
            return changed;
        });
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
    m_panel->colorField()->themeColors = [this] { return m_document->model()->activePalette; };
    for (int kind = 0; kind < GanttPlan::kBarKinds; ++kind)
        m_panel->defaultColorField(GanttPlan::BarKind(kind))->themeColors = [this] { return m_document->model()->activePalette; };
    connect(m_panel, &GanttTaskPanel::closeRequested, this, &GanttView::closePanel);
    outer->addWidget(m_panel);
    outer->addWidget(pages, 1);
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

GanttColors GanttColors::light() {
    return {QColor(0xec, 0xee, 0xf1), kText, kMuted, kGrid, kLine, kShade, kSelected, kBadge, Qt::white, Qt::white, kHeaderLine};
}

// The outliner's dark page (MainWindow darkOutlineColors): the app's dark tokens, lines in white.
GanttColors GanttColors::dark() {
    using UiTheme::Token;
    return {UiTheme::color(Token::SurfaceSunken), UiTheme::color(Token::Text), UiTheme::color(Token::TextMuted),
            QColor(255, 255, 255, 18), QColor(255, 255, 255, 34), QColor(255, 255, 255, 8),
            QColor(255, 255, 255, 22), QColor(Qt::white), UiTheme::color(Token::Surface), UiTheme::color(Token::Surface),
            QColor(255, 255, 255, 18)};
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
        "QDateEdit,QLineEdit{color:%1;background:%6;border:1px solid %7;border-radius:4px;padding:0 4px;font-size:12px;"
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
    // The default (summaries yes, tasks no) is not stored: only a choice that differs from it.
    QList<std::pair<MindNode *, std::optional<bool>>> labels;
    for (const QString &id : ids.isEmpty() ? selectedIds() : ids)
        if (MindNode *topic = node(id))
            labels.append({topic, shown == !topic->children.empty() ? std::nullopt : std::optional<bool>(shown)});
    return m_document->setBarLabels(labels);
}

void GanttView::selectRange(const QString &from, const QString &to, bool add) {
    const QModelIndex last = m_tableModel->indexOf(to);
    if (!last.isValid()) return;
    // The rows shown, top to bottom, from one end to the other (collapsed children left out).
    QItemSelection selection;
    bool inside = false;
    for (QModelIndex index = m_tableModel->index(0, 0); index.isValid(); index = m_table->indexBelow(index)) {
        const QString id = m_tableModel->id(index);
        const bool end = id == from || id == to;
        if (end || inside) selection.select(index, m_tableModel->indexOf(id, ColumnCount - 1));
        if (end && (inside || from == to || !m_tableModel->indexOf(from).isValid())) break;
        if (end) inside = true;
    }
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
        m_panel->showDefaultColor(kind, defaultBarColor(m_tableModel->plan(), kind), m_tableModel->plan().defaultColor(kind).isValid());
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
    QColor color;
    bool manual = false;
    for (int i = 0; i < targets.size(); ++i) {
        const MindNode *topic = node(targets.at(i));
        const bool own = topic && NodeStyle::hasManualValue(topic, StyleProperty::BranchColor);
        const QColor bar = own ? topic->branchColor : defaultBarColor(plan, plan.barKind(targets.at(i)));
        manual = manual || own;
        if (i == 0) color = bar;
        else if (bar != color) color = QColor();
    }
    m_panel->showBarColor(color, manual);
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
    QStringList keys;
    for (int visual = place; visual < header->count(); ++visual)
        if (const QString key = columnKey(header->logicalIndex(visual)); !key.isEmpty()) keys.append(key);
    QSettings().setValue(QStringLiteral("gantt/columnOrder"), keys);
}

void GanttView::setColumnShown(int column, bool shown) {
    if (column == Number) {
        if (!m_numbered || m_table->header()->isSectionHidden(Number) != shown) return;
        m_numberHidden = !shown;
        m_table->header()->setSectionHidden(Number, !shown);
        QSettings().setValue(QStringLiteral("gantt/numberHidden"), m_numberHidden);
        fitColumns();
        return;
    }
    if (column <= Name || column > Person || m_table->header()->isSectionHidden(column) != shown) return;
    m_table->header()->setSectionHidden(column, !shown);
    fitColumns();
    QStringList keys;
    for (int c = Start; c <= Person; ++c)
        if (!m_table->header()->isSectionHidden(c)) keys.append(columnKey(c));
    QSettings().setValue(QStringLiteral("gantt/columns"), keys);
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

QString GanttView::nextBaselineName() const {
    const QVector<GanttBaseline> &baselines = m_tableModel->plan().baselines();
    for (int n = 1;; ++n) {
        const QString name = tr("Baseline %1").arg(n);
        if (std::none_of(baselines.cbegin(), baselines.cend(), [&name](const GanttBaseline &b) { return b.name == name; })) return name;
    }
}

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
    GanttExportSource source;
    source.sheetName = m_document->sheets().value(m_document->currentSheet()).name;
    source.plan = m_tableModel->plan();
    source.schedule = m_tableModel->schedule();
    // The shown columns, in the header's order; the Extras icons are left out.
    QHeaderView *header = m_table->header();
    QVector<int> columns;
    for (int visual = 0; visual < header->count(); ++visual) {
        const int column = header->logicalIndex(visual);
        if (header->isSectionHidden(column) || column == Extras) continue;
        columns.append(column);
        GanttExportSource::Column entry;
        entry.title = m_tableModel->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString();
        entry.name = column == Name;
        using Kind = GanttExportSource::Column::Kind;
        entry.kind = column == Name       ? Kind::Name
                     : column == Start    ? Kind::Start
                     : column == End      ? Kind::End
                     : column == Duration ? Kind::Duration
                     : column == Progress ? Kind::Progress
                                          : Kind::Text;
        entry.right = column == Duration || column == Progress;
        entry.muted = column == Number;
        source.columns.append(entry);
    }
    std::function<void(const QModelIndex &, int)> visit = [&](const QModelIndex &parent, int depth) {
        for (int r = 0; r < m_tableModel->rowCount(parent); ++r) {
            const QModelIndex index = m_tableModel->index(r, 0, parent);
            GanttExportSource::Row row;
            row.id = m_tableModel->id(index);
            row.depth = depth;
            row.summary = m_tableModel->rowCount(index) > 0;
            for (int column : std::as_const(columns))
                row.cells.append(m_tableModel->index(r, column, parent).data(Qt::DisplayRole).toString().simplified());
            source.rows.append(row);
            visit(index, depth + 1);
        }
    };
    visit({}, 0);
    QSet<QString> labelled;
    std::function<void(const MindNode *)> collect = [&](const MindNode *topic) {
        if (topic->ganttLabel.value_or(!topic->children.empty())) labelled.insert(topic->id);
        if (NodeStyle::hasManualValue(topic, StyleProperty::BranchColor))
            source.looks.insert(topic->id, {topic->branchColor, topic->branchColor, barTextColor(topic->branchColor)});
        for (const auto &child : topic->children) collect(child.get());
    };
    for (const auto &child : m_document->model()->root()->children) collect(child.get());
    source.labelled = labelled;
    source.baseline = m_tableModel->baseline;
    source.comparison = m_tableModel->comparison;
    source.critical = m_chart->criticalShown();
    source.scale = m_chart->scale();
    source.assignments = m_document->model()->ganttAssignments();
    source.people = m_document->resources().people;
    source.currency = m_document->resources().displaySymbol();
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
    QHash<QString, GanttBarLook> looks;
    // The name on the bar: summaries yes, tasks no, unless the topic says otherwise (user, 2026-10-27).
    QSet<QString> labelled;
    std::function<void(const MindNode *)> visit = [&](const MindNode *topic) {
        if (topic->ganttLabel.value_or(!topic->children.empty())) labelled.insert(topic->id);
        if (NodeStyle::hasManualValue(topic, StyleProperty::BranchColor)) {
            const QColor color = topic->branchColor;
            looks.insert(topic->id, {color, color, barTextColor(color)});
        }
        for (const auto &child : topic->children) visit(child.get());
    };
    for (const auto &child : m_document->model()->root()->children) visit(child.get());
    m_chart->setLabelled(labelled);
    m_chart->setBaseline(m_tableModel->baseline, m_tableModel->comparison);
    auto *legend = static_cast<GanttLegend *>(m_legend);
    legend->baseline = m_tableModel->baseline ? m_tableModel->baseline->name : QString();
    legend->text = m_colors.muted;
    legend->update();
    m_chart->setData(m_tableModel->plan(), m_tableModel->schedule(), rows, QSet<QString>(selected.cbegin(), selected.cend()), looks);
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
    MindNode *created = anchor ? (child ? m_document->addChild(anchor) : m_document->addSibling(anchor))
                               : m_document->addChild(nullptr);
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

bool GanttView::renameTask(const QString &id, const QString &name) {
    MindNode *topic = node(id);
    if (!topic || !m_document->setTitle(topic, name)) return false;
    finishMapChange();
    return true;
}

bool GanttView::indentSelected() {
    const QString id = currentId();
    MindNode *topic = node(id);
    if (!topic || !m_document->indentNode(topic)) return false;
    if (topic->parent) m_collapsed.remove(topic->parent->id);
    finishMapChange();
    refresh();
    selectTask(id);
    return true;
}

bool GanttView::outdentSelected() {
    const QString id = currentId();
    MindNode *topic = node(id);
    // A main topic stays one: the central topic has no row.
    if (!topic || !topic->parent || !topic->parent->parent || !m_document->outdentNode(topic)) return false;
    finishMapChange();
    refresh();
    selectTask(id);
    return true;
}

bool GanttView::moveTask(const QString &id, const QString &target, DropPlace place) {
    MindNode *topic = node(id);
    MindNode *anchor = node(target);
    if (!topic || !anchor || topic == anchor) return false;
    MindNode *parent = place == DropPlace::Inside ? anchor : anchor->parent;
    int row = -1;  // inside: its last child
    if (place != DropPlace::Inside && parent) {
        const auto found = std::find_if(parent->children.cbegin(), parent->children.cend(),
                                        [anchor](const auto &child) { return child.get() == anchor; });
        row = int(found - parent->children.cbegin()) + (place == DropPlace::After ? 1 : 0);
    }
    // Cycles, summaries' topics and the same place are refused by the model.
    if (!parent || !m_document->moveNode(topic, parent, row, 0)) return false;
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

QStringList GanttView::chainIds(const GanttPlan &plan) const {
    const QStringList selected = selectedIds();
    QStringList ids;
    for (const QString &id : m_selectionOrder)
        if (selected.contains(id) && plan.task(id) && !plan.hasChildren(id)) ids.append(id);
    return ids;
}

GanttPlan GanttView::currentPlan() const { return m_document->model()->gantt(); }

bool GanttView::chainUnlinks(const GanttPlan &plan) const {
    const QStringList chain = chainIds(plan);
    const QSet<QString> ids(chain.cbegin(), chain.cend());
    const QVector<GanttDependency> &dependencies = plan.dependencies();
    return std::any_of(dependencies.cbegin(), dependencies.cend(),
                       [&](const GanttDependency &d) { return ids.contains(d.from) || ids.contains(d.to); });
}

bool GanttView::chainSelected() {
    const GanttPlan current = m_document->model()->gantt();  // gantt() builds a new plan: keep it alive
    const QStringList ids = chainIds(current);
    // A toggle (user, 2026-10-29): when some selected task has links, all their links are removed;
    // the next click links them in a chain.
    if (chainUnlinks(current)) {
        return editPlan([&](GanttPlan &plan) {
            QVector<QPair<QString, QString>> links;
            for (const GanttDependency &d : plan.dependencies())
                if (ids.contains(d.from) || ids.contains(d.to)) links.append({d.from, d.to});
            bool removed = false;
            for (const auto &[from, to] : links) removed = plan.removeDependency(from, to) || removed;
            return removed;
        });
    }
    if (ids.size() < 2) return false;
    return editPlan([&](GanttPlan &plan) {
        bool linked = false;
        for (int i = 1; i < ids.size(); ++i) {
            GanttDependency dependency;
            dependency.from = ids.at(i - 1);
            dependency.to = ids.at(i);
            linked = plan.addDependency(dependency) || linked;  // existing or cyclic links are skipped
        }
        // User, 2026-10-29: a chain whose first task has no date starts today; the others follow it.
        if (!plan.schedule().tasks.value(ids.first()).dated) linked = plan.setStart(ids.first(), GanttToday::date()) || linked;
        return linked;
    });
}

void GanttView::chooseColor(const QPoint &globalPos) {
    const QStringList ids = selectedIds();
    if (ids.isEmpty()) return;
    const GanttPlan plan = m_document->model()->gantt();  // gantt() builds a new plan: keep it alive
    const GanttTask *first = plan.task(ids.first());
    ColorPicker::Options options;
    const MindNode *firstTopic = node(ids.first());
    options.current = first && firstTopic && NodeStyle::hasManualValue(firstTopic, StyleProperty::BranchColor) ? first->color
                      : defaultBarColor(plan, plan.barKind(ids.first()));
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
