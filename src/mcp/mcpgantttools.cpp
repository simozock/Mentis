// FEAT-045 phase 3, stage D: the agents' tools on a sheet's Gantt plan (ADR-004: the tasks are the
// sheet's topics). Dates are ISO (YYYY-MM-DD); every change goes through Document::editGantt or the
// resources' operations, the same as the Gantt view, so the plan keeps its own checks.
#include "mcptoolsupport.h"

#include "document.h"
#include "ganttplan.h"
#include "ganttresources.h"
#include "mindmapmodel.h"

#include <QJsonArray>
#include <QUuid>
#include <array>

namespace McpMapTools::Support {
namespace {

const QStringList kDependencyTypes{QStringLiteral("finish-start"), QStringLiteral("start-start"), QStringLiteral("finish-finish"),
                                   QStringLiteral("start-finish")};

QString dependencyKey(GanttDependency::Type type) { return kDependencyTypes.value(int(type)); }

using StyleList = GanttPlan::StyleList;
const QStringList &styleKeys(StyleList list) { return GanttPlan::styleKeys(list); }

// The keys of an enum from its key function, 0, 1, 2... until one has no key (the map's fills and end marks).
template <typename Enum>
QStringList keysOf(QString (*keyOf)(Enum)) {
    QStringList keys;
    for (int value = 0; value < 64; ++value) {
        const QString key = keyOf(static_cast<Enum>(value));
        if (key.isEmpty() || keys.contains(key)) break;
        keys.append(key);
    }
    return keys;
}
const QStringList &fillKeys() { static const QStringList keys = keysOf<NodeFillStyle>(&NodeStyle::fillStyleKey); return keys; }
const QStringList &endKeys() { static const QStringList keys = keysOf<BranchEnd>(&NodeStyle::branchEndKey); return keys; }

constexpr std::array kBarKinds{GanttPlan::BarKind::Task, GanttPlan::BarKind::Summary, GanttPlan::BarKind::Milestone};
const QStringList kKindNames{QStringLiteral("tasks"), QStringLiteral("summaries"), QStringLiteral("milestones")};  // by BarKind
constexpr std::array kShapeLists{StyleList::BarShape, StyleList::SummaryShape, StyleList::MilestoneShape};  // by BarKind

// FEAT-080: the sheet's chart style, as set_gantt_style takes it (color null = the view's default).
QJsonObject styleJson(const GanttPlan &plan) {
    QJsonObject style;
    const std::array shapes{GanttPlan::styleKey(plan.barShape()), GanttPlan::styleKey(plan.summaryShape()),
                            GanttPlan::styleKey(plan.milestoneShape())};
    for (GanttPlan::BarKind kind : kBarKinds) {
        const QColor color = plan.defaultColor(kind);
        style.insert(kKindNames.at(int(kind)), QJsonObject{{"shape", shapes[int(kind)]}, {"fill", GanttPlan::styleKey(plan.fillStyle(kind))},
                                                           {"border", GanttPlan::styleKey(plan.borderStyle(kind))},
                                                           {"color", color.isValid() ? QJsonValue(color.name()) : QJsonValue()}});
    }
    style.insert(QStringLiteral("relations"), QJsonObject{{"line", styleKeys(StyleList::LinkLine).at(int(plan.linkLine()))},
                                                          {"end", NodeStyle::branchEndKey(plan.linkEnd())},
                                                          {"width", styleKeys(StyleList::LinkWidth).at(int(plan.linkWidth()))},
                                                          {"end_size", styleKeys(StyleList::LinkEndSize).at(int(plan.linkEndSize()))}});
    return style;
}

QJsonObject ganttJson(const Document &document, const MindMapModel &model) {
    const GanttPlan plan = model.gantt();
    const GanttSchedule schedule = plan.schedule();
    QJsonArray tasks, dependencies, people;
    for (const GanttTask &task : plan.tasks()) {
        const GanttScheduledTask planned = schedule.tasks.value(task.id);
        QJsonObject json{{"id", task.id}, {"name", task.name}, {"duration", task.duration}, {"progress", planned.progress},
                         {"summary", planned.summary}, {"milestone", task.milestone}, {"critical", planned.critical}};
        if (!task.parentId.isEmpty()) json.insert(QStringLiteral("parent"), task.parentId);
        if (task.start.isValid()) json.insert(QStringLiteral("start"), task.start.toString(Qt::ISODate));
        if (planned.dated) {
            json.insert(QStringLiteral("plannedStart"), planned.start.toString(Qt::ISODate));
            json.insert(QStringLiteral("plannedEnd"), planned.end.toString(Qt::ISODate));
        }
        if (const MindNode *node = model.nodeById(task.id)) {
            if (!node->ganttPeople.isEmpty()) {
                json.insert(QStringLiteral("people"), QJsonArray::fromStringList(node->ganttPeople));
                QJsonObject shares;  // FEAT-083: each person's percent of the task
                for (const QString &person : node->ganttPeople) shares.insert(person, node->percentOf(person));
                json.insert(QStringLiteral("shares"), shares);
            }
            if (node->ganttPriority) json.insert(QStringLiteral("priority"), node->ganttPriority);
            if (node->ganttLabel) json.insert(QStringLiteral("label"), *node->ganttLabel);
        }
        for (const auto &[key, value] : {std::pair{"shape", task.shape}, std::pair{"fill", task.fill}, std::pair{"border", task.border}})
            if (!value.isEmpty()) json.insert(QString::fromLatin1(key), value);  // FEAT-080: the task's own style
        tasks.append(json);
    }
    for (const GanttDependency &dependency : plan.dependencies())
        dependencies.append(QJsonObject{{"from", dependency.from}, {"to", dependency.to}, {"type", dependencyKey(dependency.type)},
                                        {"lag", dependency.lag}});
    for (const GanttResource &person : document.resources().people)
        people.append(QJsonObject{{"id", person.id}, {"name", person.displayName()}, {"first_name", person.firstName},
                                  {"last_name", person.lastName}, {"role", person.role}, {"email", person.email},
                                  {"hourly_cost", double(person.hourlyCostCents) / 100}});
    QJsonArray baselines, workingDays, holidays;
    for (const GanttBaseline &baseline : plan.baselines())
        baselines.append(QJsonObject{{"id", baseline.id}, {"name", baseline.name}, {"taken", baseline.taken.toString(Qt::ISODate)}});
    QList<int> days = plan.calendar().workingDays.values();
    std::sort(days.begin(), days.end());
    for (int day : days) workingDays.append(day);
    QList<QDate> dates = plan.calendar().holidays.values();
    std::sort(dates.begin(), dates.end());
    for (const QDate &date : dates) holidays.append(date.toString(Qt::ISODate));
    QJsonObject result{{"tasks", tasks}, {"dependencies", dependencies}, {"people", people}, {"baselines", baselines},
                       {"currency", document.resources().currencyText()}, {"style", styleJson(plan)},
                       {"calendar", QJsonObject{{"working_days", workingDays}, {"holidays", holidays},
                                                {"hours_per_day", plan.calendar().minutesPerDay / 60.0}}}};
    if (schedule.start.isValid()) {
        result.insert(QStringLiteral("start"), schedule.start.toString(Qt::ISODate));
        result.insert(QStringLiteral("end"), schedule.end.toString(Qt::ISODate));
    }
    return result;
}

std::optional<QDate> isoDate(const QJsonValue &value) {
    const QDate date = QDate::fromString(value.toString(), Qt::ISODate);
    if (!value.isString() || !date.isValid() || date.year() < GanttPlan::kFirstYear || date.year() > GanttPlan::kLastYear) return std::nullopt;
    return date;
}

QJsonObject intProperty(const QString &description, int minimum, int maximum) {
    return {{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}, {"description", description}};
}

const QJsonObject kDependencyType{{"enum", QJsonArray::fromStringList(kDependencyTypes)},
                                  {"description", "omitted = finish-start (the second starts when the first ends)"}};

}  // namespace

namespace {

// The person's fields from args (only the given ones); false with error set for a wrong value.
bool readPerson(const QJsonObject &args, GanttResource *person, QString *error) {
    for (const auto &[key, field] : {std::pair{"first_name", &GanttResource::firstName}, std::pair{"last_name", &GanttResource::lastName},
                                     std::pair{"role", &GanttResource::role}, std::pair{"email", &GanttResource::email}}) {
        const std::optional<QString> value = text(args, QString::fromLatin1(key), false, error, GanttResources::kMaxTextLength);
        if (!error->isEmpty()) return false;
        if (value) person->*field = value->trimmed();
    }
    if (!GanttResources::isValidEmail(person->email)) { *error = QStringLiteral("email is not an address"); return false; }
    if (args.contains(QStringLiteral("hourly_cost"))) {
        const double cost = args.value(QStringLiteral("hourly_cost")).toDouble(-1);
        if (cost < 0 || cost * 100 > double(GanttResources::kMaxHourlyCostCents)) { *error = QStringLiteral("hourly_cost must be 0 to 100000"); return false; }
        person->hourlyCostCents = qRound64(cost * 100);
    }
    return true;
}

const QJsonObject kPersonFields{{"first_name", QJsonObject{{"type", "string"}}}, {"last_name", QJsonObject{{"type", "string"}}},
                                {"role", QJsonObject{{"type", "string"}}}, {"email", QJsonObject{{"type", "string"}}},
                                {"hourly_cost", QJsonObject{{"type", "number"}, {"minimum", 0}, {"description", "In the map's currency"}}}};

}  // namespace

void appendGanttEdits(QList<MapEdit> &edits) {
    edits.append({{QStringLiteral("read_gantt"), QStringLiteral("Read the Gantt"),
                   QStringLiteral("Returns the sheet's Gantt plan: every topic is a task (id = the topic's id) with its start, "
                                  "duration in working days, progress and the planned dates; dependencies; the people."),
                   schema({{"sheet", kSheet}}, {}), true, {}},
                  false,
                  [](const Access &, const QJsonObject &, Document &document, MindMapScene &, MindMapModel &model) {
                      return ToolResult::object(ganttJson(document, model));
                  }});

    edits.append({{QStringLiteral("set_gantt_task"), QStringLiteral("Plan a task"),
                   QStringLiteral("Sets a task's start (YYYY-MM-DD, null = none), duration (working days), progress (percent "
                                  "done; a milestone is done at 100) and priority (0 none, 1 very low ... 5 very high). "
                                  "Summaries (topics with children) take their dates and progress from their tasks. A milestone "
                                  "is a topic with a checkbox (set_task)."),
                   schema({{"sheet", kSheet}, {"id", stringProperty(QStringLiteral("Id of the topic"))},
                           {"start", QJsonObject{{"type", QJsonArray{"string", "null"}}, {"description", "YYYY-MM-DD"}}},
                           {"duration", intProperty(QStringLiteral("Working days"), GanttPlan::kMinDuration, GanttPlan::kMaxDuration)},
                           {"progress", intProperty(QStringLiteral("Percent done"), 0, GanttPlan::kMaxProgress)},
                           {"priority", intProperty(QStringLiteral("0 none, 1 very low ... 5 very high"), 0, MindNode::kMaxPriority)}},
                          {QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      MindNode *node = topic(model, args, QStringLiteral("id"), &error);
                      if (!node) return ToolResult::error(error);
                      if (!node->parent) return ToolResult::error(QStringLiteral("The central topic is not a task"));
                      const bool hasStart = args.contains(QStringLiteral("start")), hasDuration = args.contains(QStringLiteral("duration")),
                                 hasProgress = args.contains(QStringLiteral("progress")), hasPriority = args.contains(QStringLiteral("priority"));
                      if (!hasStart && !hasDuration && !hasProgress && !hasPriority)
                          return ToolResult::error(QStringLiteral("Nothing to change: give start, duration, progress or priority"));
                      const QJsonValue priority = args.value(QStringLiteral("priority"));
                      if (hasPriority && (!priority.isDouble() || priority.toInt() < 0 || priority.toInt() > MindNode::kMaxPriority))
                          return ToolResult::error(QStringLiteral("priority must be 0 to 5"));
                      std::optional<QDate> start;
                      if (hasStart && !args.value(QStringLiteral("start")).isNull()) {
                          start = isoDate(args.value(QStringLiteral("start")));
                          if (!start) return ToolResult::error(QStringLiteral("start must be a date YYYY-MM-DD"));
                      }
                      const QJsonValue duration = args.value(QStringLiteral("duration")), progress = args.value(QStringLiteral("progress"));
                      if (hasDuration && (!duration.isDouble() || duration.toInt() < GanttPlan::kMinDuration || duration.toInt() > GanttPlan::kMaxDuration))
                          return ToolResult::error(QStringLiteral("duration must be %1 to %2 working days").arg(GanttPlan::kMinDuration).arg(GanttPlan::kMaxDuration));
                      if (hasProgress && (!progress.isDouble() || progress.toInt() < 0 || progress.toInt() > GanttPlan::kMaxProgress))
                          return ToolResult::error(QStringLiteral("progress must be 0 to 100"));
                      if (!node->children.empty() && (hasDuration || hasProgress))
                          return ToolResult::error(QStringLiteral("%1 is a summary: its duration and progress come from its tasks").arg(node->title));
                      const QString id = node->id;
                      // The priority is the topic's own field: changed before editGantt, whose step takes it in too.
                      if (hasPriority && node->ganttPriority != priority.toInt()) {
                          node->ganttPriority = priority.toInt();
                          emit model.ganttChanged();
                          document.markChanged();
                      }
                      // moveTo: a task placed by a dependency keeps it, its lag changes (as dragging the bar).
                      if (hasStart || hasDuration || hasProgress) document.editGantt([&](GanttPlan &plan) {
                          if (hasStart) start ? plan.moveTo(id, *start) : plan.setStart(id, QDate());
                          if (hasDuration) plan.setDuration(id, duration.toInt());
                          if (hasProgress) plan.setProgress(id, progress.toInt());
                          return true;
                      });
                      const GanttPlan plan = model.gantt();
                      const GanttTask *task = plan.task(id);
                      if (!task || (hasDuration && task->duration != duration.toInt()) || (hasProgress && task->progress != progress.toInt()))
                          return ToolResult::error(QStringLiteral("The plan refused the change"));
                      const GanttScheduledTask planned = plan.schedule().tasks.value(id);
                      QJsonObject result{{"id", id}, {"duration", task->duration}, {"progress", task->progress}, {"priority", node->ganttPriority}};
                      if (planned.dated) {
                          result.insert(QStringLiteral("plannedStart"), planned.start.toString(Qt::ISODate));
                          result.insert(QStringLiteral("plannedEnd"), planned.end.toString(Qt::ISODate));
                      }
                      return ToolResult::object(result);
                  }});

    edits.append({{QStringLiteral("add_dependency"), QStringLiteral("Link two tasks"),
                   QStringLiteral("Adds a dependency between two tasks (not summaries): by default the second starts when the first "
                                  "ends; lag in working days (negative = overlap). Cycles are refused."),
                   schema({{"sheet", kSheet}, {"from", stringProperty(QStringLiteral("Id of the first task"))},
                           {"to", stringProperty(QStringLiteral("Id of the task that depends on it"))}, {"type", kDependencyType},
                           {"lag", intProperty(QStringLiteral("Working days"), -GanttPlan::kMaxLag, GanttPlan::kMaxLag)}},
                          {QStringLiteral("from"), QStringLiteral("to")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const MindNode *from = topic(model, args, QStringLiteral("from"), &error);
                      const MindNode *to = from ? topic(model, args, QStringLiteral("to"), &error) : nullptr;
                      if (!to) return ToolResult::error(error);
                      GanttDependency dependency{from->id, to->id};
                      if (args.contains(QStringLiteral("type"))) {
                          const int type = int(kDependencyTypes.indexOf(args.value(QStringLiteral("type")).toString()));
                          if (type < 0) return ToolResult::error(QStringLiteral("type must be one of %1").arg(kDependencyTypes.join(QStringLiteral(", "))));
                          dependency.type = GanttDependency::Type(type);
                      }
                      if (args.contains(QStringLiteral("lag"))) {
                          if (!args.value(QStringLiteral("lag")).isDouble()) return ToolResult::error(QStringLiteral("lag must be a whole number"));
                          dependency.lag = args.value(QStringLiteral("lag")).toInt();
                      }
                      if (!document.editGantt([&](GanttPlan &plan) { return plan.addDependency(dependency); }))
                          return ToolResult::error(QStringLiteral("Refused: unknown or summary task, the same task, an existing link, or a cycle"));
                      return ToolResult::object({{"from", from->id}, {"to", to->id}});
                  }});

    edits.append({{QStringLiteral("remove_dependency"), QStringLiteral("Unlink two tasks"),
                   QStringLiteral("Removes the dependency between two tasks."),
                   schema({{"sheet", kSheet}, {"from", stringProperty(QStringLiteral("Id of the first task"))},
                           {"to", stringProperty(QStringLiteral("Id of the dependent task"))}},
                          {QStringLiteral("from"), QStringLiteral("to")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
                      const QString from = args.value(QStringLiteral("from")).toString(), to = args.value(QStringLiteral("to")).toString();
                      if (!document.editGantt([&](GanttPlan &plan) { return plan.removeDependency(from, to); }))
                          return ToolResult::error(QStringLiteral("No dependency from %1 to %2").arg(from, to));
                      return ToolResult::object({{"removed", true}});
                  }});

    edits.append({{QStringLiteral("add_person"), QStringLiteral("Add a person"),
                   QStringLiteral("Adds a person (a resource of the whole map) who can be assigned to tasks."),
                   schema(kPersonFields, {QStringLiteral("first_name")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      GanttResource person;
                      person.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                      if (!readPerson(args, &person, &error)) return ToolResult::error(error);
                      if (person.firstName.isEmpty()) return ToolResult::error(QStringLiteral("first_name is empty"));
                      GanttResources resources = document.resources();
                      resources.people.append(person);
                      if (!document.setResources(resources, model.gantt().calendar())) return ToolResult::error(QStringLiteral("The person could not be added"));
                      return ToolResult::object({{"id", person.id}, {"name", person.displayName()}});
                  }});

    edits.append({{QStringLiteral("assign_people"), QStringLiteral("Assign people"),
                   QStringLiteral("Assigns people (ids from read_gantt) to tasks and/or removes them; summaries have no people. "
                                  "percent is the share of the task's time the assign people give it (1-100, default 100); "
                                  "assign people already on the task with a percent to change only their share."),
                   schema({{"sheet", kSheet}, {"ids", idsProperty(QStringLiteral("Tasks"))},
                           {"assign", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}},
                           {"unassign", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}},
                           {"percent", intProperty(QStringLiteral("Share of the task for the assign people"), MindNode::kMinPercent,
                                                   MindNode::kMaxPercent)}},
                          {QStringLiteral("ids")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      if (nodes.isEmpty()) return ToolResult::error(error);
                      QStringList known;
                      for (const GanttResource &person : document.resources().people) known.append(person.id);
                      const auto ids = [&](const QString &key, QStringList *out) {
                          for (const QJsonValue &value : args.value(key).toArray()) {
                              if (!known.contains(value.toString())) { error = QStringLiteral("No person with id %1 (read_gantt lists them)").arg(value.toString()); return false; }
                              out->append(value.toString());
                          }
                          return true;
                      };
                      QStringList assign, unassign;
                      if (!ids(QStringLiteral("assign"), &assign) || !ids(QStringLiteral("unassign"), &unassign)) return ToolResult::error(error);
                      if (assign.isEmpty() && unassign.isEmpty()) return ToolResult::error(QStringLiteral("Nothing to change: give assign or unassign"));
                      std::optional<int> percent;
                      if (args.contains(QStringLiteral("percent"))) {
                          const QJsonValue value = args.value(QStringLiteral("percent"));
                          if (!value.isDouble() || value.toInt() < MindNode::kMinPercent || value.toInt() > MindNode::kMaxPercent)
                              return ToolResult::error(QStringLiteral("percent must be 1 to 100"));
                          if (assign.isEmpty()) return ToolResult::error(QStringLiteral("percent is the share of the assign people: give assign"));
                          percent = value.toInt();
                      }
                      document.setTaskInfo(nodes, std::nullopt, assign, unassign, percent);
                      return ToolResult::object({{"tasks", int(nodes.size())}});
                  }});

    edits.append({{QStringLiteral("update_person"), QStringLiteral("Change a person"),
                   QStringLiteral("Changes a person's first and last name, role, e-mail or hourly cost (omitted fields stay)."),
                   [] { QJsonObject p = kPersonFields; p.insert(QStringLiteral("id"), QJsonObject{{"type", "string"}}); return schema(p, {QStringLiteral("id")}); }(),
                   false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      GanttResources resources = document.resources();
                      const QString id = args.value(QStringLiteral("id")).toString();
                      const auto person = std::find_if(resources.people.begin(), resources.people.end(), [&id](const GanttResource &p) { return p.id == id; });
                      if (person == resources.people.end()) return ToolResult::error(QStringLiteral("No person %1 (read_gantt lists them)").arg(id));
                      QString error;
                      if (!readPerson(args, &*person, &error)) return ToolResult::error(error);
                      if (person->firstName.isEmpty() && person->lastName.isEmpty()) return ToolResult::error(QStringLiteral("A person needs a name"));
                      document.setResources(resources, model.gantt().calendar());
                      return ToolResult::object({{"id", id}, {"name", person->displayName()}});
                  }});

    edits.append({{QStringLiteral("remove_person"), QStringLiteral("Remove a person"),
                   QStringLiteral("Removes a person from the map; one assigned to tasks only after the user's explicit confirmation "
                                  "(Mentis asks when open; otherwise ask in the chat and pass confirm: true)."),
                   schema({{"id", stringProperty(QStringLiteral("Person id"))}, {"confirm", kConfirm}}, {QStringLiteral("id")}), false, {}},
                  true,
                  [](const Access &access, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      GanttResources resources = document.resources();
                      const QString id = args.value(QStringLiteral("id")).toString();
                      const auto person = std::find_if(resources.people.cbegin(), resources.people.cend(), [&id](const GanttResource &p) { return p.id == id; });
                      if (person == resources.people.cend()) return ToolResult::error(QStringLiteral("No person %1").arg(id));
                      int assigned = 0;
                      const std::function<void(const MindNode *)> count = [&](const MindNode *node) {
                          assigned += node->ganttPeople.contains(id) ? 1 : 0;
                          for (const auto &child : node->children) count(child.get());
                      };
                      count(model.root());
                      QString error;
                      if (assigned && !confirmed(access, args, QStringLiteral("remove_person"), person->displayName(), &error)) return ToolResult::error(error);
                      resources.people.erase(person);
                      document.setResources(resources, model.gantt().calendar());
                      return ToolResult::object({{"removed", id}, {"tasks", assigned}});
                  }});

    edits.append({{QStringLiteral("set_currency"), QStringLiteral("Set the currency"),
                   QStringLiteral("Sets the currency of the costs: a name and optionally its symbol, e.g. \"EUR €\", \"USD $\", \"CHF\"."),
                   schema({{"currency", stringProperty(QStringLiteral("Name, then the symbol after a space"))}}, {QStringLiteral("currency")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      const QString currency = args.value(QStringLiteral("currency")).toString().simplified();
                      if (currency.isEmpty() || currency.size() > GanttResources::kMaxCurrencyLength + GanttResources::kMaxSymbolLength + 1)
                          return ToolResult::error(QStringLiteral("currency must be like EUR € or CHF"));
                      GanttResources resources = document.resources();
                      resources.setCurrencyText(currency);
                      document.setResources(resources, model.gantt().calendar());
                      return ToolResult::object({{"currency", document.resources().currencyText()}});
                  }});

    edits.append({{QStringLiteral("set_calendar"), QStringLiteral("Set the working calendar"),
                   QStringLiteral("Sets the sheet's working weekdays (1 Monday ... 7 Sunday), holidays (YYYY-MM-DD) and hours per "
                                  "working day (quarter hours). Omitted parts stay."),
                   schema({{"sheet", kSheet},
                           {"working_days", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}, {"minimum", 1}, {"maximum", 7}}}}},
                           {"holidays", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}}},
                           {"hours_per_day", QJsonObject{{"type", "number"}, {"minimum", 0.25}, {"maximum", 24}}}},
                          {}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      GanttCalendar calendar = model.gantt().calendar();
                      if (args.contains(QStringLiteral("working_days"))) {
                          calendar.workingDays.clear();
                          for (const QJsonValue &day : args.value(QStringLiteral("working_days")).toArray()) {
                              if (!day.isDouble() || day.toInt() < 1 || day.toInt() > 7) return ToolResult::error(QStringLiteral("working_days are 1 to 7"));
                              calendar.workingDays.insert(day.toInt());
                          }
                          if (calendar.workingDays.isEmpty()) return ToolResult::error(QStringLiteral("At least one working day"));
                      }
                      if (args.contains(QStringLiteral("holidays"))) {
                          calendar.holidays.clear();
                          for (const QJsonValue &day : args.value(QStringLiteral("holidays")).toArray()) {
                              const std::optional<QDate> date = isoDate(day);
                              if (!date) return ToolResult::error(QStringLiteral("holidays must be dates YYYY-MM-DD"));
                              calendar.holidays.insert(*date);
                          }
                      }
                      if (args.contains(QStringLiteral("hours_per_day"))) {
                          const int minutes = qRound(args.value(QStringLiteral("hours_per_day")).toDouble() * 60 / GanttCalendar::kMinutesStep) * GanttCalendar::kMinutesStep;
                          if (minutes < GanttCalendar::kMinutesStep || minutes > GanttCalendar::kMaxMinutesPerDay)
                              return ToolResult::error(QStringLiteral("hours_per_day must be 0.25 to 24"));
                          calendar.minutesPerDay = minutes;
                      }
                      if (!document.setResources(document.resources(), calendar) && !(calendar == model.gantt().calendar()))
                          return ToolResult::error(QStringLiteral("The calendar was refused"));
                      return ToolResult::object({{"working_days", int(model.gantt().calendar().workingDays.size())},
                                                 {"holidays", int(model.gantt().calendar().holidays.size())}});
                  }});

    edits.append({{QStringLiteral("add_baseline"), QStringLiteral("Save a baseline"),
                   QStringLiteral("Saves the plan as it is now under a name, to compare with later."),
                   schema({{"sheet", kSheet}, {"name", stringProperty(QStringLiteral("Name of the baseline"))}}, {QStringLiteral("name")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
                      const QString name = args.value(QStringLiteral("name")).toString().simplified();
                      if (name.isEmpty()) return ToolResult::error(QStringLiteral("name is empty"));
                      QString id;
                      if (!document.editGantt([&](GanttPlan &plan) { id = plan.addBaseline(name, QDateTime::currentDateTime()); return !id.isEmpty(); }))
                          return ToolResult::error(QStringLiteral("No baseline: the plan has no tasks"));
                      return ToolResult::object({{"id", id}});
                  }});

    edits.append({{QStringLiteral("rename_baseline"), QStringLiteral("Rename a baseline"),
                   QStringLiteral("Gives a baseline (read_gantt lists them) a new name."),
                   schema({{"sheet", kSheet}, {"id", stringProperty(QStringLiteral("Baseline id"))}, {"name", stringProperty(QStringLiteral("New name"))}},
                          {QStringLiteral("id"), QStringLiteral("name")}), false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &) {
                      const QString id = args.value(QStringLiteral("id")).toString(), name = args.value(QStringLiteral("name")).toString().simplified();
                      if (!document.editGantt([&](GanttPlan &plan) { return plan.renameBaseline(id, name); }))
                          return ToolResult::error(QStringLiteral("No baseline %1, or an empty or unchanged name").arg(id));
                      return ToolResult::object({{"id", id}, {"name", name}});
                  }});

    edits.append({{QStringLiteral("set_gantt_style"), QStringLiteral("Style the Gantt chart"),
                   QStringLiteral("Sets the look of the sheet's whole chart, as the Gantt page of the side panel: for tasks, "
                                  "summaries and milestones their shape, fill, border and colour (#rrggbb, null = the default); "
                                  "for the dependency arrows their line, end mark, width and end size. Omitted parts stay; a "
                                  "task with its own style (set_gantt_task_style) keeps it. read_gantt returns the style."),
                   [] {
                       QJsonObject properties{{"sheet", kSheet}};
                       for (GanttPlan::BarKind kind : kBarKinds)
                           properties.insert(kKindNames.at(int(kind)),
                                             QJsonObject{{"type", "object"}, {"additionalProperties", false},
                                                         {"properties", QJsonObject{{"shape", QJsonObject{{"enum", QJsonArray::fromStringList(styleKeys(kShapeLists[int(kind)]))}}},
                                                                                    {"fill", QJsonObject{{"enum", QJsonArray::fromStringList(fillKeys())}}},
                                                                                    {"border", QJsonObject{{"enum", QJsonArray::fromStringList(styleKeys(StyleList::Border))}}},
                                                                                    {"color", QJsonObject{{"type", QJsonArray{"string", "null"}}}}}}});
                       properties.insert(QStringLiteral("relations"),
                                         QJsonObject{{"type", "object"}, {"additionalProperties", false},
                                                     {"properties", QJsonObject{{"line", QJsonObject{{"enum", QJsonArray::fromStringList(styleKeys(StyleList::LinkLine))}}},
                                                                                {"end", QJsonObject{{"enum", QJsonArray::fromStringList(endKeys())}}},
                                                                                {"width", QJsonObject{{"enum", QJsonArray::fromStringList(styleKeys(StyleList::LinkWidth))}}},
                                                                                {"end_size", QJsonObject{{"enum", QJsonArray::fromStringList(styleKeys(StyleList::LinkEndSize))}}}}}});
                       return schema(properties, {});
                   }(),
                   false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      // Every value is checked before anything changes; then one editGantt, one step.
                      QList<std::function<void(GanttPlan &)>> changes;
                      QString error;
                      const auto key = [&error](const QJsonObject &part, const QString &partName, const QString &name, const QStringList &keys) {
                          const int found = int(keys.indexOf(part.value(name).toString()));
                          if (found < 0) error = QStringLiteral("%1.%2 must be one of %3").arg(partName, name, keys.join(QStringLiteral(", ")));
                          return found;
                      };
                      const auto known = [&error](const QJsonObject &part, const QString &partName, const QStringList &names) {
                          for (const QString &name : part.keys())
                              if (!names.contains(name)) { error = QStringLiteral("%1 has no %2 (%3)").arg(partName, name, names.join(QStringLiteral(", "))); return false; }
                          return true;
                      };
                      for (GanttPlan::BarKind kind : kBarKinds) {
                          const QString partName = kKindNames.at(int(kind));
                          if (!args.contains(partName)) continue;
                          const QJsonObject part = args.value(partName).toObject();
                          if (!args.value(partName).isObject() || !known(part, partName, {"shape", "fill", "border", "color"}))
                              return ToolResult::error(error.isEmpty() ? QStringLiteral("%1 must be an object").arg(partName) : error);
                          if (part.contains(QStringLiteral("shape"))) {
                              const int shape = key(part, partName, QStringLiteral("shape"), styleKeys(kShapeLists[int(kind)]));
                              if (shape < 0) return ToolResult::error(error);
                              changes.append([kind, shape](GanttPlan &plan) {
                                  if (kind == GanttPlan::BarKind::Task) plan.setBarShape(GanttPlan::BarShape(shape));
                                  else if (kind == GanttPlan::BarKind::Summary) plan.setSummaryShape(GanttPlan::SummaryShape(shape));
                                  else plan.setMilestoneShape(GanttPlan::MilestoneShape(shape));
                              });
                          }
                          if (part.contains(QStringLiteral("fill"))) {
                              if (key(part, partName, QStringLiteral("fill"), fillKeys()) < 0) return ToolResult::error(error);
                              const NodeFillStyle fill = *NodeStyle::fillStyleFromKey(part.value(QStringLiteral("fill")).toString());
                              changes.append([kind, fill](GanttPlan &plan) { plan.setFillStyle(kind, fill); });
                          }
                          if (part.contains(QStringLiteral("border"))) {
                              const int border = key(part, partName, QStringLiteral("border"), styleKeys(StyleList::Border));
                              if (border < 0) return ToolResult::error(error);
                              changes.append([kind, border](GanttPlan &plan) { plan.setBorderStyle(kind, GanttPlan::BorderStyle(border)); });
                          }
                          if (part.contains(QStringLiteral("color"))) {
                              const QJsonValue value = part.value(QStringLiteral("color"));
                              const QColor color = value.isNull() ? QColor() : QColor(value.toString());
                              if (!value.isNull() && (!value.isString() || !color.isValid()))
                                  return ToolResult::error(QStringLiteral("%1.color must be a colour like #2563eb or null").arg(partName));
                              changes.append([kind, color](GanttPlan &plan) { plan.setDefaultColor(kind, color); });
                          }
                      }
                      if (args.contains(QStringLiteral("relations"))) {
                          const QString partName = QStringLiteral("relations");
                          const QJsonObject part = args.value(partName).toObject();
                          if (!args.value(partName).isObject() || !known(part, partName, {"line", "end", "width", "end_size"}))
                              return ToolResult::error(error.isEmpty() ? QStringLiteral("relations must be an object") : error);
                          if (part.contains(QStringLiteral("line"))) {
                              const int line = key(part, partName, QStringLiteral("line"), styleKeys(StyleList::LinkLine));
                              if (line < 0) return ToolResult::error(error);
                              changes.append([line](GanttPlan &plan) { plan.setLinkLine(GanttPlan::LinkLine(line)); });
                          }
                          if (part.contains(QStringLiteral("end"))) {
                              if (key(part, partName, QStringLiteral("end"), endKeys()) < 0) return ToolResult::error(error);
                              const BranchEnd end = *NodeStyle::branchEndFromKey(part.value(QStringLiteral("end")).toString());
                              changes.append([end](GanttPlan &plan) { plan.setLinkEnd(end); });
                          }
                          if (part.contains(QStringLiteral("width"))) {
                              const int width = key(part, partName, QStringLiteral("width"), styleKeys(StyleList::LinkWidth));
                              if (width < 0) return ToolResult::error(error);
                              changes.append([width](GanttPlan &plan) { plan.setLinkWidth(GanttPlan::LinkWidth(width)); });
                          }
                          if (part.contains(QStringLiteral("end_size"))) {
                              const int size = key(part, partName, QStringLiteral("end_size"), styleKeys(StyleList::LinkEndSize));
                              if (size < 0) return ToolResult::error(error);
                              changes.append([size](GanttPlan &plan) { plan.setLinkEndSize(GanttPlan::LinkEndSize(size)); });
                          }
                      }
                      if (changes.isEmpty())
                          return ToolResult::error(QStringLiteral("Nothing to change: give tasks, summaries, milestones or relations"));
                      const bool changed = document.editGantt([&changes](GanttPlan &plan) {
                          for (const auto &change : std::as_const(changes)) change(plan);
                          return true;
                      });
                      return ToolResult::object({{"changed", changed}, {"style", styleJson(model.gantt())}});
                  }});

    edits.append({{QStringLiteral("set_gantt_task_style"), QStringLiteral("Style Gantt bars"),
                   QStringLiteral("Gives tasks their own look over the sheet's (set_gantt_style): shape (a key of the task's kind: "
                                  "tasks, summaries or milestones), fill, border, colour (#rrggbb, the topic's branch colour) and "
                                  "whether the name is on the bar. null = back to the sheet's (label: the default, shown on "
                                  "summaries only)."),
                   [] {
                       QStringList shapes = styleKeys(StyleList::BarShape);
                       for (const QString &shape : styleKeys(StyleList::SummaryShape) + styleKeys(StyleList::MilestoneShape))
                           if (!shapes.contains(shape)) shapes.append(shape);
                       const auto keyOrNull = [](const QStringList &keys) {
                           QJsonArray values = QJsonArray::fromStringList(keys);
                           values.append(QJsonValue());
                           return QJsonObject{{"enum", values}};
                       };
                       return schema({{"sheet", kSheet}, {"ids", idsProperty(QStringLiteral("Tasks"))}, {"shape", keyOrNull(shapes)},
                                      {"fill", keyOrNull(fillKeys())}, {"border", keyOrNull(styleKeys(StyleList::Border))},
                                      {"color", QJsonObject{{"type", QJsonArray{"string", "null"}}}},
                                      {"label", QJsonObject{{"type", QJsonArray{"boolean", "null"}}, {"description", "The name on the bar"}}}},
                                     {QStringLiteral("ids")});
                   }(),
                   false, {}},
                  true,
                  [](const Access &, const QJsonObject &args, Document &document, MindMapScene &, MindMapModel &model) {
                      QString error;
                      const QList<MindNode *> nodes = topics(model, args, QStringLiteral("ids"), &error);
                      if (nodes.isEmpty()) return ToolResult::error(error);
                      for (const MindNode *node : nodes)
                          if (!node->parent) return ToolResult::error(QStringLiteral("The central topic is not a task"));
                      using OwnStyle = GanttPlan::OwnStyle;
                      QList<std::pair<OwnStyle, QString>> own;
                      for (const auto &[name, part] : {std::pair{"shape", OwnStyle::Shape}, std::pair{"fill", OwnStyle::Fill}, std::pair{"border", OwnStyle::Border}}) {
                          const QString key = QString::fromLatin1(name);
                          if (!args.contains(key)) continue;
                          const QString value = args.value(key).toString();  // null and "" = the sheet's
                          if (!args.value(key).isNull() && !args.value(key).isString()) return ToolResult::error(QStringLiteral("%1 must be a key or null").arg(key));
                          if (!value.isEmpty() && !GanttPlan::validStyleKey(part, value))
                              return ToolResult::error(QStringLiteral("%1: unknown value %2 (the schema lists them)").arg(key, value));
                          own.append({part, value});
                      }
                      const bool hasColor = args.contains(QStringLiteral("color")), hasLabel = args.contains(QStringLiteral("label"));
                      const QJsonValue colorValue = args.value(QStringLiteral("color")), labelValue = args.value(QStringLiteral("label"));
                      const QColor color = colorValue.isNull() ? QColor() : QColor(colorValue.toString());
                      if (hasColor && !colorValue.isNull() && (!colorValue.isString() || !color.isValid()))
                          return ToolResult::error(QStringLiteral("color must be a colour like #2563eb or null"));
                      if (hasLabel && !labelValue.isNull() && !labelValue.isBool()) return ToolResult::error(QStringLiteral("label must be true, false or null"));
                      if (own.isEmpty() && !hasColor && !hasLabel)
                          return ToolResult::error(QStringLiteral("Nothing to change: give shape, fill, border, color or label"));
                      // Colour and name are the topics' own fields: changed first, editGantt's step takes them in.
                      if (hasColor) {
                          if (color.isValid()) model.setStyle(nodes, StyleProperty::BranchColor, QVariant(color));
                          else model.clearStyle(nodes, StyleProperty::BranchColor);
                      }
                      if (hasLabel) {
                          const std::optional<bool> shown = labelValue.isNull() ? std::nullopt : std::optional<bool>(labelValue.toBool());
                          for (MindNode *node : nodes) node->ganttLabel = shown;
                          emit model.ganttChanged();
                      }
                      if (hasColor || hasLabel) document.markChanged();
                      if (!own.isEmpty())
                          document.editGantt([&](GanttPlan &plan) {
                              for (const MindNode *node : nodes)
                                  for (const auto &[part, value] : std::as_const(own)) plan.setOwnStyle(node->id, part, value);
                              return true;
                          });
                      return ToolResult::object({{"tasks", int(nodes.size())}});
                  }});
}

QList<McpServer::Tool> currencyTools(std::function<Access()> access) {
    const auto guarded = [access](std::function<ToolResult(const QJsonObject &)> run) {
        return [access, run](const QJsonObject &args) {
            if (!access().enabled) return ToolResult::error(offMessage());
            return run(args);
        };
    };
    // The text of a currency ("RON lei"), as set_currency takes it; nullopt with error set.
    const auto currencyArgument = [](const QJsonObject &args, QString *error) -> std::optional<GanttResources> {
        const QString text = args.value(QStringLiteral("currency")).toString().simplified();
        if (text.isEmpty() || text.size() > GanttResources::kMaxCurrencyLength + GanttResources::kMaxSymbolLength + 1) {
            *error = QStringLiteral("currency must be like RON lei or CHF");
            return std::nullopt;
        }
        GanttResources entry;
        entry.setCurrencyText(text);
        return entry.normalized();
    };
    const auto savedJson = [] { return QJsonArray::fromStringList(GanttResources::savedCurrencies()); };
    QList<McpServer::Tool> list;
    list.append({QStringLiteral("list_currencies"), QStringLiteral("List the currencies"),
                 QStringLiteral("Lists the currencies offered in the Gantt's Resources: the built-in ones and those saved on this "
                                "computer (add_currency). A map's own currency is in read_gantt."),
                 schema({}, {}), true, guarded([savedJson](const QJsonObject &) {
                     QStringList builtIn;
                     for (const QString &currency : GanttResources::currencies()) {
                         GanttResources entry;
                         entry.currency = currency;
                         builtIn.append(entry.normalized().currencyText());
                     }
                     return ToolResult::object({{"built_in", QJsonArray::fromStringList(builtIn)}, {"saved", savedJson()},
                                                {"max_saved", GanttResources::kMaxSavedCurrencies}});
                 })});
    list.append({QStringLiteral("add_currency"), QStringLiteral("Add a currency"),
                 QStringLiteral("Saves a new currency on this computer, offered in every map after the built-in ones: its name "
                                "and optionally its symbol after a space (\"RON lei\", \"JPY ¥\"). Past the limit the oldest goes. "
                                "set_currency then uses it in a map."),
                 schema({{"currency", stringProperty(QStringLiteral("Name, then the symbol after a space"))}}, {QStringLiteral("currency")}),
                 false, guarded([currencyArgument, savedJson](const QJsonObject &args) {
                     QString error;
                     const std::optional<GanttResources> entry = currencyArgument(args, &error);
                     if (!entry) return ToolResult::error(error);
                     if (!entry->canSaveCurrency())
                         return ToolResult::error(QStringLiteral("%1 is already offered (list_currencies)").arg(entry->currencyText()));
                     entry->saveCurrency();
                     return ToolResult::object({{"currency", entry->currencyText()}, {"saved", savedJson()}});
                 })});
    list.append({QStringLiteral("remove_currency"), QStringLiteral("Remove a saved currency"),
                 QStringLiteral("Removes a currency saved on this computer (built-in ones stay); maps using it keep it."),
                 schema({{"currency", stringProperty(QStringLiteral("As list_currencies shows it"))}}, {QStringLiteral("currency")}),
                 false, guarded([currencyArgument, savedJson](const QJsonObject &args) {
                     QString error;
                     const std::optional<GanttResources> entry = currencyArgument(args, &error);
                     if (!entry) return ToolResult::error(error);
                     if (!GanttResources::savedCurrencies().contains(entry->currencyText()))
                         return ToolResult::error(QStringLiteral("%1 is not a saved currency (list_currencies)").arg(entry->currencyText()));
                     GanttResources::removeSavedCurrency(entry->currencyText());
                     return ToolResult::object({{"removed", entry->currencyText()}, {"saved", savedJson()}});
                 })});
    return list;
}

}  // namespace McpMapTools::Support
