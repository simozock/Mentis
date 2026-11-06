// FEAT-074: Gantt plan model and scheduling (working days, dependencies, summaries, critical path,
// refused edits, JSON round trip, performance).
#include "ganttcosts.h"
#include "ganttplan.h"
#include "ganttresources.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QTest>

class GanttPlanTest final : public QObject {
    Q_OBJECT
private slots:
    void calendar();
    void dependencyTypes();
    void linkPlaces();
    void summaries();
    void refusedEdits();
    void criticalPath();
    void json();
    void workingWeek();
    void resources();
    void undated();
    void baselines();
    void costs();
    void performance();
};

namespace {

const QDate kMonday(2026, 11, 2);

// duration 0 = a milestone (of one day, the default).
QString add(GanttPlan &plan, const QString &id, const QDate &start, int duration, const QString &parent = {}) {
    GanttTask task;
    task.id = id;
    task.name = id;
    task.start = start;
    task.duration = std::max(duration, GanttPlan::kMinDuration);
    task.milestone = duration == 0;
    task.parentId = parent;
    return plan.addTask(task);
}

GanttDependency dependency(const QString &from, const QString &to, GanttDependency::Type type = GanttDependency::Type::FinishStart,
                           int lag = 0) {
    GanttDependency dependency;
    dependency.from = from;
    dependency.to = to;
    dependency.type = type;
    dependency.lag = lag;
    return dependency;
}

// The default calendar has every day working; the rules are checked on a Monday-Friday week.
GanttCalendar workWeek() {
    GanttCalendar calendar;
    calendar.workingDays = {1, 2, 3, 4, 5};
    return calendar;
}

GanttPlan workWeekPlan() {
    GanttPlan plan;
    plan.setCalendar(workWeek());
    return plan;
}

}  // namespace

void GanttPlanTest::calendar() {
    QCOMPARE(kMonday.dayOfWeek(), 1);
    GanttPlan plan;
    add(plan, "A", kMonday, 3);
    add(plan, "W", kMonday.addDays(5), 1);  // Saturday
    add(plan, "M", kMonday.addDays(6), 0);  // milestone on a Sunday
    // Default calendar: every day works (user, 2026-10-27), weekends included.
    GanttSchedule schedule = plan.schedule();
    QCOMPARE(schedule.tasks["W"].start, kMonday.addDays(5));
    QCOMPARE(schedule.tasks["M"].start, kMonday.addDays(6));
    QCOMPARE(plan.workingDaysBetween(kMonday, kMonday.addDays(6)), 7);

    QVERIFY(plan.setCalendar(workWeek()));
    schedule = plan.schedule();
    QCOMPARE(schedule.tasks["A"].end, kMonday.addDays(2));
    QCOMPARE(schedule.tasks["W"].start, kMonday.addDays(7));
    QCOMPARE(schedule.tasks["W"].end, kMonday.addDays(7));
    QCOMPARE(schedule.tasks["M"].start, kMonday.addDays(7));
    QCOMPARE(schedule.tasks["M"].end, kMonday.addDays(7));

    GanttCalendar calendar = workWeek();
    calendar.holidays.insert(kMonday.addDays(1));
    QVERIFY(plan.setCalendar(calendar));
    QCOMPARE(plan.schedule().tasks["A"].end, kMonday.addDays(3));  // holiday Tuesday: ends Thursday
    QCOMPARE(plan.workingDaysBetween(kMonday, kMonday.addDays(6)), 4);  // Mon-Sun without Tuesday
    QCOMPARE(plan.workingDaysBetween(kMonday.addDays(5), kMonday.addDays(5)), 0);  // Saturday
    QCOMPARE(plan.workingDaysBetween(kMonday.addDays(1), kMonday), 0);

    calendar.workingDays = {1, 2, 3, 4, 5, 6};  // six-day week
    QVERIFY(plan.setCalendar(calendar));
    schedule = plan.schedule();
    QCOMPARE(schedule.tasks["W"].start, kMonday.addDays(5));

    calendar.workingDays.clear();
    QVERIFY(!plan.setCalendar(calendar));
    calendar.workingDays = {0};
    QVERIFY(!plan.setCalendar(calendar));
    QVERIFY(plan.calendar().workingDays.contains(6));

    // A year far from 2026 keeps the arithmetic consistent.
    GanttPlan far;
    add(far, "X", QDate(1900, 1, 1), 1);
    QCOMPARE(far.schedule().tasks["X"].start, QDate(1900, 1, 1));
}

