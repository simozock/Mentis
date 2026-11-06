// The wheel over a closed select leaves its value alone and goes on to what is under it (2026-10-26).
#include "wheelguard.h"

#include <QApplication>
#include <QComboBox>
#include <QTest>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtTest/qtestspontaneevent.h>

namespace {
// Stands for the panel or the map under the select: counts the wheel events that reach it.
class WheelCounter final : public QWidget {
public:
    int received = 0;

protected:
    void wheelEvent(QWheelEvent *event) override { ++received; event->accept(); }
};
}  // namespace

class WheelGuardTest final : public QObject {
    Q_OBJECT

private slots:
    void closedSelectKeepsItsValueAndThePanelGetsTheWheel();
};

void WheelGuardTest::closedSelectKeepsItsValueAndThePanelGetsTheWheel() {
    WheelGuard::install();
    WheelGuard::install();  // a second call adds nothing
    WheelCounter panel;
    auto *layout = new QVBoxLayout(&panel);
    auto *combo = new QComboBox(&panel);
    combo->addItems({QStringLiteral("One"), QStringLiteral("Two"), QStringLiteral("Three")});
    combo->setCurrentIndex(1);
    layout->addWidget(combo);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    combo->setFocus();
    const QPointF at(combo->rect().center());
    for (const int delta : {-120, 120, -120}) {
        QWheelEvent wheel(at, combo->mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        // As from the mouse: only those go on to the parents (sendEvent would clear the flag).
        QSpontaneKeyEvent::setSpontaneous(&wheel);
        qApp->notify(combo, &wheel);
        QCOMPARE(combo->currentIndex(), 1);
    }
    QCOMPARE(panel.received, 3);
}

QTEST_MAIN(WheelGuardTest)
#include "wheel_guard_test.moc"
