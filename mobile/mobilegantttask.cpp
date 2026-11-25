#include "mobilegantttask.h"

#include "calendarmonth.h"
#include "document.h"
#include "ganttcanvas.h"
#include "ganttcommands.h"
#include "ganttnames.h"
#include "gantttaskedits.h"
#include "mindmapmodel.h"
#include "mobilemap.h"
#include "nodestyle.h"
#include "richtitle.h"

#include <QCoreApplication>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <memory>

namespace {

constexpr GanttDependency::Type kTypes[] = {GanttDependency::Type::FinishStart, GanttDependency::Type::StartStart,
                                            GanttDependency::Type::FinishFinish, GanttDependency::Type::StartFinish};

// The desktop panel's texts, the same in both apps.
QString panelText(const char *text, int n = -1) { return QCoreApplication::translate("GanttTaskPanel", text, nullptr, n); }

QString iso(const QDate &date) { return date.isValid() ? date.toString(Qt::ISODate) : QString(); }
QDate fromIso(const QString &text) { return QDate::fromString(text, Qt::ISODate); }
// As the desktop's date fields: the interface's short date.
QString shortDate(const QDate &date) { return date.isValid() ? QLocale().toString(date, QLocale::ShortFormat) : QString(); }

// The value the tasks share, -1 when they differ or there is none.
int shared(const QList<int> &values) {
    return !values.isEmpty() && values.count(values.first()) == values.size() ? values.first() : -1;
}

}  // namespace

MobileGanttTask::MobileGanttTask(QObject *parent) : QObject(parent) {}

void MobileGanttTask::setMap(MobileMap *map) {
    if (m_map == map) return;
    if (m_map) disconnect(m_map, nullptr, this, nullptr);
    m_map = map;
    if (m_map)
        for (auto signal : {&MobileMap::contentChanged, &MobileMap::selectionChanged, &MobileMap::snapshotChanged, &MobileMap::opened})
            connect(m_map, signal, this, &MobileGanttTask::reload);
    emit mapChanged();
    reload();
}

void MobileGanttTask::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    emit activeChanged();
    reload();
}

void MobileGanttTask::setBaseline(const QString &id) {
    if (m_baseline == id) return;
    m_baseline = id;
    emit baselineChanged();
    reload();
}

// FEAT-082: the shown task against the compared snapshot, its Start, End and Duration as the
// desktop's table cells (GanttCommands::baselineDifference) and the snapshot's dates under them.
void MobileGanttTask::loadBaseline(const GanttScheduledTask &scheduled, bool milestone) {
    const GanttBaseline *baseline = m_baseline.isEmpty() ? nullptr : m_plan.baseline(m_baseline);
    if (!baseline) return;
    const GanttBaselineTask *then = baseline->task(m_id);
    m_fields.insert(QStringLiteral("baselineShown"), true);
    if (!then) {
        m_fields.insert(QStringLiteral("isNew"), true);
        m_fields.insert(QStringLiteral("baselineText"), tr("New: not in %1").arg(baseline->name));
        return;
    }
    using Field = GanttCommands::BaselineField;
    const int duration = m_plan.task(m_id)->duration;
    m_fields.insert(QVariantMap{{QStringLiteral("startChange"), GanttCommands::baselineDifference(*then, scheduled, duration, Field::Start)},
                                {QStringLiteral("endChange"), milestone ? 0 : GanttCommands::baselineDifference(*then, scheduled, duration, Field::End)},
                                {QStringLiteral("durationChange"),
                                 milestone ? 0 : GanttCommands::baselineDifference(*then, scheduled, duration, Field::Duration)}});
    QString text;
    if (!then->dated) text = tr("%1: no dates").arg(baseline->name);
    else if (milestone || then->milestone) text = tr("%1: %2").arg(baseline->name, shortDate(then->start));
    else text = tr("%1: %2 – %3, %n day(s)", nullptr, then->duration).arg(baseline->name, shortDate(then->start), shortDate(then->end));
    m_fields.insert(QStringLiteral("baselineText"), text);
}

QStringList MobileGanttTask::typeNames() const {
    QStringList names;
    for (const auto type : kTypes) names.append(GanttCanvas::typeName(type));
    return names;
}