void GanttPlanTest::dependencyTypes() {
    using Type = GanttDependency::Type;
    GanttPlan plan = workWeekPlan();
    add(plan, "A", kMonday, 3);  // Mon-Wed
    add(plan, "B", kMonday, 2);
    add(plan, "C", kMonday, 1);
    add(plan, "D", kMonday, 2);
    add(plan, "E", kMonday, 2);
    add(plan, "F", kMonday, 1);
    add(plan, "M", kMonday.addDays(3), 0);  // Thursday milestone
    add(plan, "N", kMonday, 1);
    QVERIFY(plan.addDependency(dependency("A", "B")));
    QVERIFY(plan.addDependency(dependency("A", "C", Type::FinishStart, 2)));
    QVERIFY(plan.addDependency(dependency("A", "D", Type::StartStart, 1)));
    QVERIFY(plan.addDependency(dependency("A", "E", Type::FinishFinish, 1)));
    QVERIFY(plan.addDependency(dependency("C", "F", Type::StartFinish, 0)));
    QVERIFY(plan.addDependency(dependency("M", "N")));
    const GanttSchedule schedule = plan.schedule();
    QCOMPARE(schedule.tasks["B"].start, kMonday.addDays(3));  // Thursday
    QCOMPARE(schedule.tasks["B"].end, kMonday.addDays(4));
    QCOMPARE(schedule.tasks["C"].start, kMonday.addDays(7));  // Thursday + 2 = Monday
    QCOMPARE(schedule.tasks["D"].start, kMonday.addDays(1));
    QCOMPARE(schedule.tasks["E"].end, kMonday.addDays(3));    // finish Thursday + 1 → last day Thursday
    QCOMPARE(schedule.tasks["F"].end, kMonday.addDays(4));    // finish at C's start: last day Friday
    QCOMPARE(schedule.tasks["N"].start, kMonday.addDays(3));  // starts on the milestone's day
    // Off, the milestone takes its kept duration again.
    QVERIFY(plan.setMilestone("M", false));
    QVERIFY(!plan.setMilestone("M", false));
    QCOMPARE(plan.schedule().tasks["N"].start, kMonday.addDays(4));
    QVERIFY(plan.setMilestone("M", true));

    QVERIFY(plan.updateDependency(dependency("A", "C", Type::FinishStart, -1)));  // one-day lead
    QCOMPARE(plan.schedule().tasks["C"].start, kMonday.addDays(2));
    QVERIFY(!plan.updateDependency(dependency("A", "C", Type::FinishStart, -1)));
    QVERIFY(plan.removeDependency("A", "C"));
    QCOMPARE(plan.schedule().tasks["C"].start, kMonday.addDays(2));  // stays where the link put it
    QCOMPARE(plan.task("C")->start, kMonday.addDays(2));
}

void GanttPlanTest::linkPlaces() {
    // User, 2026-10-27: a dependency places its successor exactly; moving it changes the lag.
    using Type = GanttDependency::Type;
    GanttPlan plan = workWeekPlan();
    add(plan, "A", kMonday, 3);
    add(plan, "B", kMonday.addDays(18), 2);  // Friday 11-20: its own date no longer counts
    QVERIFY(plan.addDependency(dependency("A", "B")));
    QCOMPARE(plan.schedule().tasks["B"].start, kMonday.addDays(3));  // Thursday, right after A
    QCOMPARE(plan.schedule().placedBy.value("B"), 0);
    QVERIFY(!plan.schedule().placedBy.contains("A"));
    // Moved to Tuesday of the next week: three working days later (Fri, Mon, Tue), lag 3.
    QVERIFY(plan.moveTo("B", kMonday.addDays(8)));
    QCOMPARE(plan.dependencies().at(0).lag, 3);
    QCOMPARE(plan.schedule().tasks["B"].start, kMonday.addDays(8));
    // Moved before A's end: a lead.
    QVERIFY(plan.moveTo("B", kMonday.addDays(1)));
    QCOMPARE(plan.dependencies().at(0).lag, -2);
    QVERIFY(!plan.moveTo("B", kMonday.addDays(1)));  // same place: no change
    // Moving A moves B along.
    QVERIFY(plan.moveTo("A", kMonday.addDays(7)));
    QCOMPARE(plan.schedule().tasks["B"].start, kMonday.addDays(8));
    // Start to start: the same start; Finish to finish: the same end, whatever the durations.
    QVERIFY(plan.updateDependency(dependency("A", "B", Type::StartStart)));
    QCOMPARE(plan.schedule().tasks["B"].start, plan.schedule().tasks["A"].start);
    QVERIFY(plan.moveTo("B", kMonday.addDays(9)));  // two working days after A's start
    QCOMPARE(plan.dependencies().at(0).lag, 2);
    QVERIFY(plan.updateDependency(dependency("A", "B", Type::FinishFinish)));
    QCOMPARE(plan.schedule().tasks["B"].end, plan.schedule().tasks["A"].end);
    // Two predecessors: the latest places the task and takes the move; the other one stays.
    add(plan, "C", kMonday, 10);
    QVERIFY(plan.updateDependency(dependency("A", "B")));
    QVERIFY(plan.addDependency(dependency("C", "B")));
    QCOMPARE(plan.schedule().placedBy.value("B"), 1);
    QVERIFY(plan.moveTo("B", plan.schedule().tasks["B"].start.addDays(1)));
    QCOMPARE(plan.dependencies().at(1).lag, 1);
    QCOMPARE(plan.dependencies().at(0).lag, 0);
    // A task without predecessors moves its own start.
    QVERIFY(plan.moveTo("A", kMonday));
    QCOMPARE(plan.task("A")->start, kMonday);
}

