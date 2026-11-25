#pragma once

#include "ganttplan.h"

#include <QColor>
#include <QObject>
#include <QQuickTextDocument>

class QTextDocument;
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

class MobileMap;
struct MindNode;

// FEAT-122 stage B2: the side panel's Task page, as the desktop's GanttTaskPanel (FEAT-078): the
// selected task (the last one tapped) and, with several selected, all of them as targets. It reads
// the plan only while the page is shown (`active`) and sends every edit through the core
// (GanttTaskEdits, GanttCommands, Document), one Undo step each, then MobileMap::finishEdit.
//
// `fields` says what the page shows: task (a task is selected), id, several, title, name, nameEnabled,
// milestoneShown, milestone, nameOnBar, datesShown, startShown, start and startText (ISO and the
// interface's short date), clearShown, addShown, lengthShown, duration (-1: none or mixed),
// endEnabled, end and endText, detailsShown, progress (-1: mixed), progressEnabled, progressStep,
// priority (-1: mixed), linksShown, count (the targets); stage C1: peopleShown (not only summaries),
// hasResources (the document has people), everyoneAssigned; stage C2, the Style section: styleKind
// (GanttPlan::BarKind, -1 for a mix: no Shape row), ownShape, ownFill, ownBorder (-1 when they differ),
// ownShapeSet, ownFillSet, ownBorderSet (the × back to the sheet's), color (the bars' colour, invalid
// when they differ), colorManual (one has its own: the × back to the default), borderColor (the
// colour the borders are drawn in, invalid when they differ) and borderColorSet (one has its own).
class MobileGanttTask : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileMap *map READ map WRITE setMap NOTIFY mapChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(QVariantMap fields READ fields NOTIFY changed)
    // The shown task's links: [{id, name, parent, type, lag}], in the plan's order.
    Q_PROPERTY(QVariantList predecessors READ predecessors NOTIFY changed)
    Q_PROPERTY(QVariantList successors READ successors NOTIFY changed)
    // FEAT-081/083: the people assigned to the targets (not summaries), in the Resources list's order:
    // [{id, name, role, partial (on only some of them), percent (-1: they differ)}].
    Q_PROPERTY(QVariantList people READ people NOTIFY changed)
    Q_PROPERTY(int minPercent READ minPercent CONSTANT)
    Q_PROPERTY(int percentStep READ percentStep CONSTANT)
    Q_PROPERTY(QStringList typeNames READ typeNames CONSTANT)       // by GanttDependency::Type
    Q_PROPERTY(QStringList priorityNames READ priorityNames CONSTANT)  // None, Very low ... Very high
    Q_PROPERTY(int minDuration READ minDuration CONSTANT)  // the plan's limits (GanttPlan)
    Q_PROPERTY(int maxDuration READ maxDuration CONSTANT)
    Q_PROPERTY(int maxLag READ maxLag CONSTANT)
    // FEAT-122 stage D: the snapshot the Gantt compares with (MobileGantt::comparedBaseline; empty:
    // none). With one task shown, `fields` then has baselineShown, startChange, endChange,
    // durationChange (> 0 later or longer, < 0 earlier or shorter: red or green, as the desktop's
    // cells) and baselineText (the snapshot's dates, in place of the desktop's tooltips), or isNew.
    Q_PROPERTY(QString baseline READ baseline WRITE setBaseline NOTIFY baselineChanged)

