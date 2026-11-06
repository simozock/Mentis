// FEAT-093: the guided tours. The map's on a practice map (each step lights its controls, the waiting
// steps go on after the real action, Esc and Skip leave it); the outliner's and the Gantt's offered
// the first time those views open; the first-start offer; the Pitch tip shown once.
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "tour.h"

#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTest>

#include <memory>

class TourTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
        QCoreApplication::setApplicationName(QStringLiteral("MentisTourTest"));
        QSettings().clear();
    }
    void init() {
        m_window = std::make_unique<MainWindow>();
        m_window->resize(1400, 900);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    }
    void cleanup() { m_window.reset(); }

    void mapTourLightsEveryAreaAndWaitsForTheActions();
    void escAndSkipLeaveTheMapAsItIs();
    void onlyTheStepCanBeDone();
    void offeredOnceAtTheFirstStart();
    void outlinerTourOfferedAtItsFirstOpening();
    void ganttTourLightsItsControls();
    void pitchTipShowsOnceAndCanBeTurnedOff();

private:
    GuidedTour *tour() const { return m_window->tour(); }
    MindMapModel *model() const { return m_window->findChild<MindMapModel *>(); }
    MindMapView *view() const { return m_window->findChild<MindMapView *>(); }
    bool homeShown() const {
        return qobject_cast<QStackedWidget *>(m_window->centralWidget())->currentWidget()->objectName() == QLatin1String("homePage");
    }
    static QPushButton *button(QWidget *bubble, const QString &text) {
        for (auto *button : bubble->findChildren<QPushButton *>())
            if (button->isVisible() && button->text() == text) return button;
        return nullptr;
    }
    QPushButton *viewButton(const QString &text) const {
        for (auto *candidate : m_window->findChildren<QPushButton *>())
            if (candidate->isVisible() && candidate->text() == text) return candidate;
        return nullptr;
    }
    // The control is inside a hole of the veil, not dimmed.
    bool lit(const QString &name) const {
        auto *control = m_window->findChild<QWidget *>(name);
        if (!control || !control->isVisible()) return false;
        const QRect rect(m_window->mapFromGlobal(control->mapToGlobal(QPoint())), control->size());
        for (const QRect &hole : tour()->veil()->holes())
            if (hole.contains(rect)) return true;
        return false;
    }
    void settle() {
        QTest::qWait(30);
        tour()->refresh();
    }
    // The step lights something and its bubble stays in the window.
    void checkLit() {
        settle();
        QVERIFY2(!tour()->veil()->hole().isEmpty(), qPrintable(tour()->step()));
        QVERIFY2(m_window->rect().contains(tour()->bubble()->geometry()), qPrintable(tour()->step()));
    }
    void nextUntil(const QString &step) {
        for (int guard = 0; guard < 30 && tour()->step() != step; ++guard) {
            checkLit();
            QPushButton *next = button(tour()->bubble(), QStringLiteral("Next"));
            QVERIFY2(next, qPrintable(tour()->step()));
            next->click();
        }
        QCOMPARE(tour()->step(), step);
    }
    // The practice map with the central topic and one subtopic.
    void startOnPracticeMap() {
        tour()->start();
        button(tour()->bubble(), QStringLiteral("Next"))->click();
        view()->setFocus();
        m_window->findChild<MindMapScene *>()->selectNode(model()->root());
        QTest::keyClick(view(), Qt::Key_F2);
        QTest::keyClicks(view(), QStringLiteral("Plants"));
        QTest::keyClick(view(), Qt::Key_Return);
        settle();
        QTest::keyClick(view(), Qt::Key_Tab);
        QTest::keyClicks(view(), QStringLiteral("Roots"));
        QTest::keyClick(view(), Qt::Key_Return);
        settle();
        tour()->stop();
    }
    std::unique_ptr<MainWindow> m_window;
};