QStringList MobileGanttTask::priorityNames() const {
    QStringList names{panelText("None")};
    for (int p = 1; p <= MindNode::kMaxPriority; ++p) names.append(GanttCommands::priorityName(p));
    return names;
}

void MobileGanttTask::reload() {
    if (!m_active) return;  // read again when the page is shown
    Document *document = m_map && m_map->isOpen() ? m_map->document() : nullptr;
    m_plan = document ? document->model()->gantt() : GanttPlan();  // a new plan each time: kept by value
    m_schedule = m_plan.schedule();
    // As the desktop: the task tapped last, then the other selected tasks.
    m_id = m_map && m_plan.task(m_map->selectedId()) ? m_map->selectedId() : QString();
    m_targets.clear();
    if (!m_id.isEmpty()) {
        m_targets.append(m_id);
        for (const QString &id : m_map->selectedIds())
            if (id != m_id && m_plan.task(id)) m_targets.append(id);
    }
    m_fields.clear();
    m_links[0].clear();
    m_links[1].clear();
    m_people.clear();
    const GanttTask *task = m_plan.task(m_id);
    m_fields.insert(QStringLiteral("task"), task != nullptr);
    if (!task) {
        emit changed();
        return;
    }
    const MindNode *topic = document->model()->nodeById(m_id);
    const GanttScheduledTask scheduled = m_schedule.tasks.value(m_id);
    const bool summary = m_plan.hasChildren(m_id);
    const bool milestone = task->milestone && !summary;
    // Only the fields of the task's kind, as the desktop (user, 2026-10-29): a milestone has a start,
    // no duration nor end; a summary's dates, % and priority come from its children.
    m_fields.insert(QVariantMap{{QStringLiteral("several"), false},
                     {QStringLiteral("count"), int(m_targets.size())},
                     {QStringLiteral("id"), m_id},
                     {QStringLiteral("title"), panelText("Task")},
                     {QStringLiteral("name"), task->shownName()},
                     {QStringLiteral("nameEnabled"), true},
                     {QStringLiteral("milestoneShown"), !summary},
                     {QStringLiteral("milestone"), milestone},
                     {QStringLiteral("nameOnBar"), topic && GanttCommands::showsName(topic)},
                     {QStringLiteral("datesShown"), !summary},
                     {QStringLiteral("startShown"), scheduled.dated},
                     {QStringLiteral("start"), iso(scheduled.start)},
                     {QStringLiteral("startText"), shortDate(scheduled.start)},
                     {QStringLiteral("clearShown"), task->start.isValid()},
                     {QStringLiteral("addShown"), !scheduled.dated},
                     {QStringLiteral("lengthShown"), !summary && !milestone},
                     {QStringLiteral("duration"), task->duration},
                     {QStringLiteral("endEnabled"), scheduled.dated},
                     {QStringLiteral("end"), iso(scheduled.end)},
                     {QStringLiteral("endText"), shortDate(scheduled.end)},
                     {QStringLiteral("detailsShown"), !summary},
                     {QStringLiteral("progress"), task->progress},
                     {QStringLiteral("progressEnabled"), !summary},
                     {QStringLiteral("progressStep"), milestone ? GanttPlan::kMaxProgress : 1},  // a checkbox
                     {QStringLiteral("priority"), topic ? topic->ganttPriority : 0},
                     {QStringLiteral("linksShown"), !summary}});
    if (m_targets.size() > 1) loadSeveral();
    else if (!summary) {
        m_links[0] = linkRows(false);
        m_links[1] = linkRows(true);
        loadBaseline(scheduled, milestone);  // the Dates section: not with several tasks nor a summary
    }
    loadPeople();
    loadStyle();
    emit changed();
}

