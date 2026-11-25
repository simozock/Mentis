#pragma once
// FEAT-122 stage 0 (docs/features/FEAT-122-gantt-sul-telefono.md): the Gantt's row operations
// without widgets (MentisDomain), shared by the desktop's GanttView and the phone. The rows are the
// map's topics (ADR-004): adding, indenting and moving a task changes the map through Document, one
// history step each; chaining edits the plan (the caller passes the edit to Document::editGantt).
#include "ganttcanvas.h"
#include "ganttplan.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

class Document;
struct MindNode;

class GanttCommands {
    Q_DECLARE_TR_FUNCTIONS(GanttCommands)
public:
    // Where a dragged row goes: before, inside (last child) or after the target's topic.
    enum class DropPlace { Before, Inside, After };

    // A sibling after anchor's topic, its child, or a main topic when there is no anchor; null when refused.
    static MindNode *addTask(Document &document, MindNode *anchor, bool child);
    static bool indent(Document &document, MindNode *topic);
    // A main topic stays one: the central topic has no row.
    static bool outdent(Document &document, MindNode *topic);
    // The topic with its branch before, inside or after the target's; false for a cycle or the same place.
    static bool move(Document &document, MindNode *topic, MindNode *target, DropPlace place);

    // Chain / Unchain (FEAT-077): the selected tasks in the order they were selected, summaries and
    // ids not in the plan left out.
    static QStringList chainIds(const GanttPlan &plan, const QStringList &selectionOrder, const QStringList &selected);
    // A toggle (user, 2026-10-29): when some of the tasks have links, chaining removes all their links.
    static bool chainUnlinks(const GanttPlan &plan, const QStringList &ids);
    // The edit: their links removed (unlink), or finish-start links in a chain (existing or cyclic
    // ones skipped), a first task without a date starting today. False when nothing changed.
    static bool chain(GanttPlan &plan, const QStringList &ids, bool unlink);

    // The duration that makes a task starting at start end on end (End typed or dragged), 1 at least.
    static int durationTo(const GanttPlan &plan, const QDate &start, const QDate &end);
    // "Baseline N" (FEAT-082), the first N not used by the plan's snapshots.
    static QString nextBaselineName(const GanttPlan &plan);
    // FEAT-082: a task's Start, End or Duration (duration < 0: none) against the snapshot's: > 0 later
    // or longer, < 0 earlier or shorter, 0 the same or a side without dates. The desktop's table and
    // the phone's Task page colour it red or green.
    enum class BaselineField { Start, End, Duration };
    static int baselineDifference(const GanttBaselineTask &then, const GanttScheduledTask &now, int duration, BaselineField field);
    // Shift+click: the rows shown (top to bottom) from one task to the other, both included; only
    // `to` when from is not a task of the plan.
    static QStringList range(const QStringList &shown, const QString &from, const QString &to, bool fromIsTask);

    // The rows of the table in tree order (siblings in the plan's order), the subtasks of the
    // folded summaries left out (folding is the view's, not saved).
    struct Row {
        QString id;
        int depth = 0;
        bool summary = false;  // has subtasks
    };
    static QList<Row> rows(const GanttPlan &plan, const QSet<QString> &folded = {});
    // What the topics under root say about their bars (user, 2026-10-27): a branch colour chosen by
    // hand colours the bar (the map's inherited colours do not); the name on the bar for the
    // summaries, not the tasks, unless the topic chose (MindNode::ganttLabel).
    struct BarLooks {
        QHash<QString, GanttBarLook> looks;
        QSet<QString> labelled;
    };
    static BarLooks barLooks(const MindNode *root);
    static bool showsName(const MindNode *topic);  // the name on its bar

    // The task panel's edits of the topics (FEAT-078), one step each; the caller lays the map out.
    // Name on the bar: only a choice that differs from the default (summaries yes, tasks no) is stored.
    static bool setNameOnBar(Document &document, const QList<MindNode *> &topics, bool shown);
    // A milestone is the topic's checkbox (FEAT-048): summaries left out; on, a checked one stays checked.
    static bool setMilestone(Document &document, const QList<MindNode *> &topics, bool milestone);
    // A priority's name, 1-5 ("Very low" ... "Very high"); empty for 0 (none).
    static QString priorityName(int priority);
};
