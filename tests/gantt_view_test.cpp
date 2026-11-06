// FEAT-075: the Gantt view of a sheet (view buttons, empty plan, table edits, keys, mouse on the
// bars, history and saving, critical path painting, performance).
#include "datepicker.h"
#include "ganttbaselinemenu.h"
#include "ganttexport.h"
#include "ganttoffice.h"
#include "officeexportdialog.h"
#include "ooxmltest.h"
#include "segmentedcontrol.h"
#include "exportdialog.h"
#include "pagedialog.h"
#include "document.h"
#include "ganttview.h"
#include "gantttaskpanel.h"
#include "localization.h"
#include "toggleswitch.h"
#include "tour.h"
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "modaldialog.h"
#include "numberfield.h"
#include "nodestyle.h"
#include "stylecontrols.h"
#include "noteeditor.h"
#include "resourcesdialog.h"
#include "shortcuts.h"
#include "uitheme.h"

#include <QAbstractItemModel>
#include <QApplication>
#include <QHeaderView>
#include <QHelpEvent>
#include <QComboBox>
#include <QDateEdit>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalSpy>
#include <QSpinBox>
#include <QToolButton>
#include <QToolTip>
#include <QWheelEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyleFactory>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QTreeView>

class GanttViewTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void viewButtons();
    void namesFollowTheInterfaceLanguage();
    void emptyPlan();
    void tableEdits();
    void keys();
    void mouse();
    void chainAndStartLink();
    void chainWithoutDatesStartsToday();
    void historyAndSave();
    void summaryDropsPlanning();
    void baselines();
    void baselineMenu();
    void touchpadScroll();
    void exportPages();
    void exportWindows();
    void officeFiles();
    void officeWindow();
    void peoplePercent();
    void panelStyleWaitsUntilShown();  // startup, 2026-11-04
    void criticalPainting();
    void dependencyMenu();
    void columns();
    void columnOrder();
    void numberColumn();
    void extrasColumn();
    void actions();
    void opensNearToday();
    void zoom();
    void taskPanel();
    void barColours();
    void barShape();
    void panelTheme();
    void fillStyle();
    void numberCells();
    void summaryShape();
    void summaryFrame();
    void taskShape();
    void exportShapes();
    void selectedSummaryArrow();
    void sheetColours();
    void relationStyle();
    void milestoneStyle();
    void arrowKeys();
    void borderStyle();
    void barNames();
    void undatedName();
    void linkedMove();
    void arrowRoute();
    void barLabel();
    void performance();
    void datePicker();
    void panelFieldsIgnoreWheelUnfocused();
    void numberField();
    void milestoneSwitch();
    void panelColour();
    void shiftSelection();
    void panelClosing();  // FEAT-081: sections closed from their title
    void panelSeveral();  // columns: 2 = End, 3 = Duration, 4 = %
    void panelSections();
    void panelRemove();
    void moveRows();
    void resourcesDialog();
    void resourcesButton();
    void copyAndPasteStyleOfTasks();  // FEAT-085

private:
    void setPlan(const QList<GanttTask> &tasks, const QList<GanttDependency> &dependencies = {});
    void setExportPlan(int phases, int spacing);
    void drag(const QPointF &from, const QPointF &to);

    QTemporaryDir m_dir;
    std::unique_ptr<Document> m_document;
    std::unique_ptr<ShortcutRegistry> m_shortcuts;
    std::unique_ptr<GanttView> m_view;
};

namespace {

const QDate kMonday(2026, 11, 2);

// duration 0 = a milestone (of one day, the default).
GanttTask task(const QString &id, const QDate &start, int duration, const QString &parent = {}) {
    GanttTask result;
    result.id = id;
    result.name = id;
    result.start = start;
    result.duration = std::max(duration, GanttPlan::kMinDuration);
    result.milestone = duration == 0;
    result.parentId = parent;
    return result;
}

GanttDependency dependency(const QString &from, const QString &to) {
    GanttDependency result;
    result.from = from;
    result.to = to;
    return result;
}

}  // namespace

void GanttViewTest::initTestCase() {
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisGanttViewTest"));
    QSettings().clear();  // CI starts from nothing: a run here must too (the export windows remember)
    TourSettings::setTourOffered();  // no tour invitation dimming the window (FEAT-093)
    TourSettings::setTipsEnabled(false);
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QVERIFY(m_dir.isValid());
}

void GanttViewTest::init() {
    QSettings().remove(QStringLiteral("gantt/columns"));  // default columns in every test
    QSettings().remove(QStringLiteral("gantt/numberHidden"));
    QSettings().remove(QStringLiteral("gantt/columnOrder"));
    QSettings().remove(QStringLiteral("gantt/critical"));
    QSettings().remove(QStringLiteral("gantt/currencies"));
    QSettings().remove(QStringLiteral("gantt/resourceColumns"));
    QSettings().remove(QStringLiteral("gantt/closedSections"));
    m_document = std::make_unique<Document>();
    m_document->model()->clear(QStringLiteral("Project"));  // only the central topic: no rows
    m_document->startUntitled();
    m_shortcuts = std::make_unique<ShortcutRegistry>();
    // The same keys MainWindow registers.
    m_shortcuts->add("ganttAddTask", ShortcutRegistry::Area::Gantt, "Add task", {QKeySequence(Qt::Key_Return)});
    m_shortcuts->add("ganttIndent", ShortcutRegistry::Area::Gantt, "Make subtask", {QKeySequence(Qt::Key_Tab)});
    m_shortcuts->add("ganttOutdent", ShortcutRegistry::Area::Gantt, "Move up a level", {QKeySequence("Shift+Tab")});
    m_shortcuts->add("ganttDelete", ShortcutRegistry::Area::Gantt, "Delete tasks", {QKeySequence(Qt::Key_Delete)});
    m_shortcuts->add("ganttChain", ShortcutRegistry::Area::Gantt, "Link in a chain", {QKeySequence("Ctrl+Shift+L")});
    m_view = std::make_unique<GanttView>(m_document.get(), m_shortcuts.get());
    m_view->resize(1500, 700);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
}

void GanttViewTest::cleanup() {
    m_view.reset();
    m_document->close();
    m_document.reset();
    m_shortcuts.reset();
}

// FEAT-075: the rows are topics. The map is rebuilt with one topic per task (its id and title are
// the task's id, under the central topic or the task's parent) and becomes the starting state of
// the history; the dependencies are then one step.
void GanttViewTest::setPlan(const QList<GanttTask> &tasks, const QList<GanttDependency> &dependencies) {
    MindMapModel *model = m_document->model();
    model->clear(QStringLiteral("Project"));
    for (const GanttTask &t : tasks) {
        MindNode *parent = t.parentId.isEmpty() ? model->root() : model->nodeById(t.parentId);
        QVERIFY(parent);
        MindNode *topic = m_document->addChild(parent, t.name);
        topic->id = t.id;
        topic->ganttStart = t.start;
        topic->ganttDuration = t.duration;
        if (t.milestone) topic->task = t.progress == GanttPlan::kMaxProgress ? MindNode::Task::Done : MindNode::Task::Open;
        topic->ganttProgress = t.progress;
        if (t.color.isValid()) QVERIFY(model->setStyle({topic}, StyleProperty::BranchColor, t.color));
    }
    m_document->startUntitled();
    if (!dependencies.isEmpty())
        QVERIFY(m_document->editGantt([&](GanttPlan &plan) {
            for (const GanttDependency &d : dependencies) plan.addDependency(d);
            return true;
        }));
    m_view->refresh();
}

void GanttViewTest::summaryDropsPlanning() {
    // A task that gets children is a summary: it loses its people and its links, in the same step
    // (user, 2026-10-30); Undo brings them back.
    setPlan({task("A", kMonday, 2), task("B", kMonday, 2), task("C", kMonday, 2)}, {dependency("A", "B"), dependency("B", "C")});
    GanttResources resources;
    resources.people = {{"p1", "Anna", "Rossi", "", "Developer"}};
    QVERIFY(m_document->setResources(resources, m_document->model()->gantt().calendar()));
    MindNode *b = m_document->model()->nodeById("B");
    QVERIFY(m_document->setTaskInfo({b}, std::nullopt, {"p1"}));
    QVERIFY(m_document->addChild(b, "D"));
    m_document->flushPendingStep();
    QVERIFY(b->ganttPeople.isEmpty());
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());
    m_document->undo();
    b = m_document->model()->nodeById("B");
    QCOMPARE(b->ganttPeople, QStringList({"p1"}));
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 2);
}

void GanttViewTest::baselines() {
    // FEAT-082: snapshots are steps of the sheet, saved with it; the comparison is the view's.
    setPlan({task("A", kMonday, 3), task("B", kMonday.addDays(3), 2)});
    QCOMPARE(m_view->nextBaselineName(), QString("Baseline 1"));
    const QString id = m_view->addBaseline(m_view->nextBaselineName());
    QVERIFY(!id.isEmpty());
    QCOMPARE(m_view->nextBaselineName(), QString("Baseline 2"));
    m_document->undo();
    m_view->refresh();
    QVERIFY(m_document->model()->gantt().baselines().isEmpty());
    m_document->redo();
    m_view->refresh();
    QCOMPARE(m_document->model()->gantt().baselines().size(), 1);
    QVERIFY(m_view->renameBaseline(id, "Approved"));
    QCOMPARE(m_document->model()->gantt().baseline(id)->name, QString("Approved"));
    QVERIFY(!m_view->renameBaseline(id, "  "));

    // Comparing: shorter bars, the snapshot's outline, the thin bar's colour, the table's cells.
    GanttChart *chart = m_view->chart();
    const double height = chart->barRect("A").height();
    QSignalSpy compared(m_view.get(), &GanttView::baselineCompared);
    m_view->compareBaseline(id);
    QCOMPARE(compared.count(), 1);
    QVERIFY(compared.last().at(0).toBool());
    QVERIFY(chart->comparing());
    QCOMPARE(chart->barRect("A").height(), height - 7);
    // Red is "later" only: the critical path is not drawn while comparing, the setting stays.
    QVERIFY(chart->criticalShown() && !chart->criticalPainted());
    // The legend above the chart: the snapshot's name, Later, Earlier, New.
    const QImage band = m_view->baselineLegend()->grab().toImage();
    bool grey = false;
    for (int x = 0; x < 60 && !grey; ++x) grey = band.pixelColor(x, band.height() / 2) == QColor(0x9a, 0xa2, 0xac);
    QVERIFY(grey);
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setDuration("A", 5); }));
    m_view->refresh();
    const QRectF outline = chart->baselineRect("A");
    QCOMPARE(outline.left(), chart->barRect("A").left());
    QCOMPARE(outline.width(), 3.0 * chart->dayWidth());
    QCOMPARE(chart->baselineMarkColor("A"), QColor(0xe5, 0x48, 0x4d));  // ends later: red
    QCOMPARE(chart->baselineMarkColor("B"), QColor(0x9a, 0xa2, 0xac));  // the same: grey
    // The slip: from the snapshot's end to the current one, two days; none when the end is the same.
    QCOMPARE(chart->slipRect("A").left(), outline.right());
    QCOMPARE(chart->slipRect("A").width(), 2.0 * chart->dayWidth());
    QVERIFY(chart->slipRect("B").isEmpty());
    QVERIFY(chart->baselineMarkRect("A").top() >= chart->barRect("A").bottom() + 3);
    QVERIFY(chart->baselineMarkRect("A").bottom() <= chart->barRect("A").top() + GanttChart::kRowHeight);
    QAbstractItemModel *table = m_view->table()->model();
    const QModelIndex end = table->index(0, 2), start = table->index(0, 1), duration = table->index(0, 3);
    QCOMPARE(end.data(Qt::ForegroundRole).value<QColor>(), QColor(0xe5, 0x48, 0x4d));
    QCOMPARE(duration.data(Qt::ForegroundRole).value<QColor>(), QColor(0xe5, 0x48, 0x4d));
    QVERIFY(start.data(Qt::ForegroundRole).value<QColor>() != QColor(0xe5, 0x48, 0x4d));
    QCOMPARE(end.data(Qt::ToolTipRole).toString(), QString("Baseline: ") + QLocale().toString(kMonday.addDays(2), QLocale::ShortFormat));
    QCOMPARE(duration.data(Qt::ToolTipRole).toString(), QString("Baseline: 3 day(s)"));
    // A new topic: the "New" pill before its bar; nothing for the old snapshot's tasks.
    MindNode *added = m_document->addChild(m_document->model()->root(), "N");
    added->ganttStart = kMonday;
    m_view->refresh();
    QVERIFY(!chart->newPillRect(added->id).isEmpty());
    QVERIFY(chart->newPillRect("A").isEmpty());
    QVERIFY(chart->baselineRect(added->id).isEmpty());
    // A task that lost its dates still shows the snapshot's side.
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setStart("B", {}); }));
    m_view->refresh();
    QVERIFY(chart->barRect("B").isEmpty());
    QVERIFY(!chart->baselineRect("B").isEmpty());
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setStart("B", kMonday.addDays(3)); }));
    m_view->refresh();
    // The pixels: the thin bar grey at the snapshot's days, the slip red after it. The plan's week
    // in view: the chart opens on today, which would leave the snapshot off the left edge.
    chart->scrollToDay(kMonday);
    const QRectF mark = chart->baselineMarkRect("A");
    const QImage image = chart->viewport()->grab().toImage();
    QCOMPARE(image.pixelColor(QPointF(mark.left() + 10, mark.center().y()).toPoint()), QColor(0x9a, 0xa2, 0xac));
    QCOMPARE(image.pixelColor(QPointF(chart->slipRect("A").center().x(), mark.center().y()).toPoint()), QColor(0xe5, 0x48, 0x4d));

    // Removing the compared snapshot stops the comparison; Undo brings the snapshot, not the comparison.
    QVERIFY(m_view->removeBaseline(id));
    QVERIFY(m_view->comparedBaseline().isEmpty());
    QVERIFY(!compared.last().at(0).toBool());
    QVERIFY(!chart->comparing());
    QCOMPARE(chart->barRect("A").height(), height);
    QVERIFY(chart->criticalPainted());
    m_document->undo();
    m_view->refresh();
    QVERIFY(m_document->model()->gantt().baseline(id));
    QVERIFY(m_view->comparedBaseline().isEmpty());
    // Another sheet stops it, coming back does not bring it back.
    m_view->compareBaseline(id);
    QVERIFY(chart->comparing());
    m_document->addSheet();
    m_view->refresh();
    QVERIFY(m_view->comparedBaseline().isEmpty() && !chart->comparing());
    QVERIFY(m_document->switchSheet(0).ok);
    m_view->refresh();
    QVERIFY(m_view->comparedBaseline().isEmpty());
    // Saved with the sheet (the comparison is the view's, not in the file).
    const QString file = m_dir.filePath("baselines.mentis");
    QVERIFY(m_document->save(file, BmapStorage::SaveReason::Explicit).ok);
    const GanttPlan saved = m_document->model()->gantt();
    m_document->close();
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.model()->gantt().baselines(), saved.baselines());
    QCOMPARE(reopened.model()->gantt().baselines().size(), 1);
    reopened.close();
}

void GanttViewTest::baselineMenu() {
    // FEAT-082: the popover: a name filled in, Save, the list newest first, None, rename, ×.
    setPlan({task("A", kMonday, 3)});
    m_view->show();
    m_view->showBaselineMenu(m_view.get());
    GanttBaselineMenu *menu = m_view->baselineMenu();
    QVERIFY(menu && menu->isVisible());
    QCOMPARE(menu->nameField()->text(), QString("Baseline 1"));
    QVERIFY(menu->findChild<QLabel *>("GanttBaselineEmpty"));
    menu->saveButton()->click();
    QCOMPARE(m_document->model()->gantt().baselines().size(), 1);
    QCOMPARE(menu->nameField()->text(), QString("Baseline 2"));
    menu->nameField()->setText("Second");
    QTest::keyClick(menu->nameField(), Qt::Key_Return);
    QTRY_COMPARE(menu->findChildren<QWidget *>("GanttBaselineRow").size(), 2);
    const QVector<GanttBaseline> baselines = m_document->model()->gantt().baselines();
    const auto rows = [menu] {
        QList<QWidget *> visible;
        for (QWidget *row : menu->findChildren<QWidget *>("GanttBaselineRow"))
            if (!row->property("baselineId").toString().isEmpty() && row->parentWidget()) visible.append(row);
        std::sort(visible.begin(), visible.end(), [](QWidget *a, QWidget *b) { return a->y() < b->y(); });
        return visible;
    };
    QTRY_COMPARE(rows().size(), 2);
    QCOMPARE(rows().first()->property("baselineId").toString(), baselines.last().id);  // newest first
    // A click on a name compares; on the checked one stops; None too.
    rows().last()->findChild<QToolButton *>("GanttBaselineName")->click();
    QCOMPARE(m_view->comparedBaseline(), baselines.first().id);
    QTRY_VERIFY(!menu->findChild<QToolButton *>("GanttBaselineNone")->isChecked());
    menu->findChild<QToolButton *>("GanttBaselineNone")->click();
    QVERIFY(m_view->comparedBaseline().isEmpty());
    // Rename from the pencil: Enter renames.
    QTRY_COMPARE(rows().size(), 2);
    rows().first()->findChild<QToolButton *>("GanttBaselineRename")->click();
    auto *field = menu->findChild<QLineEdit *>("GanttBaselineRenameField");
    QVERIFY(field);
    field->setText("Renamed");
    QTest::keyClick(field, Qt::Key_Return);
    QCOMPARE(m_document->model()->gantt().baseline(baselines.last().id)->name, QString("Renamed"));
    // Esc cancels: the old name, the popover still open.
    QTRY_COMPARE(rows().size(), 2);
    rows().first()->findChild<QToolButton *>("GanttBaselineRename")->click();
    field = menu->findChild<QLineEdit *>("GanttBaselineRenameField");
    QVERIFY(field);
    field->setText("Not this");
    QTest::keyClick(field, Qt::Key_Escape);
    QVERIFY(menu->isVisible());
    QCOMPARE(m_document->model()->gantt().baseline(baselines.last().id)->name, QString("Renamed"));
    // Removed tasks under the compared snapshot.
    m_view->compareBaseline(baselines.first().id);
    QVERIFY(m_view->removeTasks({"A"}));
    m_view->refresh();
    QTRY_VERIFY(menu->findChild<QToolButton *>("GanttBaselineRemoved"));
    QCOMPARE(menu->findChild<QToolButton *>("GanttBaselineRemoved")->text(), QString("▸ 1 task(s) removed"));
    menu->findChild<QToolButton *>("GanttBaselineRemoved")->click();
    QTRY_VERIFY(menu->findChild<QLabel *>("GanttBaselineRemovedTask"));
    QCOMPARE(menu->findChild<QLabel *>("GanttBaselineRemovedTask")->text(), QString("A"));
    // No tasks: Save disabled. ×: one step.
    QVERIFY(!menu->saveButton()->isEnabled());
    QTRY_COMPARE(rows().size(), 2);
    rows().first()->findChild<QToolButton *>("GanttBaselineRemove")->click();
    QCOMPARE(m_document->model()->gantt().baselines().size(), 1);
    menu->close();
}