// Several tasks: a value they share, or none; a field shown when one of them has it.
void MobileGanttTask::loadSeveral() {
    const GanttTaskEdits::Several several = GanttTaskEdits::several(m_plan, m_schedule, m_targets);
    QList<int> priorities;
    for (const MindNode *topic : topics()) priorities.append(topic->ganttPriority);
    const bool leaves = !several.leaves.isEmpty();
    m_fields.insert(QVariantMap{{QStringLiteral("several"), true},
                     {QStringLiteral("title"), panelText("%n task(s)", int(m_targets.size()))},
                     {QStringLiteral("nameEnabled"), false},  // a name, and the links, are one task's
                     {QStringLiteral("milestoneShown"), leaves},
                     {QStringLiteral("milestone"), several.allMilestones},
                     {QStringLiteral("datesShown"), leaves},
                     {QStringLiteral("startShown"), several.dated},
                     {QStringLiteral("clearShown"), several.stored},
                     {QStringLiteral("addShown"), !several.dated && several.undated},
                     {QStringLiteral("lengthShown"), !several.durations.isEmpty()},
                     {QStringLiteral("duration"), shared(several.durations)},
                     {QStringLiteral("endEnabled"), several.lengths},
                     {QStringLiteral("detailsShown"), leaves},
                     {QStringLiteral("progress"), shared(several.progress)},
                     {QStringLiteral("progressEnabled"), leaves},
                     {QStringLiteral("progressStep"), 1},
                     {QStringLiteral("priority"), shared(priorities)},
                     {QStringLiteral("linksShown"), false}});
    // Start and End: the shown task's dates, or the first dated task's when it has none.
    if (!m_schedule.tasks.value(m_id).dated && several.firstStart.isValid())
        m_fields.insert(QVariantMap{{QStringLiteral("start"), iso(several.firstStart)},
                         {QStringLiteral("startText"), shortDate(several.firstStart)},
                         {QStringLiteral("end"), iso(several.firstEnd)},
                         {QStringLiteral("endText"), shortDate(several.firstEnd)}});
}

QList<MindNode *> MobileGanttTask::peopleTopics() const {
    QList<MindNode *> result;
    for (MindNode *topic : topics())
        if (!m_plan.hasChildren(topic->id)) result.append(topic);
    return result;
}

// As the desktop's GanttTaskPanel::loadPeople: every person on at least one target, muted when not on
// all of them; the share they have, or none when it differs.
void MobileGanttTask::loadPeople() {
    const QList<MindNode *> targets = peopleTopics();
    const QVector<GanttResource> &resources = m_map->document()->resources().people;
    bool everyone = !resources.isEmpty();
    for (const GanttResource &person : resources) {
        int count = 0, percent = -1;
        for (const MindNode *topic : targets) {
            if (!topic->ganttPeople.contains(person.id)) continue;
            ++count;
            const int value = topic->percentOf(person.id);
            percent = percent < 0 || percent == value ? value : 0;
        }
        everyone = everyone && count == targets.size();
        if (count == 0) continue;
        m_people.append(QVariantMap{{QStringLiteral("id"), person.id},
                                    {QStringLiteral("name"), person.displayName().isEmpty() ? person.email : person.displayName()},
                                    {QStringLiteral("role"), person.role},
                                    {QStringLiteral("partial"), count < targets.size()},
                                    {QStringLiteral("percent"), percent > 0 ? percent : -1}});
    }
    m_fields.insert(QVariantMap{{QStringLiteral("peopleShown"), !targets.isEmpty()},
                                {QStringLiteral("hasResources"), !resources.isEmpty()},
                                {QStringLiteral("everyoneAssigned"), everyone}});
}

// As the desktop's GanttTaskPanel::loadStyle and the bar colour GanttView shows on the panel.
void MobileGanttTask::loadStyle() {
    const GanttTaskEdits::OwnStyle style = GanttTaskEdits::ownStyle(m_plan, m_targets);
    QColor color, borderColor;
    bool manual = false;
    const QList<MindNode *> targets = topics();
    for (int i = 0; i < targets.size(); ++i) {
        const MindNode *topic = targets.at(i);
        const bool own = NodeStyle::hasManualValue(topic, StyleProperty::BranchColor);
        const QColor bar = own ? topic->branchColor : GanttCanvas::defaultBarColor(m_plan, m_plan.barKind(topic->id));
        const QColor outline = GanttCanvas::outlineColor(m_plan, topic->id, bar, bar);
        manual = manual || own;
        if (i == 0) {
            color = bar;
            borderColor = outline;
        }
        if (bar != color) color = QColor();
        if (outline != borderColor) borderColor = QColor();
    }
    m_fields.insert(QVariantMap{{QStringLiteral("styleKind"), style.kind},
                                {QStringLiteral("ownShape"), style.shape},
                                {QStringLiteral("ownFill"), style.fill},
                                {QStringLiteral("ownBorder"), style.border},
                                {QStringLiteral("ownShapeSet"), style.ownShape},
                                {QStringLiteral("ownFillSet"), style.ownFill},
                                {QStringLiteral("ownBorderSet"), style.ownBorder},
                                {QStringLiteral("color"), color},
                                {QStringLiteral("colorManual"), manual},
                                {QStringLiteral("borderColor"), borderColor},
                                {QStringLiteral("borderColorSet"), style.ownBorderColor}});
}

