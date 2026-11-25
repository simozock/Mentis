// FEAT-122 stage 0: the Gantt chart's drawing and geometry, the row and task panel rules in the
// core. Links only MentisDomain: building this test is the check that they need no widget.
#include "ganttcanvas.h"
#include "ganttcommands.h"
#include "ganttplan.h"
#include "gantttaskedits.h"

#include <QImage>
#include <QPainter>
#include <QTest>

class GanttCanvasTest final : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void geometry();
    void range();
    void gestures();
    void painting();
    void chain();
    void rules();
    void taskEdits();
};

namespace {

const QDate kMonday(2026, 11, 2);

QString add(GanttPlan &plan, const QString &id, const QDate &start, int duration) {
    GanttTask task;
    task.id = id;
    task.name = id;
    task.start = start;
    task.duration = std::max(duration, GanttPlan::kMinDuration);
    task.milestone = duration == 0;
    return plan.addTask(task);
}

// A canvas showing plan's tasks in their order, scrolled by the given offsets.
struct Chart {
    GanttPlan plan;
    GanttCanvas canvas;
    int scrollX = 0;
    double scrollY = 0;
    Chart() {
        canvas.horizontalOffset = [this] { return scrollX; };
        canvas.verticalOffset = [this] { return scrollY; };
    }
    void load() {
        QStringList rows;
        for (const GanttTask &task : plan.tasks()) rows.append(task.id);
        canvas.setData(plan, plan.schedule(), rows, {});
        canvas.updateRange();
    }
};

}  // namespace

void GanttCanvasTest::init() { GanttToday::setForTests(kMonday); }
void GanttCanvasTest::cleanup() { GanttToday::setForTests({}); }

void GanttCanvasTest::geometry() {
    Chart chart;
    add(chart.plan, "A", kMonday, 3);
    add(chart.plan, "M", kMonday.addDays(4), 0);
    chart.load();
    GanttCanvas &canvas = chart.canvas;
    QCOMPARE(canvas.dayWidth(), 28);
    // Two weeks before the first task, on a Monday.
    QCOMPARE(canvas.origin(), kMonday.addDays(-14));
    const QRectF bar = canvas.barRect("A");
    QCOMPARE(bar.left(), 14.0 * 28);
    QCOMPARE(bar.width(), 3.0 * 28);
    QCOMPARE(bar.top(), double(GanttCanvas::kHeaderHeight) + GanttCanvas::kBarInset);
    // The milestone: an 18 x 14 diamond in the middle of its day, on the second row.
    const QRectF milestone = canvas.barRect("M");
    QCOMPARE(milestone.size(), QSizeF(18, 14));
    QCOMPARE(milestone.center().x(), canvas.xOf(kMonday.addDays(4)) + 14);
    QCOMPARE(canvas.rowAt(milestone.center().y()), 1);
    QVERIFY(canvas.isMilestone("M"));
    // The owner's offsets move everything.
    chart.scrollX = 28;
    chart.scrollY = 10;
    QCOMPARE(canvas.barRect("A").topLeft(), bar.topLeft() - QPointF(28, 10));
    QCOMPARE(canvas.barAt(canvas.barRect("A").center()), QString("A"));
    QCOMPARE(canvas.dateAt(canvas.barRect("A").left() + 1), kMonday);
    // Zoom and scale change the day width.
    canvas.setZoomPercent(500);
    QCOMPARE(canvas.zoomPercent(), GanttCanvas::kMaxZoom);
    canvas.setZoomPercent(100);
    canvas.setScale(GanttCanvas::Scale::Weeks);
    QCOMPARE(canvas.dayWidth(), 12);
}

void GanttCanvasTest::range() {
    Chart chart;
    chart.load();  // empty plan: around today
    QCOMPARE(chart.canvas.openingDay(), kMonday);
    const QDate origin = chart.canvas.origin();
    // A task a month earlier: the origin moves left and the shift keeps the dates in place.
    add(chart.plan, "A", kMonday.addDays(-28), 2);
    QStringList rows{"A"};
    chart.canvas.setData(chart.plan, chart.plan.schedule(), rows, {});
    const int shift = chart.canvas.updateRange();
    QCOMPARE(shift, int(chart.canvas.origin().daysTo(origin)) * 28);
    QVERIFY(shift > 0);
    // The plan ended before today: it opens at its end.
    QCOMPARE(chart.canvas.openingDay(), kMonday.addDays(-27));
    QCOMPARE(chart.canvas.offsetOfDay(chart.canvas.origin().addDays(2)), 56);
}

