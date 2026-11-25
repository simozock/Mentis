#include "penasmouse.h"

#include <QTabletEvent>
#include <QWindow>
#include <private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>

bool PenAsMouse::eventFilter(QObject *watched, QEvent *event) {
    // The pen lifted away from the screen leaves the window, as a mouse leaving it: the button it
    // hovered loses its hover fill and tooltip (https://doc.qt.io/qt-6/qtabletevent.html).
    if (event->type() == QEvent::TabletLeaveProximity) {
        if (m_window) QWindowSystemInterface::handleLeaveEvent<QWindowSystemInterface::SynchronousDelivery>(m_window);
        return QObject::eventFilter(watched, event);
    }
    QEvent::Type mouseType;
    switch (event->type()) {
    case QEvent::TabletPress: mouseType = QEvent::MouseButtonPress; break;
    case QEvent::TabletMove: mouseType = QEvent::MouseMove; break;
    case QEvent::TabletRelease: mouseType = QEvent::MouseButtonRelease; break;
    default: return QObject::eventFilter(watched, event);
    }
    auto *window = qobject_cast<QWindow *>(watched);
    if (!window) return QObject::eventFilter(watched, event);
    m_window = window;
    auto *tablet = static_cast<QTabletEvent *>(event);
    // Through QGuiApplication's mouse path, as a platform's mouse (and QTest's): it updates the
    // pointer's state and adds the double click (the double tap of Format painter and Relationship),
    // which a QMouseEvent sent straight to the window would not. It takes the platform's native
    // pixels, as a platform gives them: the tablet event's are scaled by Qt's high DPI factor (1.75
    // on a Galaxy Tab S9 Ultra; user's report 2026-11-16: with the logical ones every pen tap landed
    // up and left of the pen, only the drags worked).
    QWindowSystemInterface::handleMouseEvent<QWindowSystemInterface::SynchronousDelivery>(
        window, tablet->timestamp(), QHighDpi::toNativeLocalPosition(tablet->position(), window),
        QHighDpi::toNativeGlobalPosition(tablet->globalPosition(), window), tablet->buttons(),
        mouseType == QEvent::MouseMove ? Qt::NoButton : tablet->button(), mouseType, tablet->modifiers());
    tablet->accept();
    return true;
}
