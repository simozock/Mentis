// FEAT-087: focus mode puts the window full screen with Mentis unchanged. Ctrl+Alt+F (the "Focus
// mode" action), the top bar button and an unused Esc toggle it; it stays across views; leaving full
// screen otherwise ends it; the window returns to the state it had.
#include "mainwindow.h"
#include "mindmapmodel.h"
#include "mindmapscene.h"
#include "mindmapview.h"
#include "nodeitem.h"

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

#include <memory>

class FocusModeTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void actionButtonAndEscToggle();
    void staysAcrossViews();
    void leavingFullScreenEndsIt();
    void returnsMaximized();
    void escWhileEditingKeepsIt();
    void fullScreenCornersAreTheFrame();

private:
    QAction *action(const QString &text) const;
    QToolButton *button() const { return m_window->findChild<QToolButton *>(QStringLiteral("FocusModeToggle")); }
    // The platform reports window states later: each step waits for them.
    static void settle() { QTest::qWait(100); }
    void toggle() { action(QStringLiteral("Focus mode"))->trigger(); settle(); }
    void pressEscape() {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(m_window.get(), &press);
    }
    std::unique_ptr<MainWindow> m_window;
};

void FocusModeTest::initTestCase() {
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisFocusModeTest"));
    m_window = std::make_unique<MainWindow>();
    m_window->resize(1200, 800);
    m_window->show();
    QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
    qobject_cast<QStackedWidget *>(m_window->centralWidget())->setCurrentIndex(1);  // the editor
}

void FocusModeTest::cleanupTestCase() { m_window.reset(); }

QAction *FocusModeTest::action(const QString &text) const {
    for (QAction *candidate : m_window->findChildren<QAction *>())
        if (candidate->text() == text) return candidate;
    return nullptr;
}

void FocusModeTest::actionButtonAndEscToggle() {
    QAction *focus = action(QStringLiteral("Focus mode"));
    QVERIFY(focus);
    QVERIFY(focus->shortcuts().contains(QKeySequence(QStringLiteral("Ctrl+Alt+F"))));
    QVERIFY(button() && button()->isVisible());
    toggle();
    QVERIFY(m_window->isFullScreen());
    QVERIFY(focus->isChecked());
    QVERIFY(button()->isChecked());
    toggle();
    QVERIFY(!m_window->isFullScreen());
    QVERIFY(!focus->isChecked() && !button()->isChecked());
    button()->click();
    settle();
    QVERIFY(m_window->isFullScreen());
    QVERIFY(button()->isChecked());
    pressEscape();
    settle();
    QVERIFY(!m_window->isFullScreen());
    QVERIFY(!button()->isChecked());
    pressEscape();  // no focus mode: Esc does nothing to the window
    settle();
    QVERIFY(!m_window->isFullScreen());
}

void FocusModeTest::staysAcrossViews() {
    toggle();
    QVERIFY(m_window->isFullScreen());
    for (const QString view : {QStringLiteral("Switch between map and outliner"), QStringLiteral("Gantt chart"),
                               QStringLiteral("Switch between map and outliner")}) {
        QAction *show = action(view);
        QVERIFY2(show, qPrintable(view));
        show->trigger();
        settle();
        QVERIFY2(m_window->isFullScreen(), qPrintable(view));
        QVERIFY(button()->isVisible());  // in every view
        QVERIFY(button()->isChecked());
    }
    toggle();
    QVERIFY(!m_window->isFullScreen());
}

void FocusModeTest::leavingFullScreenEndsIt() {
    toggle();
    QVERIFY(m_window->isFullScreen());
    m_window->showNormal();  // like Win+Down or the Maximize button
    settle();
    QVERIFY(!m_window->isFullScreen());
    QVERIFY(!button()->isChecked());
    QVERIFY(!action(QStringLiteral("Focus mode"))->isChecked());
    pressEscape();
    settle();
    QVERIFY(!m_window->isFullScreen());
}

void FocusModeTest::returnsMaximized() {
    m_window->showMaximized();
    settle();
    QVERIFY(m_window->isMaximized());
    toggle();
    QVERIFY(m_window->isFullScreen());
    toggle();
    QVERIFY(m_window->isMaximized());
    QVERIFY(!m_window->isFullScreen());
    m_window->showNormal();
    settle();
}

// Esc first closes the topic's text editing; the next Esc exits focus mode.
void FocusModeTest::escWhileEditingKeepsIt() {
    auto *scene = m_window->findChild<MindMapScene *>();
    auto *view = m_window->findChild<MindMapView *>();
    for (int tries = 0; tries < 3 && !view->isVisible(); ++tries) {  // back to the map
        action(QStringLiteral("Switch between map and outliner"))->trigger();
        settle();
    }
    QVERIFY(view->isVisible());
    const MindNode *root = m_window->findChild<MindMapModel *>()->root();
    NodeItem *item = nullptr;
    for (QGraphicsItem *candidate : scene->items())
        if (auto *node = qgraphicsitem_cast<NodeItem *>(candidate); node && node->node() == root) item = node;
    QVERIFY(item);
    toggle();
    QVERIFY(m_window->isFullScreen());
    item->beginEditing();
    QApplication::processEvents();
    QVERIFY(item->isEditing());
    QTest::keyClick(view->viewport(), Qt::Key_Escape);
    settle();
    QVERIFY(!item->isEditing());
    QVERIFY(m_window->isFullScreen());
    QTest::keyClick(view, Qt::Key_Escape);
    settle();
    QVERIFY(!m_window->isFullScreen());
}

// BUG-030: full screen with the dark frame (side panel open): the top corners are the frame's
// colour, not the window's white background outside a rounded corner.
void FocusModeTest::fullScreenCornersAreTheFrame() {
    QAction *panel = action(QStringLiteral("Format panel"));
    QVERIFY(panel);
    panel->trigger();
    settle();
    toggle();
    if (!m_window->isFullScreen()) {  // offscreen with the panel open: a window larger than its screen
        toggle();
        panel->trigger();
        settle();
        QSKIP("the platform did not go full screen");
    }
    QTest::qWait(400);  // the panel opening animation
    const QImage shot = m_window->grab().toImage();
    const QColor frame = shot.pixelColor(4, shot.height() / 2);
    for (const QPoint corner : {QPoint(0, 0), QPoint(shot.width() - 1, 0), QPoint(0, shot.height() / 2)})
        QCOMPARE(shot.pixelColor(corner), frame);
    toggle();
    panel->trigger();
    settle();
}

QTEST_MAIN(FocusModeTest)
#include "focus_mode_test.moc"