void GanttPlanTest::summaries() {
    GanttPlan plan = workWeekPlan();
    add(plan, "S", kMonday, 1);
    add(plan, "A", kMonday, 2, "S");
    add(plan, "B", kMonday.addDays(7), 6, "S");
    add(plan, "M", kMonday.addDays(1), 0, "S");
    QVERIFY(plan.setProgress("A", 100));
    QVERIFY(plan.setProgress("B", 50));
    QVERIFY(plan.setProgress("M", 100));  // a milestone weighs nothing
    GanttSchedule schedule = plan.schedule();
    const GanttScheduledTask summary = schedule.tasks["S"];
    QVERIFY(summary.summary);
    QCOMPARE(summary.start, kMonday);
    QCOMPARE(summary.end, kMonday.addDays(14));  // B: Mon 11-09 + 6 days = Mon 11-16
    QCOMPARE(summary.progress, (100 * 2 + 50 * 6) / 8);
    QCOMPARE(plan.children("S"), QStringList({"A", "B", "M"}));

    // Only milestones: plain average.
    GanttPlan milestones;
    add(milestones, "S", kMonday, 1);
    add(milestones, "X", kMonday, 0, "S");
    add(milestones, "Y", kMonday, 0, "S");
    QVERIFY(milestones.setProgress("X", 100));
    QCOMPARE(milestones.schedule().tasks["S"].progress, 50);
    QCOMPARE(milestones.schedule().tasks["S"].end, kMonday);

    // A dependency of a task that gains children is kept but ignored.
    GanttPlan grows;
    add(grows, "A", kMonday, 2);
    add(grows, "B", kMonday, 2);
    QVERIFY(grows.addDependency(dependency("A", "B")));
    add(grows, "C", kMonday.addDays(14), 1, "B");
    schedule = grows.schedule();
    QCOMPARE(schedule.ignoredDependencies, QSet<int>{0});
    QCOMPARE(schedule.tasks["B"].start, kMonday.addDays(14));
    QVERIFY(grows.removeTask("C"));
    QVERIFY(grows.schedule().ignoredDependencies.isEmpty());

    // Moving and removing keep the order among siblings and drop dependencies of removed tasks.
    QVERIFY(plan.moveTask("M", "S", 0));
    QCOMPARE(plan.children("S"), QStringList({"M", "A", "B"}));
    QVERIFY(!plan.moveTask("M", "S", 0));
    QVERIFY(plan.moveTask("B", {}));
    QCOMPARE(plan.children({}), QStringList({"S", "B"}));
    QVERIFY(plan.addDependency(dependency("A", "B")));
    QVERIFY(plan.removeTask("S"));
    QCOMPARE(plan.tasks().size(), 1);
    QVERIFY(plan.dependencies().isEmpty());
}

void GanttPlanTest::refusedEdits() {
    GanttPlan plan;
    add(plan, "S", kMonday, 1);
    add(plan, "A", kMonday, 1, "S");
    add(plan, "B", kMonday, 1);
    add(plan, "C", kMonday, 1);
    QVERIFY(plan.addDependency(dependency("A", "B")));
    QVERIFY(plan.addDependency(dependency("B", "C")));
    const GanttPlan before = plan;

    QVERIFY(!plan.addDependency(dependency("C", "A")));   // cycle
    QVERIFY(!plan.addDependency(dependency("B", "A")));   // reverse of an existing pair
    QVERIFY(!plan.addDependency(dependency("A", "B")));   // duplicate
    QVERIFY(!plan.addDependency(dependency("B", "B")));   // itself
    QVERIFY(!plan.addDependency(dependency("S", "C")));   // summary
    QVERIFY(!plan.addDependency(dependency("A", "X")));   // unknown
    QVERIFY(!plan.addDependency(dependency("A", "C", GanttDependency::Type::FinishStart, GanttPlan::kMaxLag + 1)));
    QVERIFY(!plan.moveTask("S", "A"));              // under its own child
    QVERIFY(!plan.moveTask("S", "S"));
    QVERIFY(!plan.setDuration("A", 0));  // at least one day: a milestone is a flag
    QVERIFY(!plan.setDuration("A", GanttPlan::kMaxDuration + 1));
    QVERIFY(!plan.setProgress("A", 101));
    QVERIFY(!plan.setStart("A", QDate(2201, 1, 1)));
    QVERIFY(!plan.setName("A", "A"));
    QVERIFY(!plan.removeTask("X"));
    GanttTask orphan;
    orphan.start = kMonday;
    orphan.parentId = "X";
    QVERIFY(plan.addTask(orphan).isEmpty());
    QVERIFY(plan == before);

    GanttTask copy;
    copy.id = "A";  // used id: a new one is given
    copy.start = kMonday;
    const QString id = plan.addTask(copy);
    QVERIFY(!id.isEmpty() && id != "A");
}

