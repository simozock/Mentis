#pragma once
// FEAT-122 stage 0 (docs/features/FEAT-122-gantt-sul-telefono.md): the task panel's rules for
// one or several tasks at once (FEAT-078), without widgets (MentisDomain), shared by the desktop's
// GanttTaskPanel and the phone. Each edit applies to the targets that are tasks without subtasks
// (the summaries' values come from them), milestones left out of the length fields; the caller
// runs it as one step (Document::editGantt).
#include "ganttplan.h"

#include <QColor>
#include <QDate>
#include <QList>
#include <QStringList>

#include <functional>

namespace GanttTaskEdits {

bool editLeaves(GanttPlan &plan, const QStringList &targets, const std::function<bool(GanttPlan &, const QString &)> &change,
                bool skipMilestones = false);

bool setProgress(GanttPlan &plan, const QStringList &targets, int progress);
bool stepProgress(GanttPlan &plan, const QStringList &targets, int delta);  // each from its own value
// A task placed by a dependency changes its lag (FEAT-074).
bool moveStart(GanttPlan &plan, const QStringList &targets, const QDate &start);
bool clearStart(GanttPlan &plan, const QStringList &targets);
// Today on the tasks without a date in schedule (the plan's when the panel was loaded).
bool addStart(GanttPlan &plan, const GanttSchedule &schedule, const QStringList &targets);
bool setDuration(GanttPlan &plan, const QStringList &targets, int duration);
bool stepDuration(GanttPlan &plan, const QStringList &targets, int delta);
// As the table's End: the duration in working days from each dated task's start, at least one.
bool setEnd(GanttPlan &plan, const GanttSchedule &schedule, const QStringList &targets, const QDate &end);

// What the panel's fields show for several tasks: the leaves' values (a field shows one only when
// they share it) and which date fields apply.
struct Several {
    QStringList leaves;     // targets without subtasks
    QList<int> progress;    // of the leaves
    QList<int> durations;   // of the leaves that are not milestones
    bool dated = false;     // some leaf has dates: Start shown
    bool undated = false;   // some leaf has none: Add date shown (when none is dated)
    bool stored = false;    // some leaf has its own start: the clear button shown
    bool lengths = false;   // some dated leaf has a length: End enabled
    bool allMilestones = false;
    QDate firstStart, firstEnd;  // the first dated leaf's dates
};
Several several(const GanttPlan &plan, const GanttSchedule &schedule, const QStringList &targets);

// FEAT-080: the Task page's Style section for the targets: the shape, fill and border they share (-1
// when they differ), the shape only for targets of one kind (kind -1 otherwise), and whether one of
// them has its own value (the × back to the sheet's; a shape key of another kind is not its own).
struct OwnStyle {
    int kind = -1;  // GanttPlan::BarKind
    int shape = -1, fill = -1, border = -1;
    bool ownShape = false, ownFill = false, ownBorder = false;
    bool ownBorderColor = false;  // one of them has its own border colour (user, 2026-11-22)
};
OwnStyle ownStyle(const GanttPlan &plan, const QStringList &targets);
// The targets' own shape, fill or border (a GanttPlan::styleKey), empty = back to the sheet's.
bool setOwnStyle(GanttPlan &plan, const QStringList &targets, GanttPlan::OwnStyle part, const QString &key);
// The targets' own border colour; invalid = back to the sheet's.
bool setBorderColor(GanttPlan &plan, const QStringList &targets, const QColor &color);

// The links of one task (FEAT-077): the one between id and other in the direction of the list
// (predecessors: other comes first), the task's links of that side in the plan's order, and the tasks
// a new link may reach (leaves, no existing link, no cycle). The candidates copy the plan once per task:
// asked when the list is opened, not at every load.
GanttDependency linkWith(const QString &id, bool predecessors, const QString &other);
QList<GanttDependency> links(const GanttPlan &plan, const QString &id, bool predecessors);
QStringList linkCandidates(const GanttPlan &plan, const QString &id, bool predecessors);

}  // namespace GanttTaskEdits