void GanttViewTest::touchpadScroll() {
    // Two fingers sideways over the chart move the days, not the rows (user, 2026-10-30); both
    // axes at once move both.
    QList<GanttTask> tasks;
    for (int i = 0; i < 40; ++i) tasks.append(task(QString("T%1").arg(i), kMonday.addDays(i), 2));
    setPlan(tasks);
    m_view->resize(1000, 400);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
    QWidget *viewport = m_view->chart()->viewport();
    QScrollBar *days = m_view->chart()->horizontalScrollBar();
    QScrollBar *rows = m_view->table()->verticalScrollBar();
    days->setValue(days->maximum() / 2);
    rows->setValue(0);
    const int x = days->value();
    const QPointF at(100, 150);
    const auto swipe = [&](const QPoint &pixels) {
        QWheelEvent wheel(at, viewport->mapToGlobal(at), pixels, pixels * 2, Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(viewport, &wheel);
    };
    swipe(QPoint(-60, 0));
    QCOMPARE(days->value(), x + 60);
    QCOMPARE(rows->value(), 0);
    swipe(QPoint(30, -40));
    QCOMPARE(days->value(), x + 30);
    QCOMPARE(rows->value(), 40);
}

void GanttViewTest::panelStyleWaitsUntilShown() {
    // Setting the style sheet re-styles every control of the panel: a hidden panel keeps it until
    // it is shown (or applyPendingStyle() is called), with the colours of the last setColors().
    GanttTaskPanel panel;
    panel.setColors(GanttColors::light());
    panel.setColors(GanttColors::dark());
    QVERIFY(panel.styleSheet().isEmpty());
    panel.show();
    QVERIFY(panel.styleSheet().contains(GanttColors::dark().card.name()));
    panel.setColors(GanttColors::light());  // shown: applied at once
    QVERIFY(panel.styleSheet().contains(GanttColors::light().card.name()));
    GanttTaskPanel hidden;
    hidden.setColors(GanttColors::dark());
    hidden.applyPendingStyle();
    QVERIFY(hidden.styleSheet().contains(GanttColors::dark().card.name()));
}

void GanttViewTest::peoplePercent() {
    // FEAT-083: a person's share of a task, 1-100 % (step 5), in the panel's Resources table; one
    // step; saved as {"id", "percent"} when not 100 %, an id alone otherwise.
    setPlan({task("A", kMonday, 3), task("B", kMonday, 2)});
    GanttResources resources;
    resources.people = {{"p1", "Anna", "Rossi", "", "Developer", 5000}};
    QVERIFY(m_document->setResources(resources, m_document->model()->gantt().calendar()));
    MindNode *a = m_document->model()->nodeById("A");
    MindNode *b = m_document->model()->nodeById("B");
    QVERIFY(m_document->setTaskInfo({a, b}, std::nullopt, {"p1"}));
    m_view->refresh();
    GanttTaskPanel *panel = m_view->panel();
    m_view->openPanel("A");
    NumberField *percent = panel->personPercent("p1");
    QVERIFY(percent);
    QCOMPARE(percent->value(), 100.0);
    QCOMPARE(percent->minimum(), 1.0);
    QCOMPARE(percent->maximum(), 100.0);
    QTest::mouseClick(percent, Qt::LeftButton, {}, percent->minusRect().center());  // − steps by 5
    QCOMPARE(m_document->model()->nodeById("A")->percentOf("p1"), 95);
    m_view->refresh();
    percent = panel->personPercent("p1");
    percent->setText("40");
    QTest::keyClick(percent, Qt::Key_Return);
    a = m_document->model()->nodeById("A");
    QCOMPARE(a->percentOf("p1"), 40);
    QCOMPARE(m_document->model()->nodeById("B")->percentOf("p1"), 100);
    const QJsonArray people = m_document->model()->toJson().value("root").toObject().value("children").toArray()
                                  .at(0).toObject().value("gantt").toObject().value("people").toArray();
    QCOMPARE(people.at(0).toObject().value("percent").toInt(), 40);
    QCOMPARE(m_document->model()->ganttAssignments().value("A"), (QVector<GanttAssignment>{{"p1", 40}}));
    m_document->undo();
    QCOMPARE(m_document->model()->nodeById("A")->percentOf("p1"), 95);
    m_document->redo();
    // Several tasks with different shares: "—"; a value sets all of them.
    m_view->refresh();
    m_view->selectRange("A", "B", false);
    m_view->openPanel("A");
    percent = panel->personPercent("p1");
    QVERIFY(!percent->hasValue());
    percent->setText("50");
    QTest::keyClick(percent, Qt::Key_Return);
    QCOMPARE(m_document->model()->nodeById("A")->percentOf("p1"), 50);
    QCOMPARE(m_document->model()->nodeById("B")->percentOf("p1"), 50);
    // Removed from the task: the share goes too; back at 100 % nothing is written.
    QVERIFY(m_document->setTaskInfo({m_document->model()->nodeById("A")}, std::nullopt, {}, {"p1"}));
    QVERIFY(m_document->model()->nodeById("A")->ganttPercent.isEmpty());
    QVERIFY(m_document->setPeoplePercent({m_document->model()->nodeById("B")}, "p1", 100));
    QVERIFY(m_document->model()->nodeById("B")->ganttPercent.isEmpty());
    QVERIFY(!m_document->setPeoplePercent({m_document->model()->nodeById("B")}, "p1", 100));
    // Reading: an id alone is 100 %, a share out of range is clamped.
    MindMapModel read;
    QJsonObject root = m_document->model()->toJson();
    QJsonObject topic{{"id", "T"}, {"title", "T"},
                      {"gantt", QJsonObject{{"people", QJsonArray{"p1", QJsonObject{{"id", "p2"}, {"percent", 250}}, QJsonObject{{"id", "p3"}, {"percent", 0}}}}}}};
    QJsonObject central = root.value("root").toObject();
    central.insert("children", QJsonArray{topic});
    root.insert("root", central);
    QVERIFY(read.fromJson(root));
    const MindNode *t = read.nodeById("T");
    QVERIFY(t);
    QCOMPARE(t->ganttPeople, QStringList({"p1", "p2", "p3"}));
    QCOMPARE(t->percentOf("p1"), 100);
    QCOMPARE(t->percentOf("p2"), 100);
    QCOMPARE(t->percentOf("p3"), 1);
}

// FEAT-083: a plan of five phases of seven tasks over about four months, two people on every task.
void GanttViewTest::setExportPlan(int phases, int spacing) {
    QList<GanttTask> tasks;
    for (int s = 0; s < phases; ++s) {
        const QString summary = QString("Phase %1").arg(s + 1);
        tasks.append(task(summary, {}, 1));
        for (int t = 0; t < 7; ++t)
            tasks.append(task(QString("Task %1.%2").arg(s + 1).arg(t + 1), kMonday.addDays(s * spacing + t * 2), 3 + t % 4, summary));
    }
    setPlan(tasks);
    GanttResources resources;
    resources.people = {{"p1", "Anna", "Rossi", "", "Developer", 5000}, {"p2", "Marco", "Bianchi", "", "PM", 7250}};
    QVERIFY(m_document->setResources(resources, m_document->model()->gantt().calendar()));
    QList<MindNode *> leaves;
    for (const GanttTask &t : tasks)
        if (!t.parentId.isEmpty()) leaves.append(m_document->model()->nodeById(t.id));
    QVERIFY(m_document->setTaskInfo(leaves, std::nullopt, {"p1", "p2"}));
    m_view->refresh();
}

void GanttViewTest::exportPages() {
    // FEAT-083: legible pages. Fit width raises the unit; rows over pages, a summary never last with
    // its tasks on the next page; the timeline over pages in width only at actual size.
    setExportPlan(5, 18);
    const GanttExportSource source = m_view->exportSource();
    QCOMPARE(source.rows.size(), 40);
    QCOMPARE(source.columns.size(), 3);  // the view's: Name, Start, Duration
    QCOMPARE(source.columns.at(0).title, QString("Name"));
    QVERIFY(source.rows.at(0).summary && source.rows.at(1).depth == 1);
    GanttPages::Options options;  // A4 landscape, fit width, days
    const GanttPages fit(source, options);
    QVERIFY(fit.isValid());
    QCOMPARE(fit.chosenScale(), GanttChart::Scale::Weeks);  // four months do not fit in days
    QVERIFY(!fit.splitInWidth());
    QVERIFY(fit.rowsPerPage() >= 15);  // rows of 8.5 mm on A4 landscape
    int rows = 0;
    for (int i = 0; i < fit.count(); ++i) {
        const GanttPages::PageInfo page = fit.pageInfo(i);
        QVERIFY(page.chart);
        QCOMPARE(page.first, rows);
        rows += page.count;
        const int last = page.first + page.count - 1;
        if (last + 1 < source.rows.size()) QVERIFY(!source.rows.at(last).summary);
    }
    QCOMPARE(rows, 40);
    QCOMPARE(fit.layout(0).orientation(), QPageLayout::Landscape);
    // Actual size in days: the timeline over pages in width, row by row.
    options.fitWidth = false;
    const GanttPages actual(source, options);
    QVERIFY(actual.splitInWidth());
    QCOMPARE(actual.chosenScale(), GanttChart::Scale::Days);
    QCOMPARE(actual.pageInfo(0).first, actual.pageInfo(1).first);
    QVERIFY(actual.pageInfo(1).firstDay > actual.pageInfo(0).firstDay);
    QCOMPARE(actual.pageInfo(0).firstDay.addDays(actual.pageInfo(0).days), actual.pageInfo(1).firstDay);
    // Costs only: no chart page; a total never alone on a page.
    options.content = GanttPages::Content::Costs;
    const GanttPages costs(source, options);
    QVERIFY(costs.isValid() && costs.count() >= 2);
    for (int i = 0; i < costs.count(); ++i) {
        QVERIFY(!costs.pageInfo(i).chart);
        QVERIFY(!costs.pageInfo(i).startsWithTotal);
    }
    QVERIFY(costs.pageInfo(costs.count() - 1).endsWithTotal);
    options.content = GanttPages::Content::GanttAndCosts;
    options.fitWidth = true;
    QCOMPARE(GanttPages(source, options).count(), fit.count() + costs.count());
    QCOMPARE(GanttPages::hoursText(450), QLocale().toString(7) + QString(" h 30"));
    QCOMPARE(GanttPages::hoursText(480), QLocale().toString(8) + QString(" h"));
    // A plan of three years fits in months.
    setExportPlan(40, 28);
    options.content = GanttPages::Content::Gantt;
    const GanttPages longPlan(m_view->exportSource(), options);
    QCOMPARE(longPlan.chosenScale(), GanttChart::Scale::Months);
    // The PDF: one page per page, written atomically.
    const QString pdf = m_dir.filePath("gantt.pdf");
    QVERIFY(MapExporter::writePdf(fit, pdf, "Gantt", false).ok);
    QFile file(pdf);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QCOMPARE(int(bytes.count("/Type /Page\n") + bytes.count("/Type /Page\r")), fit.count());
    // The picture: table and chart in one image, the scale applied.
    bool reduced = true;
    QString error;
    const QImage image = GanttPages::toImage(source, GanttChart::Scale::Weeks, 2, &reduced, &error);
    QVERIFY(!image.isNull() && !reduced);
    GanttPages::Options single;
    single.singlePage = true;
    single.fitWidth = false;
    single.scale = GanttChart::Scale::Weeks;
    QCOMPARE(image.size(), (GanttPages(source, single).imageSize() * 2).toSize());
    // With the costs: their tables under the chart, in the same image.
    single.content = GanttPages::Content::GanttAndCosts;
    const GanttPages both(source, single);
    QCOMPARE(both.count(), 2);
    QVERIFY(!both.pageInfo(1).chart);
    QCOMPARE(both.imageSize().height(), both.pageSizePx(0).height() + both.pageSizePx(1).height());
    const QImage withCosts = GanttPages::toImage(source, GanttChart::Scale::Weeks, 1, &reduced, &error, GanttPages::Content::GanttAndCosts);
    QCOMPARE(withCosts.size(), both.imageSize().toSize());
    // Fit to the Gantt (PDF): the chart on one page of its size, the costs on landscape A4.
    GanttPages::Options fitToGantt;
    fitToGantt.size = MapExporter::PageSize::FitToMap;
    fitToGantt.content = GanttPages::Content::GanttAndCosts;
    const GanttPages whole(source, fitToGantt);
    QVERIFY(whole.pageInfo(0).chart && whole.pageInfo(0).count == 40);
    QVERIFY(!whole.pageInfo(1).chart);
    QVERIFY(whole.layout(0).fullRectPoints().height() > whole.layout(1).fullRectPoints().height());
    QCOMPARE(whole.layout(1).orientation(), QPageLayout::Landscape);
    // FEAT-084: the frame. The image larger by it; Fit to map's chart page grows, the A4 of the
    // costs stays A4.
    ExportFrame frame;
    frame.style = ExportFrame::Style::Dentils;
    frame.thickness = 10;
    const QImage framed = GanttPages::toImage(source, GanttChart::Scale::Weeks, 1, &reduced, &error, GanttPages::Content::Gantt, frame);
    single.content = GanttPages::Content::Gantt;
    QCOMPARE(framed.size(), (GanttPages(source, single).imageSize() + QSizeF(20, 20)).toSize());
    MapExporter::FramedPages framedPages(std::make_unique<GanttPages>(source, fitToGantt), frame);
    QCOMPARE(framedPages.count(), whole.count());
    QCOMPARE(framedPages.layout(0).fullRectPoints().width(), whole.layout(0).fullRectPoints().width() + 2 * 10 * 0.75);
    QCOMPARE(framedPages.layout(1).pageSize().id(), QPageSize::A4);
}

void GanttViewTest::officeFiles() {
    // FEAT-083 stage C: the Gantt in Excel as the user's weekly planner (bars as conditional formats
    // in the app's colours, the costs coherent with it), in Word (the PDF's pages, cost tables) and
    // in Markdown (tables).
    setExportPlan(2, 18);
    MindNode *launch = m_document->addChild(m_document->model()->root(), "Launch");
    launch->ganttStart = kMonday.addDays(30);
    launch->task = MindNode::Task::Open;  // a milestone
    m_document->model()->nodeById("Task 1.4")->ganttProgress = 50;
    m_view->refresh();
    const GanttExportSource source = m_view->exportSource();
    QCOMPARE(source.rows.size(), 17);
    QCOMPARE(source.columns.at(1).kind, GanttExportSource::Column::Kind::Start);
    const auto serial = [](const QDate &date) { return QString::number(QDate(1899, 12, 30).daysTo(date)); };

    const QMap<QString, QByteArray> book = OoxmlTest::unzip(GanttOffice::xlsx(source, GanttPages::Content::GanttAndCosts,
                                                                              GanttChart::Scale::Weeks, "Plan", kMonday.addDays(2)));
    QCOMPARE(OoxmlTest::packageProblem(book), QString());
    const QString workbook = QString::fromUtf8(book.value("xl/workbook.xml"));
    for (const char *name : {"name=\"Gantt\"", "name=\"Costs\"", "name=\"People\"", "name=\"Per week\""}) QVERIFY2(workbook.contains(name), name);
    QVERIFY(workbook.contains("_xlnm.Print_Titles") && workbook.contains("'Gantt'!$A:$D,'Gantt'!$4:$5"));
    const QString gantt = QString::fromUtf8(book.value("xl/worksheets/sheet1.xml"));
    const QString styles = QString::fromUtf8(book.value("xl/styles.xml"));
    // The legend on two lines; frozen at the timeline (column E) under the header (row 6).
    QVERIFY(gantt.contains("xSplit=\"4\" ySplit=\"6\" topLeftCell=\"E7\""));
    QVERIFY(gantt.contains("showGridLines=\"0\"") && gantt.contains("orientation=\"landscape\""));
    // Typed cells: Task 1.1 (row 8) starts on Monday, a date; the weeks from that Monday.
    QVERIFY(QRegularExpression(QString("<c r=\"C8\" s=\"\\d+\"><v>%1</v>").arg(serial(kMonday))).match(gantt).hasMatch());
    QVERIFY(QRegularExpression(QString("<c r=\"E5\" s=\"\\d+\"><v>%1</v>").arg(serial(kMonday))).match(gantt).hasMatch());
    // Editing Start in Excel moves the bar: the helper reads the shown cell.
    QVERIFY(gantt.contains("IF(ISNUMBER(C8),C8,&quot;&quot;)"));
    QVERIFY(gantt.contains("<conditionalFormatting") && gantt.contains("AND(ISNUMBER($"));
    // The app's colours: tasks peach, summaries lilac (GanttChart::barPaint), the milestone's mark.
    QVERIFY(styles.contains("FFF9BD89") && styles.contains("FFAAAFFA"));
    QVERIFY(gantt.contains(QStringLiteral("\u25C6")));
    QVERIFY(styles.contains("[h]&quot; h &quot;mm"));  // hours with minutes
    // Costs: Task 1.1 = 3 days x 8 h for Anna (50/h) and Marco (72.50/h); a summary sums its tasks.
    const QString costs = QString::fromUtf8(book.value("xl/worksheets/sheet2.xml"));
    QVERIFY(QRegularExpression("<c r=\"D5\" s=\"\\d+\"><v>2</v>").match(costs).hasMatch());  // 48 h
    QVERIFY(QRegularExpression("<c r=\"E5\" s=\"\\d+\"><v>2940</v>").match(costs).hasMatch());
    QVERIFY(costs.contains("<f>SUM(D5:D11)</f>"));
    QVERIFY(costs.contains("<f>SUM(D4,D12,D20)</f>"));  // the total: the top level
    QVERIFY(QString::fromUtf8(book.value("xl/worksheets/sheet3.xml")).contains("<f>SUM(F4:F5)</f>"));
    QVERIFY(QString::fromUtf8(book.value("xl/worksheets/sheet4.xml")).contains("ySplit=\"4\""));
    // Content: the Gantt alone, or the costs alone.
    QVERIFY(!OoxmlTest::unzip(GanttOffice::xlsx(source, GanttPages::Content::Gantt, GanttChart::Scale::Days, "Plan")).contains("xl/worksheets/sheet2.xml"));
    const QMap<QString, QByteArray> costsOnly = OoxmlTest::unzip(GanttOffice::xlsx(source, GanttPages::Content::Costs, GanttChart::Scale::Days, "Plan"));
    QVERIFY(costsOnly.contains("xl/worksheets/sheet3.xml") && !costsOnly.contains("xl/worksheets/sheet4.xml"));

    // Word: one picture per PDF page (landscape A4, fit width), then the tables with their header repeated.
    QString error;
    const QMap<QString, QByteArray> word = OoxmlTest::unzip(GanttOffice::docx(source, GanttPages::Content::GanttAndCosts,
                                                                              GanttChart::Scale::Days, "Plan", &error));
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(OoxmlTest::packageProblem(word), QString());
    const GanttPages pages(source, GanttPages::Options());
    int pictures = 0;
    for (const QString &name : word.keys()) pictures += name.startsWith("word/media/") ? 1 : 0;
    QCOMPARE(pictures, pages.count());
    const QString document = QString::fromUtf8(word.value("word/document.xml"));
    QVERIFY(document.contains("w:orient=\"landscape\"") && document.contains("<w:tblHeader/>"));
    QVERIFY(document.contains("F4F5F7"));  // the summaries' row
    QVERIFY(document.contains(GanttPages::amountText(294000, source.currency).toHtmlEscaped()));

    // Markdown: the shown columns, summaries bold, tasks indented; the cost tables with their totals.
    const QString markdown = GanttOffice::markdown(source, GanttPages::Content::GanttAndCosts);
    QVERIFY(markdown.contains("| Name | Start | Duration |"));
    QVERIFY(markdown.contains("| **Phase 1** |") && markdown.contains("| &emsp;Task 1.1 |"));
    QVERIFY(markdown.contains("| Task | People | Hours | Cost |") && markdown.contains("| **Total** |"));
    QVERIFY(!GanttOffice::markdown(source, GanttPages::Content::Costs).contains("| Name | Start"));
}

void GanttViewTest::officeWindow() {
    // FEAT-083 stage C: Word..., Excel... and Markdown... ask first: the mind map as before, or the Gantt.
    QSettings().remove("export/officeGanttContent");
    QSettings().remove("export/officeGanttUnit");
    {
        OfficeExportDialog word(nullptr, GanttOffice::Format::Word, GanttChart::Scale::Weeks, true);
        word.show();
        QCOMPARE(word.findChild<SegmentedControl *>("OfficeExportKind")->currentIndex(), 1);
        QVERIFY(word.findChild<QWidget *>("OfficeExportContent")->isVisibleTo(&word));
        QCOMPARE(word.findChild<SegmentedControl *>("OfficeExportUnit")->currentIndex(), 1);  // the view's unit
        QVERIFY(!word.findChild<QWidget *>("OfficeExportMapNote")->isVisibleTo(&word));
        word.findChild<SegmentedControl *>("OfficeExportContent")->findChildren<QToolButton *>().at(2)->click();
        word.accept();  // remembered
        word.findChild<SegmentedControl *>("OfficeExportKind")->findChildren<QToolButton *>().at(0)->click();
        QVERIFY(word.findChild<QWidget *>("OfficeExportMapNote")->isVisibleTo(&word));
        QVERIFY(!word.findChild<QWidget *>("OfficeExportContent")->isVisibleTo(&word));
    }
    OfficeExportDialog markdown(nullptr, GanttOffice::Format::Markdown, GanttChart::Scale::Days, false);
    QCOMPARE(markdown.findChild<SegmentedControl *>("OfficeExportKind")->currentIndex(), 0);
    QVERIFY(!markdown.findChild<SegmentedControl *>("OfficeExportUnit"));  // tables only
    QCOMPARE(markdown.content(), GanttPages::Content::Costs);

    // Excel... in the window: the Gantt chosen, the workbook written with the Gantt sheet.
    MainWindow window;
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *document = window.findChild<Document *>();
    QVERIFY(document);
    MindNode *topic = document->addChild(document->model()->root(), "Design");
    topic->ganttStart = kMonday;
    // The Gantt view is built after the side panel, once the window has drawn its first frame.
    QTRY_VERIFY(window.findChild<GanttView *>());
    window.findChild<GanttView *>()->refresh();
    const QString path = m_dir.filePath("window.xlsx");
    window.setExportFilePicker([&path] { return path; });
    bool asked = false;
    window.setOfficeExportChooser([&asked](OfficeExportDialog &dialog) {
        asked = true;
        dialog.findChild<SegmentedControl *>("OfficeExportKind")->findChildren<QToolButton *>().at(1)->click();
        dialog.findChild<SegmentedControl *>("OfficeExportContent")->findChildren<QToolButton *>().at(0)->click();
        return true;
    });
    QAction *excel = nullptr;
    for (QAction *action : window.findChildren<QAction *>())
        if (action->text() == QStringLiteral("Export Excel\u2026")) excel = action;
    QVERIFY(excel);
    excel->trigger();
    QVERIFY(asked);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QMap<QString, QByteArray> written = OoxmlTest::unzip(file.readAll());
    QVERIFY(QString::fromUtf8(written.value("xl/workbook.xml")).contains("name=\"Gantt\""));
    QVERIFY(QString::fromUtf8(written.value("xl/worksheets/sheet1.xml")).contains("Design"));
}

void GanttViewTest::exportWindows() {
    // FEAT-083: the Gantt tab in the PDF and Image windows, chosen when the Gantt view is open.
    QTest::failOnWarning(QRegularExpression("Negative sizes"));  // the empty note's warning (user, 2026-10-31)
    setExportPlan(5, 18);  // four months: fit width takes weeks
    PageDialog pdf(nullptr, PageDialog::Mode::Pdf, "Plan", false, [](ExportDialog::Scope) { return QList<MapExporter::Page>(); }, {},
                   std::nullopt, PageDialog::GanttChoice{m_view->exportSource(), true});
    auto *kind = pdf.findChild<SegmentedControl *>("PageKind");
    QVERIFY(kind);
    pdf.show();  // laid out, as the user sees it
    QCOMPARE(kind->currentIndex(), 2);
    auto *size = pdf.findChild<QComboBox *>("PageSize");  // a paper size: "Fit to map" never coarsens
    size->setCurrentIndex(size->findData(int(MapExporter::PageSize::A4)));
    QVERIFY(pdf.plan() && pdf.plan()->isValid());
    QVERIFY(!pdf.findChild<QWidget *>("PageScope")->isVisibleTo(&pdf));
    QVERIFY(pdf.findChild<QWidget *>("PageGanttContent")->isVisibleTo(&pdf));
    QCOMPARE(pdf.findChild<SegmentedControl *>("PageOrientation")->currentIndex(), 1);  // landscape
    // A coarser unit than days; which one depends on the text widths of the system's font engine.
    const QString note = pdf.findChild<QLabel *>("PageGanttNote")->text();
    QVERIFY2(note.contains("weeks") || note.contains("months"), qPrintable(note));
    QVERIFY(pdf.findChild<QLabel *>("PageGanttNote")->isVisibleTo(&pdf));
    // The choices are remembered between runs: Gantt first, then Gantt and costs.
    const QList<QToolButton *> contents = pdf.findChild<SegmentedControl *>("PageGanttContent")->findChildren<QToolButton *>();
    contents.at(0)->click();
    const int chart = pdf.plan()->count();
    contents.at(1)->click();
    QVERIFY(pdf.plan()->count() > chart);
    // Costs only: no note, hidden (empty and shown, its word wrap asked a height of -1).
    contents.at(2)->click();
    QCoreApplication::processEvents();
    QVERIFY(pdf.findChild<QLabel *>("PageGanttNote")->text().isEmpty());
    QVERIFY(!pdf.findChild<QLabel *>("PageGanttNote")->isVisibleTo(&pdf));
    // Fit to map: the whole Gantt on one page of its size (user, 2026-10-31: it was greyed).
    contents.at(0)->click();
    size->setCurrentIndex(size->findData(int(MapExporter::PageSize::FitToMap)));
    QCOMPARE(pdf.plan()->count(), 1);
    QVERIFY(!pdf.findChild<SegmentedControl *>("PageGanttFit")->isEnabled());
    size->setCurrentIndex(size->findData(int(MapExporter::PageSize::A4)));
    const QString path = m_dir.filePath("plan.pdf");
    pdf.pickPath = [&path](const QString &) { return path; };
    static_cast<QPushButton *>(pdf.acceptButton())->click();
    QVERIFY(QFile::exists(path));
    kind->findChildren<QToolButton *>().at(0)->click();  // the map: its rows again
    QVERIFY(pdf.findChild<QWidget *>("PageScope")->isVisibleTo(&pdf));
    // No note there: an empty word-wrapped label asked a height of -1 (Qt's warning, user 2026-10-31).
    QVERIFY(!pdf.findChild<QLabel *>("PageGanttNote")->isVisibleTo(&pdf));
    ExportDialog image(nullptr, "Plan", false, [](ExportDialog::Scope) { return QList<MapExporter::Page>(); }, {},
                       m_view->exportSource(), true);
    QCOMPARE(image.findChild<SegmentedControl *>("ExportKind")->currentIndex(), 1);
    QVERIFY(image.findChild<QWidget *>("ExportGanttUnit")->isVisibleTo(&image));
    QVERIFY(image.findChild<QWidget *>("ExportGanttContent")->isVisibleTo(&image));  // the costs too
    QVERIFY(!image.findChild<QWidget *>("ExportScope")->isVisibleTo(&image));
    const QString png = m_dir.filePath("plan.png");
    image.pickPath = [&png](MapExporter::Format, const QString &) { return png; };
    for (QPushButton *button : image.findChildren<QPushButton *>())
        if (button->text() == "PNG") button->click();
    QVERIFY(QImage(png).width() > 0);
    // Opened on the map, the note is empty when the window shows: no warning either (user, 2026-10-31).
    PageDialog onMap(nullptr, PageDialog::Mode::Pdf, "Plan", false, [](ExportDialog::Scope) { return QList<MapExporter::Page>(); }, {},
                     std::nullopt, PageDialog::GanttChoice{m_view->exportSource(), false});
    onMap.show();
    QVERIFY(QTest::qWaitForWindowExposed(&onMap));
}

void GanttViewTest::drag(const QPointF &from, const QPointF &to) {
    QWidget *viewport = m_view->chart()->viewport();
    QTest::mousePress(viewport, Qt::LeftButton, {}, from.toPoint());
    QTest::mouseMove(viewport, ((from + to) / 2).toPoint());
    QTest::mouseMove(viewport, to.toPoint());
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, to.toPoint());
}

// BUG-035: month and day names follow the interface language chosen in Settings, not the system's.
void GanttViewTest::namesFollowTheInterfaceLanguage() {
    struct Case { const char *code, *january, *monday; };
    for (const Case &language : {Case{"en", "January", "Mo"}, Case{"it", "Gennaio", "Lu"}}) {
        Localization::setPreference(QString::fromLatin1(language.code));
        QCOMPARE(Localization::preference(), QString::fromLatin1(language.code));
        const GanttChart chart;
        QCOMPARE(chart.monthName(1, QLocale::LongFormat), QString::fromLatin1(language.january));
        DatePicker picker(nullptr, {});
        picker.showMonth(QDate(2027, 1, 10));
        QCOMPARE(picker.monthTitle(), QString::fromLatin1(language.january) + QStringLiteral(" 2027"));
        QCOMPARE(picker.weekdayName(Qt::Monday), QString::fromLatin1(language.monday));
    }
    Localization::setPreference(QString());
}

void GanttViewTest::viewButtons() {
    MainWindow window;
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    qobject_cast<QStackedWidget *>(window.centralWidget())->setCurrentIndex(1);  // editor page
    const auto texts = [&window] {
        QStringList shown;
        for (const QString &text : {QStringLiteral("Mind map"), QStringLiteral("Outliner"), QStringLiteral("Gantt")})
            for (auto *button : window.findChildren<QPushButton *>())
                if (button->text() == text && button->isVisibleTo(&window)) shown.append(text);
        return shown;
    };
    const auto click = [&window](const QString &text) {
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == text && button->isVisibleTo(&window)) return button->click(), true;
        return false;
    };
    QCOMPARE(texts(), QStringList({"Outliner", "Gantt"}));
    QVERIFY(click("Gantt"));
    auto *gantt = window.findChild<GanttView *>();
    QVERIFY(gantt && gantt->isVisible());
    // The Critical path button: in the top bar on the Gantt page only, pressed while shown (user, 2026-10-28).
    auto *criticalButton = window.findChild<QToolButton *>(QStringLiteral("ganttCriticalButton"));
    QVERIFY(criticalButton);
    QVERIFY(criticalButton->isCheckable() && criticalButton->isChecked());
    QVERIFY(criticalButton->isVisibleTo(&window));
    QCOMPARE(criticalButton->size(), QSize(32, 32));  // as the other buttons of the top bar
    // The group's order: panel, resources, critical path, baseline (user, 2026-10-30).
    QStringList order;
    for (QToolButton *button : criticalButton->parentWidget()->findChildren<QToolButton *>(Qt::FindDirectChildrenOnly))
        order.append(button->objectName());
    QCOMPARE(order, QStringList({"ganttPanelButton", "ganttResourcesButton", "ganttCriticalButton", "ganttBaselineButton"}));
    criticalButton->click();
    QVERIFY(!gantt->criticalShown());
    criticalButton->click();
    QVERIFY(gantt->criticalShown() && criticalButton->isChecked());
    // The map's main topics are the rows; a map with the central topic alone shows the empty plan.
    QCOMPARE(gantt->showsEmptyState(), gantt->table()->model()->rowCount() == 0);
    // Below the top bars, above the bottom groups (user's photo): the table header is fully visible.
    QVERIFY(gantt->table()->mapTo(&window, QPoint(0, 0)).y() >= 72);
    // The splitter cannot squeeze the task table below its minimum (user's photo).
    auto *splitter = window.findChild<QSplitter *>(QStringLiteral("GanttSplitter"));
    QVERIFY(splitter);
    splitter->setSizes({0, splitter->width()});
    // Every column header whole: the table pane stops at the name's minimum plus the shown columns
    // (default Name, Start, Duration, FEAT-078). The empty plan hides the pane, so the sizes are
    // checked, not the laid-out widgets.
    QHeaderView *header = gantt->table()->header();
    const int columns = GanttView::kNameMinWidth + header->sectionSize(1) + header->sectionSize(3);
    QCOMPARE(gantt->table()->minimumWidth(), columns);
    QCOMPARE(splitter->sizes().at(0), columns);
    // A dark map: the Gantt takes the outliner's dark page, not the map's colour (user's photo).
    auto *document = window.findChild<Document *>();
    QVERIFY(document);
    MindMapModel *model = document->model();
    QCOMPARE(gantt->pageColor(), GanttColors::light().page);  // the light grey around the cards (FEAT-078)
    model->setTheme(model->paletteId, model->activePalette, model->paletteName, -1, QColor("#0d0d0d"));
    QCOMPARE(gantt->pageColor(), UiTheme::color(UiTheme::Token::SurfaceSunken));
    // The margins around table and chart are the dark page too, not the white behind the view.
    const QPoint margin = gantt->mapTo(&window, QPoint(4, gantt->height() - 4));
    QCOMPARE(window.grab().toImage().pixelColor(margin), UiTheme::color(UiTheme::Token::SurfaceSunken));
    // The task panel's pages on the dark card too, not white (user's picture, 2026-10-30).
    gantt->togglePanel();
    for (const auto page : {GanttTaskPanel::Page::Task, GanttTaskPanel::Page::Gantt}) {
        gantt->panel()->showPage(page);
        const QPoint inside = gantt->panel()->mapTo(&window, QPoint(4, gantt->panel()->height() / 2));
        QCOMPARE(window.grab().toImage().pixelColor(inside), GanttColors::dark().card);
    }
    gantt->togglePanel();
    model->setTheme(model->paletteId, model->activePalette, model->paletteName, -1, QColor(Qt::white));
    QCOMPARE(gantt->pageColor(), GanttColors::light().page);
    QCOMPARE(texts(), QStringList({"Mind map", "Outliner"}));
    QVERIFY(click("Outliner"));
    QCOMPARE(texts(), QStringList({"Mind map", "Gantt"}));
    QVERIFY(click("Gantt"));
    QVERIFY(click("Mind map"));
    QVERIFY(!criticalButton->isVisibleTo(&window));
    // FEAT-079: the Resources button sits next to it, on the Gantt page only.
    auto *resourcesButton = window.findChild<QToolButton *>(QStringLiteral("ganttResourcesButton"));
    QVERIFY(resourcesButton && !resourcesButton->isCheckable());
    QVERIFY(!resourcesButton->isVisibleTo(&window));
    QVERIFY(click("Gantt"));
    QVERIFY(resourcesButton->isVisibleTo(&window));
    QCOMPARE(resourcesButton->size(), QSize(32, 32));
    QCOMPARE(resourcesButton->parentWidget(), criticalButton->parentWidget());
    QVERIFY(click("Mind map"));
    QCOMPARE(texts(), QStringList({"Outliner", "Gantt"}));
}

void GanttViewTest::emptyPlan() {
    QVERIFY(m_view->showsEmptyState());
    m_view->addFirstTaskButton()->click();
    // The row is a new main topic of the map, without a date (no bar yet).
    const GanttPlan plan = m_document->model()->gantt();
    QCOMPARE(plan.tasks().size(), 1);
    QCOMPARE(m_document->model()->root()->children.size(), size_t(1));
    QCOMPARE(plan.tasks().at(0).id, m_document->model()->root()->children.front()->id);
    QVERIFY(plan.tasks().at(0).start.isNull());
    QVERIFY(m_view->chart()->barRect(plan.tasks().at(0).id).isEmpty());
    QVERIFY(!m_view->showsEmptyState());
    // The name is open for editing; a new name confirmed by the commit (found in the table: tests
    // running at the same time can take the focus).
    auto *editor = m_view->table()->viewport()->findChild<QLineEdit *>();
    QVERIFY(editor);
    editor->setText("Design");
    m_view->commitPendingEdit();
    QCOMPARE(m_document->model()->root()->children.front()->title, QString("Design"));  // the topic's title
    // A start date typed in the table gives the row its bar.
    QAbstractItemModel *table = m_view->table()->model();
    QVERIFY(table->setData(table->index(0, 1), kMonday));
    QCOMPARE(m_document->model()->root()->children.front()->ganttStart, kMonday);
    QVERIFY(!m_view->chart()->barRect(plan.tasks().at(0).id).isEmpty());
}

void GanttViewTest::tableEdits() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 2, "S"), task("B", kMonday, 3)});
    QAbstractItemModel *model = m_view->table()->model();
    const QModelIndex summary = model->index(0, 0);
    const QModelIndex a = model->index(0, 0, summary);
    QCOMPARE(model->data(a).toString(), QString("A"));
    QVERIFY(!(model->flags(summary.siblingAtColumn(3)) & Qt::ItemIsEditable));
    QVERIFY(model->flags(summary) & Qt::ItemIsEditable);
    QVERIFY(model->setData(a.siblingAtColumn(3), 99999));  // clamped
    QCOMPARE(m_document->model()->gantt().task("A")->duration, GanttPlan::kMaxDuration);
    const QModelIndex b = model->index(1, 0);
    QVERIFY(model->setData(b.siblingAtColumn(2), kMonday.addDays(7)));  // End: next Monday
    QCOMPARE(m_document->model()->gantt().task("B")->duration, 8);  // weekends count (default calendar)
    QVERIFY(model->setData(b.siblingAtColumn(4), 140));
    QCOMPARE(m_document->model()->gantt().task("B")->progress, 100);
    QVERIFY(model->setData(b.siblingAtColumn(1), kMonday.addDays(1)));
    QCOMPARE(m_document->model()->gantt().task("B")->start, kMonday.addDays(1));
    QVERIFY(!model->setData(b, "   "));  // empty name refused
    QCOMPARE(model->data(summary.siblingAtColumn(2), Qt::EditRole).toDate(), m_document->model()->gantt().schedule().tasks["S"].end);
}