void GanttPlanTest::criticalPath() {
    GanttPlan plan = workWeekPlan();
    add(plan, "A", kMonday, 5);
    add(plan, "B", kMonday, 2);
    add(plan, "C", kMonday, 1);
    QVERIFY(plan.addDependency(dependency("A", "C")));
    QVERIFY(plan.addDependency(dependency("B", "C")));
    const GanttSchedule schedule = plan.schedule();
    QVERIFY(schedule.tasks["A"].critical);
    QVERIFY(schedule.tasks["C"].critical);
    QVERIFY(!schedule.tasks["B"].critical);
    QCOMPARE(schedule.tasks["B"].totalFloat, 3);
    QCOMPARE(schedule.tasks["A"].totalFloat, 0);
    QCOMPARE(schedule.criticalDependencies, QSet<int>{0});
    QCOMPARE(schedule.start, kMonday);
    QCOMPARE(schedule.end, kMonday.addDays(7));

    // A summary is critical when a leaf is; its float is the smallest.
    GanttPlan nested;
    add(nested, "S", kMonday, 1);
    add(nested, "A", kMonday, 5, "S");
    add(nested, "B", kMonday, 2, "S");
    const GanttSchedule summary = nested.schedule();
    QVERIFY(summary.tasks["S"].critical);
    QCOMPARE(summary.tasks["S"].totalFloat, 0);
    QCOMPARE(summary.tasks["B"].totalFloat, 3);

    // Backward pass of every type: each pair below is tight, so both ends are critical.
    using Type = GanttDependency::Type;
    const struct {
        Type type;
        int lag, fromDays, toDays;
    } cases[] = {
        {Type::FinishStart, -1, 3, 2},  // lead: B starts Wednesday, ends Thursday
        {Type::StartStart, 1, 3, 2},    // B Tuesday-Wednesday, A ends Wednesday
        {Type::FinishFinish, 1, 3, 1},  // B on Thursday
        {Type::StartFinish, 3, 1, 3},   // B finishes three days after A starts
    };
    for (const auto &c : cases) {
        GanttPlan pair;
        add(pair, "A", kMonday, c.fromDays);
        add(pair, "B", kMonday, c.toDays);
        QVERIFY(pair.addDependency(dependency("A", "B", c.type, c.lag)));
        const GanttSchedule tight = pair.schedule();
        QCOMPARE(tight.tasks["A"].totalFloat, 0);
        QCOMPARE(tight.tasks["B"].totalFloat, 0);
        QCOMPARE(tight.criticalDependencies, QSet<int>{0});
    }
    // SF whose successor has float: not a critical dependency.
    GanttPlan loose;
    add(loose, "A", kMonday.addDays(2), 1);
    add(loose, "B", kMonday, 2);
    QVERIFY(loose.addDependency(dependency("A", "B", Type::StartFinish)));
    const GanttSchedule free = loose.schedule();
    QCOMPARE(free.tasks["B"].start, kMonday);
    QCOMPARE(free.tasks["B"].totalFloat, 1);
    QVERIFY(free.criticalDependencies.isEmpty());
}

// FEAT-079: weekends off, a typed duration counts working days; hours per day are only stored.
void GanttPlanTest::workingWeek() {
    GanttPlan plan;
    const QDate friday = kMonday.addDays(4);
    add(plan, "F", friday, 2);
    QVERIFY(plan.setCalendar(workWeek()));
    QCOMPARE(plan.schedule().tasks["F"].end, kMonday.addDays(7));  // Friday + 2 days ends Monday
    // A right edge released on Sunday keeps the working days covered: Friday only.
    QCOMPARE(plan.workingDaysBetween(friday, kMonday.addDays(6)), 1);
    QCOMPARE(plan.workingDaysBetween(friday, kMonday.addDays(7)), 2);

    GanttCalendar calendar = plan.calendar();
    QCOMPARE(calendar.minutesPerDay, 480);
    QVERIFY(!plan.toJson().value("calendar").toObject().contains("minutesPerDay"));
    for (int wrong : {0, 1450, 490}) {  // out of range, not a quarter hour
        calendar.minutesPerDay = wrong;
        QVERIFY(!plan.setCalendar(calendar));
    }
    calendar.minutesPerDay = 8 * 60 + 15;
    QVERIFY(plan.setCalendar(calendar));
    QCOMPARE(plan.schedule().tasks["F"].end, kMonday.addDays(7));  // the hours do not move dates
    QCOMPARE(plan.toJson().value("calendar").toObject().value("minutesPerDay").toInt(), 495);
    QCOMPARE(GanttPlan::fromJson(plan.toJson()).calendar(), plan.calendar());
    int repairs = 0;
    GanttPlan repaired = GanttPlan::fromJson(QJsonObject{{"calendar", QJsonObject{{"minutesPerDay", 2000}}}}, &repairs);
    QCOMPARE(repaired.calendar().minutesPerDay, 1440);
    QCOMPARE(repairs, 1);
    repairs = 0;
    repaired = GanttPlan::fromJson(QJsonObject{{"calendar", QJsonObject{{"minutesPerDay", 500}}}}, &repairs);
    QCOMPARE(repaired.calendar().minutesPerDay, 495);  // the nearest quarter hour
    QCOMPARE(repairs, 1);
}

