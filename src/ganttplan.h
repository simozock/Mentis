#pragma once
// FEAT-074 (docs/features/FEAT-074-gantt-modello-e-calcolo.md): data of a Gantt plan and the
// computation of its dates and critical path (Critical Path Method). Core value type, no widgets;
// edits that break a rule are refused and leave the plan unchanged.
#include "nodestyle.h"  // BranchEnd: the map relationships' end marks

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <array>

struct GanttTask {
    QString id;
    QString name;
    QString parentId;  // empty = top level; siblings keep the order of GanttPlan::tasks()
    QString nodeId;    // linked topic (FEAT-076), empty = none
    QDate start;       // "start no earlier than"; null = no date (FEAT-075)
    int duration = 1;  // working days, at least 1; kept while the task is a milestone
    int progress = 0;  // percent
    QColor color;      // invalid = default
    // FEAT-080: own style, keys (GanttPlan::styleKey); empty = the sheet's. The shape is a bar,
    // summary or milestone shape, read for the task's kind.
    QString shape;
    QString fill;
    QString border;
    bool milestone = false;  // a point on its start day: no length, no weight in the progress
    int length() const { return milestone ? 0 : duration; }  // working days it takes in the schedule
    bool operator==(const GanttTask &) const = default;
};

struct GanttDependency {
    enum class Type { FinishStart, StartStart, FinishFinish, StartFinish };
    QString from;  // predecessor
    QString to;    // successor
    Type type = Type::FinishStart;
    int lag = 0;   // working days, negative = lead
    bool operator==(const GanttDependency &) const = default;
};

// Working weekdays (Qt::Monday = 1 ... Qt::Sunday = 7) and holidays. Default: every day (user,
// 2026-10-27: weekends are shaded in the chart but tasks may run on them).
struct GanttCalendar {
    static constexpr int kDefaultMinutesPerDay = 8 * 60;
    static constexpr int kMaxMinutesPerDay = 24 * 60;
    static constexpr int kMinutesStep = 15;  // quarter hours (user, 2026-10-29: 8 h 15, 8 h 30)
    QSet<int> workingDays{1, 2, 3, 4, 5, 6, 7};
    QSet<QDate> holidays;
    // FEAT-079: length of a working day in minutes (15-1440, quarter hours), for the costs to come;
    // the schedule does not use it.
    int minutesPerDay = kDefaultMinutesPerDay;
    bool operator==(const GanttCalendar &) const = default;
    bool isWorkingDay(const QDate &date) const;
};

struct GanttScheduledTask {
    bool dated = false;   // false: no date of its own nor from a predecessor, no bar (FEAT-075)
    QDate start;
    QDate end;            // last working day, inclusive (a milestone: its start)
    int totalFloat = 0;   // working days; summaries: the smallest of their leaves
    int progress = 0;     // summaries: leaves weighted by duration
    bool critical = false;
    bool summary = false;
};

struct GanttSchedule {
    QHash<QString, GanttScheduledTask> tasks;
    QDate start;  // earliest task start (invalid with no tasks)
    QDate end;    // latest task end
    QSet<int> ignoredDependencies;   // indices in GanttPlan::dependencies(): an end is a summary
    QSet<int> criticalDependencies;
    // Task id -> the dependency that places it (the latest one asked), for tasks with predecessors.
    QHash<QString, int> placedBy;
};

// FEAT-082: a task as a baseline saw it, its computed dates (summaries included).
struct GanttBaselineTask {
    QString id;
    QString name;
    bool dated = false;
    QDate start;
    QDate end;
    int duration = 0;  // working days shown in the table (0 for a milestone)
    bool milestone = false;
    bool operator==(const GanttBaselineTask &) const = default;
};

// FEAT-082: a named snapshot of the plan, compared with the current one; never edited but its name.
struct GanttBaseline {
    static constexpr int kMaxName = 80;
    QString id;
    QString name;
    QDateTime taken;
    QVector<GanttBaselineTask> tasks;  // in the plan's order
    const GanttBaselineTask *task(const QString &id) const;
    bool operator==(const GanttBaseline &) const = default;
};

// FEAT-082: the current schedule against a baseline. Later and Earlier by the end; a start that
// moved with the same end is Same (the thin bar is grey). No dates on either side = Same.
struct GanttBaselineComparison {
    enum class Change { Same, Later, Earlier, New };
    QHash<QString, Change> tasks;
    QStringList removed;  // names of the baseline's tasks no longer in the plan, in its order
};

