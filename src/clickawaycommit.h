#pragma once

// A click on a part of a window that takes no focus (a panel's background, a label, a button without
// focus) ends the editing of the text field that has it, as a click on the map does: the field loses
// the focus and confirms its value (QLineEdit::editingFinished). Without this Qt leaves the focus in
// the field, the typed value stays shown and is never applied (user's report 2026-11-24: opacity,
// thickness). Covers every QLineEdit of the application, NumberField included.

#include <QObject>

class ClickAwayCommit final : public QObject {
public:
    static void install();  // once, after QApplication exists

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    using QObject::QObject;
};
