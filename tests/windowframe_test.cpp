// FEAT-071: what Windows is told is under the mouse (WindowFrame::hitTest): the 6 px edge band with
// 12 px corners, the drag areas, the Maximize button, and no band while maximized.
// mainWindowOnWindows runs only with the windows platform (by hand: QT_QPA_PLATFORM=windows; CTest
// uses offscreen): the real window answers Windows' messages.
#include "mainwindow.h"
#include "modaldialog.h"
#include "windowframe.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTest>
#include <QToolButton>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using WindowFrame::Hit;
using WindowFrame::hitTest;

class WindowFrameTest final : public QObject {
    Q_OBJECT

private slots:
    void edgesAndCorners();
    void contentInsideTheBand();
    void maximizedHasNoBand();
    void mainWindowOnWindows();
    void modalMovesWithItsWindow();
};

namespace {
const QSize kSize(1400, 900);
Hit at(int x, int y, bool caption = false, bool maximize = false, bool resizable = true) {
    return hitTest(QPoint(x, y), kSize, resizable, caption, maximize);
}
}  // namespace

void WindowFrameTest::edgesAndCorners() {
    QCOMPARE(at(0, 450), Hit::Left);
    QCOMPARE(at(5, 450), Hit::Left);
    QCOMPARE(at(6, 450), Hit::Client);
    QCOMPARE(at(1399, 450), Hit::Right);
    QCOMPARE(at(1394, 450), Hit::Right);
    QCOMPARE(at(1393, 450), Hit::Client);
    QCOMPARE(at(700, 0), Hit::Top);
    QCOMPARE(at(700, 899), Hit::Bottom);
    // Corners reach 12 px along each edge.
    QCOMPARE(at(0, 0), Hit::TopLeft);
    QCOMPARE(at(11, 2), Hit::TopLeft);
    QCOMPARE(at(12, 2), Hit::Top);
    QCOMPARE(at(2, 11), Hit::TopLeft);
    QCOMPARE(at(2, 12), Hit::Left);
    QCOMPARE(at(1390, 3), Hit::TopRight);
    QCOMPARE(at(3, 890), Hit::BottomLeft);
    QCOMPARE(at(1395, 895), Hit::BottomRight);
}

void WindowFrameTest::contentInsideTheBand() {
    // The band wins over the top bar, so the top edge stays resizable.
    QCOMPARE(at(700, 3, true), Hit::Top);
    QCOMPARE(at(1380, 3, false, true), Hit::Top);
    QCOMPARE(at(1395, 3, false, true), Hit::TopRight);
    QCOMPARE(at(700, 20, true), Hit::Caption);
    QCOMPARE(at(1380, 20, false, true), Hit::MaximizeButton);
    QCOMPARE(at(700, 450), Hit::Client);
}

void WindowFrameTest::maximizedHasNoBand() {
    QCOMPARE(at(0, 450, false, false, false), Hit::Client);
    QCOMPARE(at(0, 0, true, false, false), Hit::Caption);
    QCOMPARE(at(1399, 0, false, true, false), Hit::MaximizeButton);
    QCOMPARE(at(700, 899, false, false, false), Hit::Client);
}