void GanttViewTest::keys() {
    setPlan({task("A", kMonday, 2), task("B", kMonday, 2)});
    QTreeView *table = m_view->table();
    m_view->selectTask("B");
    table->setFocus();
    QTest::keyClick(table, Qt::Key_Tab);  // B under A
    QCOMPARE(m_document->model()->gantt().task("B")->parentId, QString("A"));
    QTest::keyClick(table, Qt::Key_Backtab);
    QVERIFY(m_document->model()->gantt().task("B")->parentId.isEmpty());
    QCOMPARE(m_document->model()->gantt().children({}), QStringList({"A", "B"}));
    m_view->selectTask("A");
    QTest::keyClick(table, Qt::Key_Return);  // new task after A, name open
    QCOMPARE(m_document->model()->gantt().children({}).size(), 3);
    QCOMPARE(m_document->model()->gantt().children({}).at(2), QString("B"));
    QVERIFY(qobject_cast<QLineEdit *>(QApplication::focusWidget()));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    // A name confirmed with Enter keeps the focus in the table: the next Enter adds another task.
    QTest::keyClick(table, Qt::Key_Return);
    QApplication::focusWidget()->setProperty("text", QStringLiteral("Test"));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
    QApplication::processEvents();
    QVERIFY(table->hasFocus());
    QTest::keyClick(table, Qt::Key_Return);
    QCOMPARE(m_document->model()->gantt().children({}).size(), 5);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    for (int i = 0; i < 2; ++i) {
        m_view->selectTask(m_document->model()->gantt().children({}).at(1));
        QTest::keyClick(table, Qt::Key_Delete);
    }
    m_view->selectTask(m_document->model()->gantt().children({}).at(1));
    QTest::keyClick(table, Qt::Key_Delete);
    QCOMPARE(m_document->model()->gantt().children({}), QStringList({"A", "B"}));
}

void GanttViewTest::mouse() {
    setPlan({task("A", kMonday, 3), task("B", kMonday, 2)});
    GanttChart *chart = m_view->chart();
    const int dw = chart->dayWidth();
    QRectF a = chart->barRect("A");
    QVERIFY(!a.isEmpty() && a.right() < chart->viewport()->width());

    drag(a.center(), a.center() + QPointF(2 * dw, 0));  // move two days
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday.addDays(2));
    QVERIFY(m_document->canUndo());
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday);
    m_view->refresh();

    a = chart->barRect("A");
    drag(QPointF(a.right() - 1, a.center().y()), QPointF(a.right() - 1 + dw, a.center().y()));  // one more day
    QCOMPARE(m_document->model()->gantt().task("A")->duration, 4);

    a = chart->barRect("A");
    QTest::mouseMove(chart->viewport(), a.center().toPoint());  // the badge shows on hover
    const QPointF handle = chart->progressHandle("A");
    drag(handle, QPointF(a.left() + a.width() / 2, handle.y()));
    QCOMPARE(m_document->model()->gantt().task("A")->progress, 50);

    QTest::mouseMove(chart->viewport(), a.center().toPoint());  // the handles appear on hover
    drag(chart->linkHandle("A"), chart->barRect("B").center());
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 1);
    QCOMPARE(m_document->model()->gantt().task("B") ? m_document->model()->gantt().schedule().tasks["B"].start : QDate(),
             kMonday.addDays(4));  // after A's four days
    // The reverse would close a cycle: refused, nothing changes.
    const GanttPlan before = m_document->model()->gantt();
    drag(chart->linkHandle("B"), chart->barRect("A").center());
    QVERIFY(m_document->model()->gantt() == before);
    // A click without moving changes nothing but selects the row.
    QTest::mouseClick(chart->viewport(), Qt::LeftButton, {}, chart->barRect("B").center().toPoint());
    QVERIFY(m_document->model()->gantt() == before);
    QCOMPARE(m_view->selectedIds(), QStringList({"B"}));
}

void GanttViewTest::chainAndStartLink() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 2, "S"), task("B", kMonday, 1), task("C", kMonday, 3)});
    // Selected C, then B, then A (and the summary): links follow that order, the summary is left out.
    QTreeView *table = m_view->table();
    auto *selection = table->selectionModel();
    const auto select = [&](const QString &id) {
        for (QModelIndex row = table->model()->index(0, 0); row.isValid(); row = table->indexBelow(row))
            if (row.data().toString() == id) selection->select(row, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    };
    for (const QString &id : {"C", "B", "S", "A"}) select(id);
    QCOMPARE(m_view->chainIds(), QStringList({"C", "B", "A"}));
    table->setFocus();
    QTest::keyClick(table, Qt::Key_L, Qt::ControlModifier | Qt::ShiftModifier);
    const GanttPlan &plan = m_document->model()->gantt();
    QCOMPARE(plan.dependencies().size(), 2);
    QCOMPARE(plan.dependencies().at(0).from, QString("C"));
    QCOMPARE(plan.dependencies().at(1).to, QString("A"));
    // Again with the whole chain there: its links are removed (a toggle), one step.
    QVERIFY(m_view->chainUnlinks());
    QVERIFY(m_view->chainSelected());
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 2);
    // One task alone: all its links go.
    selection->clearSelection();
    select("B");
    QVERIFY(m_view->chainUnlinks());
    QVERIFY(m_view->chainSelected());
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());
    m_document->undo();
    m_document->undo();
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());
    // Several tasks, only some linked: first click removes, second links them all.
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.addDependency(dependency("C", "B")); }));
    selection->clearSelection();
    for (const QString &id : {"C", "B", "A"}) select(id);
    QVERIFY(m_view->chainUnlinks());
    QVERIFY(m_view->chainSelected());
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());
    QVERIFY(!m_view->chainUnlinks());
    QVERIFY(m_view->chainSelected());
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 2);
    for (int step = 0; step < 3; ++step) m_document->undo();  // link, removal, C -> B
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());

    // Shift+click from A (under S) to C: the rows join top to bottom, as clicked one by one.
    selection->clearSelection();
    const auto rowCenter = [&](const QString &id) {
        for (QModelIndex row = table->model()->index(0, 0); row.isValid(); row = table->indexBelow(row))
            if (row.data().toString() == id) return table->visualRect(row).center();
        return QPoint();
    };
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, rowCenter("A"));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::ShiftModifier, rowCenter("C"));
    QCOMPARE(m_view->chainIds(), QStringList({"A", "B", "C"}));
    selection->clearSelection();

    // Drawn from the start circle of C onto B: B comes first.
    GanttChart *chart = m_view->chart();
    QTest::mouseMove(chart->viewport(), chart->barRect("C").center().toPoint());
    drag(chart->linkHandleAtStart("C"), chart->barRect("B").center());
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 1);
    QCOMPARE(m_document->model()->gantt().dependencies().at(0).from, QString("B"));
    QCOMPARE(m_document->model()->gantt().dependencies().at(0).to, QString("C"));
}

void GanttViewTest::chainWithoutDatesStartsToday() {
    setPlan({task("A", {}, 2), task("B", {}, 1), task("C", kMonday, 1)});
    QTreeView *table = m_view->table();
    for (QModelIndex row = table->model()->index(0, 0); row.isValid(); row = table->indexBelow(row))
        table->selectionModel()->select(row, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QCOMPARE(m_view->chainIds(), QStringList({"A", "B", "C"}));
    // The first task has no date: it starts today and the others follow it, one step.
    QVERIFY(m_view->chainSelected());
    const GanttPlan plan = m_document->model()->gantt();
    QCOMPARE(plan.task("A")->start, QDate::currentDate());
    QVERIFY(plan.task("B")->start.isNull());  // placed by its link
    const GanttSchedule schedule = plan.schedule();
    QVERIFY(schedule.tasks.value("B").dated);
    QCOMPARE(schedule.tasks.value("B").start, QDate::currentDate().addDays(2));
    QCOMPARE(plan.task("C")->start, kMonday);  // a date of its own is kept
    m_document->undo();
    QVERIFY(m_document->model()->gantt().task("A")->start.isNull());
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());
}

void GanttViewTest::historyAndSave() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 2, "S"), task("B", kMonday, 3)}, {dependency("A", "B")});
    const GanttPlan plan = m_document->model()->gantt();
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setProgress("B", 40); }));
    QCOMPARE(m_document->model()->nodeById("B")->ganttProgress, 40);  // stored on the topic
    QVERIFY(!m_document->editGantt([](GanttPlan &p) { return p.setName("B", "Build"); }));  // the map's
    // Renaming to the same text changes nothing (the title keeps its form, no history step).
    QAbstractItemModel *table = m_view->table()->model();
    const int steps = m_document->canUndo();
    QVERIFY(!table->setData(table->index(1, 0), QStringLiteral(" B ")));
    QCOMPARE(int(m_document->canUndo()), steps);
    // The bar's colour is the branch colour, both ways: a branch colour chosen on the map is the
    // bar's; a colour edit through editGantt is refused (it is a style of the topic).
    QVERIFY(m_document->model()->setStyle({m_document->model()->nodeById("B")}, StyleProperty::BranchColor, QColor("#2cd551")));
    QCOMPARE(m_document->model()->gantt().task("B")->color, QColor("#2cd551"));
    QCOMPARE(m_document->model()->gantt().task("S")->color, m_document->model()->nodeById("S")->branchColor);
    QVERIFY(!m_document->editGantt([](GanttPlan &p) { return p.setColor("B", QColor("#ff0000")); }));
    QVERIFY(m_document->model()->clearStyle({m_document->model()->nodeById("B")}, StyleProperty::BranchColor));
    // A link to a topic that is gone stays in the sheet and comes back with the topic.
    QVERIFY(m_document->removeNodes({m_document->model()->nodeById("B")}));
    m_document->flushPendingStep();
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setProgress("A", 10); }));
    QVERIFY(m_document->model()->toJson().value("gantt").toObject().value("dependencies").toArray().size() == 1);
    m_document->undo();
    m_document->undo();
    m_document->undo();
    QVERIFY(m_document->model()->gantt() == plan);
    m_document->redo();
    QCOMPARE(m_document->model()->gantt().task("B")->progress, 40);

    const QString file = m_dir.filePath("gantt.mentis");
    QVERIFY(m_document->save(file, BmapStorage::SaveReason::Explicit).ok);
    const GanttPlan saved = m_document->model()->gantt();
    m_document->close();
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QVERIFY(reopened.model()->gantt() == saved);
    reopened.close();

    // A map without a plan: empty plan, no key written.
    Document plain;
    plain.startUntitled();
    QVERIFY(plain.model()->gantt().tasks().isEmpty());
    QVERIFY(!plain.model()->toJson().contains("gantt"));
    plain.close();
}

void GanttViewTest::columns() {
    setPlan({task("A", kMonday, 3)});
    QHeaderView *header = m_view->table()->header();
    // Default: Name, Start, Duration (user, 2026-10-27).
    QVERIFY(!header->isSectionHidden(0) && !header->isSectionHidden(1) && !header->isSectionHidden(3));
    QVERIFY(header->isSectionHidden(2) && header->isSectionHidden(4));
    const int narrow = m_view->table()->minimumWidth();
    // Showing End and % widens the minimum by their widths and is kept as an app setting.
    m_view->setColumnShown(2, true);
    m_view->setColumnShown(4, true);
    QCOMPARE(m_view->table()->minimumWidth(), narrow + header->sectionSize(2) + header->sectionSize(4));
    QCOMPARE(QSettings().value("gantt/columns").toStringList(), QStringList({"start", "end", "duration", "progress"}));
    m_view->setColumnShown(1, false);
    m_view->setColumnShown(0, false);  // Name is always shown
    QVERIFY(!header->isSectionHidden(0));
    // A new view reads the setting.
    GanttView other(m_document.get(), m_shortcuts.get());
    QVERIFY(other.table()->header()->isSectionHidden(1));
    QVERIFY(!other.table()->header()->isSectionHidden(2));
}

// FEAT-078: the columns after Name can be dragged; No., Name and Extras stay first (user, 2026-10-29).
void GanttViewTest::columnOrder() {
    setPlan({task("A", kMonday, 3)});
    QHeaderView *header = m_view->table()->header();
    constexpr int name = 0, start = 1, duration = 3, number = 7, extras = 8;
    QVERIFY(header->sectionsMovable());
    QCOMPARE(header->visualIndex(number), 0);
    QCOMPARE(header->visualIndex(name), 1);
    QCOMPARE(header->visualIndex(extras), 2);
    QVERIFY(header->visualIndex(start) < header->visualIndex(duration));
    // Duration dragged before Start.
    header->moveSection(header->visualIndex(duration), header->visualIndex(start));
    QVERIFY(header->visualIndex(duration) < header->visualIndex(start));
    QCOMPARE(QSettings().value("gantt/columnOrder").toStringList().mid(0, 2), QStringList({"duration", "start"}));
    // Start dropped before Name goes right after the fixed columns; Name dragged goes back.
    header->moveSection(header->visualIndex(start), 0);
    QCOMPARE(header->visualIndex(number), 0);
    QCOMPARE(header->visualIndex(name), 1);
    QCOMPARE(header->visualIndex(extras), 2);
    QCOMPARE(header->visualIndex(start), 3);
    header->moveSection(header->visualIndex(name), 5);
    QCOMPARE(header->visualIndex(name), 1);
    QCOMPARE(m_view->table()->treePosition(), name);
    // A new view reads the order.
    GanttView other(m_document.get(), m_shortcuts.get());
    QHeaderView *otherHeader = other.table()->header();
    QCOMPARE(otherHeader->visualIndex(start), 3);
    QVERIFY(otherHeader->visualIndex(duration) < otherHeader->visualIndex(2));  // before End
}

// FEAT-024: the map's numbers in a read-only column before Name, shown when the map is numbered unless
// the user hid it (app setting).
void GanttViewTest::numberColumn() {
    setPlan({task("A", kMonday, 3), task("B", kMonday, 2, "A"), task("C", kMonday, 1)});
    QHeaderView *header = m_view->table()->header();
    QAbstractItemModel *table = m_view->table()->model();
    constexpr int number = 7;
    QVERIFY(header->isSectionHidden(number));
    const int narrow = m_view->table()->minimumWidth();
    BranchNumbering numbering;
    numbering.format = QStringLiteral("1.2.3");
    QVERIFY(m_document->model()->setStyle({m_document->model()->root()}, StyleProperty::Numbering, QVariant::fromValue(numbering)));
    m_view->refresh();
    QVERIFY(!header->isSectionHidden(number));
    QCOMPARE(header->visualIndex(number), 0);
    QCOMPARE(m_view->table()->treePosition(), 0);  // arrows and indentation stay on Name
    const QModelIndex a = table->index(0, number), b = table->index(0, number, table->index(0, 0));
    QCOMPARE(a.data().toString(), QStringLiteral("1"));
    QCOMPARE(b.data().toString(), QStringLiteral("1.1"));
    QCOMPARE(table->index(1, number).data().toString(), QStringLiteral("2"));
    QCOMPARE(table->index(0, 0).data().toString(), QStringLiteral("A"));  // the name has no number
    QVERIFY(!(table->flags(b) & Qt::ItemIsEditable));
    QCOMPARE(m_view->table()->minimumWidth(), narrow + header->sectionSize(number));
    // Hidden by the user: it stays hidden while the map changes and in a new view (app setting).
    m_view->setColumnShown(number, false);
    QVERIFY(header->isSectionHidden(number));
    QCOMPARE(m_view->table()->minimumWidth(), narrow);
    QVERIFY(!QSettings().value("gantt/columns").toStringList().contains(QString()));
    m_document->addChild(m_document->model()->root(), QStringLiteral("D"));
    m_view->refresh();
    QVERIFY(header->isSectionHidden(number));
    {
        GanttView other(m_document.get(), m_shortcuts.get());
        other.refresh();
        QVERIFY(other.table()->header()->isSectionHidden(number));
    }
    m_view->setColumnShown(number, true);
    QVERIFY(!header->isSectionHidden(number));
    // Numbering removed: the column goes away.
    QVERIFY(m_document->model()->setStyle({m_document->model()->root()}, StyleProperty::Numbering, QVariant::fromValue(BranchNumbering())));
    m_view->refresh();
    QVERIFY(header->isSectionHidden(number));
    QCOMPARE(m_view->table()->minimumWidth(), narrow);
    m_view->setColumnShown(number, false);  // no choice without numbering
    QVERIFY(!QSettings().value("gantt/numberHidden").toBool());
    // Numbering started again: shown, as the user left it.
    QVERIFY(m_document->model()->setStyle({m_document->model()->root()}, StyleProperty::Numbering, QVariant::fromValue(numbering)));
    m_view->refresh();
    QVERIFY(!header->isSectionHidden(number));
}

// Extras column (user, 2026-10-28): web link and note icons after Name, shown only when some topic
// has one; hover says "Link" / "Note", a click opens the page or the note without selecting the row.
void GanttViewTest::extrasColumn() {
    setPlan({task("A", kMonday, 3), task("B", kMonday, 2), task("C", kMonday, 1)});
    QTreeView *table = m_view->table();
    QHeaderView *header = table->header();
    constexpr int extras = 8;
    QVERIFY(header->isSectionHidden(extras));
    const int narrow = table->minimumWidth();
    MindMapModel *model = m_document->model();
    model->nodeById("A")->webLink = QStringLiteral("https://example.org/");
    model->nodeById("B")->notes = QStringLiteral("Remember");
    model->nodeById("C")->webLink = QStringLiteral("https://example.com/");
    model->nodeById("C")->notes = QStringLiteral("Both");
    m_view->refresh();
    QVERIFY(!header->isSectionHidden(extras));
    QCOMPARE(header->visualIndex(extras), header->visualIndex(0) + 1);
    QCOMPARE(table->minimumWidth(), narrow + header->sectionSize(extras));
    QApplication::processEvents();
    // First icon 8 px from the cell's left, the second 24 px after it.
    const auto icon = [&](int row, int n) {
        const QRect cell = table->visualRect(table->model()->index(row, extras));
        return QPoint(cell.left() + 8 + n * 24 + 8, cell.center().y());
    };
    QSignalSpy links(m_view.get(), &GanttView::webLinkRequested);
    QSignalSpy notes(m_view.get(), &GanttView::noteRequested);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, icon(0, 0));
    QCOMPARE(links.count(), 1);
    QCOMPARE(links.at(0).at(0).toString(), QString("A"));
    QVERIFY(m_view->selectedIds().isEmpty());  // the click does not select the row
    QVERIFY(!m_view->panel()->isVisible());
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, icon(1, 0));  // B: the note alone, first
    QCOMPARE(notes.count(), 1);
    QCOMPARE(notes.at(0).at(0).toString(), QString("B"));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, icon(2, 1));  // C: link, then note
    QCOMPARE(notes.count(), 2);
    QCOMPARE(notes.at(1).at(0).toString(), QString("C"));
    QVERIFY(notes.at(1).at(1).toRect().contains(table->viewport()->mapToGlobal(icon(2, 1))));
    // Tooltips.
    const auto tip = [&](const QPoint &pos) {
        QHelpEvent help(QEvent::ToolTip, pos, table->viewport()->mapToGlobal(pos));
        QApplication::sendEvent(table->viewport(), &help);
        const QString text = QToolTip::text();
        QToolTip::hideText();
        return text;
    };
    QCOMPARE(tip(icon(2, 0)), QString("Link"));
    QCOMPARE(tip(icon(2, 1)), QString("Note"));
    // No link nor note left: the column goes away.
    for (const char *id : {"A", "B", "C"}) {
        model->nodeById(id)->webLink.clear();
        model->nodeById(id)->notes.clear();
    }
    m_view->refresh();
    QVERIFY(header->isSectionHidden(extras));
    QCOMPARE(table->minimumWidth(), narrow);

    // In the window: the link opens the web page, the note opens the note editor.
    MainWindow window;
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    qobject_cast<QStackedWidget *>(window.centralWidget())->setCurrentIndex(1);
    QList<QUrl> opened;
    window.setUrlOpener([&opened](const QUrl &url) { opened.append(url); return true; });
    auto *document = window.findChild<Document *>();
    MindNode *topic = document->addChild(document->model()->root(), QStringLiteral("Task"));
    topic->webLink = QStringLiteral("https://example.org/");
    topic->notes = QStringLiteral("A note");
    for (auto *button : window.findChildren<QPushButton *>())
        if (button->text() == "Gantt" && button->isVisibleTo(&window)) { button->click(); break; }
    auto *gantt = window.findChild<GanttView *>();
    gantt->refresh();
    QTreeView *windowTable = gantt->table();
    const QRect cell = windowTable->visualRect(windowTable->model()->index(0, extras));
    QTest::mouseClick(windowTable->viewport(), Qt::LeftButton, {}, QPoint(cell.left() + 16, cell.center().y()));
    QCOMPARE(opened, QList<QUrl>({QUrl("https://example.org/")}));
    QTest::mouseClick(windowTable->viewport(), Qt::LeftButton, {}, QPoint(cell.left() + 40, cell.center().y()));
    QVERIFY(window.noteEditor()->isOpen());
    QCOMPARE(window.noteEditor()->nodeId(), topic->id);
}

void GanttViewTest::actions() {
    setPlan({task("A", kMonday, 2), task("B", kMonday, 2), task("C", kMonday, 2)});
    m_view->refresh();
    const auto button = [this](const char *name) {
        auto *found = m_view->actionsRow()->findChild<QToolButton *>(QLatin1String(name));
        if (!found) qFatal("missing button %s", name);
        return found;
    };
    // The row sits above the columns; the chart's time header is level with the table's header.
    QVERIFY(m_view->actionsRow()->mapTo(m_view.get(), QPoint(0, 0)).y() < m_view->table()->mapTo(m_view.get(), QPoint(0, 0)).y());
    QCOMPARE(m_view->table()->header()->mapTo(m_view.get(), QPoint(0, 0)).y(), m_view->chart()->viewport()->mapTo(m_view.get(), QPoint(0, 0)).y());
    m_view->selectTask("A");
    QVERIFY(!button("ganttIndent")->isEnabled());   // no previous sibling
    QVERIFY(!button("ganttOutdent")->isEnabled());  // a main topic
    QVERIFY(!button("ganttChain")->isEnabled());
    QVERIFY(button("ganttAddTask")->toolTip().contains("Add task"));
    m_view->selectTask("B");
    QVERIFY(button("ganttIndent")->isEnabled());
    button("ganttIndent")->click();
    QCOMPARE(m_document->model()->gantt().task("B")->parentId, QString("A"));
    QVERIFY(button("ganttOutdent")->isEnabled());
    button("ganttOutdent")->click();
    QVERIFY(m_document->model()->gantt().task("B")->parentId.isEmpty());
    // Add subtask under the selected row.
    m_view->selectTask("C");
    button("ganttAddSubtask")->click();
    QCOMPARE(m_document->model()->gantt().children("C").size(), 1);
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_Escape);
    // Link in a chain: enabled with two tasks selected.
    m_view->selectTask("A");
    QTreeView *table = m_view->table();
    table->selectionModel()->select(table->model()->index(1, 0), QItemSelectionModel::Select | QItemSelectionModel::Rows);
    QVERIFY(button("ganttChain")->isEnabled());
    button("ganttChain")->click();
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 1);
    // Go to today: today three days from the left (user, 2026-10-30).
    m_view->chart()->horizontalScrollBar()->setValue(0);
    button("ganttToday")->click();
    QCOMPARE(m_view->chart()->xOf(QDate::currentDate()), double(GanttChart::kOpenDaysBefore * m_view->chart()->dayWidth()));
}

void GanttViewTest::zoom() {
    setPlan({task("A", kMonday, 3)});
    GanttChart *chart = m_view->chart();
    QCOMPARE(chart->zoomPercent(), 100);
    QCOMPARE(chart->dayWidth(), 28);
    QSignalSpy changed(chart, &GanttChart::zoomChanged);
    chart->setZoomPercent(200);
    QCOMPARE(chart->dayWidth(), 56);
    QCOMPARE(changed.size(), 1);
    chart->setZoomPercent(10);  // clamped
    QCOMPARE(chart->zoomPercent(), GanttChart::kMinZoom);
    chart->setZoomPercent(999);
    QCOMPARE(chart->zoomPercent(), GanttChart::kMaxZoom);
    // A scale change keeps the percentage: Weeks at 300 % is 36 px a day.
    chart->setScale(GanttChart::Scale::Weeks);
    QCOMPARE(chart->dayWidth(), 36);
    chart->setScale(GanttChart::Scale::Days);
    chart->setZoomPercent(100);
    // Ctrl+wheel steps up and keeps the date under the mouse.
    const QPointF at(300, 100);
    const double before = chart->horizontalScrollBar()->value() + at.x();
    const double day = before / chart->dayWidth();
    QWheelEvent wheel(at, chart->viewport()->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(chart->viewport(), &wheel);
    QCOMPARE(chart->zoomPercent(), 125);
    QVERIFY(std::abs((chart->horizontalScrollBar()->value() + at.x()) / chart->dayWidth() - day) < 0.1);
    // Months at the smallest zoom: days still 2 px wide.
    chart->setScale(GanttChart::Scale::Months);
    chart->setZoomPercent(50);
    QCOMPARE(chart->dayWidth(), 2);
}

void GanttViewTest::barColours() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 3, "S"), task("B", kMonday, 3)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    const auto colourOf = [chart](const QString &id) {
        const QRectF bar = chart->barRect(id);
        return chart->viewport()->grab().toImage().pixelColor(QPointF(bar.left() + 6, bar.center().y()).toPoint());
    };
    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) < 12 && std::abs(a.green() - b.green()) < 12 && std::abs(a.blue() - b.blue()) < 12;
    };
    // The defaults whatever the map's colours: tasks peach, summaries lilac (user, 2026-10-27).
    QVERIFY(near(colourOf("A"), QColor(0xf9, 0xbd, 0x89)));
    QVERIFY(near(colourOf("B"), QColor(0xf9, 0xbd, 0x89)));
    QVERIFY(near(colourOf("S"), QColor(0xaa, 0xaf, 0xfa)));
    // A colour chosen by hand colours that bar alone; its children keep theirs.
    MindMapModel *model = m_document->model();
    QVERIFY(model->setStyle({model->nodeById("S")}, StyleProperty::BranchColor, QColor("#2cd551")));
    m_view->refresh();
    QVERIFY(near(colourOf("S"), QColor("#2cd551")));
    QVERIFY(near(colourOf("A"), QColor(0xf9, 0xbd, 0x89)));
    QVERIFY(model->clearStyle({model->nodeById("S")}, StyleProperty::BranchColor));
    m_view->refresh();
    QVERIFY(near(colourOf("S"), QColor(0xaa, 0xaf, 0xfa)));
}

void GanttViewTest::panelTheme() {
    // The task panel's inputs follow the page, light or dark, also when it changes (user, 2026-10-29).
    setPlan({task("A", kMonday, 3)});
    m_view->refresh();
    GanttTaskPanel *panel = m_view->panel();
    m_view->togglePanel();
    QLineEdit *name = panel->nameField();
    for (const bool dark : {true, false, true}) {
        m_view->setDark(dark);
        const GanttColors colors = dark ? GanttColors::dark() : GanttColors::light();
        const QList<ToggleSwitch *> toggles = panel->findChildren<ToggleSwitch *>();
        QCOMPARE(toggles.size(), 2);
        for (ToggleSwitch *toggle : toggles) QCOMPARE(toggle->offTrack, colors.selected);
        for (ColorField *field : panel->findChildren<ColorField *>()) QCOMPARE(field->mixedFill, colors.card);
        name->ensurePolished();
        QCOMPARE(name->palette().color(QPalette::PlaceholderText), colors.muted);
        // Captions readable on the dark card, between text and muted (user's picture, 2026-10-30).
        QLabel *caption = nullptr;
        for (QLabel *label : panel->findChildren<QLabel *>())
            if (label->property("caption").toBool()) caption = label;
        QVERIFY(caption);
        caption->ensurePolished();
        QCOMPARE(caption->palette().color(QPalette::WindowText), dark ? QColor("#b9bcbf") : colors.muted);
        // The chart's bottom corners are rounded on the page (covered at the end of its paint).
        QWidget *chart = m_view->chart()->viewport();
        const QImage image = chart->grab().toImage();
        QCOMPARE(image.pixelColor(0, image.height() - 1), colors.page);
        QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1), colors.page);
    }
    m_view->setDark(false);
}