bool MobileGanttTask::setOwnStyle(int part, int value) {
    if (m_id.isEmpty()) return false;
    QString key;
    GanttPlan::OwnStyle own = GanttPlan::OwnStyle::Shape;
    switch (part) {
    case 0: {
        // The shape of the targets' one kind (no Shape row for a mix).
        const int kind = m_fields.value(QStringLiteral("styleKind")).toInt();
        if (kind == int(GanttPlan::BarKind::Task)) key = GanttPlan::styleKey(GanttPlan::BarShape(value));
        else if (kind == int(GanttPlan::BarKind::Summary)) key = GanttPlan::styleKey(GanttPlan::SummaryShape(value));
        else if (kind == int(GanttPlan::BarKind::Milestone)) key = GanttPlan::styleKey(GanttPlan::MilestoneShape(value));
        break;
    }
    case 1:
        own = GanttPlan::OwnStyle::Fill;
        key = GanttPlan::styleKey(NodeFillStyle(value));
        break;
    case 2:
        own = GanttPlan::OwnStyle::Border;
        key = GanttPlan::styleKey(GanttPlan::BorderStyle(value));
        break;
    default: return false;
    }
    if (key.isEmpty() || !GanttPlan::validStyleKey(own, key)) return false;
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setOwnStyle(plan, m_targets, own, key); });
}

bool MobileGanttTask::clearOwnStyle(int part) {
    if (part < 0 || part > 2) return false;
    const GanttPlan::OwnStyle own = part == 0 ? GanttPlan::OwnStyle::Shape : part == 1 ? GanttPlan::OwnStyle::Fill : GanttPlan::OwnStyle::Border;
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setOwnStyle(plan, m_targets, own, QString()); });
}

bool MobileGanttTask::setColor(const QColor &color) {
    const QList<MindNode *> targets = topics();
    if (targets.isEmpty() || !color.isValid()) return false;
    QColor opaque = color;
    opaque.setAlpha(255);  // the desktop's field has no opacity
    if (!m_map->document()->model()->setStyle(targets, StyleProperty::BranchColor, opaque)) return false;
    m_map->document()->markChanged();
    return finish();
}

bool MobileGanttTask::clearColor() {
    QList<MindNode *> targets = topics();
    targets.removeIf([](const MindNode *topic) { return !NodeStyle::hasManualValue(topic, StyleProperty::BranchColor); });
    if (targets.isEmpty() || !m_map->document()->model()->clearStyle(targets, StyleProperty::BranchColor)) return false;
    m_map->document()->markChanged();
    return finish();
}

bool MobileGanttTask::setBorderColor(const QColor &color) {
    if (m_id.isEmpty() || !color.isValid()) return false;
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setBorderColor(plan, m_targets, QColor(color.rgb())); });
}

bool MobileGanttTask::clearBorderColor() {
    return !m_id.isEmpty() && editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setBorderColor(plan, m_targets, QColor()); });
}

int MobileGanttTask::minPercent() { return MindNode::kMinPercent; }
int MobileGanttTask::percentStep() { return MindNode::kPercentStep; }

QVariantList MobileGanttTask::personCandidates() const {
    QVariantList rows;
    if (m_id.isEmpty()) return rows;
    const QList<MindNode *> targets = peopleTopics();
    for (const GanttResource &person : m_map->document()->resources().people) {
        if (std::all_of(targets.cbegin(), targets.cend(), [&](const MindNode *topic) { return topic->ganttPeople.contains(person.id); }))
            continue;
        const QString name = person.displayName().isEmpty() ? person.email : person.displayName();
        // Two people may share a name: the role tells them apart (the desktop's menu).
        rows.append(QVariantMap{{QStringLiteral("id"), person.id},
                                {QStringLiteral("name"), person.role.isEmpty() ? name : panelText("%1 — %2").arg(name, person.role)}});
    }
    return rows;
}

