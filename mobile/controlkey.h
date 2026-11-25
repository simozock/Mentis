#pragma once

#include <QObject>

// Whether Ctrl is held on a hardware keyboard, for Ctrl + a tap of the finger or the pen that adds
// to the selection, as the desktop's Ctrl+click (user's request 2026-11-23). A touch carries no
// keyboard modifiers on every platform, so the state comes from the keyboard's own Ctrl events, and
// from the modifiers of the mouse and pen events that carry them. Installed once on the
// application (MobileApp::prepare); the state is cleared when the app leaves the foreground, as the
// release may then never arrive.
class ControlKey : public QObject {
public:
    explicit ControlKey(QObject *parent = nullptr);
    static bool held();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};