class GanttPlan {
public:
    static constexpr int kMinDuration = 1;
    static constexpr int kMaxDuration = 3650;
    static constexpr int kMaxLag = 3650;
    static constexpr int kMaxProgress = 100;
    static constexpr int kFirstYear = 1900;
    static constexpr int kLastYear = 2200;
    // FEAT-080: the shape of every bar of the chart, a look only (scheduling ignores it).
    enum class BarShape { Pill, Rounded, Rectangle, Arrow, Hexagon, Leaf, Ticket, Parallelogram };
    // FEAT-080: tasks with subtasks drawn as the other bars, or as a thin shape over their subtasks.
    enum class SummaryShape { Bar, Bracket, Brace, IBeam, Hook, Dimension, Arrow, Rail, Frame };
    // FEAT-080: line pattern of every dependency arrow; its end mark is one of the map's (BranchEnd).
    enum class LinkLine { Solid, Dashed, Dotted };
    // FEAT-080: thickness of every dependency arrow and size of its end mark (user, 2026-11-01);
    // the pixels are the view's (GanttChart::linkWidthPx, linkEndSizePx).
    enum class LinkWidth { Thin, Medium, Bold, ExtraBold };
    enum class LinkEndSize { Small, Medium, Large };
    // FEAT-080: the shape of every milestone; besides the diamond, some of the map's node shapes.
    enum class MilestoneShape { Diamond, Circle, Triangle, Star, Hexagon, Heart, Flag, Gem };
    // FEAT-080: the kinds of bar with a colour of the sheet. A milestone that is a summary draws as a summary.
    enum class BarKind { Task, Summary, Milestone };
    static constexpr int kBarKinds = 3;
    // FEAT-080: the border of each kind of bar (task bars, summaries as bars or brackets, milestones);
    // the hand-drawn ones as the map topics' (NodeSketch), added at the end so the values stay.
    enum class BorderStyle { Solid, Dashed, Dotted, None, HandDrawnSolid, HandDrawnDashed };

    const QVector<GanttTask> &tasks() const { return m_tasks; }
    const QVector<GanttDependency> &dependencies() const { return m_dependencies; }
    const GanttCalendar &calendar() const { return m_calendar; }
    BarShape barShape() const { return m_barShape; }
    SummaryShape summaryShape() const { return m_summaryShape; }
    LinkLine linkLine() const { return m_linkLine; }
    BranchEnd linkEnd() const { return m_linkEnd; }
    LinkWidth linkWidth() const { return m_linkWidth; }
    LinkEndSize linkEndSize() const { return m_linkEndSize; }
    MilestoneShape milestoneShape() const { return m_milestoneShape; }
    BorderStyle borderStyle(BarKind kind) const { return m_borderStyles[int(kind)]; }
    // FEAT-080: the fill of each kind of bar, the map topics' fill styles (solid, hand-drawn, strokes...).
    NodeFillStyle fillStyle(BarKind kind) const { return m_fillStyles[int(kind)]; }
    // FEAT-080: the sheet's colour of a kind of bar (invalid = the view's default: peach or
    // lilac, the milestones' slate). A branch colour chosen by hand on a topic still colours that bar alone.
    QColor defaultColor(BarKind kind) const { return m_defaultColors[int(kind)]; }
    BarKind barKind(const QString &id) const;  // Task for an unknown id
    const GanttTask *task(const QString &id) const;
    bool hasChildren(const QString &id) const;
    QStringList children(const QString &parentId) const;  // in order; empty parent = top level

    // Adds the task under its parentId at row among the siblings (-1 = last). An empty or used id
    // gets a new one. Returns the id; empty if refused (unknown parent, bad start or values).
    QString addTask(GanttTask task, int row = -1);
    // Removes the task, its descendants and their dependencies.
    bool removeTask(const QString &id);
    // Moves the task (with its descendants) under parentId at row among the new siblings (-1 = last).
    // Refused under itself or a descendant.
    bool moveTask(const QString &id, const QString &parentId, int row = -1);
    bool setName(const QString &id, const QString &name);
    bool setStart(const QString &id, const QDate &start);
    // FEAT-074 (user, 2026-10-27): puts the task's start on date. A task placed by a dependency keeps
    // it: that dependency's lag changes by the working days moved; otherwise the stored start.
    bool moveTo(const QString &id, const QDate &start);
    bool setDuration(const QString &id, int duration);
    bool setMilestone(const QString &id, bool milestone);
    bool setProgress(const QString &id, int progress);
    bool setColor(const QString &id, const QColor &color);
    bool setNodeId(const QString &id, const QString &nodeId);