bool MobileGanttTask::assignPerson(const QString &person) {
    const QList<MindNode *> targets = peopleTopics();
    return !targets.isEmpty() && m_map->document()->setTaskInfo(targets, std::nullopt, {person}, {}) && finish();
}

bool MobileGanttTask::unassignPerson(const QString &person) {
    const QList<MindNode *> targets = peopleTopics();
    return !targets.isEmpty() && m_map->document()->setTaskInfo(targets, std::nullopt, {}, {person}) && finish();
}

bool MobileGanttTask::setPersonPercent(const QString &person, int percent) {
    const QList<MindNode *> targets = peopleTopics();
    return !targets.isEmpty() && m_map->document()->setPeoplePercent(targets, person, percent) && finish();
}

bool MobileGanttTask::stepPersonPercent(const QString &person, int delta) {
    // Each task from its own share: one step for them all only when they share it, as the desktop's
    // field (which shows none for different shares and types the value for all of them).
    const QList<MindNode *> targets = peopleTopics();
    int shared = -1;
    for (const MindNode *topic : targets) {
        if (!topic->ganttPeople.contains(person)) continue;
        const int value = topic->percentOf(person);
        shared = shared < 0 || shared == value ? value : 0;
    }
    if (shared <= 0) return false;
    return setPersonPercent(person, std::clamp(shared + delta, int(MindNode::kMinPercent), int(MindNode::kMaxPercent)));
}

QString MobileGanttTask::parentName(const QString &id) const {
    const GanttTask *task = m_plan.task(id);
    const GanttTask *parent = task ? m_plan.task(task->parentId) : nullptr;
    return parent ? parent->shownName() : QString();
}

QVariantList MobileGanttTask::linkRows(bool predecessors) const {
    QVariantList rows;
    for (const GanttDependency &link : GanttTaskEdits::links(m_plan, m_id, predecessors)) {
        const QString other = predecessors ? link.from : link.to;
        rows.append(QVariantMap{{QStringLiteral("id"), other},
                                {QStringLiteral("name"), m_plan.task(other)->shownName()},
                                {QStringLiteral("parent"), parentName(other)},
                                {QStringLiteral("type"), int(std::find(std::begin(kTypes), std::end(kTypes), link.type) - std::begin(kTypes))},
                                {QStringLiteral("lag"), link.lag}});
    }
    return rows;
}

QList<MindNode *> MobileGanttTask::topics() const {
    QList<MindNode *> result;
    if (!m_map || !m_map->document()) return result;
    for (const QString &id : m_targets)
        if (MindNode *topic = m_map->document()->model()->nodeById(id)) result.append(topic);
    return result;
}

bool MobileGanttTask::finish() {
    m_map->finishEdit(m_map->selectedId());  // the map's layout joins the step, the selection stays
    return true;
}

bool MobileGanttTask::editPlan(const std::function<bool(GanttPlan &)> &change) {
    return !m_id.isEmpty() && m_map && m_map->document() && m_map->document()->editGantt(change) && finish();
}

void MobileGanttTask::loadName(QQuickTextDocument *field, const QString &id) const {
    loadName(field ? field->textDocument() : nullptr, id);
}

void MobileGanttTask::loadName(QTextDocument *field, const QString &id) const {
    const MindNode *topic = m_map && m_map->document() && m_plan.task(id) ? m_map->document()->model()->nodeById(id) : nullptr;
    if (!field) return;
    if (topic) GanttNames::load(field, topic, m_plan.hasChildren(id));
    else field->setPlainText(QString());
    field->clearUndoRedoStacks();  // a name loaded is no step of the field's Undo (MarkdownTyping)
}