void GanttViewTest::barShape() {
    // FEAT-080: the Gantt page's shape changes every bar of the sheet, one step, saved in the sheet.
    setPlan({task("S", kMonday, 2), task("A", kMonday, 3, "S")});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    GanttTaskPanel *panel = m_view->panel();
    m_view->togglePanel();
    QVERIFY(panel->showsNoTask());
    panel->showPage(GanttTaskPanel::Page::Gantt);
    QCOMPARE(panel->barShapeField()->currentIndex(), 0);
    // Pictures as the map's Border select, the name in the tooltip, a grid menu of two.
    QComboBox *shapes = panel->barShapeField();
    QVERIFY(!shapes->itemIcon(1).isNull() && shapes->itemText(1).isEmpty());
    QCOMPARE(shapes->itemData(1, Qt::ToolTipRole).toString(), QString("Rounded rectangle"));
    shapes->showPopup();
    QWidget *grid = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "GridMenuPopup" && widget->isVisible()) grid = widget;
    QVERIFY(grid);
    QCOMPARE(grid->findChildren<QToolButton *>().size(), 8);
    grid->close();
    // 3 px inside a bar's corner: the page around a pill, the bar's colour in a 4 px corner.
    const auto corner = [chart](const QString &id) {
        const QRectF bar = chart->barRect(id);
        return chart->viewport()->grab().toImage().pixelColor(QPointF(bar.left() + 3, bar.top() + 3).toPoint());
    };
    const QColor peach(0xf9, 0xbd, 0x89);
    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) < 12 && std::abs(a.green() - b.green()) < 12 && std::abs(a.blue() - b.blue()) < 12;
    };
    QVERIFY(!near(corner("A"), peach));
    emit panel->barShapeField()->activated(1);
    QCOMPARE(m_document->model()->gantt().barShape(), GanttPlan::BarShape::Rounded);
    QVERIFY(near(corner("A"), peach));
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().barShape(), GanttPlan::BarShape::Pill);
    QCOMPARE(panel->barShapeField()->currentIndex(), 0);
    m_document->redo();
    QCOMPARE(panel->barShapeField()->currentIndex(), 1);
    // Written as "rounded", pill not written; missing or unknown values read as pill.
    QCOMPARE(m_document->model()->gantt().toJson().value("barShape").toString(), QString("rounded"));
    const QString file = m_dir.filePath("shape.mentis");
    QVERIFY(m_document->save(file, BmapStorage::SaveReason::Explicit).ok);
    m_document->close();  // its lock
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.model()->gantt().barShape(), GanttPlan::BarShape::Rounded);
    reopened.close();
    int repairs = 0;
    QCOMPARE(GanttPlan::fromJson(QJsonObject{{"barShape", "star"}}, &repairs).barShape(), GanttPlan::BarShape::Pill);
    QCOMPARE(repairs, 1);
    QVERIFY(!GanttPlan().toJson().contains("barShape"));
    // Rectangle, Arrow, Hexagon (user, 2026-11-01): saved by key; drawn by their corners; the
    // ring and the halo keep the outline's angles, inside their offset.
    using Shape = GanttPlan::BarShape;
    QCOMPARE(shapes->count(), 8);
    struct Expected { Shape shape; QString name, key; bool startCorner, endCorner; };
    for (const Expected &expected : {Expected{Shape::Rectangle, "Rectangle", "rectangle", true, true},
                                     Expected{Shape::Arrow, "Arrow", "arrow", true, false},
                                     Expected{Shape::Hexagon, "Hexagon", "hexagon", false, false},
                                     Expected{Shape::Leaf, "Leaf", "leaf", false, true},
                                     Expected{Shape::Ticket, "Ticket", "ticket", true, true},
                                     Expected{Shape::Parallelogram, "Parallelogram", "parallelogram", false, true}}) {
        GanttPlan plan;
        QVERIFY(plan.setBarShape(expected.shape));
        QCOMPARE(plan.toJson().value("barShape").toString(), expected.key);
        QCOMPARE(GanttPlan::fromJson(plan.toJson()).barShape(), expected.shape);
        const int index = shapes->findData(int(expected.shape));
        QCOMPARE(shapes->itemData(index, Qt::ToolTipRole).toString(), expected.name);
        const QRectF box(10, 10, 120, 24);
        for (const qreal grow : {0.0, 3.0}) {
            const QPainterPath path = GanttChart::taskPath(expected.shape, box, grow);
            QVERIFY(qAbs(path.boundingRect().left() - (box.left() - grow)) < 0.01 && qAbs(path.boundingRect().right() - (box.right() + grow)) < 0.01);
            QCOMPARE(path.contains(QPointF(box.left() - grow + 1, box.top() - grow + 1)), expected.startCorner);
            QCOMPARE(path.contains(QPointF(box.right() + grow - 1, box.top() - grow + 1)), expected.endCorner);
            QVERIFY(path.contains(box.center()));
            if (expected.shape == Shape::Ticket)  // the notches, smaller round the halo
                QVERIFY(!path.contains(QPointF(box.left() - grow + 1, box.center().y())) && !path.contains(QPointF(box.right() + grow - 1, box.center().y())));
        }
        QVERIFY(GanttChart::taskPath(expected.shape, QRectF(10, 10, 4, 24)).boundingRect().width() <= 4.01);  // a short bar
    }
}

void GanttViewTest::selectedSummaryArrow() {
    // BUG-026: the selected row's grey continues under the indentation and its arrow stays visible.
    setPlan({task("S", kMonday, 2), task("A", kMonday, 3, "S")});
    m_view->refresh();
    QTreeView *table = m_view->table();
    m_view->selectTask("S");
    QApplication::processEvents();
    const QModelIndex name = table->model()->index(0, 0);  // Name carries the tree
    const QRect row = table->visualRect(name);
    const int branchLeft = table->columnViewportPosition(0);
    QVERIFY(row.left() - branchLeft > 8);
    const QImage image = table->viewport()->grab().toImage();
    QCOMPARE(image.pixelColor(branchLeft + 2, row.top() + 2), QColor(0xe3, 0xe5, 0xe6));
    int dark = 0;  // the arrow's pixels
    for (int y = row.top(); y <= row.bottom(); ++y)
        for (int x = branchLeft; x < row.left(); ++x)
            if (image.pixelColor(x, y).lightness() < 0x90) ++dark;
    QVERIFY(dark > 3);
}

void GanttViewTest::summaryShape() {
    // FEAT-080: tasks with subtasks as the other bars or as a bracket; one step, saved in the sheet.
    // The bracket by default (user, 2026-10-30): the test starts from the bar.
    setPlan({task("S", kMonday, 2), task("A", kMonday, 3, "S")});
    QCOMPARE(m_document->model()->gantt().summaryShape(), GanttPlan::SummaryShape::Bracket);
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setSummaryShape(GanttPlan::SummaryShape::Bar); }));
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    GanttTaskPanel *panel = m_view->panel();
    m_view->togglePanel();
    panel->showPage(GanttTaskPanel::Page::Gantt);
    QComboBox *shapes = panel->summaryShapeField();
    QCOMPARE(shapes->currentIndex(), 0);
    QVERIFY(!shapes->itemIcon(1).isNull() && shapes->itemText(1).isEmpty());
    QCOMPARE(shapes->itemData(1, Qt::ToolTipRole).toString(), QString("Bracket"));
    // 5 px under the middle of the summary's place: inside a bar, under a bracket's band.
    // The commonest colour along the middle half of that line, not one pixel: a letter of the label
    // can sit there with another system's font (macOS CI).
    const auto below = [chart] {
        const QRectF bar = chart->barRect("S");
        const QImage image = chart->viewport()->grab().toImage();  // in device pixels
        QHash<QRgb, int> counts;
        for (qreal x = bar.left() + bar.width() / 4; x < bar.right() - bar.width() / 4; x += 1)
            ++counts[image.pixel((QPointF(x, bar.center().y() + 5) * image.devicePixelRatio()).toPoint())];
        QRgb commonest = 0;
        for (auto it = counts.cbegin(); it != counts.cend(); ++it)
            if (it.value() > counts.value(commonest)) commonest = it.key();
        return QColor(commonest);
    };
    const QColor lilac(0xaa, 0xaf, 0xfa);
    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) < 12 && std::abs(a.green() - b.green()) < 12 && std::abs(a.blue() - b.blue()) < 12;
    };
    QVERIFY2(near(below(), lilac), qPrintable(below().name()));
    emit shapes->activated(1);
    QCOMPARE(m_document->model()->gantt().summaryShape(), GanttPlan::SummaryShape::Bracket);
    QVERIFY(!near(below(), lilac));
    // The tips hang under the band at both ends.
    const QRectF bar = chart->barRect("S");
    const QImage image = chart->viewport()->grab().toImage();
    QVERIFY(near(image.pixelColor(QPointF(bar.left() + 2, bar.center().y() + 4).toPoint()), lilac));
    QVERIFY(near(image.pixelColor(QPointF(bar.right() - 3, bar.center().y() + 4).toPoint()), lilac));
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().summaryShape(), GanttPlan::SummaryShape::Bar);
    QCOMPARE(shapes->currentIndex(), 0);
    m_document->redo();
    QCOMPARE(shapes->currentIndex(), 1);
    QVERIFY(!m_document->model()->gantt().toJson().contains("summaryShape"));  // the default, not written
    int repairs = 0;
    const GanttPlan plan = GanttPlan::fromJson(QJsonObject{{"summaryShape", "zigzag"}}, &repairs);
    QCOMPARE(plan.summaryShape(), GanttPlan::SummaryShape::Bracket);
    QCOMPARE(repairs, 1);
    QVERIFY(!GanttPlan().toJson().contains("summaryShape"));
    GanttPlan barPlan;
    QVERIFY(barPlan.setSummaryShape(GanttPlan::SummaryShape::Bar));
    QCOMPARE(barPlan.toJson().value("summaryShape").toString(), QString("bar"));
    QCOMPARE(GanttPlan::fromJson(barPlan.toJson()).summaryShape(), GanttPlan::SummaryShape::Bar);
    // The other thin shapes (user, 2026-11-01): in the menu's grid of four, saved by key, drawn in
    // the bracket's place (the bar's colour on the centre line or at the top) and inside their days
    // even when short.
    using Shape = GanttPlan::SummaryShape;
    const QList<std::tuple<Shape, QString, QString>> others{
        {Shape::Brace, "Brace", "brace"},       {Shape::IBeam, "Horizontal I", "ibeam"}, {Shape::Hook, "Hook", "hook"},
        {Shape::Dimension, "Dimension line", "dimension"}, {Shape::Arrow, "Arrow", "arrow"}, {Shape::Rail, "Pinned rail", "rail"}};
    QCOMPARE(shapes->count(), 9);
    shapes->showPopup();
    QWidget *grid = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "GridMenuPopup" && widget->isVisible()) grid = widget;
    QVERIFY(grid);
    const QList<QToolButton *> tiles = grid->findChildren<QToolButton *>();
    QCOMPARE(tiles.size(), 9);
    QCOMPARE(tiles.at(4)->y(), tiles.at(0)->y() + tiles.at(0)->height() + 4);  // four columns
    grid->close();
    for (const auto &[shape, name, key] : others) {
        const int index = shapes->findData(int(shape));
        QVERIFY(index >= 2);
        QCOMPARE(shapes->itemData(index, Qt::ToolTipRole).toString(), name);
        emit shapes->activated(index);
        QCOMPARE(m_document->model()->gantt().summaryShape(), shape);
        QCOMPARE(m_document->model()->gantt().toJson().value("summaryShape").toString(), key);
        QCOMPARE(GanttPlan::fromJson(m_document->model()->gantt().toJson()).summaryShape(), shape);
        const QRectF bar = chart->barRect("S");
        const QImage image = chart->viewport()->grab().toImage();
        const bool atTop = shape == Shape::Brace || shape == Shape::Hook;
        const QPointF probe(atTop ? bar.left() + 1.5 : bar.center().x(), atTop ? bar.center().y() + 4 : bar.center().y());
        QVERIFY2(near(image.pixelColor(probe.toPoint()), lilac), qPrintable(name));
        for (const double width : {4.0, 12.0, 200.0}) {
            const QRectF place(10, 10, width, 16);
            QVERIFY2(place.adjusted(-0.01, -0.01, 0.01, 0.01).contains(GanttChart::summaryPath(shape, place).boundingRect()), qPrintable(name));
        }
    }
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().summaryShape(), Shape::Arrow);
    QCOMPARE(shapes->currentIndex(), shapes->findData(int(Shape::Arrow)));
}

void GanttViewTest::summaryFrame() {
    // FEAT-080 (user, 2026-11-01): the group frame, a header on the summary's row over a tinted frame
    // round its subtasks; every summary level inside widens it by 3 px on each side.
    setPlan({task("S", kMonday, 2), task("N", kMonday, 2, "S"), task("N1", kMonday, 2, "N"), task("A", kMonday, 1, "S")});
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setSummaryShape(GanttPlan::SummaryShape::Frame); }));
    m_view->refresh();
    QCOMPARE(m_document->model()->gantt().toJson().value("summaryShape").toString(), QString("frame"));
    GanttChart *chart = m_view->chart();
    const QImage image = chart->viewport()->grab().toImage();
    const double left = chart->barRect("S").left();
    QCOMPARE(chart->barRect("N1").left(), left);
    const auto at = [&image](double x, const QRectF &row) { return image.pixelColor(QPointF(x, row.center().y()).toPoint()); };
    const QColor lilac(0xaa, 0xaf, 0xfa);
    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) < 12 && std::abs(a.green() - b.green()) < 12 && std::abs(a.blue() - b.blue()) < 12;
    };
    const QRectF s = chart->barRect("S"), n = chart->barRect("N"), a = chart->barRect("A");
    QVERIFY(near(at(left - 8, s), lilac));    // S's header: 6 + 3 px out (N inside)
    QVERIFY(near(at(left - 5, n), lilac));    // N's header: 6 px out
    QVERIFY(!near(at(left - 8, n), lilac));   // beside N's header: S's frame
    QVERIFY(at(left - 8, n) != at(left - 14, n));  // tinted, unlike the day outside every frame
    QVERIFY(at(left - 8, a) != at(left - 14, a));  // A, the last subtask, still in S's frame
    QVERIFY(!near(at(left - 5, a), lilac));   // N's frame ended above A
    QVERIFY(chart->barRect("N1").bottom() < chart->barRect("A").top());
    // The header fills S's row from the bars' top (user, 2026-11-01): lilac just above the row's line.
    QVERIFY(near(image.pixelColor(QPointF(s.center().x(), s.bottom() + 2).toPoint()), lilac));
    // "Name on the bar": the name inside the header, nothing after it.
    const QRectF header(left - 9, s.top(), s.width() + 18, s.height());
    const auto darkIn = [chart](const QRectF &box) {
        const QImage shot = chart->viewport()->grab().toImage();
        int dark = 0;
        for (int y = int(box.top()); y < int(box.bottom()); ++y)
            for (int x = int(box.left()); x < int(box.right()); ++x) dark += qGray(shot.pixel(x, y)) < 110;
        return dark;
    };
    const QRectF after(header.right() + 4, s.top(), 60, s.height());
    chart->setLabelled({});
    const int insideBefore = darkIn(header.adjusted(10, 2, -10, -2)), afterBefore = darkIn(after);
    chart->setLabelled({"S"});
    QVERIFY(darkIn(header.adjusted(10, 2, -10, -2)) > insideBefore);
    QCOMPARE(darkIn(after), afterBefore);
}

void GanttViewTest::relationStyle() {
    // FEAT-080: the line pattern and end mark of every arrow of the sheet (user, 2026-10-29).
    GanttDependency late = dependency("A", "B");
    late.lag = 3;  // a long last segment on B's row, for the pattern, still in view
    setPlan({task("A", kMonday, 1), task("B", kMonday, 1)}, {late});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    chart->setCriticalShown(false);
    GanttTaskPanel *panel = m_view->panel();
    m_view->togglePanel();
    panel->showPage(GanttTaskPanel::Page::Gantt);
    QComboBox *lines = panel->linkLineField();
    QComboBox *ends = panel->linkEndField();
    // A section per kind (user, 2026-10-29): Tasks and Summaries with shape and colour, then Relations.
    const auto sectionHas = [panel](const char *name, const QString &title, const QList<QWidget *> &fields) {
        const QWidget *box = panel->findChild<QWidget *>(name);
        if (!box || box->findChild<QLabel *>("SectionTitle")->text() != title) return false;
        return std::all_of(fields.cbegin(), fields.cend(), [box](QWidget *field) { return box->isAncestorOf(field); });
    };
    QVERIFY(sectionHas("GanttSectionTasks", "Tasks", {panel->barShapeField(), panel->defaultColorField(GanttPlan::BarKind::Task)}));
    QVERIFY(sectionHas("GanttSectionSummaries", "Summaries", {panel->summaryShapeField(), panel->defaultColorField(GanttPlan::BarKind::Summary)}));
    QVERIFY(sectionHas("GanttSectionRelations", "Relations", {lines, ends}));
    QCOMPARE(lines->count(), 3);
    QCOMPARE(ends->count(), 6);
    QCOMPARE(lines->currentData().toInt(), int(GanttPlan::LinkLine::Solid));
    QCOMPARE(ends->currentData().toInt(), int(BranchEnd::FilledArrow));
    QVERIFY(!ends->itemIcon(0).isNull() && ends->itemText(0).isEmpty());
    QCOMPARE(lines->itemData(1, Qt::ToolTipRole).toString(), QString("Dashed line"));
    QCOMPARE(ends->itemData(3, Qt::ToolTipRole).toString(), QString("Dot"));

    const QPolygonF path = chart->arrowPath(0);
    const QPointF tip = path.last();
    QCOMPARE(path.at(path.size() - 2).y(), tip.y());  // the last segment is horizontal
    QVERIFY(tip.x() < chart->viewport()->width() - 2 && tip.x() - path.at(path.size() - 2).x() > 40);
    const int y = qRound(tip.y());
    const auto same = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) < 8 && std::abs(a.green() - b.green()) < 8 && std::abs(a.blue() - b.blue()) < 8;
    };
    // Columns of the last segment where the line leaves the backdrop (6 px above, same column) visible.
    const auto gaps = [&] {
        const QImage image = chart->viewport()->grab().toImage();
        int count = 0;
        for (int x = qRound(path.at(path.size() - 2).x()) + 2; x < qRound(tip.x()) - 10; ++x)
            if (same(image.pixelColor(x, y), image.pixelColor(x, y - 6))) ++count;
        return count;
    };
    // 4 px behind the tip, 2.5 px off the line: inside a filled arrow, square or dot; empty without a mark.
    const auto markShown = [&] {
        const QImage image = chart->viewport()->grab().toImage();
        const QPoint at = QPointF(tip.x() - 4, tip.y() - 2.5).toPoint();
        return !same(image.pixelColor(at), image.pixelColor(at.x(), at.y() - 6));
    };
    QCOMPARE(gaps(), 0);
    QVERIFY(markShown());

    emit lines->activated(lines->findData(int(GanttPlan::LinkLine::Dashed)));
    QCOMPARE(m_document->model()->gantt().linkLine(), GanttPlan::LinkLine::Dashed);
    const int dashed = gaps();
    QVERIFY(dashed > 0);
    emit lines->activated(lines->findData(int(GanttPlan::LinkLine::Dotted)));
    QVERIFY(gaps() > dashed);

    emit ends->activated(ends->findData(int(BranchEnd::None)));
    QCOMPARE(m_document->model()->gantt().linkEnd(), BranchEnd::None);
    QVERIFY(!markShown());
    emit ends->activated(ends->findData(int(BranchEnd::Square)));
    QVERIFY(markShown());

    m_document->undo();  // back to no mark, one step
    QCOMPARE(m_document->model()->gantt().linkEnd(), BranchEnd::None);
    QCOMPARE(ends->currentData().toInt(), int(BranchEnd::None));
    m_document->redo();
    QCOMPARE(ends->currentData().toInt(), int(BranchEnd::Square));
    const QJsonObject json = m_document->model()->gantt().toJson();
    QCOMPARE(json.value("linkLine").toString(), QString("dotted"));
    QCOMPARE(json.value("linkEnd").toString(), QString("square"));
    const GanttPlan reopened = GanttPlan::fromJson(json);
    QCOMPARE(reopened.linkLine(), GanttPlan::LinkLine::Dotted);
    QCOMPARE(reopened.linkEnd(), BranchEnd::Square);
    QVERIFY(!GanttPlan().toJson().contains("linkLine") && !GanttPlan().toJson().contains("linkEnd"));
    int repairs = 0;
    const GanttPlan unknown = GanttPlan::fromJson(QJsonObject{{"linkLine", "wavy"}, {"linkEnd", "star"}}, &repairs);
    QCOMPARE(unknown.linkLine(), GanttPlan::LinkLine::Solid);
    QCOMPARE(unknown.linkEnd(), BranchEnd::FilledArrow);
    QCOMPARE(repairs, 2);

    // Thickness and end size (user, 2026-11-01), on a solid line with the filled arrow.
    QComboBox *widths = panel->linkWidthField();
    QComboBox *sizes = panel->linkEndSizeField();
    QVERIFY(sectionHas("GanttSectionRelations", "Relations", {widths, sizes}));
    QCOMPARE(widths->count(), 4);
    QCOMPARE(sizes->count(), 3);
    QCOMPARE(widths->currentData().toInt(), int(GanttPlan::LinkWidth::Thin));
    QCOMPARE(sizes->currentData().toInt(), int(GanttPlan::LinkEndSize::Small));
    QVERIFY(!widths->itemIcon(0).isNull() && widths->itemText(0).isEmpty());
    QCOMPARE(widths->itemData(3, Qt::ToolTipRole).toString(), QString("Extra bold"));
    QCOMPARE(sizes->itemData(2, Qt::ToolTipRole).toString(), QString("Large"));
    emit lines->activated(lines->findData(int(GanttPlan::LinkLine::Solid)));
    emit ends->activated(ends->findData(int(BranchEnd::FilledArrow)));
    // Rows of the line's column in the middle of the last segment that differ from the backdrop.
    const auto thickness = [&] {
        const QImage image = chart->viewport()->grab().toImage();
        const int x = qRound((path.at(path.size() - 2).x() + tip.x()) / 2);
        int rows = 0;
        for (int dy = -5; dy <= 5; ++dy)
            if (!same(image.pixelColor(x, y + dy), image.pixelColor(x, y - 9))) ++rows;
        return rows;
    };
    // 10 px behind the tip, 5 px off the line: inside the large arrow only.
    const auto largeMark = [&] {
        const QImage image = chart->viewport()->grab().toImage();
        const QPoint at = QPointF(tip.x() - 10, tip.y() - 5).toPoint();
        return !same(image.pixelColor(at), image.pixelColor(at.x(), at.y() - 6));
    };
    const int thin = thickness();
    QVERIFY(!largeMark());
    emit widths->activated(widths->findData(int(GanttPlan::LinkWidth::ExtraBold)));
    QCOMPARE(m_document->model()->gantt().linkWidth(), GanttPlan::LinkWidth::ExtraBold);
    QVERIFY(thickness() >= thin + 2);
    emit sizes->activated(sizes->findData(int(GanttPlan::LinkEndSize::Large)));
    QCOMPARE(m_document->model()->gantt().linkEndSize(), GanttPlan::LinkEndSize::Large);
    QVERIFY(largeMark());
    QVERIFY(chart->arrowPath(0).last() == tip);
    m_document->undo();  // back to the small end, one step
    QCOMPARE(sizes->currentData().toInt(), int(GanttPlan::LinkEndSize::Small));
    QVERIFY(!largeMark());
    m_document->redo();
    QCOMPARE(sizes->currentData().toInt(), int(GanttPlan::LinkEndSize::Large));
    const QJsonObject sized = m_document->model()->gantt().toJson();
    QCOMPARE(sized.value("linkWidth").toString(), QString("extraBold"));
    QCOMPARE(sized.value("linkEndSize").toString(), QString("large"));
    const GanttPlan reread = GanttPlan::fromJson(sized);
    QCOMPARE(reread.linkWidth(), GanttPlan::LinkWidth::ExtraBold);
    QCOMPARE(reread.linkEndSize(), GanttPlan::LinkEndSize::Large);
    QVERIFY(!GanttPlan().toJson().contains("linkWidth") && !GanttPlan().toJson().contains("linkEndSize"));
    repairs = 0;
    const GanttPlan odd = GanttPlan::fromJson(QJsonObject{{"linkWidth", "huge"}, {"linkEndSize", 3}}, &repairs);
    QCOMPARE(odd.linkWidth(), GanttPlan::LinkWidth::Thin);
    QCOMPARE(odd.linkEndSize(), GanttPlan::LinkEndSize::Small);
    QCOMPARE(repairs, 2);
}