// FEAT-079: the document's people.
void GanttPlanTest::resources() {
    GanttResources empty;
    QVERIFY(empty.toJson().isEmpty());  // nothing written by default
    QCOMPARE(GanttResources::fromJson({}), empty);

    QVERIFY(GanttResources::isValidEmail(""));
    QVERIFY(GanttResources::isValidEmail(" anna@example.com "));
    QVERIFY(GanttResources::isValidEmail("a@b"));
    QVERIFY(!GanttResources::isValidEmail("anna"));
    QVERIFY(!GanttResources::isValidEmail("@example.com"));
    QVERIFY(!GanttResources::isValidEmail("anna@"));
    QVERIFY(!GanttResources::isValidEmail("a@b@c"));

    GanttResources list;
    list.currency = "  ";
    list.people = {{{}, "  Anna ", "Rossi", "anna@example.com", "Developer", 4550},
                   {{}, "", "", "", "", 0},  // empty row: dropped
                   {"keep", QString(300, 'x'), "", "", "", GanttResources::kMaxHourlyCostCents + 1}};
    const GanttResources clean = list.normalized();
    QCOMPARE(clean.currency, QString("EUR"));  // no name: EUR with its symbol
    QCOMPARE(clean.currencySymbol, QString("€"));
    QCOMPARE(clean.people.size(), 2);
    QCOMPARE(clean.people[0].firstName, QString("Anna"));
    QVERIFY(!clean.people[0].id.isEmpty());
    QCOMPARE(clean.people[1].id, QString("keep"));
    QCOMPARE(clean.people[1].firstName.size(), GanttResources::kMaxTextLength);
    QCOMPARE(clean.people[1].hourlyCostCents, GanttResources::kMaxHourlyCostCents);
    QCOMPARE(clean.normalized(), clean);

    // A currency typed as its name and symbol (user, 2026-10-29).
    GanttResources typed;
    typed.setCurrencyText("  Bitcoin   ₿ ");
    QCOMPARE(typed.currency, QString("Bitcoin"));
    QCOMPARE(typed.currencySymbol, QString("₿"));
    QCOMPARE(typed.currencyText(), QString("Bitcoin ₿"));
    typed.setCurrencyText("USD");  // a listed one without its symbol takes it
    QCOMPARE(typed.normalized().currencySymbol, QString("$"));
    typed.setCurrencyText("SEK");  // no symbol: the name is shown
    QCOMPARE(typed.normalized().displaySymbol(), QString("SEK"));
    QCOMPARE(typed.normalized().currencyText(), QString("SEK"));
    typed.setCurrencyText("Very long currency name 123456");
    QCOMPARE(typed.normalized().currency.size(), GanttResources::kMaxCurrencyLength);
    QCOMPARE(typed.normalized().currencySymbol, QString("12345"));
    QVERIFY(!typed.normalized().toJson().contains("people"));

    GanttResources francs = clean;
    francs.currency = "CHF";
    francs.currencySymbol.clear();
    QJsonObject json = francs.toJson();
    QCOMPARE(json.value("currency").toString(), QString("CHF"));
    QVERIFY(!json.contains("currencySymbol"));  // a listed currency's own symbol is not written
    QCOMPARE(json.value("people").toArray()[0].toObject().value("hourlyCost").toDouble(), 45.5);
    QVERIFY(!json.value("people").toArray()[1].toObject().contains("email"));
    QCOMPARE(GanttResources::fromJson(json), francs);
    francs.setCurrencyText("RON lei");
    json = francs.toJson();
    QCOMPARE(json.value("currencySymbol").toString(), QString("lei"));
    QCOMPARE(GanttResources::fromJson(json), francs);
}