void WindowFrameTest::mainWindowOnWindows() {
#ifndef Q_OS_WIN
    QSKIP("Windows only");
#else
    if (QGuiApplication::platformName() != QLatin1String("windows")) QSKIP("needs QT_QPA_PLATFORM=windows");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("MentisTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MentisWindowFrameTest"));
    MainWindow window;
    window.resize(1200, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    const HWND hwnd = reinterpret_cast<HWND>(window.winId());
    QVERIFY(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME);
    QCOMPARE(window.frameGeometry(), window.geometry());  // no frame margins for Qt
    const qreal ratio = window.devicePixelRatioF();
    const auto hit = [&](const QPoint &local) {
        POINT point{LONG(local.x() * ratio), LONG(local.y() * ratio)};
        ClientToScreen(hwnd, &point);
        return SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
    };
    // BUG-041: Home's empty parts and its title move the window; its buttons and cards do not.
    const auto centerOf = [&](QWidget *widget) { return widget->mapTo(&window, widget->rect().center()); };
    QWidget *home = window.findChild<QWidget *>(QStringLiteral("homePage"));
    QVERIFY(home && home->isVisible());
    QCOMPARE(hit(centerOf(home->findChild<QLabel *>(QStringLiteral("HomeTitle")))), LRESULT(HTCAPTION));
    QCOMPARE(hit(QPoint(window.width() / 2, window.height() - 40)), LRESULT(HTCAPTION));  // below the cards
    QCOMPARE(hit(centerOf(home->findChild<QPushButton *>(QStringLiteral("accent")))), LRESULT(HTCLIENT));
    QCOMPARE(hit(centerOf(home->findChild<QWidget *>(QStringLiteral("HomeCard")))), LRESULT(HTCLIENT));
    qobject_cast<QStackedWidget *>(window.centralWidget())->setCurrentIndex(1);  // the editor
    QTest::qWait(100);
    QToolButton *maximize = nullptr;
    for (QToolButton *button : window.findChildren<QToolButton *>())
        if (button->property("controlKind").toString() == QLatin1String("maximize") && button->isVisible()) maximize = button;
    QVERIFY(maximize);
    const QPoint button = maximize->mapTo(&window, maximize->rect().center());
    QCOMPARE(hit(button), LRESULT(HTMAXBUTTON));
    QWidget *top = window.findChild<QWidget *>(QStringLiteral("EditorTopBar"));
    QVERIFY(top);
    QPoint empty;
    for (int x = 0; x < top->width() && empty.isNull(); x += 4) {
        const QPoint at = top->mapTo(&window, QPoint(x, top->height() / 2));
        if (window.childAt(at) == top) empty = at;
    }
    QVERIFY(!empty.isNull());
    QCOMPARE(hit(empty), LRESULT(HTCAPTION));
    QCOMPARE(hit(QPoint(window.width() / 2, window.height() / 2)), LRESULT(HTCLIENT));
    QCOMPARE(hit(QPoint(2, window.height() / 2)), LRESULT(HTLEFT));
    // A click on the button from Windows' messages maximizes inside the work area, without edges.
    const LPARAM screen = [&] { POINT p{LONG(button.x() * ratio), LONG(button.y() * ratio)}; ClientToScreen(hwnd, &p); return MAKELPARAM(p.x, p.y); }();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTMAXBUTTON, screen);
    QVERIFY(maximize->isDown());
    SendMessageW(hwnd, WM_NCLBUTTONUP, HTMAXBUTTON, screen);
    QTRY_VERIFY(window.isMaximized());
    QVERIFY(!maximize->isDown());
    QTRY_VERIFY(window.screen()->availableGeometry().contains(window.geometry()));
    QCOMPARE(hit(QPoint(2, window.height() / 2)), LRESULT(HTCLIENT));
    // FEAT-087: full screen and back keeps the resizable style (Qt restores the one it saved).
    window.showNormal();
    window.showFullScreen();
    QTRY_VERIFY(window.isFullScreen());
    window.showNormal();
    QTRY_VERIFY(!window.isFullScreen() && !window.isMaximized());
    QVERIFY(GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME);
    QCOMPARE(window.frameGeometry(), window.geometry());
    QCOMPARE(hit(QPoint(2, window.height() / 2)), LRESULT(HTLEFT));
    window.close();
#endif
}

// BUG-040: the modal covers the main window, so moving one moves the other (the system's move loop
// itself cannot run offscreen; moveEvent and the owner's Move/Resize are what it triggers).
void WindowFrameTest::modalMovesWithItsWindow() {
    QWidget owner(nullptr, Qt::FramelessWindowHint);
    owner.setGeometry(100, 100, 900, 600);
    owner.show();
    QVERIFY(QTest::qWaitForWindowExposed(&owner));
    ModalDialog dialog(&owner, QStringLiteral("Choose a map type"), QStringLiteral("Create"), 400);
    dialog.open();
    QVERIFY(QTest::qWaitForWindowExposed(&dialog));
    QCOMPARE(dialog.geometry(), owner.geometry());
    dialog.move(160, 140);
    QTRY_COMPARE(owner.pos(), QPoint(160, 140));
    owner.setGeometry(40, 30, 1000, 700);
    QTRY_COMPARE(dialog.geometry(), QRect(40, 30, 1000, 700));
    dialog.reject();
}

QTEST_MAIN(WindowFrameTest)
#include "windowframe_test.moc"