void GanttViewTest::milestoneStyle() {
    // FEAT-080: the milestones' section, shape and own colour (user, 2026-10-29).
    setPlan({task("A", kMonday, 3), task("M", kMonday, 0)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    chart->setCriticalShown(false);
    GanttTaskPanel *panel = m_view->panel();
    m_view->togglePanel();
    panel->showPage(GanttTaskPanel::Page::Gantt);
    QComboBox *shapes = panel->milestoneShapeField();
    ColorField *colour = panel->defaultColorField(GanttPlan::BarKind::Milestone);
    const QWidget *box = panel->findChild<QWidget *>("GanttSectionMilestones");
    QVERIFY(box && box->isAncestorOf(shapes) && box->isAncestorOf(colour));
    QCOMPARE(box->findChild<QLabel *>("SectionTitle")->text(), QString("Milestones"));
    QCOMPARE(shapes->count(), 8);
    QCOMPARE(shapes->currentData().toInt(), int(GanttPlan::MilestoneShape::Diamond));
    QVERIFY(!shapes->itemIcon(2).isNull() && shapes->itemText(2).isEmpty());
    QCOMPARE(shapes->itemData(2, Qt::ToolTipRole).toString(), QString("Triangle"));

    const auto near = [](const QColor &a, const QColor &b) {
        return std::abs(a.red() - b.red()) < 12 && std::abs(a.green() - b.green()) < 12 && std::abs(a.blue() - b.blue()) < 12;
    };
    const auto pixel = [chart](const QPointF &offset) {
        return chart->viewport()->grab().toImage().pixelColor((chart->barRect("M").center() + offset).toPoint());
    };
    const QColor slate(0x64, 0x74, 0x8b), peach(0xf9, 0xbd, 0x89), red(0xef, 0x44, 0x44);
    QVERIFY(near(pixel({}), slate));  // the milestones' own colour, not the tasks' peach
    QVERIFY(colour->color() == slate && !colour->isManual());
    // 5 px left and up from the centre: outside the diamond, inside the triangle.
    QVERIFY(!near(pixel({-5, -5}), slate));
    emit shapes->activated(shapes->findData(int(GanttPlan::MilestoneShape::Triangle)));
    QCOMPARE(m_document->model()->gantt().milestoneShape(), GanttPlan::MilestoneShape::Triangle);
    QVERIFY(near(pixel({-5, -5}), slate));

    emit colour->colorChosen(red);
    QCOMPARE(m_document->model()->gantt().defaultColor(GanttPlan::BarKind::Milestone), red);
    QVERIFY(near(pixel({}), red));
    const QRectF bar = chart->barRect("A");
    QVERIFY(near(chart->viewport()->grab().toImage().pixelColor(QPointF(bar.right() - 12, bar.center().y()).toPoint()), peach));
    QVERIFY(!panel->defaultColorReset(GanttPlan::BarKind::Milestone)->isHidden());

    m_document->undo();  // one step: back to slate
    QVERIFY(near(pixel({}), slate));
    m_document->redo();
    const QJsonObject json = m_document->model()->gantt().toJson();
    QCOMPARE(json.value("milestoneShape").toString(), QString("triangle"));
    QCOMPARE(json.value("milestoneColor").toString(), red.name());
    const GanttPlan reopened = GanttPlan::fromJson(json);
    QCOMPARE(reopened.milestoneShape(), GanttPlan::MilestoneShape::Triangle);
    QCOMPARE(reopened.defaultColor(GanttPlan::BarKind::Milestone), red);
    QVERIFY(!GanttPlan().toJson().contains("milestoneShape") && !GanttPlan().toJson().contains("milestoneColor"));
    int repairs = 0;
    const GanttPlan unknown = GanttPlan::fromJson(QJsonObject{{"milestoneShape", "blob"}, {"milestoneColor", "nope"}}, &repairs);
    QCOMPARE(unknown.milestoneShape(), GanttPlan::MilestoneShape::Diamond);
    QVERIFY(!unknown.defaultColor(GanttPlan::BarKind::Milestone).isValid());
    QCOMPARE(repairs, 2);
}

void GanttViewTest::arrowKeys() {
    // Up/Down move between the tasks from the chart and the panel too; the open panel follows (user, 2026-10-29).
    setPlan({task("A", kMonday, 2), task("B", kMonday, 3), task("C", kMonday, 1)});
    m_view->refresh();
    GanttTaskPanel *panel = m_view->panel();
    m_view->selectTask("A");
    m_view->togglePanel();
    QCOMPARE(panel->taskId(), QString("A"));

    QTest::keyClick(m_view->chart(), Qt::Key_Down);  // the chart does not scroll: it moves
    QCOMPARE(m_view->selectedIds(), QStringList({"B"}));
    QCOMPARE(panel->taskId(), QString("B"));

    QComboBox *priority = panel->findChild<QComboBox *>();  // the first select of the panel
    QVERIFY(priority);
    const int value = priority->currentIndex();
    priority->setFocus();
    QTest::keyClick(priority, Qt::Key_Down);  // a select does not change value
    QCOMPARE(panel->taskId(), QString("C"));
    QCOMPARE(m_view->selectedIds(), QStringList({"C"}));
    QCOMPARE(priority->currentIndex(), value);

    panel->nameField()->setFocus();
    QTest::keyClick(panel->nameField(), Qt::Key_Up);
    QCOMPARE(panel->taskId(), QString("B"));

    // A number field keeps Up/Down for its value.
    NumberField *duration = panel->durationField();
    const QString before = duration->text();
    duration->setFocus();
    QTest::keyClick(duration, Qt::Key_Up);
    QCOMPARE(panel->taskId(), QString("B"));
    QVERIFY(duration->text() != before);
}

void GanttViewTest::borderStyle() {
    // FEAT-080: the border of the task bars and of the milestones: solid, dashed, dotted or none (user, 2026-10-29).
    setPlan({task("A", kMonday, 4), task("M", kMonday, 0)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    chart->setCriticalShown(false);
    GanttTaskPanel *panel = m_view->panel();
    m_view->togglePanel();
    panel->showPage(GanttTaskPanel::Page::Gantt);
    QComboBox *tasks = panel->borderStyleField(GanttPlan::BarKind::Task);
    QComboBox *milestones = panel->borderStyleField(GanttPlan::BarKind::Milestone);
    QComboBox *summaries = panel->borderStyleField(GanttPlan::BarKind::Summary);
    QVERIFY(tasks && milestones && summaries);
    QVERIFY(panel->findChild<QWidget *>("GanttSectionSummaries")->isAncestorOf(summaries));
    QVERIFY(panel->findChild<QWidget *>("GanttSectionTasks")->isAncestorOf(tasks));
    QVERIFY(panel->findChild<QWidget *>("GanttSectionMilestones")->isAncestorOf(milestones));
    // The map's line selects' order (user, 2026-10-29): each line beside its hand-drawn one, None last.
    using Border = GanttPlan::BorderStyle;
    QList<int> order;
    for (int i = 0; i < tasks->count(); ++i) order.append(tasks->itemData(i).toInt());
    QCOMPARE(order, QList<int>({int(Border::Solid), int(Border::HandDrawnSolid), int(Border::Dashed), int(Border::HandDrawnDashed),
                                int(Border::Dotted), int(Border::None)}));
    QCOMPARE(tasks->currentData().toInt(), int(GanttPlan::BorderStyle::Solid));
    QVERIFY(!tasks->itemIcon(2).isNull() && tasks->itemText(2).isEmpty());
    QCOMPARE(tasks->itemData(2, Qt::ToolTipRole).toString(), QString("Dashed line"));
    QCOMPARE(tasks->itemData(1, Qt::ToolTipRole).toString(), QString("Hand-drawn solid"));
    // Its grid menu: two columns, None a wide tile at the bottom, as the map's.
    tasks->showPopup();
    QWidget *grid = nullptr;
    for (QWidget *widget : QApplication::topLevelWidgets())
        if (widget->objectName() == "GridMenuPopup" && widget->isVisible()) grid = widget;
    QVERIFY(grid);
    const QList<QToolButton *> tiles = grid->findChildren<QToolButton *>();
    QCOMPARE(tiles.size(), 6);
    int widest = 0;
    for (QToolButton *tile : tiles) widest = std::max(widest, tile->width());
    QCOMPARE(int(std::count_if(tiles.cbegin(), tiles.cend(), [widest](QToolButton *tile) { return tile->width() == widest; })), 1);
    grid->close();

    // Columns of the bar's straight top edge where the backdrop 4 px above shows through.
    const auto gaps = [chart] {
        const QRectF bar = chart->barRect("A");
        const QImage image = chart->viewport()->grab().toImage();
        const int y = int(std::floor(bar.top())) - 1;  // the outer half of the border, over the backdrop
        int count = 0;
        for (int x = int(bar.left()) + 16; x < int(bar.right()) - 16; ++x) {
            const QColor a = image.pixelColor(x, y), b = image.pixelColor(x, y - 4);
            if (std::abs(a.red() - b.red()) < 8 && std::abs(a.green() - b.green()) < 8 && std::abs(a.blue() - b.blue()) < 8) ++count;
        }
        return count;
    };
    const int width = int(chart->barRect("A").right()) - 16 - (int(chart->barRect("A").left()) + 16);  // the columns looked at
    QVERIFY(width > 30);
    QCOMPARE(gaps(), 0);
    emit tasks->activated(tasks->findData(int(GanttPlan::BorderStyle::Dashed)));
    QCOMPARE(m_document->model()->gantt().borderStyle(GanttPlan::BarKind::Task), GanttPlan::BorderStyle::Dashed);
    const int dashed = gaps();
    QVERIFY(dashed > 0 && dashed < width);
    emit tasks->activated(tasks->findData(int(GanttPlan::BorderStyle::Dotted)));
    const int dotted = gaps();
    QVERIFY(dotted > dashed);
    emit tasks->activated(tasks->findData(int(GanttPlan::BorderStyle::None)));
    QVERIFY(gaps() > dotted && gaps() >= width * 9 / 10);  // nearly all: a few columns differ 4 px apart even without a border
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().borderStyle(GanttPlan::BarKind::Task), GanttPlan::BorderStyle::Dotted);
    QCOMPARE(tasks->currentData().toInt(), int(GanttPlan::BorderStyle::Dotted));
    // Hand-drawn: the map's rough outline, not the straight edge, and the same at every repaint.
    emit tasks->activated(tasks->findData(int(Border::Solid)));
    const QImage straight = chart->viewport()->grab().toImage();
    emit tasks->activated(tasks->findData(int(Border::HandDrawnSolid)));
    QCOMPARE(m_document->model()->gantt().borderStyle(GanttPlan::BarKind::Task), Border::HandDrawnSolid);
    const QImage rough = chart->viewport()->grab().toImage();
    QVERIFY(rough != straight);
    chart->viewport()->update();
    QCOMPARE(chart->viewport()->grab().toImage(), rough);
    QCOMPARE(m_document->model()->gantt().toJson().value("taskBorder").toString(), QString("handDrawnSolid"));
    emit tasks->activated(tasks->findData(int(Border::HandDrawnDashed)));
    QCOMPARE(GanttPlan::fromJson(m_document->model()->gantt().toJson()).borderStyle(GanttPlan::BarKind::Task), Border::HandDrawnDashed);
    emit tasks->activated(tasks->findData(int(Border::Dotted)));

    emit milestones->activated(milestones->findData(int(GanttPlan::BorderStyle::None)));
    QCOMPARE(m_document->model()->gantt().borderStyle(GanttPlan::BarKind::Milestone), GanttPlan::BorderStyle::None);
    QCOMPARE(m_document->model()->gantt().borderStyle(GanttPlan::BarKind::Task), GanttPlan::BorderStyle::Dotted);
    const QJsonObject json = m_document->model()->gantt().toJson();
    QCOMPARE(json.value("taskBorder").toString(), QString("dotted"));
    QCOMPARE(json.value("milestoneBorder").toString(), QString("none"));
    const GanttPlan reopened = GanttPlan::fromJson(json);
    QCOMPARE(reopened.borderStyle(GanttPlan::BarKind::Milestone), GanttPlan::BorderStyle::None);
    QVERIFY(!GanttPlan().toJson().contains("taskBorder") && !GanttPlan().toJson().contains("milestoneBorder"));
    // Summaries (user, 2026-10-29): their own row, as a bar or a bracket.
    emit summaries->activated(summaries->findData(int(GanttPlan::BorderStyle::Dashed)));
    QCOMPARE(m_document->model()->gantt().borderStyle(GanttPlan::BarKind::Summary), GanttPlan::BorderStyle::Dashed);
    QCOMPARE(m_document->model()->gantt().toJson().value("summaryBorder").toString(), QString("dashed"));
    QCOMPARE(GanttPlan::fromJson(m_document->model()->gantt().toJson()).borderStyle(GanttPlan::BarKind::Summary),
             GanttPlan::BorderStyle::Dashed);
    int repairs = 0;
    const GanttPlan unknown = GanttPlan::fromJson(QJsonObject{{"taskBorder", "wavy"}}, &repairs);
    QCOMPARE(unknown.borderStyle(GanttPlan::BarKind::Task), GanttPlan::BorderStyle::Solid);
    QCOMPARE(repairs, 1);
}

void GanttViewTest::fillStyle() {
    // FEAT-080: the fill of each kind of bar, the map topics' fill styles (user, 2026-10-29).
    setPlan({task("A", kMonday, 6), task("M", kMonday, 0)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    chart->setCriticalShown(false);
    GanttTaskPanel *panel = m_view->panel();
    m_view->togglePanel();
    panel->showPage(GanttTaskPanel::Page::Gantt);
    QComboBox *tasks = panel->fillStyleField(GanttPlan::BarKind::Task);
    for (const auto kind : {GanttPlan::BarKind::Task, GanttPlan::BarKind::Summary, GanttPlan::BarKind::Milestone})
        QVERIFY(panel->fillStyleField(kind));
    QVERIFY(panel->findChild<QWidget *>("GanttSectionTasks")->isAncestorOf(tasks));
    QCOMPARE(tasks->count(), 7);
    QCOMPARE(tasks->currentData().toInt(), int(NodeFillStyle::Pieno));
    QVERIFY(!tasks->itemIcon(1).isNull() && tasks->itemText(1).isEmpty());
    QCOMPARE(tasks->itemData(1, Qt::ToolTipRole).toString(), QString("Hand-drawn fill"));

    // Peach pixels along the middle row of the bar, away from its ends.
    const QColor peach(0xf9, 0xbd, 0x89);
    const auto peachColumns = [chart, &peach] {
        const QRectF bar = chart->barRect("A");
        const QImage image = chart->viewport()->grab().toImage();
        const int y = int(bar.center().y()) + 3;
        int count = 0;
        for (int x = int(bar.left()) + 16; x < int(bar.right()) - 16; ++x) {
            const QColor c = image.pixelColor(x, y);
            if (std::abs(c.red() - peach.red()) < 16 && std::abs(c.green() - peach.green()) < 16 && std::abs(c.blue() - peach.blue()) < 16) ++count;
        }
        return count;
    };
    const int width = int(chart->barRect("A").right()) - 16 - (int(chart->barRect("A").left()) + 16);
    QVERIFY(width > 30);
    QVERIFY(peachColumns() >= width * 9 / 10);  // the name on the bar covers a few
    emit tasks->activated(tasks->findData(int(NodeFillStyle::Nessuno)));
    QCOMPARE(m_document->model()->gantt().fillStyle(GanttPlan::BarKind::Task), NodeFillStyle::Nessuno);
    QCOMPARE(peachColumns(), 0);
    emit tasks->activated(tasks->findData(int(NodeFillStyle::PennellateDiagonali)));
    const int strokes = peachColumns();
    QVERIFY2(strokes > 0 && strokes < width, qPrintable(QString::number(strokes)));  // lines with gaps
    emit tasks->activated(tasks->findData(int(NodeFillStyle::PienoManoLibera)));
    QVERIFY(peachColumns() >= width * 9 / 10);
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().fillStyle(GanttPlan::BarKind::Task), NodeFillStyle::PennellateDiagonali);
    QCOMPARE(tasks->currentData().toInt(), int(NodeFillStyle::PennellateDiagonali));

    // Milestones on their own; JSON with the map's fill keys, solid not written; unknown = solid.
    emit panel->fillStyleField(GanttPlan::BarKind::Milestone)->activated(
        panel->fillStyleField(GanttPlan::BarKind::Milestone)->findData(int(NodeFillStyle::TratteggioOrizzontale)));
    QCOMPARE(m_document->model()->gantt().fillStyle(GanttPlan::BarKind::Milestone), NodeFillStyle::TratteggioOrizzontale);
    QCOMPARE(m_document->model()->gantt().fillStyle(GanttPlan::BarKind::Task), NodeFillStyle::PennellateDiagonali);
    const QJsonObject json = m_document->model()->gantt().toJson();
    QCOMPARE(json.value("taskFill").toString(), NodeStyle::fillStyleKey(NodeFillStyle::PennellateDiagonali));
    QCOMPARE(json.value("milestoneFill").toString(), NodeStyle::fillStyleKey(NodeFillStyle::TratteggioOrizzontale));
    QVERIFY(!json.contains("summaryFill") && !GanttPlan().toJson().contains("taskFill"));
    QCOMPARE(GanttPlan::fromJson(json).fillStyle(GanttPlan::BarKind::Milestone), NodeFillStyle::TratteggioOrizzontale);
    int repairs = 0;
    const GanttPlan unknown = GanttPlan::fromJson(QJsonObject{{"summaryFill", "plaid"}}, &repairs);
    QCOMPARE(unknown.fillStyle(GanttPlan::BarKind::Summary), NodeFillStyle::Pieno);
    QCOMPARE(repairs, 1);
}

void GanttViewTest::taskShape() {
    // FEAT-080 (user, 2026-11-01): a task's own shape, as its colour, from the Task page's Style
    // section; the sheet's shape changes the others only; × back to the sheet's.
    GanttTask milestone = task("M", kMonday, 1, "S");
    milestone.milestone = true;
    setPlan({task("S", kMonday, 2), task("A", kMonday, 3, "S"), task("B", kMonday, 2, "S"), milestone});
    const auto plan = [this] { return m_document->model()->gantt(); };
    const auto barOf = [&plan](const QString &id) { return plan().barShape(*plan().task(id)); };
    m_view->openPanel("A");
    GanttTaskPanel *panel = m_view->panel();
    QVERIFY(panel->styleSection()->isVisible());
    QVERIFY(panel->ownBarShapeField()->isVisible() && !panel->ownSummaryShapeField()->isVisible());
    QCOMPARE(panel->ownBarShapeField()->count(), 8);
    QCOMPARE(panel->ownBarShapeField()->currentData().toInt(), int(GanttPlan::BarShape::Pill));
    QVERIFY(!panel->defaultShapeButton()->isVisible());
    emit panel->ownBarShapeField()->activated(panel->ownBarShapeField()->findData(int(GanttPlan::BarShape::Arrow)));
    QCOMPARE(plan().task("A")->shape, QString("arrow"));
    QCOMPARE(barOf("A"), GanttPlan::BarShape::Arrow);
    QVERIFY(panel->defaultShapeButton()->isVisible());
    // Drawn as an arrow: the end's top corner is not the bar's colour, B's is (a pill's middle).
    GanttChart *chart = m_view->chart();
    const QImage image = chart->viewport()->grab().toImage();
    const QRectF a = chart->barRect("A");
    const QColor peach(0xf9, 0xbd, 0x89);
    const auto near = [](const QColor &x, const QColor &y) {
        return std::abs(x.red() - y.red()) < 12 && std::abs(x.green() - y.green()) < 12 && std::abs(x.blue() - y.blue()) < 12;
    };
    QVERIFY(!near(image.pixelColor(QPointF(a.right() - 2, a.top() + 3).toPoint()), peach));
    QVERIFY(near(image.pixelColor(QPointF(a.right() - 12, a.center().y()).toPoint()), peach));
    // The sheet's shape: B follows, A keeps its own.
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setBarShape(GanttPlan::BarShape::Rectangle); }));
    QCOMPARE(barOf("A"), GanttPlan::BarShape::Arrow);
    QCOMPARE(barOf("B"), GanttPlan::BarShape::Rectangle);
    // Saved with the task; an unknown key is dropped.
    const QJsonObject json = plan().toJson();
    QCOMPARE(GanttPlan::fromJson(json).task("A")->shape, QString("arrow"));
    QVERIFY(GanttPlan::fromJson(json).task("B")->shape.isEmpty());
    int repairs = 0;
    const GanttPlan unknown = GanttPlan::fromJson(
        QJsonObject{{"tasks", QJsonArray{QJsonObject{{"id", "X"}, {"name", "X"}, {"duration", 1}, {"shape", "zigzag"}}}}}, &repairs);
    QVERIFY(unknown.task("X")->shape.isEmpty());
    QCOMPARE(repairs, 1);
    // × back to the sheet's; one step each.
    emit panel->defaultShapeButton()->clicked();
    QCOMPARE(barOf("A"), GanttPlan::BarShape::Rectangle);
    QVERIFY(plan().task("A")->shape.isEmpty());
    m_document->undo();
    QCOMPARE(barOf("A"), GanttPlan::BarShape::Arrow);
    // A summary: the Summaries' shapes; its own group frame, the sheet's bracket for the others.
    m_view->openPanel("S");
    QVERIFY(panel->ownSummaryShapeField()->isVisible() && !panel->ownBarShapeField()->isVisible());
    QCOMPARE(panel->ownSummaryShapeField()->currentData().toInt(), int(GanttPlan::SummaryShape::Bracket));
    emit panel->ownSummaryShapeField()->activated(panel->ownSummaryShapeField()->findData(int(GanttPlan::SummaryShape::Frame)));
    QCOMPARE(plan().summaryShape(*plan().task("S")), GanttPlan::SummaryShape::Frame);
    QCOMPARE(plan().summaryShape(), GanttPlan::SummaryShape::Bracket);
    // A milestone: the Milestones' shapes (user, 2026-11-01: colour, shape, fill and border for every kind).
    m_view->openPanel("M");
    QVERIFY(panel->styleSection()->isVisible());
    QVERIFY(panel->ownMilestoneShapeField()->isVisible() && !panel->ownBarShapeField()->isVisible());
    emit panel->ownMilestoneShapeField()->activated(panel->ownMilestoneShapeField()->findData(int(GanttPlan::MilestoneShape::Star)));
    QCOMPARE(plan().milestoneShape(*plan().task("M")), GanttPlan::MilestoneShape::Star);
    QCOMPARE(plan().milestoneShape(), GanttPlan::MilestoneShape::Diamond);
    // Fill and border: own values over the sheet's, × back.
    emit panel->ownFillField()->activated(panel->ownFillField()->findData(int(NodeFillStyle::TratteggioDiagonale)));
    emit panel->ownBorderField()->activated(panel->ownBorderField()->findData(int(GanttPlan::BorderStyle::Dashed)));
    QCOMPARE(plan().fillStyle(*plan().task("M"), GanttPlan::BarKind::Milestone), NodeFillStyle::TratteggioDiagonale);
    QCOMPARE(plan().borderStyle(*plan().task("M"), GanttPlan::BarKind::Milestone), GanttPlan::BorderStyle::Dashed);
    QVERIFY(panel->defaultFillButton()->isVisible() && panel->defaultBorderButton()->isVisible());
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setBorderStyle(GanttPlan::BarKind::Milestone, GanttPlan::BorderStyle::Dotted); }));
    QCOMPARE(plan().borderStyle(*plan().task("M"), GanttPlan::BarKind::Milestone), GanttPlan::BorderStyle::Dashed);
    emit panel->defaultFillButton()->clicked();
    QCOMPARE(plan().fillStyle(*plan().task("M"), GanttPlan::BarKind::Milestone), NodeFillStyle::Pieno);
    QVERIFY(plan().task("M")->fill.isEmpty());
    // The colour moved to the Style section.
    QVERIFY(panel->styleSection()->isAncestorOf(panel->colorField()));
    // Kept on the topics in the file.
    const QString file = m_dir.filePath("own-shape.mentis");
    QVERIFY(m_document->save(file, BmapStorage::SaveReason::Explicit).ok);
    m_document->close();  // its lock
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.model()->gantt().task("A")->shape, QString("arrow"));
    QCOMPARE(reopened.model()->gantt().task("S")->shape, QString("frame"));
    reopened.close();
}

void GanttViewTest::exportShapes() {
    // FEAT-080: the exports draw the chart's shapes: group frames, the tasks' own shapes; the Office
    // files are still written (Excel draws bars as cells, as before).
    setPlan({task("S", kMonday, 3), task("A", kMonday, 2, "S"), task("B", kMonday.addDays(1), 2, "S")});
    bool reduced = false;
    QString error;
    const auto image = [&] { return GanttPages::toImage(m_view->exportSource(), GanttChart::Scale::Days, 1, &reduced, &error); };
    const QImage plain = image();
    QVERIFY(!plain.isNull());
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setSummaryShape(GanttPlan::SummaryShape::Frame); }));
    m_view->refresh();
    const QImage framed = image();
    QVERIFY(framed != plain);
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setOwnStyle("A", GanttPlan::OwnStyle::Shape, "arrow"); }));
    m_view->refresh();
    QCOMPARE(m_view->exportSource().plan.task("A")->shape, QString("arrow"));
    const QImage picture = image();
    QVERIFY(picture != framed);
    const GanttExportSource source = m_view->exportSource();
    GanttPages::Options options;
    const GanttPages pages(source, options);
    QVERIFY(pages.isValid());
    QVERIFY(MapExporter::writePdf(pages, m_dir.filePath("shapes.pdf"), "Gantt", false).ok);
    QVERIFY(!GanttOffice::docx(source, GanttPages::Content::Gantt, GanttChart::Scale::Days, "Plan").isEmpty());
    QVERIFY(!GanttOffice::xlsx(source, GanttPages::Content::Gantt, GanttChart::Scale::Days, "Plan").isEmpty());
    QVERIFY(!GanttOffice::markdown(source, GanttPages::Content::Gantt).isEmpty());
}

void GanttViewTest::sheetColours() {
    // FEAT-080: the sheet's colours of tasks and summaries; a colour chosen on a task stays (user, 2026-10-29).
    setPlan({task("S", kMonday, 2), task("A", kMonday, 3, "S"), task("B", kMonday, 2, "S")});
    m_view->openPanel("B");
    GanttTaskPanel *panel = m_view->panel();
    emit panel->colorField()->colorChosen(QColor("#22c55e"));  // B by hand
    m_view->refresh();
    panel->showPage(GanttTaskPanel::Page::Gantt);
    ColorField *tasks = panel->defaultColorField(GanttPlan::BarKind::Task), *summaries = panel->defaultColorField(GanttPlan::BarKind::Summary);
    QCOMPARE(tasks->color(), QColor(0xf9, 0xbd, 0x89));  // peach and lilac
    QCOMPARE(summaries->color(), QColor(0xaa, 0xaf, 0xfa));
    QVERIFY(panel->defaultColorReset(GanttPlan::BarKind::Task)->isHidden());
    GanttChart *chart = m_view->chart();
    const auto colourAt = [chart](const QString &id) {  // the bar's fill, past the done part
        const QRectF bar = chart->barRect(id);
        return chart->viewport()->grab().toImage().pixelColor(QPointF(bar.right() - 12, bar.center().y()).toPoint());
    };
    emit tasks->colorChosen(QColor("#3b82f6"));
    emit summaries->colorChosen(QColor("#ef4444"));
    m_view->refresh();
    const GanttPlan plan = m_document->model()->gantt();
    QCOMPARE(plan.defaultColor(GanttPlan::BarKind::Task), QColor("#3b82f6"));
    QCOMPARE(colourAt("A"), QColor("#3b82f6"));
    QCOMPARE(colourAt("S"), QColor("#ef4444"));
    QCOMPARE(colourAt("B"), QColor("#22c55e"));  // chosen on the task: it keeps it
    QVERIFY(tasks->isManual() && !panel->defaultColorReset(GanttPlan::BarKind::Task)->isHidden());
    QCOMPARE(plan.toJson().value("taskColor").toString(), QString("#3b82f6"));
    QCOMPARE(GanttPlan::fromJson(plan.toJson()).defaultColor(GanttPlan::BarKind::Summary), QColor("#ef4444"));
    // The task page shows the sheet's colour for a task without its own.
    m_view->openPanel("A");
    QCOMPARE(panel->colorField()->color(), QColor("#3b82f6"));
    QVERIFY(!panel->colorField()->isManual());
    // × back to the default, one step; Undo brings the sheet's colour back.
    panel->showPage(GanttTaskPanel::Page::Gantt);
    panel->defaultColorReset(GanttPlan::BarKind::Task)->click();
    m_view->refresh();
    QVERIFY(!m_document->model()->gantt().defaultColor(GanttPlan::BarKind::Task).isValid());
    QCOMPARE(colourAt("A"), QColor(0xf9, 0xbd, 0x89));
    m_document->undo();
    m_view->refresh();
    QCOMPARE(m_document->model()->gantt().defaultColor(GanttPlan::BarKind::Task), QColor("#3b82f6"));
    QCOMPARE(tasks->color(), QColor("#3b82f6"));
    int repairs = 0;
    QVERIFY(!GanttPlan::fromJson(QJsonObject{{"taskColor", "nope"}}, &repairs).defaultColor(GanttPlan::BarKind::Task).isValid());
    QCOMPARE(repairs, 1);
    QVERIFY(!GanttPlan().toJson().contains("taskColor"));
}

void GanttViewTest::linkedMove() {
    // Start to start: B starts with A; dragging B or typing its start changes the lag (user, 2026-10-27).
    GanttDependency link = dependency("A", "B");
    link.type = GanttDependency::Type::StartStart;
    setPlan({task("A", kMonday, 5), task("B", kMonday.addDays(10), 2)}, {link});
    m_view->refresh();
    QCOMPARE(m_document->model()->gantt().schedule().tasks["B"].start, kMonday);
    GanttChart *chart = m_view->chart();
    const QRectF bar = chart->barRect("B");
    drag(bar.center(), bar.center() + QPointF(2 * chart->dayWidth(), 0));
    QCOMPARE(m_document->model()->gantt().dependencies().at(0).lag, 2);
    QCOMPARE(m_document->model()->gantt().schedule().tasks["B"].start, kMonday.addDays(2));
    QAbstractItemModel *table = m_view->table()->model();
    QVERIFY(table->setData(table->index(1, 1), kMonday.addDays(4)));
    QCOMPARE(m_document->model()->gantt().dependencies().at(0).lag, 4);
    // Dragging A moves B along.
    m_view->refresh();
    const QRectF first = chart->barRect("A");
    drag(first.center(), first.center() + QPointF(chart->dayWidth(), 0));
    QCOMPARE(m_document->model()->gantt().schedule().tasks["B"].start, kMonday.addDays(5));
}

void GanttViewTest::arrowRoute() {
    // A -> B jumps over the summary S, which starts where A ends: the line turns just under A and
    // runs down before B (user, 2026-10-28). B -> C, adjacent rows: the usual route.
    setPlan({task("A", kMonday, 2), task("S", kMonday, 1), task("B", kMonday, 3, "S"), task("C", kMonday, 1)},
            {dependency("A", "B"), dependency("B", "C")});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    const QRectF a = chart->barRect("A"), s = chart->barRect("S"), b = chart->barRect("B"), c = chart->barRect("C");
    QCOMPARE(s.left(), a.right());
    const double gap = a.center().y() + GanttChart::kRowHeight / 2.0;
    QCOMPARE(chart->arrowPath(0), QPolygonF({a.center() + QPointF(a.width() / 2, 0), QPointF(a.right() + 8, a.center().y()),
                                             QPointF(a.right() + 8, gap), QPointF(b.left() - 8, gap),
                                             QPointF(b.left() - 8, b.center().y()), QPointF(b.left(), b.center().y())}));
    QVERIFY(b.left() - 8 < s.left());  // the line passes before the summary, not under it
    const double above = c.center().y() - GanttChart::kRowHeight / 2.0;
    QCOMPARE(chart->arrowPath(1), QPolygonF({QPointF(b.right(), b.center().y()), QPointF(b.right() + 8, b.center().y()),
                                             QPointF(b.right() + 8, above), QPointF(c.left() - 8, above),
                                             QPointF(c.left() - 8, c.center().y()), QPointF(c.left(), c.center().y())}));

    // One predecessor, two successors on the next days (the user's picture): the farther arrow turns just under
    // the predecessor and runs down beside its successor, not along the nearer one's line.
    GanttDependency next = dependency("A", "B"), later = dependency("A", "C");
    next.lag = 1;  // milestones end on their day: B the day after A, C the day after B
    later.lag = 2;
    setPlan({task("A", kMonday, 0), task("B", kMonday, 0), task("C", kMonday, 0)}, {next, later});
    m_view->refresh();
    const QRectF a2 = chart->barRect("A"), b2 = chart->barRect("B"), c2 = chart->barRect("C");
    const double under = a2.center().y() + GanttChart::kRowHeight / 2.0;
    QCOMPARE(chart->arrowPath(1), QPolygonF({QPointF(a2.right(), a2.center().y()), QPointF(a2.right() + 8, a2.center().y()),
                                             QPointF(a2.right() + 8, under), QPointF(c2.left() - 8, under),
                                             QPointF(c2.left() - 8, c2.center().y()), QPointF(c2.left(), c2.center().y())}));
    QVERIFY(c2.left() - 8 > b2.right() && c2.left() - 8 != a2.right() + 8);  // past B, its own line
}