void GanttCanvasTest::gestures() {
    Chart chart;
    add(chart.plan, "A", kMonday, 3);
    add(chart.plan, "B", kMonday.addDays(5), 2);
    chart.load();
    GanttCanvas &canvas = chart.canvas;
    GanttCanvas::Gesture &gesture = canvas.gesture();
    const QPointF centre = canvas.barRect("A").center();
    // Moved two days.
    gesture.drag = GanttCanvas::Drag::Move;
    gesture.id = "A";
    gesture.press = centre;
    QVERIFY(!canvas.gestureEdit(centre + QPointF(2, 0)));  // under the threshold: nothing
    std::function<bool(GanttPlan &)> edit = canvas.gestureEdit(centre + QPointF(56, 0));
    QVERIFY(edit);
    GanttPlan moved = chart.plan;
    QVERIFY(edit(moved));
    QCOMPARE(moved.schedule().tasks.value("A").start, kMonday.addDays(2));
    // Its end dragged one day further.
    gesture.drag = GanttCanvas::Drag::Resize;
    edit = canvas.gestureEdit(centre + QPointF(28, 0));
    GanttPlan longer = chart.plan;
    QVERIFY(edit && edit(longer));
    QCOMPARE(longer.task("A")->duration, 4);
    // A link from A's end circle onto B.
    gesture.drag = GanttCanvas::Drag::Link;
    gesture.fromStart = false;
    gesture.press = canvas.linkHandle("A");
    QCOMPARE(canvas.linkHandleAt("A", canvas.linkHandle("A")), QString("A"));
    QVERIFY(!canvas.gestureEdit(canvas.barRect("A").center()));  // onto itself: nothing
    edit = canvas.gestureEdit(canvas.barRect("B").center());
    GanttPlan linked = chart.plan;
    QVERIFY(edit && edit(linked));
    QCOMPARE(linked.dependencies().size(), 1);
    QCOMPARE(linked.dependencies().first().from, QString("A"));
    QCOMPARE(linked.dependencies().first().to, QString("B"));
}