void GanttPlanTest::json() {
    GanttPlan plan;
    add(plan, "S", kMonday, 1);
    add(plan, "A", kMonday, 2, "S");
    add(plan, "B", kMonday.addDays(3), 0);
    QVERIFY(plan.setColor("A", QColor("#e05a47")));
    QVERIFY(plan.setProgress("A", 40));
    QVERIFY(plan.setNodeId("A", "node-1"));
    QVERIFY(plan.setDuration("B", 4));  // kept while B is a milestone
    QVERIFY(plan.addDependency(dependency("A", "B", GanttDependency::Type::StartFinish, -2)));
    GanttCalendar calendar;
    calendar.workingDays = {1, 2, 3, 4};
    calendar.holidays = {kMonday.addDays(1)};
    QVERIFY(plan.setCalendar(calendar));
    int repairs = -1;
    QVERIFY(GanttPlan::fromJson(plan.toJson(), &repairs) == plan);
    QCOMPARE(repairs, 0);
    QVERIFY(GanttPlan::fromJson(GanttPlan().toJson()) == GanttPlan());

    const QJsonObject bad{
        {"tasks", QJsonArray{
            QJsonObject{{"id", "A"}, {"start", "2026-11-04"}, {"duration", 99999}, {"progress", -5}},
            QJsonObject{{"id", "A"}, {"start", "not a date"}},              // duplicate id, bad start
            QJsonObject{{"id", "P"}, {"start", "2026-11-02"}, {"parent", "Q"}},
            QJsonObject{{"id", "Q"}, {"start", "2026-11-02"}, {"parent", "P"}},  // parent loop
            QJsonObject{{"id", "R"}, {"start", "2026-11-02"}, {"parent", "missing"}, {"color", "nope"}},
        }},
        {"dependencies", QJsonArray{
            QJsonObject{{"from", "A"}, {"to", "R"}, {"type", "XX"}, {"lag", 99999}},
            QJsonObject{{"from", "R"}, {"to", "A"}},                  // reverse pair
            QJsonObject{{"from", "A"}, {"to", "ghost"}},
        }},
        {"calendar", QJsonObject{{"workingDays", QJsonArray{0, 8}}, {"holidays", QJsonArray{"x"}}}},
    };
    const GanttPlan repaired = GanttPlan::fromJson(bad, &repairs);
    // duration, progress, duplicate id, start, loop, unknown parent, colour, type, lag, reverse pair,
    // unknown end, two working days, holiday, empty week.
    QCOMPARE(repairs, 15);
    QCOMPARE(repaired.tasks().size(), 5);
    QCOMPARE(repaired.tasks().at(0).duration, GanttPlan::kMaxDuration);
    QVERIFY(repaired.tasks().at(1).start.isNull());  // a bad date is dropped
    QCOMPARE(repaired.dependencies().size(), 1);
    QCOMPARE(repaired.dependencies().at(0).lag, GanttPlan::kMaxLag);
    QCOMPARE(repaired.calendar(), GanttCalendar());
    QVERIFY(repaired.task("R")->parentId.isEmpty());
    QVERIFY(repaired.task("P")->parentId.isEmpty() || repaired.task("Q")->parentId.isEmpty());
    repaired.schedule();  // must not hang or crash

    // Only the task closing a loop moves: X hangs below the loop and keeps its parent.
    const QJsonObject loop{{"tasks", QJsonArray{
        QJsonObject{{"id", "X"}, {"start", "2026-11-02"}, {"parent", "P"}},
        QJsonObject{{"id", "P"}, {"start", "2026-11-02"}, {"parent", "Q"}},
        QJsonObject{{"id", "Q"}, {"start", "2026-11-02"}, {"parent", "P"}},
    }}};
    const GanttPlan unlooped = GanttPlan::fromJson(loop, &repairs);
    QCOMPARE(repairs, 1);
    QCOMPARE(unlooped.task("X")->parentId, QString("P"));
    QCOMPARE(unlooped.children({}).size(), 1);

    // Non-integer values are repairs; holidays alone keep the default week without a repair.
    const QJsonObject values{
        {"tasks", QJsonArray{QJsonObject{{"id", "A"}, {"start", "2026-11-02"}, {"duration", 2.5}, {"progress", 1e12}}}},
        {"calendar", QJsonObject{{"holidays", QJsonArray{"2026-11-03"}}}},
    };
    const GanttPlan cleaned = GanttPlan::fromJson(values, &repairs);
    QCOMPARE(repairs, 2);
    QCOMPARE(cleaned.tasks().at(0).duration, 1);
    QCOMPARE(cleaned.tasks().at(0).progress, GanttPlan::kMaxProgress);
    QCOMPARE(cleaned.calendar().workingDays, GanttCalendar().workingDays);

    // Translucent and HSV colours survive the round trip.
    GanttPlan colours;
    add(colours, "A", kMonday, 1);
    add(colours, "B", kMonday, 1);
    QVERIFY(colours.setColor("A", QColor(10, 20, 30, 128)));
    QVERIFY(colours.setColor("B", QColor::fromHsv(200, 100, 150)));
    QVERIFY(GanttPlan::fromJson(colours.toJson()) == colours);
}

void GanttPlanTest::undated() {
    // FEAT-075: a task without a date has no bar until a dated predecessor gives it one.
    GanttPlan plan;
    add(plan, "S", QDate(), 1);
    add(plan, "A", QDate(), 2, "S");
    add(plan, "B", QDate(), 3, "S");
    add(plan, "C", QDate(), 1);
    GanttSchedule schedule = plan.schedule();
    QVERIFY(!schedule.tasks["A"].dated && !schedule.tasks["S"].dated);
    QVERIFY(!schedule.start.isValid());
    QVERIFY(plan.addDependency(dependency("A", "B")));
    QVERIFY(!plan.schedule().tasks["B"].dated);  // the predecessor has no date either
    QVERIFY(plan.setStart("A", kMonday));
    schedule = plan.schedule();
    QCOMPARE(schedule.tasks["B"].start, kMonday.addDays(2));
    QVERIFY(schedule.tasks["S"].dated);
    QCOMPARE(schedule.tasks["S"].end, kMonday.addDays(4));
    QVERIFY(!schedule.tasks["C"].dated);
    QVERIFY(schedule.tasks["A"].critical && schedule.tasks["B"].critical);
    QVERIFY(GanttPlan::fromJson(plan.toJson()) == plan);
    QVERIFY(plan.setStart("A", QDate()));  // the date is removed again
    QVERIFY(!plan.schedule().tasks["A"].dated);
}

