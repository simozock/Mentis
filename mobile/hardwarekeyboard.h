#pragma once

#include <QObject>

// While a hardware keyboard is attached (a tablet's cover keyboard, one paired with the phone), the
// on-screen keyboard stays closed (user's report 2026-11-16: it opened on every field and closed at
// the first key on the cover's keyboard). Qt opens it with an explicit show
// (WindowInsetsController.show, InputMethodManager.showSoftInput with no flags), which Android
// honours even with a hardware keyboard. The activity's window then takes no input method
// (WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM): the keys reach Qt as key events, as without
// an input method. Checked at start, on every focus change (before the field's show) and when the
// app comes back; on Android only. On every focus change it also turns off Android's focus
// highlight on Qt's hidden text field (disableFocusHighlight).
class HardwareKeyboard : public QObject {
public:
    explicit HardwareKeyboard(QObject *parent = nullptr);

private:
    void update();
    int m_attached = -1;  // unknown, false or true: the window's flag is changed only on a change
};