// No date, no bar: nor its name, with "Name on the bar" on, task or milestone (user, 2026-10-29).
void GanttViewTest::undatedName() {
    setPlan({task("A", kMonday, 2), task("U", QDate(), 1), task("M", QDate(), 0)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    QVERIFY(chart->barRect("U").isEmpty() && chart->barRect("M").isEmpty());
    const auto rows = [chart] {  // the chart's left part of the rows of U and M
        const QImage image = chart->viewport()->grab().toImage();
        return image.copy(0, GanttChart::kHeaderHeight + GanttChart::kRowHeight, 200, 2 * GanttChart::kRowHeight);
    };
    const QImage before = rows();
    QVERIFY(m_view->setNameOnBar(true, {"U", "M"}));
    QVERIFY(chart->showsName("U") && chart->showsName("M"));
    QCOMPARE(rows(), before);
    // Once dated, the milestone shows its name after the diamond.
    QVERIFY(m_document->editGantt([](GanttPlan &p) { return p.setStart("M", kMonday); }));
    m_view->refresh();
    QVERIFY(!chart->barRect("M").isEmpty());
}

void GanttViewTest::barNames() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 3, "S"), task("B", kMonday, 3)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    MindMapModel *model = m_document->model();
    // Default (user, 2026-10-27): the summary shows its name, tasks do not.
    QVERIFY(chart->showsName("S"));
    QVERIFY(!chart->showsName("A") && !chart->showsName("B"));
    // Shown on a task: stored on the topic, one step.
    m_view->selectTask("B");
    QVERIFY(m_view->setNameOnBar(true));
    QVERIFY(chart->showsName("B"));
    QCOMPARE(model->nodeById("B")->ganttLabel, std::optional<bool>(true));
    m_document->undo();
    m_view->refresh();
    QVERIFY(!chart->showsName("B"));
    m_document->redo();
    m_view->refresh();
    // Hidden on the summary; back to its default stores no choice.
    QVERIFY(m_view->setNameOnBar(false, {"S"}));
    QVERIFY(!chart->showsName("S"));
    QVERIFY(m_view->setNameOnBar(true, {"S"}));
    QVERIFY(!model->nodeById("S")->ganttLabel.has_value());
    // The panel's switch shows and changes it.
    m_view->openPanel("A");
    ToggleSwitch *toggle = m_view->panel()->labelSwitch();
    QVERIFY(!toggle->isChecked());
    toggle->click();
    QVERIFY(chart->showsName("A"));
    QVERIFY(toggle->isChecked());
    // Saved and reopened.
    const QString file = m_dir.filePath("names.mentis");
    QVERIFY(m_document->save(file, BmapStorage::SaveReason::Explicit).ok);
    m_document->close();
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.model()->nodeById("A")->ganttLabel, std::optional<bool>(true));
    QVERIFY(!reopened.model()->nodeById("S")->ganttLabel.has_value());
    reopened.close();
}

void GanttViewTest::taskPanel() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 3, "S"), task("B", kMonday, 2, "S"), task("C", QDate(), 1)},
            {dependency("A", "B")});
    m_view->refresh();
    GanttTaskPanel *panel = m_view->panel();
    QTreeView *table = m_view->table();
    QVERIFY(!panel->isVisible());
    // FEAT-080: a click only selects; the top bar's button opens the panel, then a click shows the
    // task, from the Gantt page too.
    const QModelIndex a = m_view->table()->model()->index(0, 0, table->model()->index(0, 0));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, table->visualRect(a).center());
    QVERIFY(!panel->isVisible());
    QSignalSpy shown(m_view.get(), &GanttView::panelShownChanged);
    m_view->selectTask("C");
    m_view->togglePanel();
    QVERIFY(panel->isVisible());
    QCOMPARE(shown.count(), 1);
    QCOMPARE(panel->taskId(), QString("C"));  // the current task
    panel->showPage(GanttTaskPanel::Page::Gantt);
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, table->visualRect(a).center());
    QVERIFY(panel->page() == GanttTaskPanel::Page::Task);
    QCOMPARE(panel->taskId(), QString("A"));
    QVERIFY(panel->mapTo(m_view.get(), QPoint(0, 0)).x() < table->mapTo(m_view.get(), QPoint(0, 0)).x());
    QCOMPARE(panel->nameField()->text(), QString("A"));
    QCOMPARE(panel->durationField()->text(), QString("3"));
    // A click on a bar shows that task; Ctrl+click only selects.
    const QRectF bar = m_view->chart()->barRect("B");
    QTest::mouseClick(m_view->chart()->viewport(), Qt::LeftButton, Qt::ControlModifier, bar.center().toPoint());
    QCOMPARE(panel->taskId(), QString("A"));
    QTest::mouseClick(m_view->chart()->viewport(), Qt::LeftButton, {}, bar.center().toPoint());
    QCOMPARE(panel->taskId(), QString("B"));
    // The arrow keys move the panel with the current row; Shift+arrow only extends the selection.
    QTest::keyClick(table, Qt::Key_Up);
    QCOMPARE(panel->taskId(), QString("A"));
    QCOMPARE(panel->nameField()->text(), QString("A"));
    QTest::keyClick(table, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(panel->taskId(), QString("A"));
    QTest::keyClick(table, Qt::Key_Down);
    QTest::keyClick(table, Qt::Key_Down);
    QCOMPARE(panel->taskId(), QString("C"));
    QTest::mouseClick(m_view->chart()->viewport(), Qt::LeftButton, {}, bar.center().toPoint());
    QCOMPARE(panel->taskId(), QString("B"));
    // Fields: one step each, and Undo shows the old value in the panel.
    panel->durationField()->setText("5");
    emit panel->durationField()->editingFinished();
    QCOMPARE(m_document->model()->gantt().task("B")->duration, 5);
    panel->progressField()->setText("40");
    emit panel->progressField()->editingFinished();
    QCOMPARE(m_document->model()->gantt().task("B")->progress, 40);
    m_document->undo();
    m_view->refresh();
    QCOMPARE(panel->progressField()->text(), QString("0"));
    panel->endField()->setDate(panel->startField()->date().addDays(3));
    QCOMPARE(m_document->model()->gantt().task("B")->duration, 4);
    // Priority and people: on the topic, one step, shown in their columns.
    QCOMPARE(panel->priorityField()->count(), 6);  // None + five levels (user 2026-10-28)
    QCOMPARE(panel->priorityField()->itemText(1), QString("Very low"));
    QCOMPARE(panel->priorityField()->itemText(5), QString("Very high"));
    // The select opens the app's list (FloatingListPopup) below it; a click on a level applies it.
    panel->priorityField()->showPopup();
    auto *list = QApplication::activePopupWidget() ? QApplication::activePopupWidget()->findChild<QListWidget *>() : nullptr;
    QVERIFY(list);
    QVERIFY(list->window()->geometry().top() > panel->priorityField()->mapToGlobal(QPoint(0, 0)).y());
    QCOMPARE(list->count(), 6);
    QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, list->visualItemRect(list->item(3)).center());
    QVERIFY(!QApplication::activePopupWidget());
    QCOMPARE(panel->priorityField()->currentIndex(), 3);
    QCOMPARE(m_document->model()->nodeById("B")->ganttPriority, 3);
    // FEAT-081: no people in the document, the "+" menu offers the Resources modal.
    QMenu empty;
    panel->fillPersonMenu(&empty);
    QCOMPARE(empty.actions().size(), 2);
    QVERIFY(!empty.actions().at(0)->isEnabled());
    QCOMPARE(empty.actions().at(0)->text(), QString("No people yet"));
    QSignalSpy requested(m_view.get(), &GanttView::resourcesRequested);
    empty.actions().at(1)->trigger();
    QTRY_COMPARE(requested.count(), 1);
    // The menu lists the people not assigned; a choice assigns, the tags follow the list's order.
    GanttResources resources;
    resources.people = {{"p1", "Anna", "Rossi", "", "Developer"}, {"p2", "Marco", "Bianchi", "", "PM"}};
    QVERIFY(m_document->setResources(resources, m_document->model()->gantt().calendar()));
    m_view->refresh();
    QVERIFY(panel->shownPeople().isEmpty() && panel->addPersonButton()->isEnabled());
    QMenu menu;
    panel->fillPersonMenu(&menu);
    QCOMPARE(menu.actions().size(), 2);
    QCOMPARE(menu.actions().at(1)->text(), QString("Marco Bianchi — PM"));
    menu.actions().at(1)->trigger();
    menu.actions().at(0)->trigger();
    QCOMPARE(m_document->model()->nodeById("B")->ganttPeople, QStringList({"p2", "p1"}));
    QCOMPARE(panel->shownPeople(), QStringList({"p1", "p2"}));
    QVERIFY(!panel->addPersonButton()->isEnabled());  // everyone assigned
    // Stage B: a table in the Resources section, a row per person, header 24 px and rows 28 px.
    QTableWidget *people = panel->peopleTable();
    QVERIFY(panel->findChild<QWidget *>("GanttSectionResources")->isAncestorOf(people));
    QVERIFY(people->isVisibleTo(panel) && !people->verticalScrollBar()->isVisible());
    QCOMPARE(people->height(), 24 + 2 * 28);
    QCOMPARE(people->item(1, 0)->text(), QString("Marco"));
    QCOMPARE(people->item(1, 1)->text(), QString("Bianchi"));
    QCOMPARE(people->item(1, 2)->text(), QString("PM"));
    QCOMPARE(people->horizontalHeaderItem(0)->text(), QString("First name"));
    // Columns: hidden and moved from the header, the last one stays, the × stays last; kept for a new panel.
    QCOMPARE(panel->resourceColumns(), QStringList({"first", "last", "role"}));
    QMenu columnsMenu;
    panel->fillResourceColumnMenu(&columnsMenu);
    QCOMPARE(columnsMenu.actions().size(), 3);
    columnsMenu.actions().at(1)->trigger();  // Last name hidden
    QCOMPARE(panel->resourceColumns(), QStringList({"first", "role"}));
    QVERIFY(panel->setResourceColumnShown("first", false));
    QVERIFY(!panel->setResourceColumnShown("role", false));  // the last shown one
    QMenu oneColumn;
    panel->fillResourceColumnMenu(&oneColumn);
    QVERIFY(!oneColumn.actions().at(2)->isEnabled());
    QVERIFY(panel->setResourceColumnShown("last", true));
    QVERIFY(panel->setResourceColumnShown("first", true));
    QHeaderView *header = people->horizontalHeader();
    header->moveSection(header->visualIndex(2), 0);  // Role first
    QCOMPARE(panel->resourceColumns(), QStringList({"role", "first", "last"}));
    header->moveSection(header->visualIndex(3), 0);  // the × goes back to the end
    QCOMPARE(header->visualIndex(3), 3);
    QCOMPARE(QSettings().value("gantt/resourceColumns").toStringList(), QStringList({"role", "first", "last"}));
    {
        GanttTaskPanel other;
        QCOMPARE(other.resourceColumns(), QStringList({"role", "first", "last"}));
        QCOMPARE(other.peopleTable()->horizontalHeader()->visualIndex(3), 3);
    }
    QSettings().remove("gantt/resourceColumns");
    QVERIFY(panel->personCandidates().isEmpty());
    m_view->setColumnShown(5, true);
    m_view->setColumnShown(6, true);
    const auto taskB = [&] { return table->model()->index(1, 0, table->model()->index(0, 0)); };
    QCOMPARE(taskB().siblingAtColumn(5).data().toString(), QString("Medium"));
    QCOMPARE(taskB().siblingAtColumn(6).data().toString(), QString("Anna Rossi, Marco Bianchi"));
    QCOMPARE(taskB().siblingAtColumn(6).data(Qt::ToolTipRole).toString(), QString("Anna Rossi\nMarco Bianchi"));
    QCOMPARE(table->model()->headerData(6, Qt::Horizontal).toString(), QString("People"));
    QVERIFY(!(taskB().siblingAtColumn(5).flags() & Qt::ItemIsEditable));
    // × removes, one step.
    panel->personRemove("p1")->click();
    QCOMPARE(m_document->model()->nodeById("B")->ganttPeople, QStringList({"p2"}));
    QCOMPARE(panel->shownPeople(), QStringList({"p2"}));
    m_document->undo();
    QCOMPARE(m_document->model()->nodeById("B")->ganttPeople, QStringList({"p2", "p1"}));
    // A person removed from the Resources list leaves the tasks; Undo brings them back.
    resources.people.removeLast();
    QVERIFY(m_document->setResources(resources, m_document->model()->gantt().calendar()));
    m_view->refresh();
    QCOMPARE(taskB().siblingAtColumn(6).data().toString(), QString("Anna Rossi"));
    QCOMPARE(panel->shownPeople(), QStringList({"p1"}));
    QCOMPARE(m_document->model()->nodeById("B")->ganttPeople, QStringList({"p2", "p1"}));
    m_document->undo();
    m_view->refresh();
    QCOMPARE(taskB().siblingAtColumn(6).data().toString(), QString("Anna Rossi, Marco Bianchi"));
    // Renamed in the list: shown renamed.
    resources.people = m_document->resources().people;
    resources.people[0].firstName = "Anna Maria";
    QVERIFY(m_document->setResources(resources, m_document->model()->gantt().calendar()));
    m_view->refresh();
    QCOMPARE(taskB().siblingAtColumn(6).data().toString(), QString("Anna Maria Rossi, Marco Bianchi"));
    // Name renames the topic.
    panel->nameField()->setText("Build");
    emit panel->nameField()->editingFinished();
    QCOMPARE(m_document->model()->nodeById("B")->title, QString("Build"));
    // Links: B has A as predecessor; type, lag and removal from its row; + adds.
    QCOMPARE(panel->links(true).size(), 1);
    QVERIFY(panel->links(false).isEmpty());
    panel->linkType(true, "A")->setCurrentIndex(1);
    emit panel->linkType(true, "A")->activated(1);
    QCOMPARE(m_document->model()->gantt().dependencies().at(0).type, GanttDependency::Type::StartStart);
    QCOMPARE(panel->linkSentence(true, "A"), QString("Starts after «A» starts."));
    panel->linkLag(true, "A")->setText("2");
    emit panel->linkLag(true, "A")->editingFinished();
    QCOMPARE(m_document->model()->gantt().dependencies().at(0).lag, 2);
    QCOMPARE(panel->linkSentence(true, "A"), QString("Starts after «A» starts, 2 day(s) later."));
    // Up/Down add or take a day.
    QTest::keyClick(panel->linkLag(true, "A"), Qt::Key_Down);
    QCOMPARE(m_document->model()->gantt().dependencies().at(0).lag, 1);
    // A cannot become B's successor (cycle), the summary and B itself are left out.
    QCOMPARE(panel->linkCandidates(false), QStringList({"C"}));
    QVERIFY(panel->addLink(false, "C"));
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 2);
    panel->linkRemove(true, "A")->click();
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 1);
    panel->linkRemove(false, "C")->click();  // C dated by B no more
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());
    // C keeps the date its link gave it; without a date "Add date" gives it today.
    m_view->openPanel("C");
    QVERIFY(panel->clearStartButton()->isVisibleTo(panel));
    panel->clearStartButton()->click();
    QVERIFY(panel->addStartButton()->isVisibleTo(panel) && !panel->startField()->isVisibleTo(panel));
    panel->addStartButton()->click();
    QCOMPARE(m_document->model()->nodeById("C")->ganttStart, QDate::currentDate());
    QVERIFY(panel->clearStartButton()->isVisibleTo(panel));
    panel->clearStartButton()->click();
    QVERIFY(!m_document->model()->nodeById("C")->ganttStart.isValid());
    // A summary: no dates, milestone switch nor people (user, 2026-10-29), no links, nor progress
    // and priority (user, 2026-10-30: the Details section hidden).
    m_view->openPanel("S");
    for (QWidget *field : {static_cast<QWidget *>(panel->milestoneSwitch()), static_cast<QWidget *>(panel->startField()),
                           static_cast<QWidget *>(panel->durationField()), static_cast<QWidget *>(panel->endField()),
                           static_cast<QWidget *>(panel->findChild<QWidget *>("GanttSectionResources")),
                           static_cast<QWidget *>(panel->progressField()), static_cast<QWidget *>(panel->priorityField())})
        QVERIFY(!field->isVisibleTo(panel));
    QVERIFY(!panel->findChild<QWidget *>("GanttSectionDates")->isVisibleTo(panel));
    QVERIFY(!panel->findChild<QWidget *>("GanttSectionDetails")->isVisibleTo(panel));
    // Back to a task: every field again.
    m_view->openPanel("C");
    QVERIFY(panel->milestoneSwitch()->isVisibleTo(panel) && panel->durationField()->isVisibleTo(panel));
    QVERIFY(panel->progressField()->isVisibleTo(panel) && panel->priorityField()->isVisibleTo(panel));
    QVERIFY(panel->endField()->isVisibleTo(panel) && panel->findChild<QWidget *>("GanttSectionResources")->isVisibleTo(panel));
    m_view->openPanel("S");
    // Esc closes it; with a deleted task it stays open and says there is no task.
    QTest::keyClick(panel, Qt::Key_Escape);
    QVERIFY(!panel->isVisible());
    QCOMPARE(shown.count(), 2);
    QVERIFY(!shown.last().at(0).toBool());
    m_view->openPanel("C");
    m_view->selectTask("C");
    QVERIFY(m_view->removeSelected());
    QVERIFY(panel->isVisible() && panel->showsNoTask());
    m_view->togglePanel();
    // Saved and reopened; a topic without them writes no keys.
    const QString file = m_dir.filePath("panel.mentis");
    QVERIFY(m_document->save(file, BmapStorage::SaveReason::Explicit).ok);
    m_document->close();
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.model()->nodeById("B")->ganttPriority, 3);
    QCOMPARE(reopened.model()->nodeById("B")->ganttPeople, QStringList({"p2", "p1"}));
    // Only B has them: defaults are not written.
    const QByteArray json = QJsonDocument(reopened.model()->toJson()).toJson(QJsonDocument::Compact);
    QCOMPARE(json.count("\"priority\":"), 1);
    QCOMPARE(json.count("\"people\":"), 1);
    QCOMPARE(json.count("\"person\":"), 0);
    // Read back: duplicates and empty ids dropped; the old free-text "person" is not read.
    QJsonObject map = reopened.model()->toJson();
    std::function<bool(QJsonObject &)> edit = [&](QJsonObject &topic) {
        if (topic["id"].toString() == "B") {
            QJsonObject gantt = topic["gantt"].toObject();
            gantt["people"] = QJsonArray{"p1", "p1", "", "p2"};
            gantt["person"] = "Julia";
            topic["gantt"] = gantt;
            return true;
        }
        QJsonArray children = topic["children"].toArray();
        for (qsizetype i = 0; i < children.size(); ++i) {
            QJsonObject child = children[i].toObject();
            if (edit(child)) {
                children[i] = child;
                topic["children"] = children;
                return true;
            }
        }
        return false;
    };
    QJsonObject root = map["root"].toObject();
    QVERIFY(edit(root));
    map["root"] = root;
    MindMapModel read;
    QVERIFY(read.fromJson(map));
    QCOMPARE(read.nodeById("B")->ganttPeople, QStringList({"p1", "p2"}));
    reopened.close();
}

void GanttViewTest::dependencyMenu() {
    setPlan({task("A", kMonday, 3), task("B", kMonday, 2)}, {dependency("A", "B")});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    const auto actions = [chart] {
        std::unique_ptr<QMenu> menu(chart->dependencyMenu(0, chart));
        QList<QAction *> list;
        for (QAction *action : menu->actions())
            if (!action->isSeparator()) list.append(action);
        menu.release()->deleteLater();
        return list;
    };
    const auto successor = [this] { return m_document->model()->gantt().schedule().tasks["B"]; };
    // Four types, the current one checked; then Lag… and Delete dependency.
    QList<QAction *> entries = actions();
    QCOMPARE(entries.size(), 6);
    QCOMPARE(entries.at(0)->text(), QString("Finish to start"));
    QVERIFY(entries.at(0)->isChecked() && !entries.at(1)->isChecked());
    QCOMPARE(successor().start, kMonday.addDays(3));
    // Same type: no step.
    const bool couldUndo = m_document->canUndo();
    entries.at(0)->trigger();
    QCOMPARE(m_document->canUndo(), couldUndo);
    // Start to start: one step, B starts with A; Undo brings finish-start back.
    entries.at(1)->trigger();
    QCOMPARE(m_document->model()->gantt().dependencies().at(0).type, GanttDependency::Type::StartStart);
    QCOMPARE(successor().start, kMonday);
    m_document->undo();
    QCOMPARE(successor().start, kMonday.addDays(3));
    m_document->redo();
    m_view->refresh();
    QVERIFY(actions().at(1)->isChecked());
    // Lag and lead in days.
    GanttDependency link = m_document->model()->gantt().dependencies().at(0);
    link.lag = 2;
    QVERIFY(chart->setDependency(link));
    QCOMPARE(successor().start, kMonday.addDays(2));
    QVERIFY(!chart->setDependency(link));  // same value: no step
    m_view->refresh();
    QCOMPARE(chart->dependencyLabel(0), QString("Start to start, 2 day(s) later"));
    link.type = GanttDependency::Type::FinishStart;
    link.lag = -1;
    QVERIFY(chart->setDependency(link));
    QCOMPARE(successor().start, kMonday.addDays(2));
    m_view->refresh();
    QCOMPARE(chart->dependencyLabel(0), QString("Finish to start, 1 day(s) earlier"));
    // Delete dependency is still the last entry.
    actions().last()->trigger();
    QVERIFY(m_document->model()->gantt().dependencies().isEmpty());
    m_document->undo();
    // Saved and reopened with its type and lag.
    const QString file = m_dir.filePath("links.mentis");
    QVERIFY(m_document->save(file, BmapStorage::SaveReason::Explicit).ok);
    m_document->close();
    Document reopened;
    QVERIFY(reopened.open(file, false).ok);
    QCOMPARE(reopened.model()->gantt().dependencies().at(0), link);
    reopened.close();
}

void GanttViewTest::barLabel() {
    QFont font;
    font.setPixelSize(12);
    const QString name = QStringLiteral("Argomento principale");
    // A bar too short for one letter is empty, not "…" (user, 2026-10-27).
    QVERIFY(GanttChart::barLabel(name, font, 12).isEmpty());
    QVERIFY(GanttChart::barLabel(name, font, 0).isEmpty());
    const QString shortened = GanttChart::barLabel(name, font, 60);
    QVERIFY(shortened.size() > 1 && shortened.endsWith(QChar(0x2026)));
    QCOMPARE(GanttChart::barLabel(name, font, 1000), name);
}

void GanttViewTest::criticalPainting() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 5, "S"), task("B", kMonday, 2, "S"), task("C", kMonday, 1)},
            {dependency("A", "C"), dependency("B", "C")});
    GanttChart *chart = m_view->chart();
    QImage image = chart->viewport()->grab().toImage();
    const auto red = [&](const QString &id) {
        const QRectF bar = chart->barRect(id);
        const QColor pixel = image.pixelColor(QPointF(bar.center().x(), bar.top() - 3).toPoint());  // the halo
        return pixel.red() > 180 && pixel.green() < 140 && pixel.blue() < 140;
    };
    QVERIFY(red("A"));
    QVERIFY(red("C"));
    QVERIFY(!red("B"));
    QVERIFY(m_document->model()->gantt().schedule().tasks["S"].critical);
    QVERIFY(!red("S"));  // summaries have no halo (user, 2026-10-28)
    // Grid (user's picture, 2026-10-28): weekends #fafbfc in the rows and the days' header.
    const double weekendX = chart->barRect("A").left() + 5.5 * (chart->barRect("A").width() / 5);  // Saturday
    const QImage view = m_view->grab().toImage();  // the card under the chart is the view's
    const auto at = [&](double x, int y) { return view.pixelColor(chart->viewport()->mapTo(m_view.get(), QPoint(int(x), y))); };
    QCOMPARE(at(weekendX, chart->viewport()->height() / 2), QColor(0xfa, 0xfb, 0xfc));
    QCOMPARE(at(weekendX - 10, GanttChart::kHeaderHeight - 3), QColor(0xfa, 0xfb, 0xfc));
    // Selected row #e3e5e6 with the grid still visible; the selected bar has a blue ring (user's
    // second picture, 2026-10-28).
    m_view->selectTask("B");
    QApplication::processEvents();
    const QImage selected = m_view->grab().toImage();
    const QRectF b = chart->barRect("B");
    const auto atSelected = [&](double x, double y) { return selected.pixelColor(chart->viewport()->mapTo(m_view.get(), QPoint(int(x), int(y)))); };
    const double rowY = b.center().y();
    QCOMPARE(atSelected(b.right() + 20, rowY), QColor(0xe3, 0xe5, 0xe6));
    const double dayWidth = chart->barRect("A").width() / 5;
    int darker = 0;  // the day lines through the selected row
    for (double x = b.right() + 2; x < b.right() + 2 + 3 * dayWidth; ++x)
        if (atSelected(x, rowY).red() < 0xe0) ++darker;
    QVERIFY(darker >= 2);
    const QColor ring = atSelected(b.center().x(), b.top() - 2);
    QVERIFY(ring.blue() > ring.red() + 40);
    // Hidden (user, 2026-10-28): no halo, no red arrow; kept as an app setting.
    m_view->setCriticalShown(false);
    image = chart->viewport()->grab().toImage();
    QVERIFY(!red("A") && !red("C"));
    bool redPixel = false;
    for (int y = 0; y < image.height() && !redPixel; ++y)
        for (int x = 0; x < image.width() && !redPixel; ++x) {
            const QColor pixel = image.pixelColor(x, y);
            redPixel = pixel.red() > 200 && pixel.green() < 100 && pixel.blue() < 100;
        }
    QVERIFY(!redPixel);
    QVERIFY(!GanttView(m_document.get(), m_shortcuts.get()).criticalShown());
    m_view->setCriticalShown(true);
    QVERIFY(GanttView(m_document.get(), m_shortcuts.get()).criticalShown());
}

void GanttViewTest::performance() {
    // 1,000 tasks in 50 summaries, 2,000 dependencies forward in the list.
    QList<GanttTask> tasks;
    QList<GanttDependency> dependencies;
    QStringList leaves;
    for (int s = 0; s < 50; ++s) {
        tasks.append(task(QStringLiteral("S%1").arg(s), kMonday, 1));
        for (int t = 0; t < 19; ++t) {
            leaves.append(QStringLiteral("T%1-%2").arg(s).arg(t));
            tasks.append(task(leaves.last(), kMonday.addDays(t % 5), 1 + t % 7, QStringLiteral("S%1").arg(s)));
        }
    }
    for (int i = 1; i < leaves.size() && dependencies.size() < 2000; ++i)
        for (int back : {1, 7, 31})
            if (i - back >= 0 && dependencies.size() < 2000) dependencies.append(dependency(leaves.at(i - back), leaves.at(i)));
    setPlan(tasks, dependencies);
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 2000);
    QElapsedTimer timer;
    // The fastest of three refreshes and of three repaints: one run can be slowed by the other tests
    // running at the same time (ctest -j 8), which is noise, not the view's cost. A refresh rebuilds
    // everything from the model, so the later ones cost as the first (measured: 42-48 ms each).
    double open = 1e9;
    for (int i = 0; i < 3; ++i) {
        timer.restart();
        m_view->refresh();
        open = std::min(open, timer.nsecsElapsed() / 1e6);
    }
    m_view->chart()->viewport()->repaint();  // the first paint also loads the fonts
    const auto frameTime = [&timer](QWidget *widget) {
        double best = 1e9;
        for (int i = 0; i < 3; ++i) {
            timer.restart();
            widget->repaint();
            best = std::min(best, timer.nsecsElapsed() / 1e6);
        }
        return best;
    };
    const double frame = frameTime(m_view->chart()->viewport());
    // Frames while dragging a bar and while drawing a dependency (each checks the link).
    GanttChart *chart = m_view->chart();
    QWidget *viewport = chart->viewport();
    const QPointF bar = chart->barRect("T0-1").center();
    QTest::mousePress(viewport, Qt::LeftButton, {}, bar.toPoint());
    QTest::mouseMove(viewport, (bar + QPointF(40, 0)).toPoint());
    const double moveFrame = frameTime(viewport);
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, bar.toPoint());  // back where it was: no change
    QTest::mouseMove(viewport, bar.toPoint());
    const QPointF handle = chart->linkHandle("T0-1");
    QTest::mousePress(viewport, Qt::LeftButton, {}, handle.toPoint());
    QTest::mouseMove(viewport, chart->barRect("T0-0").center().toPoint());
    const double linkFrame = frameTime(viewport);
    QTest::mouseRelease(viewport, Qt::LeftButton, {}, chart->barRect("T0-0").center().toPoint());  // a cycle: refused
    QCOMPARE(m_document->model()->gantt().dependencies().size(), 2000);
    qInfo("1000 tasks: refresh %.1f ms, frame %.1f ms, move frame %.1f ms, link frame %.1f ms", open, frame, moveFrame, linkFrame);
    QCOMPARE(m_document->model()->gantt().tasks().size(), 1000);
#ifdef NDEBUG  // budgets for Release builds (FEAT-075)
    QVERIFY2(open < 100, qPrintable(QString::number(open)));
    QVERIFY2(frame < 16, qPrintable(QString::number(frame)));
    QVERIFY2(moveFrame < 16, qPrintable(QString::number(moveFrame)));
    QVERIFY2(linkFrame < 16, qPrintable(QString::number(linkFrame)));
