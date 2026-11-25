#include "ganttcanvas.h"

#include "branchpath.h"  // the map's end marks (FEAT-080)
#include "ganttcommands.h"
#include "localization.h"
#include "nodeshape.h"  // the map's node outlines for the milestones (FEAT-080)
#include "nodestyle.h"

#include <QFontMetricsF>
#include <QLineF>
#include <QPainter>

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
// the selected bar has a thin ring 2 px outside it (measured on a scaled picture), in selectionRing.
const QColor kGrid(10, 30, 70, 20);
const QColor kSelected(0xe3, 0xe5, 0xe6);
const QColor kToday(0xe0, 0x78, 0x4c);    // estimated: the reference is a GIF with reduced colours
const QColor kBadge(0x1f, 0x23, 0x26);    // progress badge
const QColor kText(0x1f, 0x29, 0x37);
const QColor kMuted(0x6b, 0x72, 0x80);
// Non-working days and header lines measured on the reference Gantt (user's picture, 2026-10-28): days
// #fafbfc on white (translucent, so a selected row stays a shade darker there), lines #f0f2f4.
const QColor kShade(10, 40, 80, 5);  // = #fafbfc on white
const QColor kHeaderLine(0xf0, 0xf2, 0xf4);
const QColor kLine(15, 23, 42, 28);      // borders of the panel's fields and cards
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

// GanttCanvas::outlineColor for a task already found.
QColor outline(const GanttPlan &plan, const GanttTask *task, GanttPlan::BarKind kind, const QColor &fill, const QColor &border) {
    if (task)
        if (const QColor chosen = plan.borderColor(*task, kind); chosen.isValid()) return chosen;
    return fill.alpha() > 0 && fill.rgb() == border.rgb() ? intense(fill) : border;
}
constexpr double kEdgeGrip = 5;

}  // namespace

GanttPalette GanttPalette::light() {
    return {QColor(0xec, 0xee, 0xf1), kText, kMuted, kGrid, kLine, kShade, kSelected, kBadge, Qt::white, Qt::white, kHeaderLine};
}

GanttCanvas::GanttCanvas() : m_names(QLocale(Localization::effectiveLanguage(Localization::preference()))) {}

int GanttCanvas::dayWidth() const {
    int base = 28;
    if (m_scale == Scale::Weeks) base = 12;
    else if (m_scale == Scale::Months) base = 4;
    return std::max(2, int(std::lround(base * m_zoom / 100.0)));
}

QDate GanttCanvas::openingDay() const {
    const QDate today = GanttToday::date();
    if (m_schedule.end.isValid() && m_schedule.end < today) return m_schedule.end;
    if (m_schedule.start.isValid() && m_schedule.start > today) return m_schedule.start;
    return today;
}

bool GanttCanvas::setNamePreview(const QString &id, const QString &name) {
    const QString shown = id.isEmpty() ? QString() : GanttTask::shown(name);
    if (id == m_previewId && shown == m_previewName) return false;
    m_previewId = id;
    m_previewName = shown;
    return true;
}

QString GanttCanvas::shownName(const QString &id, const GanttTask &task) const {
    return !m_previewId.isEmpty() && id == m_previewId ? m_previewName : task.shownName();
}

QString GanttCanvas::barLabel(const QString &name, const QFont &font, double width) {
    const QString label = QFontMetricsF(font).elidedText(name, Qt::ElideRight, width);
    return label == QStringLiteral("…") ? QString() : label;
}

void GanttCanvas::setData(const GanttPlan &plan, const GanttSchedule &schedule, const QStringList &rows,
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
    if (!m_rowOf.contains(m_gesture.hovered)) m_gesture.hovered.clear();
    updateFrames();
}

int GanttCanvas::updateRange() {
    const QDate today = GanttToday::date();
    QDate first = m_schedule.start.isValid() ? std::min(m_schedule.start, today) : today;
    QDate last = m_schedule.end.isValid() ? std::max(m_schedule.end, today) : today;
    first = first.addDays(-14);
    first = m_scale == Scale::Months ? QDate(first.year(), first.month(), 1) : first.addDays(1 - first.dayOfWeek());
    const int shift = m_origin.isValid() ? int(first.daysTo(m_origin)) * dayWidth() : 0;
    m_origin = first;
    m_last = last.addDays(60);
    return shift;
}

int GanttCanvas::contentWidth() const { return int(m_origin.daysTo(m_last) + 1) * dayWidth(); }

