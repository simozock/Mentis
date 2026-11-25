#pragma once

#include <QObject>
#include <QPointer>
#include <QWindow>

// The pen of a tablet acts as the desktop's mouse in the whole app (user's report 2026-11-16: a pen
// tap selected a topic but never pressed the "+" knobs). Qt delivers a pen as QTabletEvents: the
// pointer handlers (the map's taps and drags) accept them, the Qt Quick Controls buttons do not, so
// the handler under a button took the tap. Each tablet event becomes the platform's mouse event and
// is accepted, so Qt synthesizes nothing more (Qt::AA_SynthesizeMouseForUnhandledTabletEvents acts
// only on unaccepted ones: https://doc.qt.io/qt-6/qt.html#ApplicationAttribute-enum).
class PenAsMouse : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // The window of the pen's last event: a pen leaving the screen's proximity is an application
    // event, without a window.
    QPointer<QWindow> m_window;
};