void GanttCanvasTest::painting() {
    Chart chart;
    add(chart.plan, "A", kMonday, 3);
    chart.load();
    GanttCanvas &canvas = chart.canvas;
    canvas.setPrinting(true);
    QImage image(1200, 200, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    canvas.paint(painter, image.rect());
    painter.end();
    // The bar in the default task colour, the header on the card.
    const QPoint inside = canvas.barRect("A").center().toPoint() + QPoint(0, -4);
    QCOMPARE(image.pixelColor(inside), GanttCanvas::defaultBarColor(chart.plan, GanttPlan::BarKind::Task));
    QCOMPARE(image.pixelColor(QPoint(2, 2)), canvas.colors().card);
    QCOMPARE(GanttCanvas::typeName(GanttDependency::Type::StartStart), QString("Start to start"));
}

void GanttCanvasTest::chain() {
    GanttPlan plan;
    add(plan, "A", kMonday, 2);
    add(plan, "B", {}, 2);
    add(plan, "C", {}, 1);
    // In the order selected; ids not in the plan left out.
    const QStringList ids = GanttCommands::chainIds(plan, {"C", "X", "A"}, {"A", "C", "X"});
    QCOMPARE(ids, QStringList({"C", "A"}));
    QVERIFY(!GanttCommands::chainUnlinks(plan, ids));
    // C has no date: the chain starts today.
    QVERIFY(GanttCommands::chain(plan, ids, false));
    QCOMPARE(plan.dependencies().size(), 1);
    QCOMPARE(plan.schedule().tasks.value("C").start, kMonday);
    QVERIFY(!GanttCommands::chain(plan, ids, false));  // the link exists: nothing changes
    // Linked: the toggle removes their links.
    QVERIFY(GanttCommands::chainUnlinks(plan, ids));
    QVERIFY(GanttCommands::chain(plan, ids, true));
    QVERIFY(plan.dependencies().isEmpty());
    QVERIFY(!GanttCommands::chain(plan, {"A"}, false));  // one task: no chain
}

void GanttCanvasTest::rules() {
    GanttPlan plan;
    GanttCalendar week;
    week.workingDays = {1, 2, 3, 4, 5};
    plan.setCalendar(week);
    // Monday to the next Monday: 6 working days; an end before the start: 1.
    QCOMPARE(GanttCommands::durationTo(plan, kMonday, kMonday.addDays(7)), 6);
    QCOMPARE(GanttCommands::durationTo(plan, kMonday, kMonday.addDays(-3)), 1);
    QCOMPARE(GanttCommands::nextBaselineName(plan), QString("Baseline 1"));
    add(plan, "A", kMonday, 1);  // a snapshot needs a task
    QVERIFY(!plan.addBaseline("Baseline 1", QDateTime::currentDateTime()).isEmpty());
    QCOMPARE(GanttCommands::nextBaselineName(plan), QString("Baseline 2"));
    // Shift+click ranges, either way up.
    const QStringList shown{"A", "B", "C", "D"};
    QCOMPARE(GanttCommands::range(shown, "B", "D", true), QStringList({"B", "C", "D"}));
    QCOMPARE(GanttCommands::range(shown, "D", "B", true), QStringList({"B", "C", "D"}));
    QCOMPARE(GanttCommands::range(shown, "C", "C", true), QStringList({"C"}));
    QCOMPARE(GanttCommands::range(shown, "X", "B", false), QStringList({"B"}));

    // The table's rows in tree order; a folded summary's subtasks left out.
    GanttPlan tree;
    add(tree, "P", kMonday, 1);
    add(tree, "Q", kMonday, 1);
    GanttTask child;
    child.id = child.name = "C";
    child.parentId = "P";
    child.start = kMonday;
    tree.addTask(child);
    QList<GanttCommands::Row> rows = GanttCommands::rows(tree);
    QCOMPARE(rows.size(), 3);
    QCOMPARE(rows.at(1).id, QString("C"));
    QCOMPARE(rows.at(1).depth, 1);
    QVERIFY(rows.at(0).summary && !rows.at(1).summary);
    rows = GanttCommands::rows(tree, {"P"});
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(1).id, QString("Q"));
}

void GanttCanvasTest::taskEdits() {
    GanttPlan plan;
    add(plan, "P", kMonday, 1);
    add(plan, "A", kMonday, 2);
    add(plan, "M", kMonday.addDays(3), 0);
    add(plan, "U", {}, 4);
    GanttTask child;
    child.id = "S";
    child.name = "S";
    child.parentId = "P";
    child.start = kMonday;
    child.duration = 3;
    plan.addTask(child);  // P becomes a summary
    const QStringList targets{"P", "A", "M", "U"};
    const GanttSchedule schedule = plan.schedule();
    const GanttTaskEdits::Several several = GanttTaskEdits::several(plan, schedule, targets);
    QCOMPARE(several.leaves, QStringList({"A", "M", "U"}));  // the summary left out
    QCOMPARE(several.durations, QList<int>({2, 4}));          // and the milestone's length
    QVERIFY(several.dated && several.undated && several.stored && several.lengths);
    QVERIFY(!several.allMilestones);
    QCOMPARE(several.firstStart, kMonday);
    // Durations: the milestone and the summary keep theirs.
    QVERIFY(GanttTaskEdits::setDuration(plan, targets, 5));
    QCOMPARE(plan.task("A")->duration, 5);
    QCOMPARE(plan.task("U")->duration, 5);
    QCOMPARE(plan.task("P")->duration, 1);
    QVERIFY(GanttTaskEdits::stepProgress(plan, targets, 30));
    QCOMPARE(plan.task("M")->progress, 30);
    // Add date: today on the undated task only; End: from each dated task's start.
    QVERIFY(GanttTaskEdits::addStart(plan, schedule, targets));
    QCOMPARE(plan.task("U")->start, kMonday);
    QVERIFY(GanttTaskEdits::setEnd(plan, plan.schedule(), {"A"}, kMonday.addDays(2)));
    QCOMPARE(plan.task("A")->duration, 3);
    QVERIFY(GanttTaskEdits::clearStart(plan, {"A"}));
    QVERIFY(!plan.task("A")->start.isValid());
}

QTEST_MAIN(GanttCanvasTest)
#include "gantt_canvas_test.moc"