bool GanttCanvas::setBaseline(const std::optional<GanttBaseline> &baseline, const GanttBaselineComparison &comparison) {
    m_comparison = comparison;  // a hash copy: shared data, no deep copy
    if (baseline == m_baseline) return false;  // the view sets it on every selection change
    m_baseline = baseline;
    m_baselineIndex.clear();
    if (m_baseline)
        for (int i = 0; i < m_baseline->tasks.size(); ++i) m_baselineIndex.insert(m_baseline->tasks.at(i).id, i);
    return true;
}

double GanttCanvas::xOf(const QDate &date) const {
    return double(m_origin.daysTo(date)) * dayWidth() - scrollX();
}

QDate GanttCanvas::dateAt(double xPos) const {
    return m_origin.addDays(qint64(std::floor((xPos + scrollX()) / dayWidth())));
}

double GanttCanvas::rowTop(int row) const {
    return kHeaderHeight + double(row) * kRowHeight - scrollY();
}

int GanttCanvas::rowAt(double y) const {
    if (y < kHeaderHeight) return -1;
    const int row = int(std::floor((y - kHeaderHeight + scrollY()) / kRowHeight));
    return row >= 0 && row < m_rows.size() ? row : -1;
}

const GanttTask *GanttCanvas::taskOf(const QString &id) const {
    const int index = m_taskIndex.value(id, -1);
    return index < 0 ? nullptr : &m_plan.tasks().at(index);
}

QRectF GanttCanvas::barRect(const QString &id) const {
    const int row = m_rowOf.value(id, -1);
    const auto scheduled = m_schedule.tasks.constFind(id);
    if (row < 0 || scheduled == m_schedule.tasks.cend() || !scheduled->dated) return {};  // undated: no bar
    const double top = rowTop(row);
    const double room = m_baseline ? kBaselineRoom : 0;  // FEAT-082: the thin bar's room under the bar
    if (isMilestone(id)) {
        const double center = xOf(scheduled->start) + dayWidth() / 2.0;  // in the middle of its day (user, 2026-10-29)
        return {center - kMilestoneHalfWidth, top + (kRowHeight - room) / 2.0 - kMilestoneHalfHeight,
                2 * kMilestoneHalfWidth, 2 * kMilestoneHalfHeight};
    }
    const double left = xOf(scheduled->start), right = xOf(scheduled->end) + dayWidth();
    return {left, top + kBarInset, right - left, kRowHeight - 2 * kBarInset - room};
}

const GanttBaselineTask *GanttCanvas::baselineTask(const QString &id) const {
    const int index = m_baselineIndex.value(id, -1);
    return index < 0 ? nullptr : &m_baseline->tasks.at(index);
}

// The snapshot's bar at its dates, with the height of the current kind of bar (a milestone's box
// when the snapshot had a milestone).
QRectF GanttCanvas::baselineRect(const QString &id) const {
    const GanttBaselineTask *then = m_baseline ? baselineTask(id) : nullptr;
    const int row = m_rowOf.value(id, -1);
    if (!then || !then->dated || row < 0) return {};
    const double top = rowTop(row);
    if (then->milestone) {
        const double center = xOf(then->start) + dayWidth() / 2.0;
        return {center - kMilestoneHalfWidth, top + (kRowHeight - kBaselineRoom) / 2.0 - kMilestoneHalfHeight,
                2 * kMilestoneHalfWidth, 2 * kMilestoneHalfHeight};
    }
    const double left = xOf(then->start), right = xOf(then->end) + dayWidth();
    return {left, top + kBarInset, right - left, kRowHeight - 2 * kBarInset - kBaselineRoom};
}

QRectF GanttCanvas::baselineMarkRect(const QString &id) const {
    const QRectF place = baselineRect(id);
    if (place.isEmpty()) return {};
    // 4 px under the bar: clear of the critical halo (3 px out, 2 px wide).
    const double top = rowTop(m_rowOf.value(id)) + kRowHeight - 3 - kBaselineMark;
    if (baselineTask(id)->milestone) return {place.center().x() - 4, top - 2, 8, 8};
    return {place.left(), top, place.width(), kBaselineMark};
}

QRectF GanttCanvas::slipRect(const QString &id) const {
    const GanttBaselineTask *then = m_baseline ? baselineTask(id) : nullptr;
    const GanttScheduledTask now = m_schedule.tasks.value(id);
    const QRectF mark = baselineMarkRect(id);
    if (!then || !now.dated || mark.isEmpty() || then->end == now.end) return {};
    // The end of a bar is its last day's right edge, a milestone's the middle of its day.
    const int dw = dayWidth();
    const double was = then->milestone ? xOf(then->start) + dw / 2.0 : xOf(then->end) + dw;
    const double is = isMilestone(id) ? xOf(now.start) + dw / 2.0 : xOf(now.end) + dw;
    const double middle = mark.center().y();
    return {QPointF(std::min(was, is), middle - kBaselineMark / 2), QPointF(std::max(was, is), middle + kBaselineMark / 2)};
}

