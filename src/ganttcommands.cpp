#include "ganttcommands.h"

#include "document.h"
#include "mindmapmodel.h"
#include "nodestyle.h"

#include <QSet>

#include <algorithm>
#include <functional>
#include <optional>

MindNode *GanttCommands::addTask(Document &document, MindNode *anchor, bool child) {
    if (!anchor) return document.addChild(nullptr);
    return child ? document.addChild(anchor) : document.addSibling(anchor);
}

bool GanttCommands::indent(Document &document, MindNode *topic) { return topic && document.indentNode(topic); }

bool GanttCommands::outdent(Document &document, MindNode *topic) {
    return topic && topic->parent && topic->parent->parent && document.outdentNode(topic);
}

bool GanttCommands::move(Document &document, MindNode *topic, MindNode *target, DropPlace place) {
    if (!topic || !target || topic == target) return false;
    MindNode *parent = place == DropPlace::Inside ? target : target->parent;
    int row = -1;  // inside: its last child
    if (place != DropPlace::Inside && parent) {
        const auto found = std::find_if(parent->children.cbegin(), parent->children.cend(),
                                        [target](const auto &child) { return child.get() == target; });
        row = int(found - parent->children.cbegin()) + (place == DropPlace::After ? 1 : 0);
    }
    // Cycles, summaries' topics and the same place are refused by the model.
    return parent && document.moveNode(topic, parent, row, 0);
}

QStringList GanttCommands::chainIds(const GanttPlan &plan, const QStringList &selectionOrder, const QStringList &selected) {
    QStringList ids;
    for (const QString &id : selectionOrder)
        if (selected.contains(id) && plan.task(id) && !plan.hasChildren(id)) ids.append(id);
    return ids;
}

bool GanttCommands::chainUnlinks(const GanttPlan &plan, const QStringList &ids) {
    const QSet<QString> chained(ids.cbegin(), ids.cend());
    const QVector<GanttDependency> &dependencies = plan.dependencies();
    return std::any_of(dependencies.cbegin(), dependencies.cend(),
                       [&](const GanttDependency &d) { return chained.contains(d.from) || chained.contains(d.to); });
}

bool GanttCommands::chain(GanttPlan &plan, const QStringList &ids, bool unlink) {
    if (unlink) {
        QVector<QPair<QString, QString>> links;
        for (const GanttDependency &d : plan.dependencies())
            if (ids.contains(d.from) || ids.contains(d.to)) links.append({d.from, d.to});
        bool removed = false;
        for (const auto &[from, to] : links) removed = plan.removeDependency(from, to) || removed;
        return removed;
    }
    if (ids.size() < 2) return false;
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
}

int GanttCommands::durationTo(const GanttPlan &plan, const QDate &start, const QDate &end) {
    return std::clamp(plan.workingDaysBetween(start, end), 1, GanttPlan::kMaxDuration);
}

QString GanttCommands::nextBaselineName(const GanttPlan &plan) {
    const QVector<GanttBaseline> &baselines = plan.baselines();
    for (int n = 1;; ++n) {
        const QString name = tr("Baseline %1").arg(n);
        if (std::none_of(baselines.cbegin(), baselines.cend(), [&name](const GanttBaseline &b) { return b.name == name; })) return name;
    }
}

int GanttCommands::baselineDifference(const GanttBaselineTask &then, const GanttScheduledTask &now, int duration,
                                      BaselineField field) {
    if (!then.dated) return 0;
    if (field == BaselineField::Duration) return duration >= 0 ? duration - then.duration : 0;
    if (!now.dated) return 0;
    return field == BaselineField::Start ? int(then.start.daysTo(now.start)) : int(then.end.daysTo(now.end));
}

QStringList GanttCommands::range(const QStringList &shown, const QString &from, const QString &to, bool fromIsTask) {
    QStringList ids;
    bool inside = false;
    for (const QString &id : shown) {
        const bool end = id == from || id == to;
        if (end || inside) ids.append(id);
        if (end && (inside || from == to || !fromIsTask)) break;
        if (end) inside = true;
    }
    return ids;
}

QList<GanttCommands::Row> GanttCommands::rows(const GanttPlan &plan, const QSet<QString> &folded) {
    QHash<QString, QStringList> children;  // by parent id, "" = top level
    QSet<QString> ids;
    for (const GanttTask &task : plan.tasks()) ids.insert(task.id);
    for (const GanttTask &task : plan.tasks()) children[ids.contains(task.parentId) ? task.parentId : QString()].append(task.id);
    QList<Row> result;
    const std::function<void(const QString &, int)> visit = [&](const QString &parent, int depth) {
        for (const QString &id : children.value(parent)) {
            const bool summary = children.contains(id);
            result.append({id, depth, summary});
            if (summary && !folded.contains(id)) visit(id, depth + 1);
        }
    };
    visit(QString(), 0);
    return result;
}

GanttCommands::BarLooks GanttCommands::barLooks(const MindNode *root) {
    BarLooks result;
    if (!root) return result;
    const std::function<void(const MindNode *)> visit = [&](const MindNode *topic) {
        if (showsName(topic)) result.labelled.insert(topic->id);
        if (NodeStyle::hasManualValue(topic, StyleProperty::BranchColor)) {
            const QColor color = topic->branchColor;
            result.looks.insert(topic->id, {color, color, GanttCanvas::barTextColor(color)});
        }
        for (const auto &child : topic->children) visit(child.get());
    };
    for (const auto &child : root->children) visit(child.get());
    return result;
}

bool GanttCommands::setNameOnBar(Document &document, const QList<MindNode *> &topics, bool shown) {
    QList<std::pair<MindNode *, std::optional<bool>>> labels;
    for (MindNode *topic : topics)
        if (topic) labels.append({topic, shown == !topic->children.empty() ? std::nullopt : std::optional<bool>(shown)});
    return !labels.isEmpty() && document.setBarLabels(labels);
}

bool GanttCommands::setMilestone(Document &document, const QList<MindNode *> &topics, bool milestone) {
    QList<MindNode *> changed;
    for (MindNode *topic : topics)
        if (topic && topic->children.empty() && (!milestone || topic->task == MindNode::Task::None)) changed.append(topic);
    return !changed.isEmpty() && document.setTask(changed, milestone ? MindNode::Task::Open : MindNode::Task::None);
}

QString GanttCommands::priorityName(int priority) {
    switch (priority) {
    case 1: return tr("Very low");
    case 2: return tr("Low");
    case 3: return tr("Medium");
    case 4: return tr("High");
    case 5: return tr("Very high");
    default: return {};
    }
}

bool GanttCommands::showsName(const MindNode *topic) { return topic && topic->ganttLabel.value_or(!topic->children.empty()); }
