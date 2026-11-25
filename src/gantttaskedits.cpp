#include "gantttaskedits.h"

#include "ganttcommands.h"

#include <algorithm>

namespace GanttTaskEdits {

bool editLeaves(GanttPlan &plan, const QStringList &targets, const std::function<bool(GanttPlan &, const QString &)> &change,
                bool skipMilestones) {
    bool changed = false;
    for (const QString &id : targets) {
        const GanttTask *task = plan.task(id);
        if (!task || plan.hasChildren(id) || (skipMilestones && task->milestone)) continue;
        changed = change(plan, id) || changed;
    }
    return changed;
}

bool setProgress(GanttPlan &plan, const QStringList &targets, int progress) {
    return editLeaves(plan, targets, [progress](GanttPlan &p, const QString &id) { return p.setProgress(id, progress); });
}

bool stepProgress(GanttPlan &plan, const QStringList &targets, int delta) {
    return editLeaves(plan, targets, [delta](GanttPlan &p, const QString &id) {
        return p.setProgress(id, std::clamp(p.task(id)->progress + delta, 0, GanttPlan::kMaxProgress));
    });
}

bool moveStart(GanttPlan &plan, const QStringList &targets, const QDate &start) {
    return editLeaves(plan, targets, [start](GanttPlan &p, const QString &id) { return p.moveTo(id, start); });
}

bool clearStart(GanttPlan &plan, const QStringList &targets) {
    return editLeaves(plan, targets, [](GanttPlan &p, const QString &id) { return p.setStart(id, QDate()); });
}

bool addStart(GanttPlan &plan, const GanttSchedule &schedule, const QStringList &targets) {
    return editLeaves(plan, targets, [&schedule](GanttPlan &p, const QString &id) {
        return !schedule.tasks.value(id).dated && p.setStart(id, GanttToday::date());
    });
}

bool setDuration(GanttPlan &plan, const QStringList &targets, int duration) {
    return editLeaves(plan, targets, [duration](GanttPlan &p, const QString &id) { return p.setDuration(id, duration); }, true);
}

bool stepDuration(GanttPlan &plan, const QStringList &targets, int delta) {
    return editLeaves(plan, targets, [delta](GanttPlan &p, const QString &id) {
        return p.setDuration(id, std::clamp(p.task(id)->duration + delta, GanttPlan::kMinDuration, GanttPlan::kMaxDuration));
    }, true);
}

bool setEnd(GanttPlan &plan, const GanttSchedule &schedule, const QStringList &targets, const QDate &end) {
    return editLeaves(plan, targets, [&schedule, end](GanttPlan &p, const QString &id) {
        const GanttScheduledTask scheduled = schedule.tasks.value(id);
        // Working days on the edited plan: its calendar is the one the panel was loaded with.
        return scheduled.dated && p.setDuration(id, GanttCommands::durationTo(p, scheduled.start, end));
    }, true);
}

Several several(const GanttPlan &plan, const GanttSchedule &schedule, const QStringList &targets) {
    Several result;
    bool all = true;
    for (const QString &id : targets) {
        const GanttTask *task = plan.task(id);
        if (!task || plan.hasChildren(id)) continue;
        result.leaves.append(id);
        result.progress.append(task->progress);
        const GanttScheduledTask scheduled = schedule.tasks.value(id);
        if (!task->milestone) result.durations.append(task->duration);
        if (scheduled.dated && !result.firstStart.isValid()) {
            result.firstStart = scheduled.start;
            result.firstEnd = scheduled.end;
        }
        result.dated = result.dated || scheduled.dated;
        result.undated = result.undated || !scheduled.dated;
        result.stored = result.stored || task->start.isValid();
        result.lengths = result.lengths || (!task->milestone && scheduled.dated);
        all = all && task->milestone;
    }
    result.allMilestones = !result.leaves.isEmpty() && all;
    return result;
}


GanttDependency linkWith(const QString &id, bool predecessors, const QString &other) {
    GanttDependency link;
    link.from = predecessors ? other : id;
    link.to = predecessors ? id : other;
    return link;
}

QList<GanttDependency> links(const GanttPlan &plan, const QString &id, bool predecessors) {
    QList<GanttDependency> result;
    for (const GanttDependency &link : plan.dependencies())
        if ((predecessors ? link.to : link.from) == id && plan.task(predecessors ? link.from : link.to)) result.append(link);
    return result;
}

QStringList linkCandidates(const GanttPlan &plan, const QString &id, bool predecessors) {
    QStringList ids;
    for (const GanttTask &task : plan.tasks()) {
        GanttPlan trial = plan;  // addDependency refuses summaries, itself, existing links, cycles
        if (trial.addDependency(linkWith(id, predecessors, task.id))) ids.append(task.id);
    }
    return ids;
}

OwnStyle ownStyle(const GanttPlan &plan, const QStringList &targets) {
    using Kind = GanttPlan::BarKind;
    QList<int> shapes, fills, borders, kinds;
    OwnStyle result;
    for (const QString &id : targets) {
        const GanttTask *task = plan.task(id);
        if (!task) continue;
        const Kind kind = plan.barKind(id);
        kinds.append(int(kind));
        QString shapeKey;
        switch (kind) {
        case Kind::Task:
            shapes.append(int(plan.barShape(*task)));
            shapeKey = GanttPlan::styleKey(plan.barShape(*task));
            break;
        case Kind::Summary:
            shapes.append(int(plan.summaryShape(*task)));
            shapeKey = GanttPlan::styleKey(plan.summaryShape(*task));
            break;
        case Kind::Milestone:
            shapes.append(int(plan.milestoneShape(*task)));
            shapeKey = GanttPlan::styleKey(plan.milestoneShape(*task));
            break;
        }
        const NodeFillStyle fill = plan.fillStyle(*task, kind);
        const GanttPlan::BorderStyle border = plan.borderStyle(*task, kind);
        fills.append(int(fill));
        borders.append(int(border));
        result.ownShape = result.ownShape || (!task->shape.isEmpty() && task->shape == shapeKey);
        result.ownFill = result.ownFill || (!task->fill.isEmpty() && task->fill == GanttPlan::styleKey(fill));
        result.ownBorder = result.ownBorder || (!task->border.isEmpty() && task->border == GanttPlan::styleKey(border));
        result.ownBorderColor = result.ownBorderColor || task->borderColor.isValid();
    }
    const auto shared = [](const QList<int> &values) {
        return !values.isEmpty() && values.count(values.first()) == values.size() ? values.first() : -1;
    };
    result.kind = shared(kinds);
    result.shape = result.kind < 0 ? -1 : shared(shapes);
    result.ownShape = result.kind >= 0 && result.ownShape;
    result.fill = shared(fills);
    result.border = shared(borders);
    return result;
}

bool setOwnStyle(GanttPlan &plan, const QStringList &targets, GanttPlan::OwnStyle part, const QString &key) {
    bool changed = false;
    for (const QString &id : targets) changed = plan.setOwnStyle(id, part, key) || changed;
    return changed;
}

bool setBorderColor(GanttPlan &plan, const QStringList &targets, const QColor &color) {
    bool changed = false;
    for (const QString &id : targets) changed = plan.setBorderColor(id, color) || changed;
    return changed;
}

}  // namespace GanttTaskEdits