void TourTest::mapTourLightsEveryAreaAndWaitsForTheActions() {
    QVERIFY(homeShown());
    tour()->start();
    QVERIFY(tour()->isRunning());
    QCOMPARE(tour()->step(), QStringLiteral("home"));
    QVERIFY(!button(tour()->bubble(), QStringLiteral("Back")));
    checkLit();

    // Next makes the practice map and opens it.
    button(tour()->bubble(), QStringLiteral("Next"))->click();
    QVERIFY(!homeShown());
    QCOMPARE(tour()->step(), QStringLiteral("centralTopic"));
    MindNode *root = model()->root();
    QVERIFY(root->children.empty());
    QVERIFY(!button(tour()->bubble(), QStringLiteral("Next")));  // a waiting step
    settle();
    QCOMPARE(tour()->step(), QStringLiteral("centralTopic"));   // nothing done yet

    // Type the central topic.
    view()->setFocus();
    m_window->findChild<MindMapScene *>()->selectNode(root);
    QTest::keyClick(view(), Qt::Key_F2);
    QTest::keyClicks(view(), QStringLiteral("Plants"));
    QTest::keyClick(view(), Qt::Key_Return);
    settle();
    QCOMPARE(root->title, QStringLiteral("Plants"));
    QCOMPARE(tour()->step(), QStringLiteral("subtopic"));

    // Tab adds a subtopic, Enter a sibling.
    QTest::keyClick(view(), Qt::Key_Tab);
    settle();
    QCOMPARE(int(root->children.size()), 1);
    QCOMPARE(tour()->step(), QStringLiteral("sibling"));
    QTest::keyClick(view(), Qt::Key_Return);
    settle();
    QCOMPARE(int(root->children.size()), 2);
    QCOMPARE(tour()->step(), QStringLiteral("topicBar"));
    QTest::keyClick(view(), Qt::Key_Return);  // the title confirmed: the topic bar shows
    settle();
    QVERIFY(lit(QStringLiteral("TopicToolbar")));
    // Words selected while writing a title: their bar is lit beside the topic.
    nextUntil(QStringLiteral("textBar"));
    const int holes = tour()->veil()->holes().size();
    QTest::keyClick(view(), Qt::Key_F2);
    QTest::keyClick(view(), Qt::Key_A, Qt::ControlModifier);
    settle();
    QVERIFY(tour()->veil()->holes().size() > holes);
    QTest::keyClick(view(), Qt::Key_Return);
    nextUntil(QStringLiteral("drag"));

    // A topic moved by hand (what a drag leaves).
    root->children.front()->manualOffset = QPointF(40, 0);
    settle();
    QCOMPARE(tour()->step(), QStringLiteral("toolbar"));
    nextUntil(QStringLiteral("style"));
    // The steps on the side panel open it.
    QVERIFY(m_window->findChild<QWidget *>(QStringLiteral("InspectorSidePanel"))->isVisible());
    QVERIFY(!button(tour()->bubble(), QStringLiteral("Next")));
    // The panel and a topic to select each have a hole, with an arrow towards the topic; the bubble
    // stays off the topic.
    settle();
    QVERIFY(tour()->veil()->holes().size() >= 2);
    QCOMPARE(tour()->veil()->arrows().size(), 1);
    for (const QRect &hole : tour()->veil()->holes())
        if (hole.adjusted(-12, -12, 12, 12).contains(tour()->veil()->arrows().first().p2().toPoint()))  // the arrow's hole
            QVERIFY(!tour()->bubble()->geometry().intersects(hole));
    root->children.front()->style.fill = QColor(Qt::red);
    settle();
    QCOMPARE(tour()->step(), QStringLiteral("icons"));
    QVERIFY(m_window->isIconPanelShown());
    settle();
    QCOMPARE(tour()->veil()->arrows().size(), 2);  // towards the topic and the panel's button

    // Every other area: markers, Pitch, focus mode, sheets, the views; then the outliner's and the
    // Gantt's steps in their views, back to the map for the save state, then what is left to
    // discover with the button to report a bug.
    nextUntil(QStringLiteral("list"));
    QVERIFY(m_window->findChild<QWidget *>(QStringLiteral("OutlinePage"))->isVisible());
    nextUntil(QStringLiteral("plan"));
    QVERIFY(!m_window->findChild<QWidget *>(QStringLiteral("OutlinePage"))->isVisible());
    nextUntil(QStringLiteral("save"));
    QVERIFY(view()->isVisible());
    nextUntil(QStringLiteral("discover"));
    QVERIFY(button(tour()->bubble(), QStringLiteral("Report a bug")));
    nextUntil(QStringLiteral("end"));
    QVERIFY(!Tips::current());

    // Back reaches a done waiting step: it has Next and does not wait.
    tour()->back();
    tour()->back();
    while (tour()->step() != QLatin1String("style")) tour()->back();
    QVERIFY(button(tour()->bubble(), QStringLiteral("Next")));
    settle();
    QCOMPARE(tour()->step(), QStringLiteral("style"));
    nextUntil(QStringLiteral("end"));

    // Discard: back to the Home with nothing to save.
    button(tour()->bubble(), QStringLiteral("Discard it"))->click();
    QVERIFY(!tour()->isRunning());
    QVERIFY(homeShown());
    QVERIFY(!m_window->isWindowModified());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!m_window->findChild<QWidget *>(QStringLiteral("TourVeil")));
}