public:
    explicit MobileGanttTask(QObject *parent = nullptr);

    MobileMap *map() const { return m_map; }
    void setMap(MobileMap *map);
    bool active() const { return m_active; }
    void setActive(bool active);
    QString baseline() const { return m_baseline; }
    void setBaseline(const QString &id);
    QVariantMap fields() const { return m_fields; }
    QVariantList predecessors() const { return m_links[1]; }
    QVariantList successors() const { return m_links[0]; }
    QVariantList people() const { return m_people; }
    static int minPercent();
    static int percentStep();
    QStringList typeNames() const;
    QStringList priorityNames() const;
    static int minDuration() { return GanttPlan::kMinDuration; }
    static int maxDuration() { return GanttPlan::kMaxDuration; }
    static int maxLag() { return GanttPlan::kMaxLag; }

    // Task id's name (the topic's title), id kept when the typing began: the selection may have moved
    // since. An empty one is refused.
    // FEAT-124: the name with its formats (GanttNames), as the desktop's panel field. loadName puts
    // task `id`'s shown name in the field's document; renameFrom writes the field (the word being
    // composed, preedit, typed at cursor) in one Undo step, false
    // when nothing changes or the name is empty; nameBoldWeight is bold's weight in the field (Black
    // on a summary); toggleNameFormat sets or takes off a format key's style (RichTitle::CharStyle)
    // on the field's selection [start, end), false without one.
    Q_INVOKABLE void loadName(QQuickTextDocument *field, const QString &id) const;
    Q_INVOKABLE bool renameFrom(QQuickTextDocument *field, const QString &id, const QString &preedit, int cursor);
    Q_INVOKABLE int nameBoldWeight(const QString &id) const;
    // False for a title holding a formula: no Markdown shortcuts nor format keys in its name.
    Q_INVOKABLE bool nameFormattable(const QString &id) const;
    Q_INVOKABLE bool toggleNameFormat(QQuickTextDocument *field, const QString &id, int start, int end, int style);
    // The same on a document (the field's, or a test's).
    void loadName(QTextDocument *field, const QString &id) const;
    bool renameFrom(const QTextDocument &field, const QString &id);
    Q_INVOKABLE bool setMilestone(bool milestone);
    Q_INVOKABLE bool setNameOnBar(bool shown);
    Q_INVOKABLE bool setStart(const QString &isoDate);
    Q_INVOKABLE bool clearStart();
    Q_INVOKABLE bool addStart();  // today, on the tasks without a date
    Q_INVOKABLE bool setDuration(int days);
    Q_INVOKABLE bool stepDuration(int delta);  // several tasks: each from its own value
    Q_INVOKABLE bool setEnd(const QString &isoDate);
    Q_INVOKABLE bool setProgress(int percent);
    Q_INVOKABLE bool stepProgress(int delta);
    Q_INVOKABLE bool setPriority(int priority);
    Q_INVOKABLE bool removeTasks();  // the targets' topics, map too
    // A new link's tasks, asked when its list opens: [{id, name, parent}].
    Q_INVOKABLE QVariantList linkCandidates(bool predecessors) const;
    Q_INVOKABLE bool addLink(bool predecessors, const QString &other);
    Q_INVOKABLE bool setLinkType(bool predecessors, const QString &other, int type);
    Q_INVOKABLE bool setLinkLag(bool predecessors, const QString &other, int lag);
    Q_INVOKABLE bool removeLink(bool predecessors, const QString &other);
    // FEAT-081: "+ Add"'s people, those not on every target: [{id, name}] ("Anna Rossi — Developer").
    Q_INVOKABLE QVariantList personCandidates() const;
    Q_INVOKABLE bool assignPerson(const QString &person);    // to every target, one step
    Q_INVOKABLE bool unassignPerson(const QString &person);  // from every target
    // FEAT-083: the person's share of every target having it; a step from the share they have (none
    // when it differs, as the desktop's empty field: a typed value sets it on all of them).
    Q_INVOKABLE bool setPersonPercent(const QString &person, int percent);
    Q_INVOKABLE bool stepPersonPercent(const QString &person, int delta);
    // FEAT-080: the targets' own shape (0), fill (1) or border (2), a value of the Gantt page's lists;
    // clearOwnStyle goes back to the sheet's. The colour is the topics' branch colour (map too).
    Q_INVOKABLE bool setOwnStyle(int part, int value);
    Q_INVOKABLE bool clearOwnStyle(int part);
    Q_INVOKABLE bool setColor(const QColor &color);
    Q_INVOKABLE bool clearColor();
    // The targets' own border colour (user, 2026-11-22); clearBorderColor goes back to the sheet's.
    Q_INVOKABLE bool setBorderColor(const QColor &color);
    Q_INVOKABLE bool clearBorderColor();

    // The date picker's month (any day of it, ISO): {title, weekdays: 7 two letters, days: 42 of
    // {iso, day, inMonth, today}}, names in the interface language.
    Q_INVOKABLE static QVariantMap calendar(const QString &isoDay);
    Q_INVOKABLE static QString addMonths(const QString &isoDay, int months);
    Q_INVOKABLE static QString today();

signals:
    void mapChanged();
    void activeChanged();
    void baselineChanged();
    void changed();

private:
    void reload();
    void loadSeveral();
    QVariantList linkRows(bool predecessors) const;
    QList<MindNode *> topics() const;  // the targets' topics
    QList<MindNode *> peopleTopics() const;  // those that can have people: not summaries
    void loadPeople();
    void loadStyle();
    void loadBaseline(const GanttScheduledTask &scheduled, bool milestone);
    bool editPlan(const std::function<bool(GanttPlan &)> &change);
    bool finish();
    QString parentName(const QString &id) const;
    GanttDependency link(bool predecessors, const QString &other) const;  // the existing one, or new

    QPointer<MobileMap> m_map;
    bool m_active = false;
    GanttPlan m_plan;
    GanttSchedule m_schedule;
    QString m_id;
    QString m_baseline;
    QStringList m_targets;  // m_id first
    QVariantMap m_fields;
    QVariantList m_links[2];  // successors, predecessors
    QVariantList m_people;
};
