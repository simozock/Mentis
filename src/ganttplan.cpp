#include "ganttplan.h"

#include <QJsonArray>
#include <QUuid>

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>

namespace {

using Day = qint64;  // Julian day; Day % 7 == 0 is a Monday

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

// FEAT-080: JSON keys, in the order of GanttPlan::MilestoneShape and GanttPlan::BarKind.
const QStringList &milestoneShapeKeys() {
    static const QStringList keys{QStringLiteral("diamond"), QStringLiteral("circle"), QStringLiteral("triangle"), QStringLiteral("star"),
                                  QStringLiteral("hexagon"), QStringLiteral("heart"), QStringLiteral("flag"), QStringLiteral("gem")};
    return keys;
}
const QStringList &barShapeKeys() {
    static const QStringList keys{QStringLiteral("pill"),    QStringLiteral("rounded"), QStringLiteral("rectangle"),
                                  QStringLiteral("arrow"),   QStringLiteral("hexagon"), QStringLiteral("leaf"),
                                  QStringLiteral("ticket"),  QStringLiteral("parallelogram")};
    return keys;
}
const QStringList &summaryShapeKeys() {
    static const QStringList keys{QStringLiteral("bar"),       QStringLiteral("bracket"), QStringLiteral("brace"), QStringLiteral("ibeam"),
                                  QStringLiteral("hook"),      QStringLiteral("dimension"), QStringLiteral("arrow"), QStringLiteral("rail"),
                                  QStringLiteral("frame")};
    return keys;
}
// FEAT-080: by LinkWidth and LinkEndSize; the first (thin, small) is the default and is not written.
const QStringList &linkWidthKeys() {
    static const QStringList keys{QStringLiteral("thin"), QStringLiteral("medium"), QStringLiteral("bold"), QStringLiteral("extraBold")};
    return keys;
}
const QStringList &linkEndSizeKeys() {
    static const QStringList keys{QStringLiteral("small"), QStringLiteral("medium"), QStringLiteral("large")};
    return keys;
}
const QStringList &borderStyleKeys() {
    static const QStringList keys{QStringLiteral("solid"), QStringLiteral("dashed"),         QStringLiteral("dotted"),
                                  QStringLiteral("none"),  QStringLiteral("handDrawnSolid"), QStringLiteral("handDrawnDashed")};
    return keys;
}
// Keys of the border by BarKind.
const QStringList &borderKeys() {
    static const QStringList keys{QStringLiteral("taskBorder"), QStringLiteral("summaryBorder"), QStringLiteral("milestoneBorder")};
    return keys;
}
// Keys of the fill by BarKind; the values are the map's fill keys (NodeStyle::fillStyleKey).
const QStringList &fillKeys() {
    static const QStringList keys{QStringLiteral("taskFill"), QStringLiteral("summaryFill"), QStringLiteral("milestoneFill")};
    return keys;
}
const QStringList &colorKeys() {
    static const QStringList keys{QStringLiteral("taskColor"), QStringLiteral("summaryColor"), QStringLiteral("milestoneColor")};
    return keys;
}

// Colours compare and save as 8-bit RGB with alpha, so they survive the JSON round trip.
QColor rgb(const QColor &color) {
    return color.isValid() ? QColor(color.red(), color.green(), color.blue(), color.alpha()) : QColor();
}

bool validStart(const QDate &date) {
    return date.isValid() && date.year() >= GanttPlan::kFirstYear && date.year() <= GanttPlan::kLastYear;
}

// No date (FEAT-075: a topic not planned yet) or a valid one.
bool validOptionalStart(const QDate &date) { return date.isNull() || validStart(date); }

Day floorDiv(Day a, Day b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Working days as consecutive integers, so the schedule is integer arithmetic. index(day) counts the
// working days before day: it is the index of the first working day on or after day.
class WorkingDays {
public:
    explicit WorkingDays(const GanttCalendar &calendar) {
        for (int weekday = 0; weekday < 7; ++weekday)
            m_prefix[weekday + 1] = m_prefix[weekday] + (calendar.workingDays.contains(weekday + 1) ? 1 : 0);
        for (const QDate &holiday : calendar.holidays)
            if (calendar.workingDays.contains(holiday.dayOfWeek())) m_holidays.append(holiday.toJulianDay());
        std::sort(m_holidays.begin(), m_holidays.end());
    }

    Day index(Day day) const {
        const Day weeks = floorDiv(day, 7);
        const Day holidays = std::lower_bound(m_holidays.cbegin(), m_holidays.cend(), day) - m_holidays.cbegin();
        return weeks * m_prefix[7] + m_prefix[day - weeks * 7] - holidays;
    }
    Day index(const QDate &date) const { return index(date.toJulianDay()); }

    // The working day with this index: the first day whose next day counts one more.
    QDate date(Day workingIndex) const {
        Day low = -(Day(1) << 40), high = Day(1) << 40;
        while (low < high) {
            const Day middle = floorDiv(low + high, 2);
            if (index(middle + 1) > workingIndex) high = middle;
            else low = middle + 1;
        }
        return QDate::fromJulianDay(low);
    }

private:
    std::array<Day, 8> m_prefix{};
    QVector<Day> m_holidays;
};

const char *typeKey(GanttDependency::Type type) {
    switch (type) {
    case GanttDependency::Type::StartStart: return "SS";
    case GanttDependency::Type::FinishFinish: return "FF";
    case GanttDependency::Type::StartFinish: return "SF";
    case GanttDependency::Type::FinishStart: break;
    }
    return "FS";
}

}  // namespace

bool GanttCalendar::isWorkingDay(const QDate &date) const {
    return date.isValid() && workingDays.contains(date.dayOfWeek()) && !holidays.contains(date);
}

int GanttPlan::indexOf(const QString &id) const {
    for (int i = 0; i < m_tasks.size(); ++i)
        if (m_tasks.at(i).id == id) return i;
    return -1;
}

const GanttTask *GanttPlan::task(const QString &id) const {
    const int index = indexOf(id);
    return index < 0 ? nullptr : &m_tasks.at(index);
}

bool GanttPlan::hasChildren(const QString &id) const {
    return std::any_of(m_tasks.cbegin(), m_tasks.cend(), [&](const GanttTask &task) { return task.parentId == id; });
}

QStringList GanttPlan::children(const QString &parentId) const {
    QStringList result;
    for (const GanttTask &task : m_tasks)
        if (task.parentId == parentId) result.append(task.id);
    return result;
}

int GanttPlan::dependencyIndex(const QString &from, const QString &to) const {
    for (int i = 0; i < m_dependencies.size(); ++i)
        if (m_dependencies.at(i).from == from && m_dependencies.at(i).to == to) return i;
    return -1;
}

bool GanttPlan::isDescendant(const QString &id, const QString &ancestorId) const {
    QString current = id;
    for (int steps = 0; !current.isEmpty() && steps <= m_tasks.size(); ++steps) {
        if (current == ancestorId) return true;
        const GanttTask *task = this->task(current);
        current = task ? task->parentId : QString();
    }
    return false;
}

bool GanttPlan::reaches(const QString &from, const QString &to) const {
    QHash<QString, QStringList> next;
    for (const GanttDependency &dependency : m_dependencies) next[dependency.from].append(dependency.to);
    QSet<QString> seen{from};
    QStringList stack{from};
    while (!stack.isEmpty()) {
        const QString current = stack.takeLast();
        if (current == to) return true;
        for (const QString &successor : next.value(current))
            if (!seen.contains(successor)) {
                seen.insert(successor);
                stack.append(successor);
            }
    }
    return false;
}

int GanttPlan::insertPosition(const QString &parentId, int row, const QString &skipId) const {
    int sibling = 0;
    for (int i = 0; i < m_tasks.size(); ++i) {
        const GanttTask &task = m_tasks.at(i);
        if (task.parentId != parentId || task.id == skipId) continue;
        if (sibling++ == row) return i;
    }
    return m_tasks.size();  // only the order among siblings matters
}

QString GanttPlan::addTask(GanttTask task, int row) {
    if (!task.parentId.isEmpty() && indexOf(task.parentId) < 0) return {};
    if (!validOptionalStart(task.start) || task.duration < kMinDuration || task.duration > kMaxDuration
        || task.progress < 0 || task.progress > kMaxProgress)
        return {};
    if (task.id.isEmpty() || indexOf(task.id) >= 0) task.id = newId();
    task.color = rgb(task.color);
    m_tasks.insert(insertPosition(task.parentId, row), task);
    return task.id;
}

bool GanttPlan::removeTask(const QString &id) {
    if (indexOf(id) < 0) return false;
    QSet<QString> removed;
    for (const GanttTask &task : std::as_const(m_tasks))
        if (isDescendant(task.id, id)) removed.insert(task.id);
    m_tasks.removeIf([&](const GanttTask &task) { return removed.contains(task.id); });
    m_dependencies.removeIf([&](const GanttDependency &dependency) {
        return removed.contains(dependency.from) || removed.contains(dependency.to);
    });
    return true;
}

bool GanttPlan::moveTask(const QString &id, const QString &parentId, int row) {
    const int index = indexOf(id);
    if (index < 0 || (!parentId.isEmpty() && indexOf(parentId) < 0) || isDescendant(parentId, id)) return false;
    const QVector<GanttTask> before = m_tasks;
    GanttTask task = m_tasks.takeAt(index);
    task.parentId = parentId;
    m_tasks.insert(insertPosition(parentId, row), task);
    // Appending to a parent may reorder unrelated tasks without changing any sibling order.
    const auto order = [](const QVector<GanttTask> &tasks) {
        QHash<QString, QStringList> result;
        for (const GanttTask &t : tasks) result[t.parentId].append(t.id);
        return result;
    };
    if (order(m_tasks) == order(before)) {
        m_tasks = before;
        return false;
    }
    return true;
}

bool GanttPlan::setName(const QString &id, const QString &name) {
    const int index = indexOf(id);
    if (index < 0 || m_tasks.at(index).name == name) return false;
    m_tasks[index].name = name;
    return true;
}

bool GanttPlan::setStart(const QString &id, const QDate &start) {
    const int index = indexOf(id);
    if (index < 0 || !validOptionalStart(start) || m_tasks.at(index).start == start) return false;
    m_tasks[index].start = start;
    return true;
}

bool GanttPlan::moveTo(const QString &id, const QDate &start) {
    const GanttSchedule current = schedule();
    const int placing = current.placedBy.value(id, -1);
    if (placing < 0) return setStart(id, start);
    if (!start.isValid()) return false;
    // The same working days added to the lag move the task by them, whatever the type: all four
    // measure from the successor's start or its finish, a fixed number of working days apart.
    const WorkingDays days(m_calendar);
    const Day moved = days.index(start) - days.index(current.tasks.value(id).start);
    GanttDependency dependency = m_dependencies.at(placing);
    dependency.lag = int(std::clamp<Day>(dependency.lag + moved, -kMaxLag, kMaxLag));
    return updateDependency(dependency);
}

bool GanttPlan::setDuration(const QString &id, int duration) {
    const int index = indexOf(id);
    if (index < 0 || duration < kMinDuration || duration > kMaxDuration || m_tasks.at(index).duration == duration) return false;
    m_tasks[index].duration = duration;
    return true;
}

bool GanttPlan::setMilestone(const QString &id, bool milestone) {
    const int index = indexOf(id);
    if (index < 0 || m_tasks.at(index).milestone == milestone) return false;
    m_tasks[index].milestone = milestone;
    return true;
}

bool GanttPlan::setProgress(const QString &id, int progress) {
    const int index = indexOf(id);
    if (index < 0 || progress < 0 || progress > kMaxProgress || m_tasks.at(index).progress == progress) return false;
    m_tasks[index].progress = progress;
    return true;
}

bool GanttPlan::setColor(const QString &id, const QColor &color) {
    const int index = indexOf(id);
    if (index < 0 || m_tasks.at(index).color == rgb(color)) return false;
    m_tasks[index].color = rgb(color);
    return true;
}

bool GanttPlan::setNodeId(const QString &id, const QString &nodeId) {
    const int index = indexOf(id);
    if (index < 0 || m_tasks.at(index).nodeId == nodeId) return false;
    m_tasks[index].nodeId = nodeId;
    return true;
}

const GanttBaselineTask *GanttBaseline::task(const QString &id) const {
    for (const GanttBaselineTask &task : tasks)
        if (task.id == id) return &task;
    return nullptr;
}

namespace {
QString baselineName(const QString &name) { return name.simplified().left(GanttBaseline::kMaxName).trimmed(); }
}  // namespace

const GanttBaseline *GanttPlan::baseline(const QString &id) const {
    for (const GanttBaseline &baseline : m_baselines)
        if (baseline.id == id) return &baseline;
    return nullptr;
}

QString GanttPlan::addBaseline(const QString &name, const QDateTime &taken) {
    GanttBaseline baseline;
    baseline.name = baselineName(name);
    if (baseline.name.isEmpty() || m_tasks.isEmpty()) return {};
    baseline.id = newId();
    baseline.taken = taken;
    const GanttSchedule scheduled = schedule();
    for (const GanttTask &task : std::as_const(m_tasks)) {
        const GanttScheduledTask at = scheduled.tasks.value(task.id);
        GanttBaselineTask entry;
        entry.id = task.id;
        entry.name = task.name;
        entry.dated = at.dated;
        entry.milestone = task.milestone && !at.summary;
        if (at.dated) {
            entry.start = at.start;
            entry.end = at.end;
        }
        entry.duration = at.summary ? (at.dated ? workingDaysBetween(at.start, at.end) : 0) : task.length();
        baseline.tasks.append(entry);
    }
    m_baselines.append(baseline);
    return baseline.id;
}

bool GanttPlan::renameBaseline(const QString &id, const QString &name) {
    const QString cleaned = baselineName(name);
    for (GanttBaseline &baseline : m_baselines)
        if (baseline.id == id) {
            if (cleaned.isEmpty() || cleaned == baseline.name) return false;
            baseline.name = cleaned;
            return true;
        }
    return false;
}

bool GanttPlan::removeBaseline(const QString &id) {
    return m_baselines.removeIf([&id](const GanttBaseline &baseline) { return baseline.id == id; }) > 0;
}

GanttBaselineComparison GanttPlan::compare(const GanttSchedule &schedule, const GanttBaseline &baseline) const {
    GanttBaselineComparison result;
    QSet<QString> present;
    for (const GanttTask &task : m_tasks) {
        present.insert(task.id);
        const GanttBaselineTask *then = baseline.task(task.id);
        if (!then) {
            result.tasks.insert(task.id, GanttBaselineComparison::Change::New);
            continue;
        }
        const GanttScheduledTask now = schedule.tasks.value(task.id);
        auto change = GanttBaselineComparison::Change::Same;
        if (now.dated && then->dated && now.end != then->end)
            change = now.end > then->end ? GanttBaselineComparison::Change::Later : GanttBaselineComparison::Change::Earlier;
        result.tasks.insert(task.id, change);
    }
    for (const GanttBaselineTask &task : baseline.tasks)
        if (!present.contains(task.id)) result.removed.append(task.name);
    return result;
}

bool GanttPlan::addDependency(const GanttDependency &dependency) {
    if (indexOf(dependency.from) < 0 || indexOf(dependency.to) < 0 || dependency.from == dependency.to
        || hasChildren(dependency.from) || hasChildren(dependency.to)
        || std::abs(dependency.lag) > kMaxLag || dependencyIndex(dependency.from, dependency.to) >= 0
        || dependencyIndex(dependency.to, dependency.from) >= 0 || reaches(dependency.to, dependency.from))
        return false;
    m_dependencies.append(dependency);
    return true;
}

bool GanttPlan::updateDependency(const GanttDependency &dependency) {
    const int index = dependencyIndex(dependency.from, dependency.to);
    if (index < 0 || std::abs(dependency.lag) > kMaxLag || m_dependencies.at(index) == dependency) return false;
    m_dependencies[index] = dependency;
    return true;
}

bool GanttPlan::removeDependency(const QString &from, const QString &to) {
    const int index = dependencyIndex(from, to);
    if (index < 0) return false;
    // A task that loses the last dependency placing it stays where it was (its stored start
    // becomes that date) instead of jumping back to an old one.
    const GanttSchedule before = schedule();
    m_dependencies.removeAt(index);
    if (const int task = indexOf(to); task >= 0 && before.placedBy.contains(to) && !schedule().placedBy.contains(to))
        m_tasks[task].start = before.tasks.value(to).start;
    return true;
}

bool GanttPlan::setBarShape(BarShape shape) {
    if (shape == m_barShape) return false;
    m_barShape = shape;
    return true;
}

GanttPlan::BarShape GanttPlan::barShape(const GanttTask &task) const {
    const int index = task.shape.isEmpty() ? -1 : int(barShapeKeys().indexOf(task.shape));
    return index < 0 ? m_barShape : BarShape(index);
}

GanttPlan::SummaryShape GanttPlan::summaryShape(const GanttTask &task) const {
    const int index = task.shape.isEmpty() ? -1 : int(summaryShapeKeys().indexOf(task.shape));
    return index < 0 ? m_summaryShape : SummaryShape(index);
}

GanttPlan::MilestoneShape GanttPlan::milestoneShape(const GanttTask &task) const {
    const int index = task.shape.isEmpty() ? -1 : int(milestoneShapeKeys().indexOf(task.shape));
    return index < 0 ? m_milestoneShape : MilestoneShape(index);
}

NodeFillStyle GanttPlan::fillStyle(const GanttTask &task, BarKind kind) const {
    const std::optional<NodeFillStyle> own = task.fill.isEmpty() ? std::nullopt : NodeStyle::fillStyleFromKey(task.fill);
    return own.value_or(m_fillStyles[int(kind)]);
}

GanttPlan::BorderStyle GanttPlan::borderStyle(const GanttTask &task, BarKind kind) const {
    const int index = task.border.isEmpty() ? -1 : int(borderStyleKeys().indexOf(task.border));
    return index < 0 ? m_borderStyles[int(kind)] : BorderStyle(index);
}

QString GanttPlan::styleKey(BarShape shape) { return barShapeKeys().at(int(shape)); }
QString GanttPlan::styleKey(SummaryShape shape) { return summaryShapeKeys().at(int(shape)); }
QString GanttPlan::styleKey(MilestoneShape shape) { return milestoneShapeKeys().at(int(shape)); }
QString GanttPlan::styleKey(NodeFillStyle style) { return NodeStyle::fillStyleKey(style); }
QString GanttPlan::styleKey(BorderStyle style) { return borderStyleKeys().at(int(style)); }

const QStringList &GanttPlan::styleKeys(StyleList list) {
    static const QStringList linkLines{QStringLiteral("solid"), QStringLiteral("dashed"), QStringLiteral("dotted")};  // by LinkLine
    switch (list) {
    case StyleList::BarShape: return barShapeKeys();
    case StyleList::SummaryShape: return summaryShapeKeys();
    case StyleList::MilestoneShape: return milestoneShapeKeys();
    case StyleList::Border: return borderStyleKeys();
    case StyleList::LinkLine: return linkLines;
    case StyleList::LinkWidth: return linkWidthKeys();
    case StyleList::LinkEndSize: return linkEndSizeKeys();
    }
    return linkLines;
}

bool GanttPlan::validStyleKey(OwnStyle part, const QString &key) {
    switch (part) {
    case OwnStyle::Shape: return barShapeKeys().contains(key) || summaryShapeKeys().contains(key) || milestoneShapeKeys().contains(key);
    case OwnStyle::Fill: return NodeStyle::fillStyleFromKey(key).has_value();
    case OwnStyle::Border: return borderStyleKeys().contains(key);
    }
    return false;
}

bool GanttPlan::setOwnStyle(const QString &id, OwnStyle part, const QString &key) {
    const int index = indexOf(id);
    if (index < 0 || (!key.isEmpty() && !validStyleKey(part, key))) return false;
    GanttTask &task = m_tasks[index];
    QString &value = part == OwnStyle::Shape ? task.shape : part == OwnStyle::Fill ? task.fill : task.border;
    if (value == key) return false;
    value = key;
    return true;
}

bool GanttPlan::setSummaryShape(SummaryShape shape) {
    if (shape == m_summaryShape) return false;
    m_summaryShape = shape;
    return true;
}

bool GanttPlan::setLinkLine(LinkLine line) {
    if (line == m_linkLine) return false;
    m_linkLine = line;
    return true;
}

bool GanttPlan::setLinkEnd(BranchEnd end) {
    if (end == m_linkEnd) return false;
    m_linkEnd = end;
    return true;
}

bool GanttPlan::setLinkWidth(LinkWidth width) {
    if (width == m_linkWidth) return false;
    m_linkWidth = width;
    return true;
}

bool GanttPlan::setLinkEndSize(LinkEndSize size) {
    if (size == m_linkEndSize) return false;
    m_linkEndSize = size;
    return true;
}

bool GanttPlan::setMilestoneShape(MilestoneShape shape) {
    if (shape == m_milestoneShape) return false;
    m_milestoneShape = shape;
    return true;
}

bool GanttPlan::setBorderStyle(BarKind kind, BorderStyle style) {
    if (style == m_borderStyles[int(kind)]) return false;
    m_borderStyles[int(kind)] = style;
    return true;
}

bool GanttPlan::setFillStyle(BarKind kind, NodeFillStyle style) {
    if (style == m_fillStyles[int(kind)]) return false;
    m_fillStyles[int(kind)] = style;
    return true;
}

GanttPlan::BarKind GanttPlan::barKind(const QString &id) const {
    if (hasChildren(id)) return BarKind::Summary;
    const GanttTask *found = task(id);
    return found && found->milestone ? BarKind::Milestone : BarKind::Task;
}

bool GanttPlan::setDefaultColor(BarKind kind, const QColor &color) {
    QColor &target = m_defaultColors[int(kind)];
    const QColor opaque = color.isValid() ? QColor(color.rgb()) : QColor();
    if (opaque == target) return false;
    target = opaque;
    return true;
}

bool GanttPlan::setCalendar(const GanttCalendar &calendar) {
    if (calendar.workingDays.isEmpty() || calendar == m_calendar) return false;
    if (calendar.minutesPerDay < GanttCalendar::kMinutesStep || calendar.minutesPerDay > GanttCalendar::kMaxMinutesPerDay
        || calendar.minutesPerDay % GanttCalendar::kMinutesStep)
        return false;
    for (int day : calendar.workingDays)
        if (day < 1 || day > 7) return false;
    for (const QDate &holiday : calendar.holidays)
        if (!holiday.isValid()) return false;
    m_calendar = calendar;
    return true;
}

int GanttPlan::workingDaysBetween(const QDate &first, const QDate &last) const {
    if (!first.isValid() || !last.isValid() || last < first) return 0;
    const WorkingDays days(m_calendar);
    return int(days.index(last.toJulianDay() + 1) - days.index(first));
}

GanttSchedule GanttPlan::schedule() const {
    GanttSchedule result;
    const int count = m_tasks.size();
    if (count == 0) return result;
    const WorkingDays days(m_calendar);

    QHash<QString, int> index;
    index.reserve(count);
    for (int i = 0; i < count; ++i) index.insert(m_tasks.at(i).id, i);
    QVector<QVector<int>> children(count);
    QVector<int> topLevel;
    for (int i = 0; i < count; ++i) {
        const int parent = index.value(m_tasks.at(i).parentId, -1);
        (parent < 0 ? topLevel : children[parent]).append(i);
    }

    // Dependencies between leaves, in topological order (Kahn). The plan refuses cycles; a task left
    // over anyway goes last and its dependencies against the order are ignored.
    QVector<QVector<int>> incoming(count), outgoing(count);
    QVector<int> pending(count, 0);
    for (int d = 0; d < m_dependencies.size(); ++d) {
        Q_ASSERT(index.contains(m_dependencies.at(d).from) && index.contains(m_dependencies.at(d).to));
        const int from = index.value(m_dependencies.at(d).from), to = index.value(m_dependencies.at(d).to);
        if (!children.at(from).isEmpty() || !children.at(to).isEmpty()) {
            result.ignoredDependencies.insert(d);
            continue;
        }
        incoming[to].append(d);
        outgoing[from].append(d);
        ++pending[to];
    }
    QVector<int> order;
    order.reserve(count);
    for (int i = 0; i < count; ++i)
        if (children.at(i).isEmpty() && pending.at(i) == 0) order.append(i);
    for (int next = 0; next < order.size(); ++next)
        for (int d : outgoing.at(order.at(next))) {
            const int to = index.value(m_dependencies.at(d).to);
            if (--pending[to] == 0) order.append(to);
        }
    QVector<int> position(count, -1);
    for (int p = 0; p < order.size(); ++p) position[order.at(p)] = p;
    for (int i = 0; i < count; ++i)
        if (children.at(i).isEmpty() && position.at(i) < 0) {
            position[i] = order.size();
            order.append(i);
        }
    const auto active = [&](int d) {
        const int from = index.value(m_dependencies.at(d).from), to = index.value(m_dependencies.at(d).to);
        return position.at(from) < position.at(to);
    };
    for (int d = 0; d < m_dependencies.size(); ++d)
        if (!result.ignoredDependencies.contains(d) && !active(d)) result.ignoredDependencies.insert(d);

    // Forward pass: starts and exclusive finishes as working-day indices. A task without a date gets
    // one from its dated predecessors; with neither it stays undated (no bar, FEAT-075).
    QVector<Day> start(count, 0), finish(count, 0), latestFinish(count, 0);
    QVector<bool> dated(count, false);
    QVector<Day> required(m_dependencies.size(), 0);  // start each dependency asks of its successor
    // A task with dated predecessors is placed by them (the latest one asked; user, 2026-10-27): its
    // stored start counts only without them.
    for (int i : std::as_const(order)) {
        const Day duration = m_tasks.at(i).length();
        bool linked = false;
        for (int d : incoming.at(i))
            if (active(d) && dated.at(index.value(m_dependencies.at(d).from))) linked = true;
        bool hasDate = linked || m_tasks.at(i).start.isValid();
        Day earliest = !linked && hasDate ? days.index(m_tasks.at(i).start) : std::numeric_limits<Day>::min();
        int placing = -1;
        for (int d : incoming.at(i)) {
            if (!active(d) || !dated.at(index.value(m_dependencies.at(d).from))) continue;
            const GanttDependency &dependency = m_dependencies.at(d);
            const int from = index.value(dependency.from);
            switch (dependency.type) {
            case GanttDependency::Type::FinishStart: required[d] = finish.at(from) + dependency.lag; break;
            case GanttDependency::Type::StartStart: required[d] = start.at(from) + dependency.lag; break;
            case GanttDependency::Type::FinishFinish: required[d] = finish.at(from) + dependency.lag - duration; break;
            case GanttDependency::Type::StartFinish: required[d] = start.at(from) + dependency.lag - duration; break;
            }
            if (placing < 0 || required.at(d) > earliest) {
                earliest = required.at(d);
                placing = d;
            }
        }
        if (placing >= 0) result.placedBy.insert(m_tasks.at(i).id, placing);
        dated[i] = hasDate;
        if (!hasDate) continue;
        start[i] = earliest;
        finish[i] = earliest + duration;
    }

    // Backward pass from the project's finish (dated tasks only).
    Day projectFinish = std::numeric_limits<Day>::min();
    for (int i : std::as_const(order))
        if (dated.at(i)) projectFinish = std::max(projectFinish, finish.at(i));
    for (int p = order.size() - 1; p >= 0; --p) {
        const int i = order.at(p);
        if (!dated.at(i)) continue;
        const Day duration = m_tasks.at(i).length();
        Day latest = projectFinish;
        for (int d : outgoing.at(i)) {
            if (!active(d) || !dated.at(index.value(m_dependencies.at(d).to))) continue;
            const GanttDependency &dependency = m_dependencies.at(d);
            const int to = index.value(dependency.to);
            const Day toFinish = latestFinish.at(to), toStart = toFinish - m_tasks.at(to).length();
            switch (dependency.type) {
            case GanttDependency::Type::FinishStart: latest = std::min(latest, toStart - dependency.lag); break;
            case GanttDependency::Type::StartStart: latest = std::min(latest, toStart - dependency.lag + duration); break;
            case GanttDependency::Type::FinishFinish: latest = std::min(latest, toFinish - dependency.lag); break;
            case GanttDependency::Type::StartFinish: latest = std::min(latest, toFinish - dependency.lag + duration); break;
            }
        }
        latestFinish[i] = latest;
    }

    struct Span {
        Day start = std::numeric_limits<Day>::max();
        Day finish = std::numeric_limits<Day>::min();
        Day totalFloat = std::numeric_limits<Day>::max();
        qint64 weightedProgress = 0, weight = 0, progressSum = 0, leaves = 0;
        bool critical = false;
    };
    QVector<Span> spans(count);
    for (int i : std::as_const(order)) {
        const GanttTask &task = m_tasks.at(i);
        Span &span = spans[i];
        if (dated.at(i)) {
            span.start = start.at(i);
            span.finish = finish.at(i);
            span.totalFloat = latestFinish.at(i) - finish.at(i);
            span.critical = span.totalFloat <= 0;
        }
        span.weightedProgress = qint64(task.progress) * task.length();
        span.weight = task.length();
        span.progressSum = task.progress;
        span.leaves = 1;
    }
    // Summaries from their children: parents come before children in a breadth-first order.
    QVector<int> breadth = topLevel;
    for (int next = 0; next < breadth.size(); ++next) breadth += children.at(breadth.at(next));
    for (int p = breadth.size() - 1; p >= 0; --p) {
        const int i = breadth.at(p);
        const int parent = index.value(m_tasks.at(i).parentId, -1);
        if (parent < 0) continue;
        const Span &child = spans.at(i);
        Span &span = spans[parent];
        span.start = std::min(span.start, child.start);
        span.finish = std::max(span.finish, child.finish);
        span.totalFloat = std::min(span.totalFloat, child.totalFloat);
        span.weightedProgress += child.weightedProgress;
        span.weight += child.weight;
        span.progressSum += child.progressSum;
        span.leaves += child.leaves;
        span.critical = span.critical || child.critical;
    }

    for (int i = 0; i < count; ++i) {
        const Span &span = spans.at(i);
        GanttScheduledTask scheduled;
        scheduled.summary = !children.at(i).isEmpty();
        scheduled.progress = int(span.weight > 0 ? span.weightedProgress / span.weight
                                                 : span.progressSum / std::max<qint64>(span.leaves, 1));
        scheduled.dated = span.start != std::numeric_limits<Day>::max();
        if (scheduled.dated) {
            scheduled.start = days.date(span.start);
            scheduled.end = days.date(std::max(span.finish - 1, span.start));
            scheduled.totalFloat = int(std::clamp<Day>(span.totalFloat, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
            scheduled.critical = span.critical;
            if (!result.start.isValid() || scheduled.start < result.start) result.start = scheduled.start;
            if (!result.end.isValid() || scheduled.end > result.end) result.end = scheduled.end;
        }
        result.tasks.insert(m_tasks.at(i).id, scheduled);
    }
    for (int d = 0; d < m_dependencies.size(); ++d) {
        if (result.ignoredDependencies.contains(d)) continue;
        const int from = index.value(m_dependencies.at(d).from), to = index.value(m_dependencies.at(d).to);
        if (dated.at(from) && dated.at(to) && spans.at(from).critical && spans.at(to).critical && required.at(d) == start.at(to))
            result.criticalDependencies.insert(d);
    }
    return result;
}

QJsonObject GanttPlan::toJson() const {
    QJsonArray tasks;
    for (const GanttTask &task : m_tasks) {
        QJsonObject object{{QStringLiteral("id"), task.id},
                           {QStringLiteral("name"), task.name},
                           {QStringLiteral("duration"), task.duration}};
        if (task.start.isValid()) object.insert(QStringLiteral("start"), task.start.toString(Qt::ISODate));
        if (!task.parentId.isEmpty()) object.insert(QStringLiteral("parent"), task.parentId);
        if (!task.nodeId.isEmpty()) object.insert(QStringLiteral("node"), task.nodeId);
        if (task.progress != 0) object.insert(QStringLiteral("progress"), task.progress);
        if (task.milestone) object.insert(QStringLiteral("milestone"), true);
        if (task.color.isValid()) object.insert(QStringLiteral("color"), task.color.name(task.color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb));
        if (!task.shape.isEmpty()) object.insert(QStringLiteral("shape"), task.shape);
        if (!task.fill.isEmpty()) object.insert(QStringLiteral("fill"), task.fill);
        if (!task.border.isEmpty()) object.insert(QStringLiteral("border"), task.border);
        tasks.append(object);
    }
    QJsonArray dependencies;
    for (const GanttDependency &dependency : m_dependencies) {
        QJsonObject object{{QStringLiteral("from"), dependency.from}, {QStringLiteral("to"), dependency.to}};
        if (dependency.type != GanttDependency::Type::FinishStart)
            object.insert(QStringLiteral("type"), QString::fromLatin1(typeKey(dependency.type)));
        if (dependency.lag != 0) object.insert(QStringLiteral("lag"), dependency.lag);
        dependencies.append(object);
    }
    QJsonObject json{{QStringLiteral("tasks"), tasks}};
    if (!dependencies.isEmpty()) json.insert(QStringLiteral("dependencies"), dependencies);
    if (!(m_calendar == GanttCalendar())) {
        QList<int> workingDays(m_calendar.workingDays.cbegin(), m_calendar.workingDays.cend());
        std::sort(workingDays.begin(), workingDays.end());
        QList<QDate> holidays(m_calendar.holidays.cbegin(), m_calendar.holidays.cend());
        std::sort(holidays.begin(), holidays.end());
        QJsonArray days, holidayArray;
        for (int day : workingDays) days.append(day);
        for (const QDate &holiday : holidays) holidayArray.append(holiday.toString(Qt::ISODate));
        QJsonObject calendar{{QStringLiteral("workingDays"), days}};
        if (!holidayArray.isEmpty()) calendar.insert(QStringLiteral("holidays"), holidayArray);
        if (m_calendar.minutesPerDay != GanttCalendar::kDefaultMinutesPerDay)
            calendar.insert(QStringLiteral("minutesPerDay"), m_calendar.minutesPerDay);
        json.insert(QStringLiteral("calendar"), calendar);
    }
    if (m_barShape != BarShape::Pill) json.insert(QStringLiteral("barShape"), barShapeKeys().at(int(m_barShape)));  // pill not written
    if (m_summaryShape != SummaryShape::Bracket)  // bracket not written
        json.insert(QStringLiteral("summaryShape"), summaryShapeKeys().at(int(m_summaryShape)));
    if (m_linkLine != LinkLine::Solid)  // solid not written
        json.insert(QStringLiteral("linkLine"), m_linkLine == LinkLine::Dashed ? QStringLiteral("dashed") : QStringLiteral("dotted"));
    if (m_linkEnd != BranchEnd::FilledArrow) json.insert(QStringLiteral("linkEnd"), NodeStyle::branchEndKey(m_linkEnd));  // filled arrow not written
    if (m_linkWidth != LinkWidth::Thin) json.insert(QStringLiteral("linkWidth"), linkWidthKeys().at(int(m_linkWidth)));  // thin not written
    if (m_linkEndSize != LinkEndSize::Small)  // small not written
        json.insert(QStringLiteral("linkEndSize"), linkEndSizeKeys().at(int(m_linkEndSize)));
    if (m_milestoneShape != MilestoneShape::Diamond)  // diamond not written
        json.insert(QStringLiteral("milestoneShape"), milestoneShapeKeys().at(int(m_milestoneShape)));
    for (int kind = 0; kind < kBarKinds; ++kind)  // solid not written
        if (m_borderStyles[kind] != BorderStyle::Solid)
            json.insert(borderKeys().at(kind), borderStyleKeys().at(int(m_borderStyles[kind])));
    for (int kind = 0; kind < kBarKinds; ++kind)  // solid not written
        if (m_fillStyles[kind] != NodeFillStyle::Pieno) json.insert(fillKeys().at(kind), NodeStyle::fillStyleKey(m_fillStyles[kind]));
    for (int kind = 0; kind < kBarKinds; ++kind)  // the view's defaults not written
        if (m_defaultColors[kind].isValid()) json.insert(colorKeys().at(kind), m_defaultColors[kind].name());
    QJsonArray baselines;  // FEAT-082
    for (const GanttBaseline &baseline : m_baselines) {
        QJsonArray tasks;
        for (const GanttBaselineTask &task : baseline.tasks) {
            QJsonObject object{{QStringLiteral("id"), task.id}, {QStringLiteral("name"), task.name},
                               {QStringLiteral("duration"), task.duration}};
            if (task.dated) {
                object.insert(QStringLiteral("start"), task.start.toString(Qt::ISODate));
                object.insert(QStringLiteral("end"), task.end.toString(Qt::ISODate));
            }
            if (task.milestone) object.insert(QStringLiteral("milestone"), true);
            tasks.append(object);
        }
        baselines.append(QJsonObject{{QStringLiteral("id"), baseline.id},
                                     {QStringLiteral("name"), baseline.name},
                                     {QStringLiteral("taken"), baseline.taken.toString(Qt::ISODateWithMs)},
                                     {QStringLiteral("tasks"), tasks}});
    }
    if (!baselines.isEmpty()) json.insert(QStringLiteral("baselines"), baselines);
    return json;
}

GanttPlan GanttPlan::fromJson(const QJsonObject &json, int *repairs) {
    int fixed = 0;
    GanttPlan plan;
    // An absent key takes the fallback; a value that is not a whole number in range is repaired.
    const auto integer = [&fixed](const QJsonValue &value, int fallback, int low, int high) {
        if (value.isUndefined()) return fallback;
        const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(number) || number != std::floor(number)) {
            ++fixed;
            return fallback;
        }
        const double clamped = std::clamp(number, double(low), double(high));
        if (clamped != number) ++fixed;
        return int(clamped);
    };

    const QJsonObject calendarJson = json.value(QStringLiteral("calendar")).toObject();
    if (calendarJson.contains(QStringLiteral("workingDays"))) {
        plan.m_calendar.workingDays.clear();
        for (const QJsonValue &value : calendarJson.value(QStringLiteral("workingDays")).toArray()) {
            const int day = value.toInt();
            if (day >= 1 && day <= 7) plan.m_calendar.workingDays.insert(day);
            else ++fixed;
        }
        if (plan.m_calendar.workingDays.isEmpty()) {
            plan.m_calendar.workingDays = GanttCalendar().workingDays;
            ++fixed;
        }
    }
    int minutes = integer(calendarJson.value(QStringLiteral("minutesPerDay")), GanttCalendar::kDefaultMinutesPerDay,
                          GanttCalendar::kMinutesStep, GanttCalendar::kMaxMinutesPerDay);
    if (minutes % GanttCalendar::kMinutesStep) {  // to the nearest quarter hour
        minutes = (minutes + GanttCalendar::kMinutesStep / 2) / GanttCalendar::kMinutesStep * GanttCalendar::kMinutesStep;
        ++fixed;
    }
    plan.m_calendar.minutesPerDay = minutes;
    for (const QJsonValue &value : calendarJson.value(QStringLiteral("holidays")).toArray()) {
        const QDate holiday = QDate::fromString(value.toString(), Qt::ISODate);
        if (holiday.isValid()) plan.m_calendar.holidays.insert(holiday);
        else ++fixed;
    }

    // FEAT-080: absent = pill; an unknown value is read as pill too.
    if (const QJsonValue shape = json.value(QStringLiteral("barShape")); !shape.isUndefined()) {
        const int index = int(barShapeKeys().indexOf(shape.toString()));
        if (index >= 0) plan.m_barShape = BarShape(index);
        else ++fixed;
    }
    if (const QJsonValue shape = json.value(QStringLiteral("summaryShape")); !shape.isUndefined()) {  // absent or unknown = bracket
        const int index = int(summaryShapeKeys().indexOf(shape.toString()));
        if (index >= 0) plan.m_summaryShape = SummaryShape(index);
        else ++fixed;
    }
    if (const QJsonValue line = json.value(QStringLiteral("linkLine")); !line.isUndefined()) {  // absent or unknown = solid
        if (line.toString() == QLatin1StringView("dashed")) plan.m_linkLine = LinkLine::Dashed;
        else if (line.toString() == QLatin1StringView("dotted")) plan.m_linkLine = LinkLine::Dotted;
        else ++fixed;
    }
    if (const QJsonValue end = json.value(QStringLiteral("linkEnd")); !end.isUndefined()) {  // absent or unknown = filled arrow
        if (const std::optional<BranchEnd> mark = NodeStyle::branchEndFromKey(end.toString())) plan.m_linkEnd = *mark;
        else ++fixed;
    }
    if (const QJsonValue width = json.value(QStringLiteral("linkWidth")); !width.isUndefined()) {  // absent or unknown = thin
        const int index = int(linkWidthKeys().indexOf(width.toString()));
        if (index >= 0) plan.m_linkWidth = LinkWidth(index);
        else ++fixed;
    }
    if (const QJsonValue size = json.value(QStringLiteral("linkEndSize")); !size.isUndefined()) {  // absent or unknown = small
        const int index = int(linkEndSizeKeys().indexOf(size.toString()));
        if (index >= 0) plan.m_linkEndSize = LinkEndSize(index);
        else ++fixed;
    }
    if (const QJsonValue shape = json.value(QStringLiteral("milestoneShape")); !shape.isUndefined()) {  // absent or unknown = diamond
        const int index = int(milestoneShapeKeys().indexOf(shape.toString()));
        if (index >= 0) plan.m_milestoneShape = MilestoneShape(index);
        else ++fixed;
    }
    for (int kind = 0; kind < kBarKinds; ++kind) {  // absent or unknown = solid
        const QJsonValue value = json.value(borderKeys().at(kind));
        if (value.isUndefined()) continue;
        const int index = int(borderStyleKeys().indexOf(value.toString()));
        if (index >= 0) plan.m_borderStyles[kind] = BorderStyle(index);
        else ++fixed;
    }
    for (int kind = 0; kind < kBarKinds; ++kind) {  // absent or unknown = solid
        const QJsonValue value = json.value(fillKeys().at(kind));
        if (value.isUndefined()) continue;
        if (const auto style = NodeStyle::fillStyleFromKey(value.toString())) plan.m_fillStyles[kind] = *style;
        else ++fixed;
    }
    for (int kind = 0; kind < kBarKinds; ++kind) {  // absent = the view's colour; an invalid or translucent one is dropped
        const QJsonValue value = json.value(colorKeys().at(kind));
        if (value.isUndefined()) continue;
        const QColor color = QColor::fromString(value.toString());
        if (color.isValid() && color.alpha() == 255) plan.m_defaultColors[kind] = color;
        else ++fixed;
    }

    // FEAT-082: a baseline without id or name is dropped, a task without id too; bad dates leave it undated.
    for (const QJsonValue &value : json.value(QStringLiteral("baselines")).toArray()) {
        const QJsonObject object = value.toObject();
        GanttBaseline baseline;
        baseline.id = object.value(QStringLiteral("id")).toString();
        baseline.name = baselineName(object.value(QStringLiteral("name")).toString());
        baseline.taken = QDateTime::fromString(object.value(QStringLiteral("taken")).toString(), Qt::ISODateWithMs);
        if (baseline.id.isEmpty() || baseline.name.isEmpty() || plan.baseline(baseline.id)) {
            ++fixed;
            continue;
        }
        QSet<QString> seen;
        for (const QJsonValue &taskValue : object.value(QStringLiteral("tasks")).toArray()) {
            const QJsonObject taskObject = taskValue.toObject();
            GanttBaselineTask task;
            task.id = taskObject.value(QStringLiteral("id")).toString();
            if (task.id.isEmpty() || seen.contains(task.id)) {
                ++fixed;
                continue;
            }
            seen.insert(task.id);
            task.name = taskObject.value(QStringLiteral("name")).toString();
            task.start = QDate::fromString(taskObject.value(QStringLiteral("start")).toString(), Qt::ISODate);
            task.end = QDate::fromString(taskObject.value(QStringLiteral("end")).toString(), Qt::ISODate);
            task.dated = task.start.isValid() && task.end.isValid() && task.start <= task.end;
            if (!task.dated) task.start = task.end = QDate();
            task.duration = integer(taskObject.value(QStringLiteral("duration")), 0, 0, kMaxDuration);
            task.milestone = taskObject.value(QStringLiteral("milestone")).toBool();
            baseline.tasks.append(task);
        }
        plan.m_baselines.append(baseline);
    }

    const QJsonArray taskArray = json.value(QStringLiteral("tasks")).toArray();
    QHash<QString, int> index;
    for (const QJsonValue &value : taskArray) {
        const QJsonObject object = value.toObject();
        GanttTask task;
        task.id = object.value(QStringLiteral("id")).toString();
        if (task.id.isEmpty() || index.contains(task.id)) {
            task.id = newId();
            ++fixed;
        }
        index.insert(task.id, plan.m_tasks.size());
        task.name = object.value(QStringLiteral("name")).toString();
        task.parentId = object.value(QStringLiteral("parent")).toString();
        task.nodeId = object.value(QStringLiteral("node")).toString();
        // No "start" = no date yet (FEAT-075); a date that cannot be read is dropped and counted.
        const QString start = object.value(QStringLiteral("start")).toString();
        task.start = QDate::fromString(start, Qt::ISODate);
        if (!start.isEmpty() && !validStart(task.start)) {
            task.start = QDate();
            ++fixed;
        }
        task.duration = integer(object.value(QStringLiteral("duration")), 1, kMinDuration, kMaxDuration);
        task.milestone = object.value(QStringLiteral("milestone")).toBool();
        task.progress = integer(object.value(QStringLiteral("progress")), 0, 0, kMaxProgress);
        const QString color = object.value(QStringLiteral("color")).toString();
        if (!color.isEmpty()) {
            task.color = rgb(QColor::fromString(color));
            if (!task.color.isValid()) ++fixed;
        }
        for (const auto &[part, key, value] : {std::tuple{OwnStyle::Shape, QStringLiteral("shape"), &task.shape},
                                               std::tuple{OwnStyle::Fill, QStringLiteral("fill"), &task.fill},
                                               std::tuple{OwnStyle::Border, QStringLiteral("border"), &task.border}}) {
            const QString text = object.value(key).toString();
            if (validStyleKey(part, text)) *value = text;
            else if (object.contains(key)) ++fixed;
        }
        plan.m_tasks.append(task);
    }
    for (GanttTask &task : plan.m_tasks)
        if (!task.parentId.isEmpty() && !index.contains(task.parentId)) {
            task.parentId.clear();
            ++fixed;
        }
    // Parent loops: each chain is walked once (1 = on the current walk, 2 = done). The task whose
    // parent closes a loop moves to the top level; tasks hanging below the loop keep their parent.
    QVector<char> state(plan.m_tasks.size(), 0);
    for (int i = 0; i < plan.m_tasks.size(); ++i) {
        QVector<int> walk;
        for (int current = i; current >= 0 && state.at(current) == 0;) {
            state[current] = 1;
            walk.append(current);
            const int parent = index.value(plan.m_tasks.at(current).parentId, -1);
            if (parent >= 0 && state.at(parent) == 1) {
                plan.m_tasks[current].parentId.clear();
                ++fixed;
                break;
            }
            current = parent;
        }
        for (int task : std::as_const(walk)) state[task] = 2;
    }

    // Dependencies on summaries are kept (ignored while the task has children, FEAT-074). Unknown
    // ends, self links and repeated pairs are dropped here, cycles below.
    QSet<std::pair<QString, QString>> pairs;
    for (const QJsonValue &value : json.value(QStringLiteral("dependencies")).toArray()) {
        const QJsonObject object = value.toObject();
        GanttDependency dependency;
        dependency.from = object.value(QStringLiteral("from")).toString();
        dependency.to = object.value(QStringLiteral("to")).toString();
        const QString type = object.value(QStringLiteral("type")).toString(QStringLiteral("FS"));
        if (type == QLatin1String("SS")) dependency.type = GanttDependency::Type::StartStart;
        else if (type == QLatin1String("FF")) dependency.type = GanttDependency::Type::FinishFinish;
        else if (type == QLatin1String("SF")) dependency.type = GanttDependency::Type::StartFinish;
        else if (type != QLatin1String("FS")) ++fixed;
        dependency.lag = integer(object.value(QStringLiteral("lag")), 0, -kMaxLag, kMaxLag);
        if (!index.contains(dependency.from) || !index.contains(dependency.to) || dependency.from == dependency.to
            || pairs.contains({dependency.from, dependency.to}) || pairs.contains({dependency.to, dependency.from})) {
            ++fixed;
            continue;
        }
        pairs.insert({dependency.from, dependency.to});
        plan.m_dependencies.append(dependency);
    }
    // Depth-first search in file order drops each dependency leading back into the current path:
    // linear in tasks + dependencies, and what remains has no cycle.
    QVector<QVector<int>> outgoing(plan.m_tasks.size());
    for (int d = 0; d < plan.m_dependencies.size(); ++d) outgoing[index.value(plan.m_dependencies.at(d).from)].append(d);
    QVector<bool> dropped(plan.m_dependencies.size(), false);
    state = QVector<char>(plan.m_tasks.size(), 0);
    for (int root = 0; root < plan.m_tasks.size(); ++root) {
        if (state.at(root) != 0) continue;
        QVector<std::pair<int, int>> stack{{root, 0}};  // task, next outgoing dependency
        state[root] = 1;
        while (!stack.isEmpty()) {
            const int task = stack.last().first;
            if (stack.last().second == outgoing.at(task).size()) {
                state[task] = 2;
                stack.removeLast();
                continue;
            }
            const int d = outgoing.at(task).at(stack.last().second++);
            const int to = index.value(plan.m_dependencies.at(d).to);
            if (state.at(to) == 1) {
                dropped[d] = true;
                ++fixed;
            } else if (state.at(to) == 0) {
                state[to] = 1;
                stack.append({to, 0});
            }
        }
    }
    for (int d = plan.m_dependencies.size() - 1; d >= 0; --d)
        if (dropped.at(d)) plan.m_dependencies.removeAt(d);
    if (repairs) *repairs = fixed;
    return plan;
}

namespace GanttToday {
namespace {
QDate fixedDay;
}  // namespace

QDate date() { return fixedDay.isValid() ? fixedDay : QDate::currentDate(); }

void setForTests(const QDate &day) { fixedDay = day; }
}  // namespace GanttToday