// While the tour runs, the mouse reaches only the controls of a step that asks to use them and the
// window's own buttons, and only the editing keys of the steps work.
void TourTest::onlyTheStepCanBeDone() {
    QAction *focus = nullptr;
    for (QAction *action : m_window->findChildren<QAction *>())
        if (action->isCheckable() && action->text() == QLatin1String("Focus mode")) focus = action;
    QVERIFY(focus);
    const auto centre = [this](const QString &name) {
        auto *control = m_window->findChild<QWidget *>(name);
        return control ? m_window->mapFromGlobal(control->mapToGlobal(control->rect().center())) : QPoint();
    };
    const auto blocked = [this](QPoint at) { return m_window->childAt(at) == tour()->veil(); };

    // Offered: the whole window is blocked but the bubble.
    tour()->offer();
    settle();
    QVERIFY(blocked(QPoint(4, m_window->height() - 4)));
    QVERIFY(!blocked(tour()->bubble()->geometry().center()));
    tour()->stop();

    // The Home step only shows New map.
    tour()->start();
    settle();
    QCOMPARE(tour()->step(), QStringLiteral("home"));
    QVERIFY(tour()->veil()->openings().isEmpty());
    for (QWidget *control : m_window->findChildren<QWidget *>())
        if (control->isVisible() && lit(control->objectName()) && !control->objectName().isEmpty())
            QVERIFY2(blocked(centre(control->objectName())), qPrintable(control->objectName()));

    // A waiting step lets the central topic be used, nothing else.
    button(tour()->bubble(), QStringLiteral("Next"))->click();
    settle();
    QCOMPARE(tour()->step(), QStringLiteral("centralTopic"));
    QVERIFY(!tour()->veil()->openings().isEmpty());
    const QPoint topic = tour()->veil()->openings().first().center();
    QVERIFY(!blocked(topic));
    QVERIFY(blocked(QPoint(4, m_window->height() - 4)));

    // The window stays free: its buttons are open, its drag area is found under the veil.
    int controls = 0;
    for (QWidget *control : m_window->findChildren<QWidget *>())
        if (control->property("controlKind").isValid() && control->isVisible()) {
            ++controls;
            QVERIFY(!blocked(m_window->mapFromGlobal(control->mapToGlobal(control->rect().center()))));
        }
    QVERIFY(controls >= 3);
    bool dragArea = false;
    for (QWidget *area : m_window->findChildren<QWidget *>())
        if (area->property("windowDragArea").toBool() && area->isVisible()) {
            const QPoint corner = m_window->mapFromGlobal(area->mapToGlobal(QPoint(1, 1)));
            if (tour()->veil()->below(corner) == area) dragArea = true;
        }
    QVERIFY(dragArea);

    // The topic bar of the selected central topic is not open while writing it.
    m_window->findChild<MindMapScene *>()->selectNode(model()->root());
    settle();
    QWidget *bar = m_window->findChild<QWidget *>(QStringLiteral("TopicToolbar"));
    QVERIFY(bar && bar->isVisible());
    QVERIFY(blocked(m_window->mapFromGlobal(bar->mapToGlobal(bar->rect().center()))));

    // A window shortcut is blocked; the map's editing keys work.
    view()->setFocus();
    QTest::keyClick(view(), Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
    QVERIFY(!focus->isChecked());
    m_window->findChild<MindMapScene *>()->selectNode(model()->root());
    QTest::keyClick(view(), Qt::Key_Tab);
    QCOMPARE(int(model()->root()->children.size()), 1);

    // After the tour everything works again.
    tour()->stop();
    QTest::qWait(30);
    QTest::keyClick(view(), Qt::Key_F, Qt::ControlModifier | Qt::AltModifier);
    QVERIFY(focus->isChecked());
}

void TourTest::escAndSkipLeaveTheMapAsItIs() {
    tour()->start();
    button(tour()->bubble(), QStringLiteral("Next"))->click();
    model()->root()->title = QStringLiteral("Kept");
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(m_window.get(), &press);
    QVERIFY(!tour()->isRunning());
    QCOMPARE(model()->root()->title, QStringLiteral("Kept"));
    QVERIFY(!homeShown());

    tour()->start();
    QCOMPARE(tour()->step(), QStringLiteral("home"));
    button(tour()->bubble(), QStringLiteral("Skip the tour"))->click();
    QVERIFY(!tour()->isRunning());
    QCOMPARE(model()->root()->title, QStringLiteral("Kept"));
}

void TourTest::offeredOnceAtTheFirstStart() {
    QSettings().remove(QStringLiteral("tour"));
    m_window->offerTourAtFirstStart();
    QVERIFY(tour()->isRunning());
    QVERIFY(tour()->step().isEmpty());  // offering
    QVERIFY(TourSettings::tourOffered());
    button(tour()->bubble(), QStringLiteral("Not now"))->click();
    QVERIFY(!tour()->isRunning());
    m_window->offerTourAtFirstStart();
    QVERIFY(!tour()->isRunning());
    // Start the tour from the offer: its first step.
    tour()->offer();
    button(tour()->bubble(), QStringLiteral("Start the tour"))->click();
    QCOMPARE(tour()->step(), QStringLiteral("home"));
    tour()->stop();
}

void TourTest::outlinerTourOfferedAtItsFirstOpening() {
    startOnPracticeMap();
    QSettings().remove(QStringLiteral("tour"));  // the map's tour showed the outliner already
    viewButton(QStringLiteral("Outliner"))->click();
    QVERIFY(tour()->isRunning());
    QCOMPARE(tour()->kind(), GuidedTour::Kind::Outliner);
    button(tour()->bubble(), QStringLiteral("Show me"))->click();
    QCOMPARE(tour()->step(), QStringLiteral("list"));
    nextUntil(QStringLiteral("end"));
    button(tour()->bubble(), QStringLiteral("Done"))->click();
    QVERIFY(!tour()->isRunning());

    // Not offered again; Not now leaves the Gantt's for the Help menu.
    viewButton(QStringLiteral("Mind map"))->click();
    viewButton(QStringLiteral("Outliner"))->click();
    QVERIFY(!tour()->isRunning());
    viewButton(QStringLiteral("Gantt"))->click();
    QCOMPARE(tour()->kind(), GuidedTour::Kind::Gantt);
    button(tour()->bubble(), QStringLiteral("Not now"))->click();
    QVERIFY(!tour()->isRunning());
    TourSettings::setTipsEnabled(false);
    QSettings().remove(QStringLiteral("tour/seen"));
    viewButton(QStringLiteral("Outliner"))->click();
    QVERIFY(!tour()->isRunning());  // tips off: no offer
    TourSettings::setTipsEnabled(true);
}

void TourTest::ganttTourLightsItsControls() {
    startOnPracticeMap();
    tour()->start(GuidedTour::Kind::Gantt);
    QCOMPARE(tour()->step(), QStringLiteral("plan"));
    nextUntil(QStringLiteral("end"));
    QVERIFY(TourSettings::tipSeen(QStringLiteral("gantt")));  // done: not offered at the first opening
    tour()->back();
    QCOMPARE(tour()->step(), QStringLiteral("timeline"));
    tour()->stop();
}

void TourTest::pitchTipShowsOnceAndCanBeTurnedOff() {
    QSettings().remove(QStringLiteral("tour"));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);  // the editor
    TourSettings::setTipsEnabled(false);
    m_window->showTip(QStringLiteral("pitch"));
    QVERIFY(!Tips::current());
    TourSettings::setTipsEnabled(true);
    m_window->showTip(QStringLiteral("pitch"));
    QVERIFY(Tips::current());
    QVERIFY(m_window->rect().contains(Tips::current()->geometry()));
    QVERIFY(button(Tips::current(), QStringLiteral("Read more")));
    button(Tips::current(), QStringLiteral("Got it"))->click();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(!Tips::current());
    m_window->showTip(QStringLiteral("pitch"));
    QVERIFY(!Tips::current());  // once
}

QTEST_MAIN(TourTest)
#include "tour_test.moc"