bool MobileGanttTask::renameFrom(QQuickTextDocument *field, const QString &id, const QString &preedit, int cursor) {
    QTextDocument *text = field ? field->textDocument() : nullptr;
    if (!text) return false;
    if (preedit.isEmpty()) return renameFrom(*text, id);
    // Android keeps the word being composed out of the document until it is committed.
    const std::unique_ptr<QTextDocument> typed(text->clone());
    QTextCursor at(typed.get());
    at.setPosition(std::clamp(cursor, 0, typed->characterCount() - 1));
    at.insertText(preedit);
    return renameFrom(*typed, id);
}

bool MobileGanttTask::renameFrom(const QTextDocument &field, const QString &id) {
    MindNode *topic = m_map && m_map->document() && m_plan.task(id) ? m_map->document()->model()->nodeById(id) : nullptr;
    QString title, html;
    if (!topic || !GanttNames::edited(topic, field, m_plan.hasChildren(id), &title, &html)) return false;
    // The field shows the name without its formulas: left as it is, the title keeps them.
    if (html.isEmpty() && title == topic->title.simplified()) return false;
    return m_map->document()->editTitle(topic, title, html) && finish();
}

bool MobileGanttTask::nameFormattable(const QString &id) const {
    return m_map && m_map->document() && m_plan.task(id) && GanttNames::formattable(m_map->document()->model()->nodeById(id));
}

int MobileGanttTask::nameBoldWeight(const QString &id) const { return GanttNames::rowWeights(m_plan.hasChildren(id)).bold; }

bool MobileGanttTask::toggleNameFormat(QQuickTextDocument *field, const QString &id, int start, int end, int style) {
    QTextDocument *text = field ? field->textDocument() : nullptr;
    const MindNode *topic = m_map && m_map->document() && m_plan.task(id) ? m_map->document()->model()->nodeById(id) : nullptr;
    if (!text || start == end || !GanttNames::formattable(topic)) return false;  // a formula's title stays plain
    QTextCursor cursor(text);
    cursor.setPosition(std::clamp(std::min(start, end), 0, text->characterCount() - 1));
    cursor.setPosition(std::clamp(std::max(start, end), 0, text->characterCount() - 1), QTextCursor::KeepAnchor);
    const QTextCharFormat now = cursor.charFormat();
    const OutlineFormats::BoldWeights weights = GanttNames::rowWeights(m_plan.hasChildren(id));
    QTextCharFormat format;
    switch (RichTitle::CharStyle(style)) {
    case RichTitle::CharStyle::Bold: format.setFontWeight(now.fontWeight() >= weights.bold ? weights.regular : weights.bold); break;
    case RichTitle::CharStyle::Italic: format.setFontItalic(!now.fontItalic()); break;
    case RichTitle::CharStyle::Underline: format.setFontUnderline(!now.fontUnderline()); break;
    case RichTitle::CharStyle::StrikeOut: format.setFontStrikeOut(!now.fontStrikeOut()); break;
    default: return false;
    }
    cursor.mergeCharFormat(format);
    return true;
}

bool MobileGanttTask::setMilestone(bool milestone) {
    return !m_id.isEmpty() && GanttCommands::setMilestone(*m_map->document(), topics(), milestone) && finish();
}

bool MobileGanttTask::setNameOnBar(bool shown) {
    return !m_id.isEmpty() && GanttCommands::setNameOnBar(*m_map->document(), topics(), shown) && finish();
}

bool MobileGanttTask::setStart(const QString &isoDate) {
    const QDate date = fromIso(isoDate);
    return date.isValid() && editPlan([&](GanttPlan &plan) { return GanttTaskEdits::moveStart(plan, m_targets, date); });
}

bool MobileGanttTask::clearStart() {
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::clearStart(plan, m_targets); });
}

bool MobileGanttTask::addStart() {
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::addStart(plan, m_schedule, m_targets); });
}

bool MobileGanttTask::setDuration(int days) {
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setDuration(plan, m_targets, days); });
}

bool MobileGanttTask::stepDuration(int delta) {
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::stepDuration(plan, m_targets, delta); });
}

bool MobileGanttTask::setEnd(const QString &isoDate) {
    const QDate date = fromIso(isoDate);
    return date.isValid() && editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setEnd(plan, m_schedule, m_targets, date); });
}