QColor GanttCanvas::baselineMarkColor(const QString &id) const {
    switch (m_comparison.tasks.value(id)) {
    case GanttBaselineComparison::Change::Later: return kCritical;
    case GanttBaselineComparison::Change::Earlier: return kBaselineEarlier;
    default: return kBaselineSame;
    }
}

QRectF GanttCanvas::newPillRect(const QString &id) const {
    if (!m_baseline || m_comparison.tasks.value(id) != GanttBaselineComparison::Change::New) return {};
    const QRectF bar = barRect(id);
    if (bar.isEmpty()) return {};
    const QSizeF size = newPillSize(newPillFont(textFont));
    // Before the bar, clear of the start's link circle.
    return {QPointF(bar.left() - 14 - size.width(), bar.center().y() - size.height() / 2), size};
}

void GanttCanvas::paintBaseline(QPainter &painter, const QString &id, const QColor &border) {
    if (!m_baseline) return;
    if (const QRectF pill = newPillRect(id); !pill.isEmpty()) {
        paintNewPill(&painter, pill, newPillFont(textFont), accent);
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
        outside.addRect(m_area);
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

QPainterPath GanttCanvas::barShape(const QString &id, const QRectF &box, bool milestone) const {
    QPainterPath shape;
    if (milestone) return milestonePath(milestoneShapeOf(id), box);
    if (m_schedule.tasks.value(id).summary && summaryShapeOf(id) != GanttPlan::SummaryShape::Bar)
        return summaryPath(summaryShapeOf(id), summaryPlace(id, box));
    return taskPath(barShapeOf(id), box);
}

GanttPlan::BarShape GanttCanvas::barShapeOf(const QString &id) const {
    const GanttTask *task = taskOf(id);
    return task ? m_plan.barShape(*task) : m_plan.barShape();
}

GanttPlan::MilestoneShape GanttCanvas::milestoneShapeOf(const QString &id) const {
    const GanttTask *task = taskOf(id);
    return task ? m_plan.milestoneShape(*task) : m_plan.milestoneShape();
}

GanttPlan::SummaryShape GanttCanvas::summaryShapeOf(const QString &id) const {
    const GanttTask *task = taskOf(id);
    return task ? m_plan.summaryShape(*task) : m_plan.summaryShape();
}

double GanttCanvas::barTip(GanttPlan::BarShape shape, const QRectF &box) {
    switch (shape) {
    case GanttPlan::BarShape::Arrow: return std::min(box.height() / 3, box.width() / 2);
    case GanttPlan::BarShape::Hexagon:
    case GanttPlan::BarShape::Parallelogram: return std::min(box.height() / 3, box.width() / 4);
    default: return 0;
    }
}

QPainterPath GanttCanvas::taskPath(GanttPlan::BarShape shape, const QRectF &box, qreal grow) {
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

QPointF GanttCanvas::linkHandle(const QString &id) const {
    const QRectF bar = barRect(id);
    return bar.isEmpty() ? QPointF() : QPointF(bar.right() + reach.handleGap, bar.center().y());
}

QPointF GanttCanvas::linkHandleAtStart(const QString &id) const {
    const QRectF bar = barRect(id);
    return bar.isEmpty() ? QPointF() : QPointF(bar.left() - reach.handleGap, bar.center().y());
}

bool GanttCanvas::isMilestone(const QString &id) const {
    const GanttTask *task = taskOf(id);
    // A task that became a summary is drawn as one: its children give its dates.
    return task && task->milestone && !m_schedule.tasks.value(id).summary;
}

QRectF GanttCanvas::progressBadge(const QString &id, int progress) const {
    const QRectF bar = barRect(id);
    const GanttTask *task = taskOf(id);
    if (bar.isEmpty() || !task || isMilestone(id)) return {};
    if (progress < 0) progress = task->progress;
    QFont font = textFont;
    font.setPixelSize(10);
    font.setBold(true);
    const double width = std::max(14.0, QFontMetricsF(font).horizontalAdvance(QString::number(progress)) + 8);
    const double edge = bar.left() + bar.width() * progress / 100.0;
    return {edge - width / 2, bar.bottom() - 4, width, 14};
}


// The link circle under pos (end or start) of the bar under the mouse; empty if none.
QString GanttCanvas::linkHandleAt(const QString &id, const QPointF &pos, bool *atStart) const {
    if (id.isEmpty() || m_schedule.tasks.value(id).summary) return {};
    const bool end = QLineF(pos, linkHandle(id)).length() <= reach.handle;
    const bool start = QLineF(pos, linkHandleAtStart(id)).length() <= reach.handle;
    if (!end && !start) return {};
    if (atStart) *atStart = start && !end;
    return id;
}

// Right edge of the bar; on a short bar (Weeks, Months) only its outer side, so the bar still moves.
bool GanttCanvas::onResizeGrip(const QRectF &bar, const QPointF &pos) const {
    if (std::abs(pos.x() - bar.right()) > kEdgeGrip) return false;
    return bar.width() >= 3 * kEdgeGrip || pos.x() >= bar.right();
}

QString GanttCanvas::barAt(const QPointF &pos) const {
    const int row = rowAt(pos.y());
    if (row < 0) return {};
    const QString id = m_rows.at(row);
    const QRectF bar = barRect(id).adjusted(-kEdgeGrip, -kBarInset, kEdgeGrip, kBarInset);
    return bar.contains(pos) ? id : QString();
}


bool GanttCanvas::linkAllowed(const QString &from, const QString &to) const {
    GanttPlan copy = m_plan;
    GanttDependency dependency;
    dependency.from = from;
    dependency.to = to;
    return copy.addDependency(dependency);
}

// ---- painting

void GanttCanvas::paint(QPainter &painter, const QRect &area) {
    m_area = area;
    painter.save();
    painter.setFont(textFont);
    painter.setRenderHint(QPainter::Antialiasing);
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
                painter.fillRect(QRectF(xOf(day), kHeaderHeight, dw, area.height() - kHeaderHeight), m_colors.shade);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(m_colors.grid);
    // One line per day (Days), week (Weeks) or month (Months), one under each row.
    for (QDate day = firstDay; day <= lastDay.addDays(1); day = day.addDays(1)) {
        const bool line = m_scale == Scale::Days || (m_scale == Scale::Weeks && day.dayOfWeek() == 1) || (m_scale == Scale::Months && day.day() == 1);
        if (line) painter.drawLine(QPointF(std::floor(xOf(day)), kHeaderHeight), QPointF(std::floor(xOf(day)), area.height()));
    }
    for (int row = std::max(0, rowAt(kHeaderHeight)); row < m_rows.size() && rowTop(row) < area.height(); ++row) {
        const double bottom = std::floor(rowTop(row) + kRowHeight) - 1;
        painter.drawLine(QPointF(0, bottom), QPointF(area.width(), bottom));
    }
    painter.setRenderHint(QPainter::Antialiasing);
    const double today = xOf(GanttToday::date()) + dw / 2.0;
    painter.setPen(QPen(kToday, 1.5));
    painter.drawLine(QPointF(today, kHeaderHeight), QPointF(today, area.height()));
    const int firstRow = std::max(0, rowAt(kHeaderHeight));
    if (!m_frames.isEmpty()) paintFrames(painter, firstRow, rowAt(area.height() - 1) < 0 ? int(m_rows.size()) - 1 : rowAt(area.height() - 1));
    paintArrows(painter);
    for (int row = firstRow; row < m_rows.size() && rowTop(row) < area.height(); ++row) paintTask(painter, m_rows.at(row), row);
    if (m_gesture.drag == Drag::Link) {
        const QString target = barAt(m_gesture.pos);
        painter.setPen(QPen(target.isEmpty() || target == m_gesture.id || linkAllowed(linkFrom(target), linkTo(target)) ? kArrow : kCritical, 1.5, Qt::DashLine));
        painter.drawLine(m_gesture.fromStart ? linkHandleAtStart(m_gesture.id) : linkHandle(m_gesture.id), m_gesture.pos);
    }
    painter.restore();
    paintHeader(painter, area);
    if (m_printing) {  // FEAT-083: a page, not the card on the window's page
        painter.restore();
        return;
    }
    const qreal r = GanttPalette::kCardRadius;
    for (const bool left : {true, false}) {  // the page outside the card's bottom corners
        QPainterPath outside;
        outside.addRect(QRectF(left ? 0 : area.width() - r, area.height() - r, r, r));
        QPainterPath round;
        round.addRoundedRect(QRectF(left ? 0 : area.width() - 2 * r, area.height() - 2 * r, 2 * r, 2 * r), r, r);
        painter.fillPath(outside.subtracted(round), m_colors.page);
    }
    painter.restore();
}

// Month names with a capital, also where the language writes them lower case (user, 2026-10-29).
QString GanttCanvas::monthName(int month, QLocale::FormatType format) const {
    QString name = m_names.standaloneMonthName(month, format);
    if (!name.isEmpty()) name[0] = name.at(0).toUpper();
    return name;
}

void GanttCanvas::paintHeader(QPainter &painter, const QRect &area) {
    painter.fillRect(QRect(0, 0, area.width(), kHeaderHeight), m_colors.card);
    painter.setPen(m_colors.headerLine);
    painter.drawLine(QPointF(0, kHeaderHeight - 0.5), QPointF(area.width(), kHeaderHeight - 0.5));
    painter.drawLine(QPointF(0, kHeaderHeight / 2.0 - 0.5), QPointF(area.width(), kHeaderHeight / 2.0 - 0.5));
    QFont font = headerFont;  // the timeline header is interface
    painter.setFont(font);
    const int dw = dayWidth();
    const QDate firstDay = dateAt(0).addDays(-1), lastDay = dateAt(area.width());
    const double half = kHeaderHeight / 2.0;
    // Upper line: months (Days, Weeks) or years (Months), bold, the label kept at the left edge
    // while its period is in view.
    font.setBold(true);
    painter.setFont(font);
    const auto upper = [&](const QDate &start, const QDate &next, const QString &label) {
        const double left = xOf(start), right = xOf(next);
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
        const double left = xOf(start);
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

GanttBarPaint GanttCanvas::barPaint(const GanttPlan &plan, const QString &id, const GanttBarLook *look, bool summary) {
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
    paint.outline = outline(plan, plan.task(id), kind, paint.fill, paint.border);  // the kind found once per bar
    return paint;
}

QColor GanttCanvas::outlineColor(const GanttPlan &plan, const QString &id, const QColor &fill, const QColor &border) {
    return outline(plan, plan.task(id), plan.barKind(id), fill, border);
}

QColor GanttCanvas::defaultOutlineColor(const GanttPlan &plan, GanttPlan::BarKind kind) {
    const QColor chosen = plan.defaultBorderColor(kind);
    return chosen.isValid() ? chosen : intense(defaultBarColor(plan, kind));
}

QColor GanttCanvas::linkColor(const GanttPlan &plan) { return plan.linkColor().isValid() ? plan.linkColor() : kArrow; }

QColor GanttCanvas::criticalColor() { return kCritical; }
QColor GanttCanvas::baselineColor() { return kBaselineSame; }
QColor GanttCanvas::baselineEarlierColor() { return kBaselineEarlier; }
QColor GanttCanvas::todayColor() { return kToday; }

void GanttCanvas::paintTask(QPainter &painter, const QString &id, int row) {
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
    if (id == m_gesture.id) {
        const double dx = m_gesture.pos.x() - m_gesture.press.x();
        const int dw = dayWidth();
        if (m_gesture.drag == Drag::Move) {
            QDate start = scheduled.start.addDays(qint64(std::round(dx / dw)));
            for (int i = 0; i < 7 && !m_plan.calendar().isWorkingDay(start); ++i) start = start.addDays(1);
            bar.translate(double(scheduled.start.daysTo(start)) * dw, 0);
        }
        else if (m_gesture.drag == Drag::Resize) bar.setRight(std::max(bar.left() + dw, bar.right() + std::round(dx / dw) * dw));
        else if (m_gesture.drag == Drag::Progress) progress = std::clamp(int(std::round((m_gesture.pos.x() - bar.left()) / bar.width() * 100)), 0, 100);
    }
    // Bars as pills with the name inside: peach tasks and lilac summaries, or the branch
    // colour chosen by hand on the topic (GanttView::updateChart).
    const auto look = m_looks.constFind(id);
    const GanttPlan::BarKind kind = m_plan.barKind(id);
    const GanttBarPaint colors = barPaint(m_plan, id, look != m_looks.cend() ? &*look : nullptr, scheduled.summary);
    const QColor &fill = colors.fill, &text = colors.text, &done = colors.done;
    const bool filled = fill.alpha() > 0;
    // FEAT-080: the task's own border (pattern and colour) and fill, or the sheet's for its kind.
    const GanttPlan::BorderStyle borderStyle = m_plan.borderStyle(*task, kind);
    const QPen outline = borderPen(borderStyle, colors.outline, 1.5);
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
            painter.setPen(QPen(selectionRing, 1.5));
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
            painter.drawText(QRectF(bar.right() + 6, rowTop(row), 600, kRowHeight), Qt::AlignLeft | Qt::AlignVCenter, shownName(id, *task));
        }
    } else if (const GanttPlan::SummaryShape summaryShape = summaryShapeOf(id); scheduled.summary && summaryShape != GanttPlan::SummaryShape::Bar) {
        const QRectF place = summaryPlace(id, bar);
        const QPainterPath shape = summaryPath(summaryShape, place);
        if (m_selected.contains(id))  // under the shape: 1.5 px of blue around its outline
            painter.strokePath(shape, QPen(selectionRing, 4.5, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        fillShape(painter, id, shape, fillStyle, body);
        fillDone(shape, place);
        QPen pen = outline;
        if (!handDrawn) {  // the shape's sharp tips (a hand-drawn line keeps its round ends)
            pen.setCapStyle(Qt::FlatCap);
            pen.setJoinStyle(Qt::MiterJoin);
        }
        strokeBorder(painter, id, shape, pen, handDrawn);
        if (summaryShape == GanttPlan::SummaryShape::Frame) {  // the name inside the header, as on a bar
            const QString name = m_labelled.contains(id) ? barLabel(shownName(id, *task), font, place.width() - 16) : QString();
            if (!name.isEmpty()) {
                painter.setPen(text);
                painter.drawText(place.adjusted(8, 0, -8, 0), Qt::AlignCenter, name);
            }
        } else if (m_labelled.contains(id)) {  // too thin for text: the name after it, as a milestone's
            painter.setPen(m_colors.text);
            painter.drawText(QRectF(place.right() + 6, rowTop(row), 600, kRowHeight), Qt::AlignLeft | Qt::AlignVCenter, shownName(id, *task));
        }
    } else {
        const GanttPlan::BarShape barShape = barShapeOf(id);
        const QPainterPath shape = taskPath(barShape, bar);
        fillShape(painter, id, shape, fillStyle, body);
        fillDone(shape, bar);
        strokeBorder(painter, id, shape, outline, handDrawn);
        if (m_selected.contains(id))  // the selected bar: a blue ring in place of the critical halo
            painter.strokePath(taskPath(barShape, bar, 2), QPen(selectionRing, 1.5));
        else if (scheduled.critical && !scheduled.summary && criticalPainted())
            painter.strokePath(taskPath(barShape, bar, 3), critical);
        // Out of the points: on the Arrow's end, on both of the Hexagon's; half the Parallelogram's slant.
        const double tip = barTip(barShape, bar);
        const double start = barShape == GanttPlan::BarShape::Hexagon ? tip : barShape == GanttPlan::BarShape::Parallelogram ? tip / 2 : 0;
        const double end = barShape == GanttPlan::BarShape::Parallelogram ? tip / 2 : tip;
        const QRectF room = bar.adjusted(8 + start, 0, -8 - end, 0);
        const QString name = m_labelled.contains(id) ? barLabel(shownName(id, *task), font, room.width()) : QString();
        if (!name.isEmpty()) {
            painter.setPen(text);
            painter.drawText(room, Qt::AlignCenter, name);
        }
    }

    // FEAT-082: over the bar, so the snapshot's outline shows inside a longer bar too.
    paintBaseline(painter, id, colors.outline);
    // The bar under the mouse: link circles at both ends, progress badge with the percentage.
    const bool hovered = id == m_gesture.hovered && !scheduled.summary && m_gesture.drag == Drag::None;
    if (hovered) {
        painter.setPen(QPen(kArrow, 1.5));
        painter.setBrush(m_colors.card);
        painter.drawEllipse(linkHandle(id), reach.handleRadius, reach.handleRadius);
        painter.drawEllipse(linkHandleAtStart(id), reach.handleRadius, reach.handleRadius);
    }
    if ((hovered || (id == m_gesture.id && m_gesture.drag == Drag::Progress)) && !isMilestone(id)) {
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

bool GanttCanvas::isHandDrawn(GanttPlan::BorderStyle style) {
    return style == GanttPlan::BorderStyle::HandDrawnSolid || style == GanttPlan::BorderStyle::HandDrawnDashed;
}

void GanttCanvas::strokeBorder(QPainter &painter, const QString &id, const QPainterPath &shape, const QPen &pen, bool handDrawn) {
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

void GanttCanvas::fillShape(QPainter &painter, const QString &id, const QPainterPath &shape, NodeFillStyle style, const QColor &color) {
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

QPainterPath GanttCanvas::milestonePath(GanttPlan::MilestoneShape shape, const QRectF &box, qreal grow) {
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

QPen GanttCanvas::borderPen(GanttPlan::BorderStyle style, const QColor &color, qreal width) {
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

Qt::PenStyle GanttCanvas::linkPenStyle(GanttPlan::LinkLine line) {
    switch (line) {
    case GanttPlan::LinkLine::Dashed: return Qt::DashLine;
    case GanttPlan::LinkLine::Dotted: return Qt::DotLine;
    case GanttPlan::LinkLine::Solid: break;
    }
    return Qt::SolidLine;
}

qreal GanttCanvas::linkWidthPx(GanttPlan::LinkWidth width) {
    switch (width) {
    case GanttPlan::LinkWidth::Medium: return 2;
    case GanttPlan::LinkWidth::Bold: return 3;
    case GanttPlan::LinkWidth::ExtraBold: return 4;  // more covers the 24 px bars
    case GanttPlan::LinkWidth::Thin: break;
    }
    return 1.25;
}

qreal GanttCanvas::linkEndSizePx(GanttPlan::LinkEndSize size) {
    switch (size) {
    case GanttPlan::LinkEndSize::Medium: return 9;
    case GanttPlan::LinkEndSize::Large: return 12;
    case GanttPlan::LinkEndSize::Small: break;
    }
    return BranchPath::kMinEndMarkPx;  // 6, the map's smallest mark
}

// Elbow line from the predecessor's side to the successor's side named by the type, with the head.
QPolygonF GanttCanvas::arrowPath(int d) const {
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
int GanttCanvas::barsCrossed(double x, int rowA, int rowB) const {
    int crossed = 0;
    for (int row = std::min(rowA, rowB) + 1; row < std::max(rowA, rowB); ++row) {
        const QRectF bar = barRect(m_rows.at(row));
        if (!bar.isEmpty() && bar.left() <= x && x <= bar.right()) ++crossed;
    }
    return crossed;
}

void GanttCanvas::paintArrows(QPainter &painter) {
    // Only arrows that can cross the visible rows: an end in view, or one above and one below.
    const int first = std::max(0, rowAt(kHeaderHeight)), last = first + m_area.height() / kRowHeight + 1;
    for (int d = 0; d < m_plan.dependencies().size(); ++d) {
        if (m_schedule.ignoredDependencies.contains(d)) continue;
        const int from = m_rowOf.value(m_plan.dependencies().at(d).from, -1), to = m_rowOf.value(m_plan.dependencies().at(d).to, -1);
        if (from < 0 || to < 0 || (from < first && to < first) || (from > last && to > last)) continue;
        const QPolygonF path = arrowPath(d);
        if (path.size() < 2) continue;
        const bool critical = criticalPainted() && m_schedule.criticalDependencies.contains(d);
        const QColor color = critical ? kCritical : linkColor(m_plan);  // FEAT-080: the sheet's colour
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

int GanttCanvas::dependencyAt(const QPointF &pos) const {
    for (int d = 0; d < m_plan.dependencies().size(); ++d) {
        const QPolygonF path = arrowPath(d);
        for (int i = 1; i < path.size(); ++i) {
            const QRectF segment = QRectF(path.at(i - 1), path.at(i)).normalized().adjusted(-reach.arrow, -reach.arrow, reach.arrow, reach.arrow);
            if (segment.contains(pos)) return d;
        }
    }
    return -1;
}

QRectF GanttCanvas::summaryPlace(const QString &id, const QRectF &box) const {
    if (summaryShapeOf(id) != GanttPlan::SummaryShape::Frame)
        return {box.left(), box.center().y() - kBracketHeight / 2, box.width(), kBracketHeight};
    // The group frame's header fills its row from the bars' top (user, 2026-11-01), so its name fits.
    const int row = m_rowOf.value(id, -1);
    const double reach = kFrameGap + kFrameStep * m_frames.value(row).levels;
    const double top = row < 0 ? box.top() : rowTop(row) + kBarInset, bottom = row < 0 ? box.bottom() : rowTop(row) + kRowHeight;
    return {box.left() - reach, top, box.width() + 2 * reach, bottom - top};
}

void GanttCanvas::updateFrames() {
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

void GanttCanvas::paintFrames(QPainter &painter, int firstRow, int lastRow) {
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
        const QColor line = m_selected.contains(id) ? selectionRing : colors.outline;
        painter.setPen(QPen(line, 1.5, Qt::DashLine, Qt::FlatCap));
        painter.setBrush(tint);
        painter.drawRoundedRect(QRectF(header.topLeft(), QPointF(header.right(), bottom)), kFrameRadius, kFrameRadius);
    }
    painter.restore();
}

QPainterPath GanttCanvas::summaryPath(GanttPlan::SummaryShape shape, const QRectF &rect) {
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

QString GanttCanvas::typeName(GanttDependency::Type type) {
    switch (type) {
    case GanttDependency::Type::StartStart: return tr("Start to start");
    case GanttDependency::Type::FinishFinish: return tr("Finish to finish");
    case GanttDependency::Type::StartFinish: return tr("Start to finish");
    case GanttDependency::Type::FinishStart: break;
    }
    return tr("Finish to start");
}

QString GanttCanvas::dependencyLabel(int dependency) const {
    const GanttDependency &link = m_plan.dependencies().at(dependency);
    if (link.lag == 0) return typeName(link.type);
    if (link.lag > 0) return tr("%1, %n day(s) later", nullptr, link.lag).arg(typeName(link.type));
    return tr("%1, %n day(s) earlier", nullptr, -link.lag).arg(typeName(link.type));
}


QColor GanttCanvas::defaultBarColor(const GanttPlan &plan, GanttPlan::BarKind kind) {
    if (const QColor sheet = plan.defaultColor(kind); sheet.isValid()) return sheet;
    switch (kind) {
    case GanttPlan::BarKind::Summary: return kSummaryBar;
    case GanttPlan::BarKind::Milestone: return kMilestoneBar;
    case GanttPlan::BarKind::Task: break;
    }
    return kTaskBar;
}

QColor GanttCanvas::barTextColor(const QColor &bar) {
    return NodeStyle::isDarkBackground(bar) ? QColor(Qt::white) : bar.darker(300);
}

QFont GanttCanvas::newPillFont(QFont font, int pixels) {
    font.setPixelSize(pixels);
    font.setBold(false);
    font.setWeight(QFont::DemiBold);
    return font;
}

QSizeF GanttCanvas::newPillSize(const QFont &font) { return {QFontMetricsF(font).horizontalAdvance(tr("New")) + 12, 16}; }

void GanttCanvas::paintNewPill(QPainter *painter, const QRectF &rect, const QFont &font, const QColor &accent) {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(accent);
    painter->drawRoundedRect(rect, rect.height() / 2, rect.height() / 2);
    painter->setFont(font);
    painter->setPen(Qt::white);
    painter->drawText(rect, Qt::AlignCenter, tr("New"));
    painter->restore();
}

QString GanttCanvas::taskTip(const QString &id) const {
    const GanttTask *task = taskOf(id);
    if (!task) return {};
    const GanttScheduledTask scheduled = m_schedule.tasks.value(id);
    const int days = scheduled.summary ? m_plan.workingDaysBetween(scheduled.start, scheduled.end) : task->length();
    const auto shortDate = [](const QDate &date) { return QLocale().toString(date, QLocale::ShortFormat); };
    return tr("%1\n%2 – %3\n%n working day(s)", nullptr, days).arg(shownName(id, *task), shortDate(scheduled.start), shortDate(scheduled.end))
           + QLatin1Char('\n') + tr("Float: %n day(s)", nullptr, std::max(0, scheduled.totalFloat));
}

std::function<bool(GanttPlan &)> GanttCanvas::gestureEdit(const QPointF &pos) const {
    const Drag drag = m_gesture.drag;
    const QString id = m_gesture.id;
    const double dx = pos.x() - m_gesture.press.x();
    if (drag == Drag::None || (drag != Drag::Link && std::abs(dx) < kDragThreshold)) return {};
    const GanttScheduledTask scheduled = m_schedule.tasks.value(id);
    switch (drag) {
    case Drag::Move: {
        const int days = int(std::round(dx / dayWidth()));
        if (days == 0) return {};
        // A task placed by a dependency changes that dependency's lag (FEAT-074); another dependency
        // may still hold it where it is: then nothing changes and no step is recorded.
        return [id, days, scheduled](GanttPlan &plan) {
            return plan.moveTo(id, scheduled.start.addDays(days)) && plan.schedule().tasks.value(id).start != scheduled.start;
        };
    }
    case Drag::Resize: {
        const QDate end = scheduled.end.addDays(qint64(std::round(dx / dayWidth())));
        const int duration = GanttCommands::durationTo(m_plan, scheduled.start, end);
        return [id, duration](GanttPlan &plan) { return plan.setDuration(id, duration); };
    }
    case Drag::Progress: {
        const QRectF bar = barRect(id);
        const int progress = std::clamp(int(std::round((pos.x() - bar.left()) / bar.width() * 100)), 0, 100);
        return [id, progress](GanttPlan &plan) { return plan.setProgress(id, progress); };
    }
    case Drag::Link: {
        const QString target = barAt(pos);
        if (target.isEmpty() || target == id) return {};
        GanttDependency dependency;  // from the end circle: id comes first; from the start circle: target
        dependency.from = m_gesture.fromStart ? target : id;
        dependency.to = m_gesture.fromStart ? id : target;
        return [dependency](GanttPlan &plan) { return plan.addDependency(dependency); };
    }
    case Drag::None: break;
    }
    return {};
}