    // Refused: unknown end, a summary end, the same task, an existing pair, a lag out of range or a
    // cycle. updateDependency changes type and lag of the existing pair.
    bool addDependency(const GanttDependency &dependency);
    bool updateDependency(const GanttDependency &dependency);
    // A successor left without a dependency placing it keeps its date as its stored start.
    bool removeDependency(const QString &from, const QString &to);
    // Refused without a working weekday or with values outside 1..7.
    bool setCalendar(const GanttCalendar &calendar);
    bool setBarShape(BarShape shape);
    bool setSummaryShape(SummaryShape shape);
    // FEAT-080: a task's own style (user, 2026-11-01), as its colour, over the sheet's: shape (a key
    // read for the task's kind; one of another kind only = the sheet's), fill and border.
    enum class OwnStyle { Shape, Fill, Border };
    BarShape barShape(const GanttTask &task) const;
    SummaryShape summaryShape(const GanttTask &task) const;
    MilestoneShape milestoneShape(const GanttTask &task) const;
    NodeFillStyle fillStyle(const GanttTask &task, BarKind kind) const;
    BorderStyle borderStyle(const GanttTask &task, BarKind kind) const;
    bool setOwnStyle(const QString &id, OwnStyle part, const QString &key);  // empty = the sheet's; unknown keys refused
    static bool validStyleKey(OwnStyle part, const QString &key);
    static QString styleKey(BarShape shape);
    static QString styleKey(SummaryShape shape);
    static QString styleKey(MilestoneShape shape);
    static QString styleKey(NodeFillStyle style);
    static QString styleKey(BorderStyle style);
    // FEAT-045: every key of a sheet style, in its enum's order (the file's keys), for the agents.
    enum class StyleList { BarShape, SummaryShape, MilestoneShape, Border, LinkLine, LinkWidth, LinkEndSize };
    static const QStringList &styleKeys(StyleList list);
    bool setLinkLine(LinkLine line);
    bool setLinkEnd(BranchEnd end);
    bool setLinkWidth(LinkWidth width);
    bool setLinkEndSize(LinkEndSize size);
    bool setMilestoneShape(MilestoneShape shape);
    bool setBorderStyle(BarKind kind, BorderStyle style);
    bool setFillStyle(BarKind kind, NodeFillStyle style);
    bool setDefaultColor(BarKind kind, const QColor &color);  // opaque; invalid = back to the view's default

    // FEAT-082: the sheet's baselines, oldest first. addBaseline takes the current schedule under
    // the name (spaces simplified, cut to GanttBaseline::kMaxName) and returns its id; empty for an
    // empty name or a plan without tasks. renameBaseline refuses an empty name.
    const QVector<GanttBaseline> &baselines() const { return m_baselines; }
    const GanttBaseline *baseline(const QString &id) const;
    QString addBaseline(const QString &name, const QDateTime &taken);
    bool renameBaseline(const QString &id, const QString &name);
    bool removeBaseline(const QString &id);
    GanttBaselineComparison compare(const GanttSchedule &schedule, const GanttBaseline &baseline) const;

    GanttSchedule schedule() const;
    // Working days from first to last, both included (0 if last is before first). FEAT-075.
    int workingDaysBetween(const QDate &first, const QDate &last) const;

    QJsonObject toJson() const;
    // Invalid entries are dropped or repaired (FEAT-074, "Rules"); repairs counts them.
    static GanttPlan fromJson(const QJsonObject &json, int *repairs = nullptr);

    bool operator==(const GanttPlan &) const = default;

private:
    int indexOf(const QString &id) const;
    int dependencyIndex(const QString &from, const QString &to) const;
    bool isDescendant(const QString &id, const QString &ancestorId) const;
    bool reaches(const QString &from, const QString &to) const;  // along the dependencies
    // Position in m_tasks that puts a task at row among the children of parentId.
    int insertPosition(const QString &parentId, int row, const QString &skipId = {}) const;

    QVector<GanttTask> m_tasks;
    QVector<GanttDependency> m_dependencies;
    GanttCalendar m_calendar;
    BarShape m_barShape = BarShape::Pill;
    SummaryShape m_summaryShape = SummaryShape::Bracket;  // the bracket by default (user, 2026-10-30)
    LinkLine m_linkLine = LinkLine::Solid;
    BranchEnd m_linkEnd = BranchEnd::FilledArrow;
    LinkWidth m_linkWidth = LinkWidth::Thin;
    LinkEndSize m_linkEndSize = LinkEndSize::Small;
    MilestoneShape m_milestoneShape = MilestoneShape::Diamond;
    std::array<QColor, kBarKinds> m_defaultColors;  // by BarKind
    std::array<BorderStyle, kBarKinds> m_borderStyles{};  // by BarKind
    std::array<NodeFillStyle, kBarKinds> m_fillStyles{};  // by BarKind; Pieno (solid) by default
    QVector<GanttBaseline> m_baselines;  // FEAT-082
};