bool MobileGanttTask::setProgress(int percent) {
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::setProgress(plan, m_targets, percent); });
}

bool MobileGanttTask::stepProgress(int delta) {
    return editPlan([&](GanttPlan &plan) { return GanttTaskEdits::stepProgress(plan, m_targets, delta); });
}

bool MobileGanttTask::setPriority(int priority) {
    const QList<MindNode *> targets = topics();
    return !targets.isEmpty() && m_map->document()->setTaskInfo(targets, priority) && finish();
}

bool MobileGanttTask::removeTasks() {
    const QList<MindNode *> targets = topics();
    if (targets.isEmpty() || !m_map->document()->removeNodes(targets)) return false;
    m_map->stopMultiSelect();
    m_map->finishEdit(QString());  // as the desktop: the panel then shows no task
    return true;
}

QVariantList MobileGanttTask::linkCandidates(bool predecessors) const {
    QVariantList rows;
    if (m_id.isEmpty()) return rows;
    for (const QString &id : GanttTaskEdits::linkCandidates(m_plan, m_id, predecessors))
        rows.append(QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("name"), m_plan.task(id)->shownName()},
                                {QStringLiteral("parent"), parentName(id)}});
    return rows;
}

GanttDependency MobileGanttTask::link(bool predecessors, const QString &other) const {
    const GanttDependency wanted = GanttTaskEdits::linkWith(m_id, predecessors, other);
    for (const GanttDependency &existing : m_plan.dependencies())
        if (existing.from == wanted.from && existing.to == wanted.to) return existing;
    return wanted;
}

bool MobileGanttTask::addLink(bool predecessors, const QString &other) {
    const GanttDependency added = GanttTaskEdits::linkWith(m_id, predecessors, other);
    return editPlan([&](GanttPlan &plan) { return plan.addDependency(added); });
}

bool MobileGanttTask::setLinkType(bool predecessors, const QString &other, int type) {
    if (type < 0 || type >= int(std::size(kTypes))) return false;
    GanttDependency changed = link(predecessors, other);
    changed.type = kTypes[type];
    return editPlan([&](GanttPlan &plan) { return plan.updateDependency(changed); });
}

bool MobileGanttTask::setLinkLag(bool predecessors, const QString &other, int lag) {
    GanttDependency changed = link(predecessors, other);
    changed.lag = std::clamp(lag, -GanttPlan::kMaxLag, GanttPlan::kMaxLag);
    return editPlan([&](GanttPlan &plan) { return plan.updateDependency(changed); });
}

bool MobileGanttTask::removeLink(bool predecessors, const QString &other) {
    const GanttDependency removed = link(predecessors, other);
    return editPlan([&](GanttPlan &plan) { return plan.removeDependency(removed.from, removed.to); });
}

QVariantMap MobileGanttTask::calendar(const QString &isoDay) {
    QDate day = fromIso(isoDay);
    if (!day.isValid()) day = GanttToday::date();
    const QLocale names = CalendarMonth::namesLocale();
    QStringList weekdays;
    for (int column = 0; column < CalendarMonth::kColumns; ++column)
        weekdays.append(CalendarMonth::weekdayName((int(QLocale().firstDayOfWeek()) - 1 + column) % CalendarMonth::kColumns + 1, names));
    QVariantList days;
    const QDate first = CalendarMonth::firstShown(day), today = GanttToday::date();
    for (int i = 0; i < CalendarMonth::kColumns * CalendarMonth::kRows; ++i) {
        const QDate shown = first.addDays(i);
        days.append(QVariantMap{{QStringLiteral("iso"), iso(shown)},
                                {QStringLiteral("day"), shown.day()},
                                {QStringLiteral("inMonth"), shown.month() == day.month()},
                                {QStringLiteral("today"), shown == today}});
    }
    return {{QStringLiteral("title"), CalendarMonth::title(day, names)},
            {QStringLiteral("weekdays"), weekdays},
            {QStringLiteral("days"), days}};
}

QString MobileGanttTask::addMonths(const QString &isoDay, int months) {
    const QDate day = fromIso(isoDay);
    return iso((day.isValid() ? day : GanttToday::date()).addMonths(months));
}

QString MobileGanttTask::today() { return iso(GanttToday::date()); }
