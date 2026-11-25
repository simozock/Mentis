#include "controlkey.h"

#include <QGuiApplication>
#include <QKeyEvent>
#include <QSinglePointEvent>

namespace {
bool s_held = false;
}

ControlKey::ControlKey(QObject *parent) : QObject(parent) {
    qGuiApp->installEventFilter(this);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) s_held = false;
    });
}

// Not QGuiApplication::keyboardModifiers(): it takes the modifiers of the last key event, and the
// release of Ctrl may still carry Ctrl (QTest's does), so it would stay held.
bool ControlKey::held() {
    return s_held;
}

bool ControlKey::eventFilter(QObject *watched, QEvent *event) {
    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        // The release of Ctrl itself no longer carries the modifier, the press may not yet.
        const auto *key = static_cast<QKeyEvent *>(event);
        s_held = key->key() == Qt::Key_Control ? event->type() == QEvent::KeyPress
                                               : bool(key->modifiers() & Qt::ControlModifier);
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::TabletPress:
        // Only adds the state: a touch synthesized into a mouse event may report no modifiers.
        if (static_cast<QSinglePointEvent *>(event)->modifiers() & Qt::ControlModifier) s_held = true;
        break;
    default: break;
    }
    return QObject::eventFilter(watched, event);
}