void GanttPlanTest::performance() {
    // 1,000 tasks in 50 summaries, 2,000 dependencies forward in the list (no cycles).
    GanttPlan plan;
    QStringList leaves;
    for (int s = 0; s < 50; ++s) {
        const QString summary = add(plan, QStringLiteral("S%1").arg(s), kMonday, 1);
        for (int t = 0; t < 19; ++t)
            leaves.append(add(plan, QStringLiteral("T%1-%2").arg(s).arg(t), kMonday.addDays(t % 5), 1 + t % 7, summary));
    }
    int added = 0;
    for (int i = 1; i < leaves.size() && added < 2000; ++i)
        for (int back : {1, 7, 31})
            if (i - back >= 0 && added < 2000 && plan.addDependency(dependency(leaves.at(i - back), leaves.at(i)))) ++added;
    QCOMPARE(plan.tasks().size(), 1000);
    QCOMPARE(added, 2000);
    QElapsedTimer timer;
    timer.start();
    const GanttSchedule schedule = plan.schedule();
    const double elapsed = timer.nsecsElapsed() / 1e6;
    qInfo("schedule of 1000 tasks / 2000 dependencies: %.2f ms", elapsed);
    QCOMPARE(schedule.tasks.size(), 1000);
    QVERIFY(schedule.ignoredDependencies.isEmpty());
#ifdef NDEBUG  // the budget is for Release builds (FEAT-074)
    QVERIFY2(elapsed < 20, qPrintable(QStringLiteral("%1 ms").arg(elapsed)));
#endif

    // Loading stays linear: 5,000 nested tasks and a dependency chain with a cycle back to the top.
    QJsonArray deep, chain;
    for (int i = 0; i < 5000; ++i) {
        QJsonObject task{{"id", QString::number(i)}, {"start", "2026-11-02"}};
        if (i > 0) task.insert("parent", QString::number(i - 1));
        deep.append(task);
    }
    for (int i = 0; i < 4999; ++i) chain.append(QJsonObject{{"from", QString::number(i)}, {"to", QString::number(i + 1)}});
    chain.append(QJsonObject{{"from", "4999"}, {"to", "0"}});
    timer.restart();
    int repairs = -1;
    const GanttPlan loaded = GanttPlan::fromJson(QJsonObject{{"tasks", deep}, {"dependencies", chain}}, &repairs);
    const double loading = timer.nsecsElapsed() / 1e6;
    qInfo("loading 5000 nested tasks / 5000 dependencies: %.2f ms", loading);
    QCOMPARE(repairs, 1);
    QCOMPARE(loaded.dependencies().size(), 4999);
#ifdef NDEBUG
    QVERIFY2(loading < 200, qPrintable(QStringLiteral("%1 ms").arg(loading)));
#endif
}

void GanttPlanTest::baselines() {
    // FEAT-082: a snapshot of the computed dates, summaries included; compared by the end.
    GanttPlan plan;
    QVERIFY(plan.addBaseline("Empty", QDateTime::currentDateTime()).isEmpty());  // no tasks
    add(plan, "S", {}, 1);
    add(plan, "A", kMonday, 3, "S");
    add(plan, "B", kMonday.addDays(1), 2, "S");
    add(plan, "M", kMonday.addDays(5), 0);
    add(plan, "C", kMonday.addDays(9), 2);
    QVERIFY(plan.addBaseline("   ", QDateTime::currentDateTime()).isEmpty());
    const QDateTime taken(kMonday, QTime(9, 30));
    const QString id = plan.addBaseline("  Plan   approved ", taken);
    QVERIFY(!id.isEmpty());
    const GanttBaseline *baseline = plan.baseline(id);
    QVERIFY(baseline);
    QCOMPARE(baseline->name, QString("Plan approved"));
    QCOMPARE(baseline->tasks.size(), 5);
    const GanttBaselineTask *summary = baseline->task("S");
    QVERIFY(summary && summary->dated);
    QCOMPARE(summary->start, kMonday);
    QCOMPARE(summary->end, kMonday.addDays(2));
    QCOMPARE(summary->duration, 3);
    QVERIFY(baseline->task("M")->milestone);
    QCOMPARE(baseline->task("M")->duration, 0);
    // Later, earlier, same (a start moved with the same end), new and removed.
    QVERIFY(plan.setDuration("A", 5));    // ends later
    QVERIFY(plan.setStart("C", kMonday.addDays(7)));  // ends earlier
    QVERIFY(plan.setStart("B", kMonday));
    QVERIFY(plan.setDuration("B", 3));    // same end, start earlier
    QVERIFY(plan.removeTask("M"));
    add(plan, "N", kMonday, 1);
    const GanttBaselineComparison comparison = plan.compare(plan.schedule(), *plan.baseline(id));
    QCOMPARE(comparison.tasks.value("A"), GanttBaselineComparison::Change::Later);
    QCOMPARE(comparison.tasks.value("S"), GanttBaselineComparison::Change::Later);
    QCOMPARE(comparison.tasks.value("C"), GanttBaselineComparison::Change::Earlier);
    QCOMPARE(comparison.tasks.value("B"), GanttBaselineComparison::Change::Same);
    QCOMPARE(comparison.tasks.value("N"), GanttBaselineComparison::Change::New);
    QCOMPARE(comparison.removed, QStringList({"M"}));
    // Renaming: an empty or equal name is refused; the contents never change.
    QVERIFY(!plan.renameBaseline(id, " "));
    QVERIFY(!plan.renameBaseline(id, "Plan approved"));
    QVERIFY(plan.renameBaseline(id, "First"));
    QVERIFY(!plan.renameBaseline("nope", "x"));
    QCOMPARE(plan.addBaseline(QString(100, 'x'), taken).isEmpty(), false);
    QCOMPARE(plan.baselines().last().name.size(), GanttBaseline::kMaxName);
    // JSON round trip; bad entries dropped.
    int repairs = 0;
    QCOMPARE(GanttPlan::fromJson(plan.toJson(), &repairs), plan);
    QCOMPARE(repairs, 0);
    QJsonObject json = plan.toJson();
    QJsonArray baselines = json.value("baselines").toArray();
    baselines.append(QJsonObject{{"id", ""}, {"name", "No id"}});
    baselines.append(QJsonObject{{"id", id}, {"name", "Same id"}});
    json.insert("baselines", baselines);
    const GanttPlan read = GanttPlan::fromJson(json, &repairs);
    QCOMPARE(read.baselines().size(), 2);
    QCOMPARE(repairs, 2);
    QVERIFY(plan.removeBaseline(id));
    QVERIFY(!plan.removeBaseline(id));
    QCOMPARE(plan.baselines().size(), 1);
    plan.removeBaseline(plan.baselines().first().id);
    QVERIFY(!plan.toJson().contains("baselines"));  // none: not written

    // A shorter task pulls its successors earlier: all of them are Earlier, none Later (user's
    // question, 2026-10-30), also when they keep their own stored start.
    GanttPlan chain;
    add(chain, "A", kMonday, 4);
    add(chain, "B", kMonday.addDays(4), 3);
    add(chain, "C", kMonday.addDays(7), 2);
    QVERIFY(chain.addDependency(dependency("A", "B")) && chain.addDependency(dependency("B", "C")));
    const QString base = chain.addBaseline("Base", taken);
    QVERIFY(chain.setDuration("A", 2));
    const GanttBaselineComparison shorter = chain.compare(chain.schedule(), *chain.baseline(base));
    for (const char *id : {"A", "B", "C"}) QCOMPARE(shorter.tasks.value(id), GanttBaselineComparison::Change::Earlier);
}