#endif
}

// FEAT-085: in the Gantt the bar's look goes to the selected tasks; a bar without a colour of its own
// (the sheet's default) gives none, so the target's bar shows the default too.
void GanttViewTest::copyAndPasteStyleOfTasks() {
    MainWindow window;
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    qobject_cast<QStackedWidget *>(window.centralWidget())->setCurrentIndex(1);
    auto *document = window.findChild<Document *>();
    MindMapModel *model = document->model();
    MindNode *a = document->addChild(model->root(), QStringLiteral("A"));
    MindNode *b = document->addChild(model->root(), QStringLiteral("B"));
    MindNode *c = document->addChild(model->root(), QStringLiteral("C"));
    QVERIFY(model->setStyle({a, c}, StyleProperty::BranchColor, QColor("#cc0000")));
    for (auto *button : window.findChildren<QPushButton *>())
        if (button->text() == "Gantt" && button->isVisibleTo(&window)) { button->click(); break; }
    auto *gantt = window.findChild<GanttView *>();
    gantt->refresh();
    window.activateWindow();
    gantt->table()->setFocus();
    const auto keys = [gantt](Qt::Key key) { QTest::keyClick(gantt->table(), key, Qt::ControlModifier | Qt::AltModifier); };
    gantt->selectTask(a->id);
    keys(Qt::Key_C);
    gantt->selectTask(b->id);
    keys(Qt::Key_V);
    QCOMPARE(b->style.branchColor, std::optional<QColor>(QColor("#cc0000")));
    gantt->selectTask(b->id);
    QVERIFY(model->clearStyle({b}, StyleProperty::BranchColor));
    keys(Qt::Key_C);
    gantt->selectTask(c->id);
    keys(Qt::Key_V);
    QVERIFY(!c->style.branchColor);
}

QTEST_MAIN(GanttViewTest)
// Scrolling the task panel over a field does not change it (a task's start moved by two days while
// the panel was scrolled): only a focused field takes the wheel.
void GanttViewTest::panelFieldsIgnoreWheelUnfocused() {
    setPlan({task("A", kMonday, 3)});
    QTreeView *table = m_view->table();
    m_view->togglePanel();
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, table->visualRect(table->model()->index(0, 0)).center());
    GanttTaskPanel *panel = m_view->panel();
    const auto scroll = [](QWidget *field) {
        const QPointF at(field->width() / 2.0, field->height() / 2.0);
        QWheelEvent wheel(at, field->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(field, &wheel);
    };
    const int priority = m_document->model()->nodeById("A")->ganttPriority;
    for (QWidget *field : {static_cast<QWidget *>(panel->startField()), static_cast<QWidget *>(panel->endField()),
                           static_cast<QWidget *>(panel->priorityField())}) {
        QCOMPARE(field->focusPolicy(), Qt::StrongFocus);  // the wheel does not focus it
        scroll(field);
        QVERIFY(!field->hasFocus());
    }
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday);
    QCOMPARE(m_document->model()->gantt().task("A")->duration, 3);
    QCOMPARE(m_document->model()->nodeById("A")->ganttPriority, priority);
    // Focused, the field still steps with the wheel.
    panel->startField()->setFocus();
    QApplication::processEvents();
    QVERIFY(panel->startField()->hasFocus());
    scroll(panel->startField());
    QVERIFY(m_document->model()->gantt().task("A")->start != kMonday);
}

// The app's calendar (user, 2026-10-28) in the task panel and in the table's date editor.
void GanttViewTest::datePicker() {
    setPlan({task("A", kMonday, 3)});
    QTreeView *table = m_view->table();
    m_view->togglePanel();
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, table->visualRect(table->model()->index(0, 0)).center());
    DateField *field = m_view->panel()->startField();
    const auto chevron = [](DateField *f) { return QPoint(f->width() - 10, f->height() / 2); };
    QTest::mouseClick(field, Qt::LeftButton, {}, chevron(field));
    DatePicker *picker = field->picker();
    QVERIFY(picker);
    QCOMPARE(picker->date(), kMonday);
    QCOMPARE(picker->month(), 11);
    // Month arrows; days outside the plan's range cannot be picked.
    QTest::mouseClick(picker, Qt::LeftButton, {}, picker->nextRect().center());
    QCOMPARE(picker->month(), 12);
    QTest::mouseClick(picker, Qt::LeftButton, {}, picker->previousRect().center());
    QTest::mouseClick(picker, Qt::LeftButton, {}, picker->previousRect().center());
    QCOMPARE(picker->month(), 10);
    // Keys: the focused day moves (Page Down = a month), Enter picks it; one history step.
    QTest::keyClick(picker, Qt::Key_PageDown);
    QTest::keyClick(picker, Qt::Key_Right);
    QTest::keyClick(picker, Qt::Key_Down);
    QCOMPARE(picker->focusDate(), kMonday.addDays(8));
    QCOMPARE(picker->month(), 11);
    QTest::keyClick(picker, Qt::Key_Return);
    QVERIFY(!field->picker());
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday.addDays(8));
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday);
    // A click on a day picks it; Esc closes without a change.
    QTest::mouseClick(field, Qt::LeftButton, {}, chevron(field));
    picker = field->picker();
    QTest::mouseClick(picker, Qt::LeftButton, {}, picker->dayRect(kMonday.addDays(2)).center());
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday.addDays(2));
    QTest::keyClick(field, Qt::Key_F4);
    QVERIFY(field->picker());
    QTest::keyClick(field->picker(), Qt::Key_Escape);
    QVERIFY(!field->picker());
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday.addDays(2));
    // A click outside closes it without a change (user, 2026-10-28).
    QTest::mouseClick(field, Qt::LeftButton, {}, chevron(field));
    picker = field->picker();
    QVERIFY(picker);
    QTest::mouseClick(picker, Qt::LeftButton, {}, QPoint(-20, -20));
    QVERIFY(!field->picker());
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday.addDays(2));
    picker = new DatePicker(nullptr, {});
    picker->setRange(QDate(GanttPlan::kFirstYear, 1, 1), kMonday);
    picker->setDate(kMonday);
    QTest::keyClick(picker, Qt::Key_Right);  // past the last day
    QCOMPARE(picker->focusDate(), kMonday);
    delete picker;

    // The table's editor: a picked day is applied and the editor closes.
    const QModelIndex start = table->model()->index(0, 1);
    table->edit(start);
    auto *editor = table->viewport()->findChild<DateField *>();
    QVERIFY(editor);
    QTest::mouseClick(editor, Qt::LeftButton, {}, chevron(editor));
    QVERIFY(editor->picker());
    QTest::mouseClick(editor->picker(), Qt::LeftButton, {}, editor->picker()->dayRect(kMonday.addDays(4)).center());
    QCOMPARE(m_document->model()->gantt().task("A")->start, kMonday.addDays(4));
    QTRY_VERIFY(!table->viewport()->findChild<DateField *>());
}

// Duration and % in the table: the app's number field with − and + (user, 2026-10-29).
void GanttViewTest::numberCells() {
    setPlan({task("A", kMonday, 3)});
    m_view->refresh();
    QTreeView *table = m_view->table();
    m_view->resize(1200, 600);
    m_view->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_view.get()));
    auto *splitter = m_view->findChild<QSplitter *>(QStringLiteral("GanttSplitter"));
    QVERIFY(splitter);
    splitter->setSizes({0, splitter->width()});  // the table as narrow as its columns (user's photo)
    const QModelIndex duration = table->model()->index(0, 3);
    table->setCurrentIndex(duration);
    table->edit(duration);
    auto *field = table->viewport()->findChild<NumberField *>();
    QVERIFY(field);
    QCOMPARE(field->value(), 3.0);
    QVERIFY(!table->viewport()->findChild<QSpinBox *>());
    // Duration is the last column by default: the editor stays inside the table (user's photo).
    QVERIFY(field->width() > table->visualRect(duration).width());
    QVERIFY(table->visualRect(duration).right() >= table->viewport()->width() - 2);
    QVERIFY(field->geometry().right() <= table->viewport()->width() - 1);
    QVERIFY(field->geometry().left() >= 0);
    // Two clicks on +, then Enter: one step, 5 days.
    QTest::mouseClick(field, Qt::LeftButton, {}, field->plusRect().center());
    QTest::mouseClick(field, Qt::LeftButton, {}, field->plusRect().center());
    QCOMPARE(m_document->model()->gantt().task("A")->duration, 3);  // not applied until it closes
    QTest::keyClick(field, Qt::Key_Return);  // Qt commits an editor's data on the next event loop turn
    QTRY_COMPARE(m_document->model()->gantt().task("A")->duration, 5);
    QTRY_VERIFY(!table->viewport()->findChild<NumberField *>());
    m_document->undo();
    QCOMPARE(m_document->model()->gantt().task("A")->duration, 3);
    // % is 52 px wide: the editor grows so −, + and "100" fit; a typed value is kept in range.
    m_view->refresh();
    table->header()->showSection(4);  // hidden by default
    const QModelIndex progress = table->model()->index(0, 4);
    table->setCurrentIndex(progress);
    table->edit(progress);
    field = table->viewport()->findChild<NumberField *>();
    QVERIFY(field);
    QVERIFY(field->width() > table->visualRect(progress).width());
    QVERIFY(field->width() >= 2 * NumberField::kButtonPx + field->fontMetrics().horizontalAdvance("100"));
    QTest::keyClicks(field, "250");
    QTest::keyClick(field, Qt::Key_Return);
    QTRY_COMPARE(m_document->model()->gantt().task("A")->progress, 100);
}

// The app's number field (user, 2026-10-28): only the number, − and + at the sides.
void GanttViewTest::numberField() {
    NumberField field;
    field.setRange(0, 10);
    field.setValue(1);
    field.resize(120, 24);
    field.show();
    QList<double> chosen;
    connect(&field, &NumberField::valueChosen, &field, [&chosen](double value) { chosen.append(value); });
    // A number alone is confirmed (it was refused without the unit's word), above the range = the maximum.
    field.setFocus();
    field.selectAll();
    QTest::keyClicks(&field, "8");
    QTest::keyClick(&field, Qt::Key_Return);
    QCOMPARE(chosen, QList<double>({8}));
    QCOMPARE(field.text(), QString("8"));
    QTest::keyClick(&field, Qt::Key_Return);  // nothing typed: no choice
    QCOMPARE(chosen.size(), 1);
    field.selectAll();
    QTest::keyClicks(&field, "15");
    QTest::keyClick(&field, Qt::Key_Return);
    QCOMPARE(chosen.last(), 10.0);
    // Letters are not typed; empty goes back to the value; Esc puts it back.
    field.selectAll();
    QTest::keyClicks(&field, "x");
    QCOMPARE(field.text(), QString("10"));
    field.selectAll();
    QTest::keyClick(&field, Qt::Key_Backspace);
    QTest::keyClick(&field, Qt::Key_Return);
    QCOMPARE(field.text(), QString("10"));
    QCOMPARE(chosen.size(), 2);
    field.selectAll();
    QTest::keyClicks(&field, "3");
    QTest::keyClick(&field, Qt::Key_Escape);
    QCOMPARE(field.text(), QString("10"));
    // + stops at the maximum, − takes one; from a number typed and not confirmed yet.
    QTest::mouseClick(&field, Qt::LeftButton, {}, field.plusRect().center());
    QCOMPARE(chosen.size(), 2);
    QTest::mouseClick(&field, Qt::LeftButton, {}, field.minusRect().center());
    QCOMPARE(chosen.last(), 9.0);
    field.selectAll();
    QTest::keyClicks(&field, "4");
    QTest::mouseClick(&field, Qt::LeftButton, {}, field.plusRect().center());
    QCOMPARE(chosen.last(), 5.0);
    QCOMPARE(field.text(), QString("5"));
    // Shift+arrows and Page Up/Down: ten steps, within the range.
    QTest::keyClick(&field, Qt::Key_Down, Qt::ShiftModifier);
    QCOMPARE(chosen.last(), 0.0);
    QTest::keyClick(&field, Qt::Key_PageUp);
    QCOMPARE(chosen.last(), 10.0);
    // No value (different values): − and + step each item when asked.
    QList<double> steps;
    connect(&field, &NumberField::valueStepped, &field, [&steps](double delta) { steps.append(delta); });
    field.setStepsEachItem(true);
    field.clearValue();
    QTest::mouseClick(&field, Qt::LeftButton, {}, field.plusRect().center());
    QCOMPARE(steps, QList<double>({1}));
    QCOMPARE(field.text(), QString());

    // In the task panel: the duration typed as a number, − and + one day each, at least 1.
    setPlan({task("A", kMonday, 1)});
    QTreeView *table = m_view->table();
    m_view->togglePanel();
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, table->visualRect(table->model()->index(0, 0)).center());
    NumberField *duration = m_view->panel()->durationField();
    QCOMPARE(duration->text(), QString("1"));
    duration->setFocus();
    duration->selectAll();
    QTest::keyClicks(duration, "15");
    QTest::keyClick(duration, Qt::Key_Return);
    QCOMPARE(m_document->model()->gantt().task("A")->duration, 15);
    QTest::mouseClick(duration, Qt::LeftButton, {}, duration->plusRect().center());
    QCOMPARE(m_document->model()->gantt().task("A")->duration, 16);
    QCOMPARE(duration->text(), QString("16"));
    duration->selectAll();
    QTest::keyClicks(duration, "1");
    QTest::keyClick(duration, Qt::Key_Return);
    QTest::mouseClick(duration, Qt::LeftButton, {}, duration->minusRect().center());
    QCOMPARE(m_document->model()->gantt().task("A")->duration, 1);
}

// A milestone is a topic with a checkbox (FEAT-048): the panel's switch adds or removes it; a
// diamond on the start day; the duration is kept and comes back when it is turned off. Duration and
// End are read-only meanwhile (panel and table); its % is the checkbox.
void GanttViewTest::milestoneSwitch() {
    setPlan({task("A", kMonday, 3), task("B", kMonday, 2)}, {dependency("A", "B")});
    m_view->openPanel("A");
    GanttTaskPanel *panel = m_view->panel();
    GanttChart *chart = m_view->chart();
    const QRectF bar = chart->barRect("A");
    const double barWidth = bar.width();
    QVERIFY(!panel->milestoneSwitch()->isChecked());
    panel->milestoneSwitch()->click();
    m_view->refresh();  // a map change: the view refreshes later
    const GanttPlan plan = m_document->model()->gantt();
    QVERIFY(plan.task("A")->milestone);
    QCOMPARE(plan.task("A")->duration, 3);
    QCOMPARE(m_document->model()->nodeById("A")->task, MindNode::Task::Open);
    QCOMPARE(plan.schedule().tasks["A"].end, kMonday);
    QCOMPARE(plan.schedule().tasks["B"].start, kMonday);  // after the milestone: the same day
    QVERIFY(chart->barRect("A").width() < barWidth);      // the diamond
    QCOMPARE(chart->barRect("A").center().x(), bar.left() + chart->dayWidth() / 2.0);  // in the middle of its day
    // Done (checkbox ticked): a darker diamond.
    const auto centre = [chart] { return chart->viewport()->grab().toImage().pixelColor(chart->barRect("A").center().toPoint()); };
    const QColor open = centre();
    QVERIFY(m_document->setTask({m_document->model()->nodeById("A")}, MindNode::Task::Done));
    m_document->flushPendingStep();
    m_view->refresh();
    QVERIFY(centre().lightness() < open.lightness());
    m_document->undo();
    m_view->refresh();
    QVERIFY(panel->milestoneSwitch()->isChecked());
    // A milestone has a start only (user, 2026-10-29).
    QVERIFY(!panel->durationField()->isVisibleTo(panel) && !panel->endField()->isVisibleTo(panel));
    QVERIFY(panel->startField()->isVisibleTo(panel));
    QCOMPARE(panel->durationField()->text(), QString("3"));
    QAbstractItemModel *table = m_view->table()->model();
    QVERIFY(!(table->flags(table->index(0, 3)) & Qt::ItemIsEditable));
    QVERIFY(!(table->flags(table->index(0, 2)) & Qt::ItemIsEditable));
    QVERIFY(table->flags(table->index(1, 3)) & Qt::ItemIsEditable);
    // 100% checks the checkbox, less empties it; checking it on the map gives 100%.
    MindNode *a = m_document->model()->nodeById("A");
    QVERIFY(table->setData(table->index(0, 4), 100));
    QCOMPARE(a->task, MindNode::Task::Done);
    QCOMPARE(m_document->model()->gantt().task("A")->progress, 100);
    QVERIFY(table->setData(table->index(0, 4), 40));
    QCOMPARE(a->task, MindNode::Task::Open);
    QCOMPARE(m_document->model()->gantt().task("A")->progress, 0);
    QVERIFY(m_document->toggleTask(a));
    QCOMPARE(m_document->model()->gantt().task("A")->progress, 100);
    QVERIFY(m_document->toggleTask(a));
    // One step; Undo turns it back into a task.
    m_document->undo();
    m_document->undo();
    m_document->undo();
    m_document->undo();
    m_document->undo();
    m_view->refresh();
    QCOMPARE(m_document->model()->nodeById("A")->task, MindNode::Task::None);  // the sheet is reloaded
    QVERIFY(!m_document->model()->gantt().task("A")->milestone);
    QVERIFY(!panel->milestoneSwitch()->isChecked() && panel->durationField()->isVisibleTo(panel));
    panel->milestoneSwitch()->click();
    panel->milestoneSwitch()->click();
    QCOMPARE(m_document->model()->gantt().schedule().tasks["A"].end, kMonday.addDays(2));

    // With several tasks selected the switch acts on all of them, in one step; a checked
    // checkbox stays checked.
    setPlan({task("A", kMonday, 1), task("B", kMonday, 1), task("C", kMonday, 1), task("D", kMonday, 1)});
    m_view->refresh();
    MindNode *c = m_document->model()->nodeById("C");
    c->task = MindNode::Task::Done;
    m_document->startUntitled();
    m_view->refresh();
    QWidget *bars = chart->viewport();
    QTest::mouseClick(bars, Qt::LeftButton, {}, chart->barRect("A").center().toPoint());
    QTest::mouseClick(bars, Qt::LeftButton, Qt::ShiftModifier, chart->barRect("C").center().toPoint());
    QCOMPARE(m_view->panel()->taskId(), QString("A"));
    panel->milestoneSwitch()->click();
    const auto state = [this](const char *id) { return m_document->model()->nodeById(id)->task; };
    QCOMPARE(state("A"), MindNode::Task::Open);
    QCOMPARE(state("B"), MindNode::Task::Open);
    QCOMPARE(state("C"), MindNode::Task::Done);
    QCOMPARE(state("D"), MindNode::Task::None);
    m_view->refresh();
    panel->milestoneSwitch()->click();  // off for all three
    QCOMPARE(state("A"), MindNode::Task::None);
    QCOMPARE(state("C"), MindNode::Task::None);
    m_document->undo();
    m_document->undo();
    QCOMPARE(state("B"), MindNode::Task::None);
    QCOMPARE(state("C"), MindNode::Task::Done);
}

// The panel's Colour is the bar's: the topic's branch colour, one step; × goes back to the default.
void GanttViewTest::panelColour() {
    setPlan({task("A", kMonday, 3), task("B", kMonday, 2)});
    m_view->openPanel("A");
    GanttTaskPanel *panel = m_view->panel();
    ColorField *field = panel->colorField();
    MindNode *a = m_document->model()->nodeById("A");
    QCOMPARE(field->color(), QColor(0xf9, 0xbd, 0x89));  // peach
    QVERIFY(!field->isManual() && panel->defaultColorButton()->isHidden());
    QVERIFY(field->borderColor.isValid());  // the panel's field border, not the side panel's
    emit field->colorChosen(QColor("#22c55e"));
    m_view->refresh();
    QVERIFY(NodeStyle::hasManualValue(a, StyleProperty::BranchColor));
    QCOMPARE(a->branchColor, QColor("#22c55e"));
    QCOMPARE(field->color(), QColor("#22c55e"));
    QVERIFY(field->isManual() && !panel->defaultColorButton()->isHidden());
    // Back to the default, then Undo brings the colour back.
    panel->defaultColorButton()->click();
    m_view->refresh();
    QVERIFY(!NodeStyle::hasManualValue(a, StyleProperty::BranchColor));
    QVERIFY(panel->defaultColorButton()->isHidden());
    m_document->undo();
    m_view->refresh();
    QCOMPARE(m_document->model()->nodeById("A")->branchColor, QColor("#22c55e"));
    // Two tasks with different colours: mixed; a choice colours both.
    GanttChart *chart = m_view->chart();
    QTest::mouseClick(chart->viewport(), Qt::LeftButton, {}, chart->barRect("A").center().toPoint());
    QTest::mouseClick(chart->viewport(), Qt::LeftButton, Qt::ShiftModifier, chart->barRect("B").center().toPoint());
    QCOMPARE(panel->targets().size(), 2);
    QVERIFY(field->isMixed());
    emit field->colorChosen(QColor("#3b82f6"));
    m_view->refresh();
    QCOMPARE(m_document->model()->nodeById("A")->branchColor, QColor("#3b82f6"));
    QCOMPARE(m_document->model()->nodeById("B")->branchColor, QColor("#3b82f6"));
    QVERIFY(!field->isMixed());
}

// Shift+click selects the rows between the last task clicked and this one, on the bars and in the
// table; Ctrl+Shift adds them; the panel keeps its task.
void GanttViewTest::shiftSelection() {
    setPlan({task("A", kMonday, 1), task("B", kMonday, 1), task("C", kMonday, 1), task("D", kMonday, 1), task("E", kMonday, 1)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    QWidget *bars = chart->viewport();
    const auto click = [&](const QString &id, Qt::KeyboardModifiers modifiers = {}) {
        QTest::mouseClick(bars, Qt::LeftButton, modifiers, chart->barRect(id).center().toPoint());
    };
    const auto selected = [&] {
        QStringList ids = m_view->selectedIds();
        ids.sort();
        return ids;
    };
    m_view->togglePanel();
    click("B");
    click("D", Qt::ShiftModifier);
    QCOMPARE(selected(), QStringList({"B", "C", "D"}));
    QCOMPARE(m_view->panel()->taskId(), QString("B"));
    click("A", Qt::ShiftModifier);  // from B again, upwards
    QCOMPARE(selected(), QStringList({"A", "B"}));
    click("E", Qt::ControlModifier);
    click("D", Qt::ControlModifier | Qt::ShiftModifier);  // from E, added
    QCOMPARE(selected(), QStringList({"A", "B", "D", "E"}));
    // In the table.
    QTreeView *table = m_view->table();
    const auto row = [&](int r) { return table->visualRect(table->model()->index(r, 0)).center(); };
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, row(1));
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::ShiftModifier, row(3));
    QCOMPARE(selected(), QStringList({"B", "C", "D"}));
    QCOMPARE(m_view->panel()->taskId(), QString("B"));
}

// With several tasks selected the panel edits all of them, one step per change: a value they do
// not share shows none ("—"), − and + change each from its own; name and links are one task's.
void GanttViewTest::panelClosing() {
    // FEAT-081: every section closes from its title, kept for a new panel; adding opens it again.
    setPlan({task("A", kMonday, 2), task("B", kMonday, 2)});
    m_view->refresh();
    m_view->openPanel("B");
    GanttTaskPanel *panel = m_view->panel();
    auto *details = panel->findChild<QWidget *>("GanttSectionDetails");
    QVERIFY(panel->sectionOpen("GanttSectionDetails") && panel->priorityField()->isVisibleTo(panel));
    QWidget *title = details->findChild<QLabel *>("SectionTitle")->parentWidget();
    QTest::mouseClick(title, Qt::LeftButton);
    QVERIFY(!panel->sectionOpen("GanttSectionDetails") && !panel->priorityField()->isVisibleTo(panel));
    QCOMPARE(details->findChild<QLabel *>("SectionChevron")->text(), QString("▸"));
    QCOMPARE(QSettings().value("gantt/closedSections").toStringList(), QStringList({"GanttSectionDetails"}));
    {
        GanttTaskPanel other;
        QVERIFY(!other.sectionOpen("GanttSectionDetails") && other.sectionOpen("GanttSectionDates"));
    }
    QTest::mouseClick(title, Qt::LeftButton);
    QVERIFY(panel->priorityField()->isVisibleTo(panel));
    QCOMPARE(details->findChild<QLabel *>("SectionChevron")->text(), QString("▾"));
    // "+ Add" stays on a closed section's title; a person added opens it.
    panel->setSectionOpen("GanttSectionResources", false);
    GanttResources resources;
    resources.people = {{"p1", "Anna", "Rossi", "", "Developer"}};
    QVERIFY(m_document->setResources(resources, m_document->model()->gantt().calendar()));
    m_view->refresh();
    QVERIFY(panel->addPersonButton()->isVisibleTo(panel) && !panel->peopleTable()->isVisibleTo(panel));
    QMenu menu;
    panel->fillPersonMenu(&menu);
    menu.actions().at(0)->trigger();
    QVERIFY(panel->sectionOpen("GanttSectionResources") && panel->peopleTable()->isVisibleTo(panel));
    QCOMPARE(panel->shownPeople(), QStringList({"p1"}));
    // So does a link added to a closed Successors.
    m_view->openPanel("A");
    panel->setSectionOpen("GanttSectionSuccessors", false);
    QVERIFY(panel->addLink(false, "B"));
    QVERIFY(panel->sectionOpen("GanttSectionSuccessors"));
}

void GanttViewTest::panelSeveral() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 2, "S"), task("B", kMonday.addDays(1), 4), task("C", kMonday, 2)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    QWidget *bars = chart->viewport();
    m_view->togglePanel();
    QTest::mouseClick(bars, Qt::LeftButton, {}, chart->barRect("B").center().toPoint());
    QTest::mouseClick(bars, Qt::LeftButton, Qt::ControlModifier, chart->barRect("A").center().toPoint());
    QTest::mouseClick(bars, Qt::LeftButton, Qt::ControlModifier, chart->barRect("S").center().toPoint());
    GanttTaskPanel *panel = m_view->panel();
    QCOMPARE(panel->taskId(), QString("B"));
    QCOMPARE(panel->targets().size(), 3);
    // No title above the sections (user's picture, 2026-10-30): the Task section's counts the tasks.
    QVERIFY(!panel->findChild<QLabel *>("GanttPanelTitle"));
    QCOMPARE(panel->findChild<QWidget *>("GanttSectionTask")->findChild<QLabel *>("SectionTitle")->text(), QString("3 task(s)"));
    QVERIFY(!panel->nameField()->isEnabled());
    QVERIFY(panel->progressField()->isEnabled() && panel->durationField()->isEnabled());
    QCOMPARE(panel->durationField()->text(), QString());  // 4 and 2
    QCOMPARE(panel->progressField()->text(), QString("0"));
    // + adds a day to each task (the summary left out), in one step.
    QTest::mouseClick(panel->durationField(), Qt::LeftButton, {}, panel->durationField()->plusRect().center());
    GanttPlan plan = m_document->model()->gantt();
    QCOMPARE(plan.task("A")->duration, 3);
    QCOMPARE(plan.task("B")->duration, 5);
    QCOMPARE(plan.task("C")->duration, 2);  // not selected
    m_document->undo();
    m_view->refresh();
    QCOMPARE(m_document->model()->gantt().task("A")->duration, 2);
    QCOMPARE(m_document->model()->gantt().task("B")->duration, 4);
    // A value typed applies to all of them; so do priority, people and start.
    QCOMPARE(panel->targets().size(), 3);
    panel->progressField()->setText("40");
    emit panel->progressField()->editingFinished();
    plan = m_document->model()->gantt();
    QCOMPARE(plan.task("A")->progress, 40);
    QCOMPARE(plan.task("B")->progress, 40);
    panel->priorityField()->setCurrentIndex(3);
    emit panel->priorityField()->activated(3);
    for (const char *id : {"S", "A", "B"}) QCOMPARE(m_document->model()->nodeById(id)->ganttPriority, 3);
    QCOMPARE(m_document->model()->nodeById("C")->ganttPriority, 0);
    // FEAT-081: a person on some of the tasks is muted; assigned or removed on all of them, the
    // summary left out. "gone" (removed from the list) is dropped when A's people change.
    GanttResources resources;
    resources.people = {{"p1", "Anna", "Rossi", "", "Developer"}};
    QVERIFY(m_document->setResources(resources, m_document->model()->gantt().calendar()));
    m_document->model()->nodeById("A")->ganttPeople = {"p1", "gone"};
    m_view->refresh();
    QCOMPARE(panel->shownPeople(), QStringList({"p1"}));
    QVERIFY(panel->personPartial("p1"));
    QVERIFY(panel->addPersonButton()->isVisibleTo(panel) && panel->addPersonButton()->isEnabled());
    QVERIFY(panel->peopleTable()->item(0, 0)->foreground().color() == GanttColors::light().muted);
    QCOMPARE(panel->personCandidates(), QStringList({"p1"}));
    QMenu menu;
    panel->fillPersonMenu(&menu);
    menu.actions().at(0)->trigger();
    QCOMPARE(m_document->model()->nodeById("B")->ganttPeople, QStringList({"p1"}));
    QVERIFY(m_document->model()->nodeById("S")->ganttPeople.isEmpty());
    QVERIFY(!panel->personPartial("p1"));
    panel->personRemove("p1")->click();
    QVERIFY(m_document->model()->nodeById("A")->ganttPeople.isEmpty());
    QVERIFY(m_document->model()->nodeById("B")->ganttPeople.isEmpty());
    panel->startField()->setDate(kMonday.addDays(7));
    plan = m_document->model()->gantt();
    QCOMPARE(plan.schedule().tasks["A"].start, kMonday.addDays(7));
    QCOMPARE(plan.schedule().tasks["B"].start, kMonday.addDays(7));
    QCOMPARE(plan.schedule().tasks["C"].start, kMonday);
    // One task selected again: the panel is that task's.
    QTest::mouseClick(bars, Qt::LeftButton, {}, chart->barRect("C").center().toPoint());
    QCOMPARE(panel->targets(), QStringList({"C"}));
    QVERIFY(panel->nameField()->isEnabled());
}