void GanttPlanTest::costs() {
    // FEAT-083: hours = working days x working day x share; cost = hours x rate; summaries sum
    // their tasks; per person; per ISO week for dated tasks. Numbers worked out by hand.
    GanttPlan plan;
    QVERIFY(plan.setCalendar(workWeek()));  // Monday-Friday, 8 h
    add(plan, "S", {}, 1);
    add(plan, "A", kMonday, 3, "S");                // Mon-Wed
    add(plan, "B", kMonday.addDays(3), 4, "S");     // Thu, Fri, Mon, Tue
    add(plan, "M", kMonday.addDays(2), 0);          // milestone: no hours
    add(plan, "U", {}, 2);                          // undated: its duration counts, in no week
    GanttResource p1, p2;
    p1.id = "p1";
    p1.hourlyCostCents = 5000;  // 50.00
    p2.id = "p2";
    p2.hourlyCostCents = 3333;  // 33.33
    const GanttAssignments assignments{{"A", {{"p1", 100}, {"p2", 50}}}, {"B", {{"p1", 25}, {"ghost", 100}}},
                                       {"U", {{"p2", 100}}}, {"M", {{"p1", 100}}}};
    const GanttCosts costs = GanttCosts::compute(plan, plan.schedule(), assignments, {p1, p2});
    using Amount = GanttCosts::Amount;
    QCOMPARE(costs.taskPeople.value("A").value("p1"), (Amount{24 * 60, 120000}));
    QCOMPARE(costs.taskPeople.value("A").value("p2"), (Amount{12 * 60, 39996}));
    QCOMPARE(costs.tasks.value("B"), (Amount{8 * 60, 40000}));  // the unknown person left out
    QCOMPARE(costs.tasks.value("M"), Amount{});
    QCOMPARE(costs.tasks.value("U"), (Amount{16 * 60, 53328}));
    QCOMPARE(costs.tasks.value("S"), (Amount{44 * 60, 199996}));
    QCOMPARE(costs.taskPeople.value("S").value("p1"), (Amount{32 * 60, 160000}));
    QCOMPARE(costs.people.value("p1"), (Amount{32 * 60, 160000}));
    QCOMPARE(costs.people.value("p2"), (Amount{28 * 60, 93324}));
    QCOMPARE(costs.total, (Amount{60 * 60, 253324}));
    // Weeks: B's two days of each week; U in none.
    QCOMPARE(costs.weeks.size(), 2);
    QCOMPARE(costs.weeks.value(kMonday).value("p1"), (Amount{28 * 60, 140000}));
    QCOMPARE(costs.weeks.value(kMonday).value("p2"), (Amount{12 * 60, 39996}));
    QCOMPARE(costs.weeks.value(kMonday.addDays(7)).value("p1"), (Amount{4 * 60, 20000}));
    // Rounded once per task and person, and the days still add up: 33 % of a day = 158 min.
    GanttPlan small;
    add(small, "D", kMonday, 3);
    const GanttCosts odd = GanttCosts::compute(small, small.schedule(), {{"D", {{"p2", 33}}}}, {p2});
    QCOMPARE(odd.tasks.value("D").minutes, qint64(475));  // 3 x 480 x 0.33 = 475.2
    QCOMPARE(odd.tasks.value("D").cents, qint64(26386));   // 475 / 60 x 3333 = 26386.25
    Amount week;
    for (const auto &people : odd.weeks) week += people.value("p2");
    QCOMPARE(week, odd.tasks.value("D"));
}

QTEST_GUILESS_MAIN(GanttPlanTest)
#include "gantt_plan_test.moc"