// The task panel as the map's Style page (user, 2026-10-28): sections Task, Dates, Details,
// Predecessors, Successors, always open (2026-10-29); controls 148 px wide in one column, inside the
// card; no scrolling at a usual window height.
void GanttViewTest::panelSections() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 3, "S"), task("B", kMonday, 2, "S")}, {dependency("A", "B")});
    m_view->resize(1300, 900);
    m_view->refresh();
    QTreeView *table = m_view->table();
    const QModelIndex b = table->model()->index(1, 0, table->model()->index(0, 0));
    m_view->togglePanel();
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, table->visualRect(b).center());
    GanttTaskPanel *panel = m_view->panel();
    QCOMPARE(panel->taskId(), QString("B"));
    const QStringList names{"GanttSectionTask", "GanttSectionDates", "GanttSectionDetails", "GanttSectionResources", "GanttSectionPredecessors",
                            "GanttSectionSuccessors"};
    int lastY = -1;
    for (const QString &name : names) {
        auto *section = panel->findChild<QWidget *>(name);
        QVERIFY2(section && section->isVisible(), qPrintable(name));
        const int y = section->mapTo(panel, QPoint(0, 0)).y();
        QVERIFY(y > lastY);  // in this order
        lastY = y;
    }
    QVERIFY(!panel->findChild<QToolButton *>(QStringLiteral("SectionToggle")));
    QCOMPARE(panel->findChild<QWidget *>("GanttSectionDates")->findChild<QLabel *>("SectionTitle")->text(), QString("Dates"));
    QVERIFY(panel->findChild<QWidget *>("GanttSectionPredecessors")->isAncestorOf(panel->findChild<QToolButton *>("GanttAddPredecessor")));
    // Fields in their sections, 148 px wide, inside the card.
    QVERIFY(panel->findChild<QWidget *>("GanttSectionTask")->isAncestorOf(panel->nameField()));
    QVERIFY(panel->findChild<QWidget *>("GanttSectionDates")->isAncestorOf(panel->durationField()));
    QVERIFY(panel->findChild<QWidget *>("GanttSectionResources")->isAncestorOf(panel->peopleTable()));
    for (QWidget *field : std::initializer_list<QWidget *>{panel->nameField(), panel->durationField(), panel->endField(),
                                                          panel->progressField(), panel->priorityField()}) {
        QCOMPARE(field->width(), 148);
        QVERIFY(field->mapTo(panel, QPoint(field->width(), 0)).x() <= panel->width() - 16);
    }
    // Everything fits at 760 px with one link: no scroll bar (721 px at 2026-10-29).
    m_view->resize(1300, 760);
    QCoreApplication::processEvents();
    QVERIFY(!panel->findChild<QScrollArea *>()->verticalScrollBar()->isVisible());
    // A summary has no links nor details: their sections go.
    QTest::mouseClick(table->viewport(), Qt::LeftButton, {}, table->visualRect(table->model()->index(0, 0)).center());
    QCOMPARE(panel->taskId(), QString("S"));
    QVERIFY(!panel->findChild<QWidget *>("GanttSectionPredecessors")->isVisible());
    QVERIFY(!panel->findChild<QWidget *>("GanttSectionDetails")->isVisible());  // nor progress and priority (2026-10-30)
}

// A row's name dragged moves its topic as in the outliner (2026-10-29): before, inside (last
// child) or after the target, one step; its own branch refused.
void GanttViewTest::moveRows() {
    setPlan({task("S", kMonday, 1), task("A", kMonday, 2, "S"), task("B", kMonday, 3, "S"), task("C", kMonday, 1)});
    m_view->refresh();
    QVERIFY(m_view->table()->viewport()->acceptDrops());
    MindMapModel *model = m_document->model();
    using Place = GanttView::DropPlace;
    QVERIFY(m_view->moveTask("C", "A", Place::Before));
    MindNode *s = model->nodeById("S");
    QCOMPARE(model->nodeById("C")->parent, s);
    QCOMPARE(s->children.front()->id, QString("C"));
    QCOMPARE(m_view->selectedIds(), QStringList({"C"}));
    QVERIFY(m_view->moveTask("C", "B", Place::After));
    QCOMPARE(s->children.back()->id, QString("C"));
    QVERIFY(!m_view->moveTask("C", "B", Place::After));  // already there
    QVERIFY(m_view->moveTask("A", "C", Place::Inside));
    QCOMPARE(model->nodeById("A")->parent, model->nodeById("C"));
    QVERIFY(!m_view->moveTask("S", "A", Place::Inside));  // into its own branch
    QVERIFY(m_view->moveTask("C", "S", Place::After));    // a main topic again, with A
    QCOMPARE(model->nodeById("C")->parent, model->root());
    QCOMPARE(model->nodeById("A")->parent, model->nodeById("C"));
    m_document->undo();
    QCOMPARE(model->nodeById("C")->parent, model->nodeById("S"));
}

// Delete at the bottom of the panel (user's picture, 2026-10-29): after a question, the selected
// tasks' topics go in one step and the panel closes.
void GanttViewTest::panelRemove() {
    setPlan({task("A", kMonday, 2), task("B", kMonday, 3), task("C", kMonday, 1)});
    m_view->refresh();
    GanttChart *chart = m_view->chart();
    m_view->togglePanel();
    QTest::mouseClick(chart->viewport(), Qt::LeftButton, {}, chart->barRect("A").center().toPoint());
    QTest::mouseClick(chart->viewport(), Qt::LeftButton, Qt::ControlModifier, chart->barRect("B").center().toPoint());
    GanttTaskPanel *panel = m_view->panel();
    auto *remove = panel->findChild<QPushButton *>("GanttPanelRemove");
    QVERIFY(remove && remove->isVisible());
    const auto answer = [](bool confirm) {
        QTimer::singleShot(0, [confirm] {
            auto *modal = qobject_cast<ModalDialog *>(QApplication::activeModalWidget());
            QVERIFY(modal);
            (confirm ? modal->acceptButton() : modal->cancelButton())->click();
        });
    };
    answer(false);  // Cancel: nothing deleted
    remove->click();
    QVERIFY(m_document->model()->nodeById("A") && panel->isVisible());
    answer(true);
    remove->click();
    QVERIFY(!m_document->model()->nodeById("A") && !m_document->model()->nodeById("B"));
    QVERIFY(m_document->model()->nodeById("C"));
    QVERIFY(panel->isVisible() && panel->showsNoTask());
    m_document->undo();
    QVERIFY(m_document->model()->nodeById("A") && m_document->model()->nodeById("B"));
}

// FEAT-079: the Resources modal edits a copy of the people and of the working week.
void GanttViewTest::resourcesDialog() {
    const QLocale locale;
    QLocale::setDefault(QLocale(QLocale::Italian, QLocale::Italy));
    QCOMPARE(ResourcesDialog::formatCost(4500, "€").remove(QChar(0x00a0)).remove(' '), QString("45,00€"));
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
    QCOMPARE(ResourcesDialog::formatCost(4500, "€"), QString("€45.00"));
    QVERIFY(ResourcesDialog::formatCost(4500, "CHF").contains("CHF"));

    GanttResources start;
    start.people = {{"id-1", "Anna", "Rossi", "anna@example.com", "Developer", 4500}};
    GanttCalendar calendar;
    calendar.holidays.insert(kMonday);
    ResourcesDialog dialog(nullptr, start, calendar);
    dialog.resize(1200, 900);
    dialog.show();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QTableWidget *table = dialog.table();
    // The open cell editor (a closed one waits for its deferred delete, hidden).
    const auto cellEditor = [table]() -> QLineEdit * {
        for (QLineEdit *editor : table->viewport()->findChildren<QLineEdit *>())
            if (editor->isVisible()) return editor;
        return nullptr;
    };
    QCOMPARE(table->rowCount(), 1);
    QVERIFY(!dialog.emptyLabel()->isVisible());
    QCOMPARE(table->viewport()->height(), ResourcesDialog::kMinVisibleRows * ResourcesDialog::kRowHeightPx);
    QCOMPARE(table->model()->index(0, ResourcesDialog::HourlyCost).data(Qt::DisplayRole).toLongLong(), 4500);
    QVERIFY(dialog.saturdaySwitch()->isChecked() && dialog.sundaySwitch()->isChecked());
    QCOMPARE(dialog.hoursField()->value(), 8.0);
    QCOMPARE(dialog.resources(), start);  // nothing changed: the same list, the same id
    QCOMPARE(dialog.calendar(), calendar);

    // "+ Add person": a new row with its first name being edited; Enter confirms the cell only.
    dialog.addButton()->click();
    QCOMPARE(table->rowCount(), 2);
    auto *editor = cellEditor();
    QVERIFY(editor);
    // User's picture, 2026-10-30: the field covers the whole cell (it was shorter, inside the padding),
    // and the table shows at least three whole rows (one was cut at the bottom).
    QCOMPARE(table->viewport()->height(), ResourcesDialog::kMinVisibleRows * ResourcesDialog::kRowHeightPx);
    QCOMPARE(editor->geometry(), table->visualItemRect(table->item(1, ResourcesDialog::FirstName)));
    QTest::keyClicks(editor, "Marco");
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(dialog.isVisible());
    QTRY_COMPARE(table->item(1, ResourcesDialog::FirstName)->text(), QString("Marco"));  // committed queued by Qt
    // First name, last name and role are required once a row is begun (user, 2026-10-30).
    QVERIFY(!dialog.acceptButton()->isEnabled());
    QVERIFY(table->item(1, ResourcesDialog::FirstName)->toolTip().isEmpty());
    QCOMPARE(table->item(1, ResourcesDialog::LastName)->toolTip(), QString("Required"));
    QCOMPARE(table->item(1, ResourcesDialog::Role)->toolTip(), QString("Required"));
    QVERIFY(table->item(1, ResourcesDialog::Email)->toolTip().isEmpty());  // the email is not
    table->item(1, ResourcesDialog::LastName)->setText("Bianchi");
    table->item(1, ResourcesDialog::Role)->setText("  ");  // blank is empty
    QVERIFY(!dialog.acceptButton()->isEnabled());
    table->item(1, ResourcesDialog::Role)->setText("Designer");
    QVERIFY(dialog.acceptButton()->isEnabled());
    QVERIFY(table->item(1, ResourcesDialog::LastName)->toolTip().isEmpty());
    QVERIFY(table->item(1, ResourcesDialog::Role)->toolTip().isEmpty());
    // A wrong email marks the cell and disables Save until it is fixed.
    table->item(1, ResourcesDialog::Email)->setText("marco");
    QVERIFY(!dialog.acceptButton()->isEnabled());
    QCOMPARE(table->item(1, ResourcesDialog::Email)->toolTip(), QString("Invalid email address"));
    table->item(1, ResourcesDialog::Email)->setText("marco@example.com");
    QVERIFY(dialog.acceptButton()->isEnabled());
    QVERIFY(table->item(1, ResourcesDialog::Email)->toolTip().isEmpty());
    // The cost is typed in the locale's format and shown as money.
    table->setCurrentCell(1, ResourcesDialog::HourlyCost);
    table->editItem(table->item(1, ResourcesDialog::HourlyCost));
    editor = cellEditor();
    QVERIFY(editor);
    QTest::keyClicks(editor, "38.5");
    QTest::keyClick(editor, Qt::Key_Tab);
    QCOMPARE(table->item(1, ResourcesDialog::HourlyCost)->data(Qt::EditRole).toLongLong(), 3850);
    // The currency: the four listed, then "+ Add currency..." (resourcesButton adds one).
    QComboBox *currency = dialog.currencySelect();
    QVERIFY(!currency->isEditable());
    QCOMPARE(currency->currentText(), QString("EUR €"));
    QCOMPARE(currency->count(), 5);
    QCOMPARE(currency->itemText(3), QString("CHF"));
    QCOMPARE(dialog.addCurrencyItem(), 4);
    QCOMPARE(currency->itemText(4), QString("+ Add currency…"));
    currency->setCurrentIndex(1);
    QCOMPARE(currency->currentText(), QString("USD $"));
    QVERIFY(table->model()->index(1, ResourcesDialog::HourlyCost).data(Qt::DisplayRole).isValid());

    // An empty row is dropped and does not disable Save; the × removes a row.
    dialog.addPerson();
    QTest::keyClick(cellEditor(), Qt::Key_Escape);  // the editor only
    QVERIFY(dialog.isVisible());
    QCOMPARE(table->rowCount(), 3);
    QVERIFY(dialog.acceptButton()->isEnabled());
    QVERIFY(table->item(2, ResourcesDialog::FirstName)->toolTip().isEmpty());
    GanttResources result = dialog.resources();
    QCOMPARE(result.currency, QString("USD"));
    QCOMPARE(result.people.size(), 2);
    QCOMPARE(result.people[0].id, QString("id-1"));
    QCOMPARE(result.people[1].firstName, QString("Marco"));
    QCOMPARE(result.people[1].hourlyCostCents, 3850);
    // Each row's number before the first name; dragged, it moves the person and the numbers stay
    // in order (user, 2026-10-30).
    QHeaderView *numbers = table->verticalHeader();
    QVERIFY(numbers->isVisible() && numbers->sectionsMovable());
    QCOMPARE(numbers->width(), 32);
    QCOMPARE(table->model()->headerData(1, Qt::Vertical).toString(), QString("2"));
    numbers->moveSection(1, 0);  // as "2" dropped on "1"
    QCOMPARE(numbers->logicalIndex(0), 0);  // the header back in order: the rows moved instead
    QCOMPARE(table->item(0, ResourcesDialog::FirstName)->text(), QString("Marco"));
    QCOMPARE(table->item(1, ResourcesDialog::FirstName)->text(), QString("Anna"));
    QCOMPARE(dialog.resources().people.value(1).id, QString("id-1"));
    QCOMPARE(dialog.resources().people.value(0).hourlyCostCents, 3850);
    QVERIFY(table->cellWidget(0, ResourcesDialog::Remove)->findChild<QToolButton *>(QStringLiteral("ResourcesRemove")));
    QVERIFY(dialog.acceptButton()->isEnabled());
    dialog.movePerson(0, 1);
    QCOMPARE(dialog.resources().people.value(0).id, QString("id-1"));
    QCOMPARE(dialog.resources().people.value(1).firstName, QString("Marco"));
    auto *remove = table->cellWidget(0, ResourcesDialog::Remove)->findChild<QToolButton *>(QStringLiteral("ResourcesRemove"));
    QVERIFY(remove);
    remove->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(table->rowCount(), 2);
    QCOMPARE(dialog.resources().people.value(0).firstName, QString("Marco"));
    for (int row = table->rowCount() - 1; row >= 0; --row)
        table->cellWidget(row, ResourcesDialog::Remove)->findChild<QToolButton *>()->click();
    QVERIFY(dialog.emptyLabel()->isVisible());
    QVERIFY(!table->isVisible());

    // Working week: Saturday and Sunday off, 6 h 30; the holiday stays.
    dialog.saturdaySwitch()->click();
    dialog.sundaySwitch()->click();
    dialog.hoursField()->setValue(6);
    dialog.minutesSelect()->setCurrentIndex(dialog.minutesSelect()->findData(30));
    GanttCalendar week = dialog.calendar();
    QCOMPARE(week.workingDays, QSet<int>({1, 2, 3, 4, 5}));
    QCOMPARE(week.minutesPerDay, 6 * 60 + 30);
    dialog.hoursField()->setValue(24);  // 24 h 30 is cut to 24 h, 0 h 00 raised to a quarter hour
    QCOMPARE(dialog.calendar().minutesPerDay, 1440);
    dialog.hoursField()->setValue(0);
    dialog.minutesSelect()->setCurrentIndex(0);
    QCOMPARE(dialog.calendar().minutesPerDay, 15);
    week = dialog.calendar();
    QCOMPARE(week.holidays, calendar.holidays);
    QLocale::setDefault(locale);
}

// FEAT-079: the button opens the modal; Save is one step that Undo takes back, Cancel changes nothing.
void GanttViewTest::resourcesButton() {
    MainWindow window;
    window.resize(1400, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *document = window.findChild<Document *>();
    QVERIFY(document);
    auto *button = window.findChild<QToolButton *>(QStringLiteral("ganttResourcesButton"));
    QVERIFY(button);
    const auto openAnd = [&button](const std::function<void(ResourcesDialog *)> &act) {
        bool seen = false;
        QTimer::singleShot(0, [&] {
            auto *dialog = qobject_cast<ResourcesDialog *>(QApplication::activeModalWidget());
            QVERIFY(dialog);
            seen = true;
            act(dialog);
        });
        button->click();
        QVERIFY(seen);
    };
    // "+ Add currency..." opens the Add currency modal (after the list has closed).
    const auto addCurrency = [](ResourcesDialog *dialog, const std::function<void(ModalDialog *)> &act) {
        bool seen = false;
        dialog->currencySelect()->setCurrentIndex(dialog->addCurrencyItem());
        QTimer::singleShot(0, [&] {
            auto *add = qobject_cast<ModalDialog *>(QApplication::activeModalWidget());
            QVERIFY(add && add != dialog);
            seen = true;
            act(add);
        });
        QTRY_VERIFY(seen);
    };
    const auto typeCurrency = [](ModalDialog *add, const QString &code, const QString &symbol) {
        auto *codeField = add->findChild<QLineEdit *>(QStringLiteral("AddCurrencyCode"));
        auto *symbolField = add->findChild<QLineEdit *>(QStringLiteral("AddCurrencySymbol"));
        QVERIFY(codeField && symbolField);
        QTest::keyClicks(codeField, code);
        QTest::keyClicks(symbolField, symbol);
    };
    openAnd([](ResourcesDialog *dialog) { dialog->reject(); });
    QVERIFY(!document->isDirty());
    openAnd([&](ResourcesDialog *dialog) {
        dialog->addPerson();
        auto *editor = dialog->table()->viewport()->findChild<QLineEdit *>();
        QVERIFY(editor);
        // Tab goes on to the next cell's editor: last name, email (left empty), role.
        for (const char *text : {"Anna", "Rossi", "", "Developer"}) {
            editor = nullptr;  // the open one (a closed one waits for its deferred delete, hidden)
            for (QLineEdit *field : dialog->table()->viewport()->findChildren<QLineEdit *>())
                if (field->isVisible()) editor = field;
            QVERIFY(editor);
            QTest::keyClicks(editor, text);
            QTest::keyClick(editor, Qt::Key_Tab);
        }
        QTRY_COMPARE(dialog->table()->item(0, ResourcesDialog::Role)->text(), QString("Developer"));
        QVERIFY(dialog->acceptButton()->isEnabled());
        dialog->sundaySwitch()->click();
        QComboBox *select = dialog->currencySelect();
        // Cancelled: the shown currency stays, nothing saved.
        addCurrency(dialog, [](ModalDialog *add) {
            QVERIFY(!add->acceptButton()->isEnabled());  // no code yet
            add->reject();
        });
        QCOMPARE(select->currentText(), QString("EUR €"));
        QCOMPARE(dialog->resources().currency, QString("EUR"));
        // A listed one is not added: the note says so and Add is off. Spaces are not typed.
        addCurrency(dialog, [&](ModalDialog *add) {
            typeCurrency(add, "USD", "US $");
            QCOMPARE(add->findChild<QLineEdit *>(QStringLiteral("AddCurrencySymbol"))->text(), QString("US$"));
            QVERIFY(add->findChild<QLabel *>(QStringLiteral("AddCurrencyNote"))->isVisible());
            QVERIFY(!add->acceptButton()->isEnabled());
            add->reject();
        });
        addCurrency(dialog, [&](ModalDialog *add) {
            typeCurrency(add, "RON", "lei");
            QVERIFY(!add->findChild<QLabel *>(QStringLiteral("AddCurrencyNote"))->isVisible());
            QVERIFY(add->acceptButton()->isEnabled());
            add->accept();
        });
        // Added: saved on the computer at once, chosen, before "+ Add currency...".
        QCOMPARE(GanttResources::savedCurrencies(), QStringList{"RON lei"});
        QCOMPARE(select->count(), 6);
        QCOMPARE(select->currentText(), QString("RON lei"));
        QCOMPARE(dialog->addCurrencyItem(), 5);
        // Removed while chosen, not the document's: its row goes at once, back to the document's.
        dialog->removeSavedCurrency(4);
        QVERIFY(GanttResources::savedCurrencies().isEmpty());
        QCOMPARE(select->count(), 5);
        QCOMPARE(select->currentText(), QString("EUR €"));
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::activeModalWidget(), dialog);  // the Add currency modal not opened
        addCurrency(dialog, [&](ModalDialog *add) {
            typeCurrency(add, "RON", "lei");
            add->accept();
        });
        QCOMPARE(dialog->resources().currencySymbol, QString("lei"));
        dialog->accept();
    });
    QCOMPARE(document->resources().people.value(0).firstName, QString("Anna"));
    QVERIFY(!document->model()->gantt().calendar().workingDays.contains(Qt::Sunday));
    QVERIFY(document->isDirty());
    QCOMPARE(document->undo().outcome, Document::StepOutcome::Applied);
    QVERIFY(document->resources().people.isEmpty());
    QVERIFY(document->model()->gantt().calendar().workingDays.contains(Qt::Sunday));
    QCOMPARE(document->redo().outcome, Document::StepOutcome::Applied);
    QCOMPARE(document->resources().currencyText(), QString("RON lei"));
    QCOMPARE(GanttResources::savedCurrencies(), QStringList{"RON lei"});

    // The document's own currency removed from the computer: its row stays, without the ×.
    int rows = 0, rowsHeight = 0;
    openAnd([&](ResourcesDialog *dialog) {
        QComboBox *select = dialog->currencySelect();
        QCOMPARE(select->currentText(), QString("RON lei"));  // read from the document
        QCOMPARE(select->count(), 6);
        // In the main window's style too: three whole rows with Anna alone (user's picture, 2026-10-30).
        rows = dialog->table()->rowCount();
        rowsHeight = dialog->table()->viewport()->height();
        dialog->removeSavedCurrency(4);
        QVERIFY(GanttResources::savedCurrencies().isEmpty());
        QCOMPARE(select->count(), 6);
        QVERIFY(!select->itemData(4, Qt::ToolTipRole).isValid());
        // Added again while chosen: its row becomes the saved one, not a second row.
        addCurrency(dialog, [&](ModalDialog *add) {
            typeCurrency(add, "RON", "lei");
            add->accept();
        });
        QCOMPARE(GanttResources::savedCurrencies(), QStringList{"RON lei"});
        QCOMPARE(select->count(), 6);
        QCOMPARE(select->currentText(), QString("RON lei"));
        QVERIFY(select->itemData(4, Qt::ToolTipRole).isValid());  // its ×
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::activeModalWidget(), dialog);  // the Add currency modal not opened again
        // A click on the × removes it from the computer and does not choose it; the document's row stays.
        select->setCurrentIndex(0);
        select->showPopup();
        QAbstractItemView *list = select->view();
        QTRY_VERIFY(list->isVisible());
        const QRect row = list->visualRect(list->model()->index(4, 0));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(row.right() - 12, row.center().y()));
        QCOMPARE(select->count(), 6);
        QCOMPARE(select->currentText(), QString("EUR €"));
        QVERIFY(GanttResources::savedCurrencies().isEmpty());
        select->hidePopup();
        dialog->reject();  // removing a currency does not wait for the modal's Save
    });
    QCOMPARE(rows, 1);
    QCOMPARE(rowsHeight, ResourcesDialog::kMinVisibleRows * ResourcesDialog::kRowHeightPx);
    QVERIFY(GanttResources::savedCurrencies().isEmpty());

    // Another currency removed with the × while chosen: its row goes, the choice back to the document's.
    openAnd([&](ResourcesDialog *dialog) {
        QComboBox *select = dialog->currencySelect();
        QCOMPARE(select->count(), 6);  // the document's row
        addCurrency(dialog, [&](ModalDialog *add) {
            typeCurrency(add, "SEK", "kr");
            add->accept();
        });
        QCOMPARE(select->currentText(), QString("SEK kr"));
        QCOMPARE(select->count(), 7);
        select->showPopup();
        QAbstractItemView *list = select->view();
        QTRY_VERIFY(list->isVisible());
        const QRect row = list->visualRect(list->model()->index(5, 0));
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, QPoint(row.right() - 12, row.center().y()));
        QCOMPARE(select->count(), 6);
        QCOMPARE(select->currentText(), QString("RON lei"));
        QVERIFY(GanttResources::savedCurrencies().isEmpty());
        select->hidePopup();
        QCoreApplication::processEvents();
        QCOMPARE(QApplication::activeModalWidget(), dialog);
        dialog->reject();
    });

    GanttResources typed;
    for (int number = 0; number < GanttResources::kMaxSavedCurrencies + 1; ++number) {
        typed.setCurrencyText(QStringLiteral("C%1 ¤").arg(number));
        typed.saveCurrency();
        typed.saveCurrency();  // already saved: once
    }
    const QStringList saved = GanttResources::savedCurrencies();
    QCOMPARE(saved.size(), GanttResources::kMaxSavedCurrencies);  // the oldest dropped
    QCOMPARE(saved.front(), QString("C1 ¤"));
    QCOMPARE(saved.back(), QStringLiteral("C%1 ¤").arg(GanttResources::kMaxSavedCurrencies));
    typed.setCurrencyText("USD");
    QVERIFY(!typed.canSaveCurrency());  // listed
}

// User, 2026-10-29: a sheet opens with today three days from the chart's left edge, or the last
// task's end when the plan ended before today, or the first task's start when it has not begun; the same sheet keeps its scroll, another one opens
// near its opening day again.
void GanttViewTest::opensNearToday() {
    GanttChart *chart = m_view->chart();
    const auto daysFromLeft = [chart](const QDate &day) { return chart->xOf(day) / chart->dayWidth(); };
    const QDate today = QDate::currentDate();
    QCOMPARE(chart->openingDay(), today);  // no tasks
    QCOMPARE(daysFromLeft(today), double(GanttChart::kOpenDaysBefore));
    chart->horizontalScrollBar()->setValue(0);
    m_view->refresh();  // the same sheet: kept
    QCOMPARE(chart->horizontalScrollBar()->value(), 0);
    m_view->hide();
    m_document->addSheet();
    m_view->show();
    QTRY_COMPARE(daysFromLeft(today), double(GanttChart::kOpenDaysBefore));

    // A plan over before today opens on its last day; one still running opens on today.
    const QDate past = today.addDays(-40);
    setPlan({task("A", past, 2), task("B", past.addDays(5), 3)});  // a new untitled document
    QCOMPARE(chart->openingDay(), past.addDays(7));
    QCOMPARE(daysFromLeft(past.addDays(7)), double(GanttChart::kOpenDaysBefore));
    m_view->hide();
    m_document->addSheet();
    m_view->show();
    QTRY_COMPARE(daysFromLeft(today), double(GanttChart::kOpenDaysBefore));
    m_view->hide();
    QVERIFY(m_document->switchSheet(0).ok);
    m_view->show();
    QTRY_COMPARE(daysFromLeft(past.addDays(7)), double(GanttChart::kOpenDaysBefore));
    setPlan({task("A", past, 2), task("B", today.addDays(-1), 3)});
    QCOMPARE(chart->openingDay(), today);
    // Not begun yet: its first day (user, 2026-10-30).
    setPlan({task("A", today.addDays(20), 2), task("B", today.addDays(10), 3)});
    QCOMPARE(chart->openingDay(), today.addDays(10));
    QCOMPARE(daysFromLeft(today.addDays(10)), double(GanttChart::kOpenDaysBefore));
}

#include "gantt_view_test.moc"
