#pragma once

#include "spellchecker.h"

#include <QKeySequence>
#include <QPoint>
#include <QString>

#include <functional>

class QPlainTextEdit;
class QTextCursor;
class QWidget;

// FEAT-042: menu of the text being edited (map topic, Outliner items and title): Cut, Copy, Paste,
// Select all, in the dark style of the topic menu. FEAT-044 adds the spelling items on top.
namespace TextEditMenu {

// FEAT-044: misspelled word under the click (empty text: no spelling items) and how to replace it.
struct Spelling {
    SpellCheck::Word word;
    std::function<void(const QString &)> replace;
};

// Style shared with the topic menu: text only, separators, disabled items in grey.
QString styleSheet();
// Opens the menu at screenPos; perform runs the chosen command (Cut, Copy, Paste or SelectAll).
void exec(QWidget *parent, const QPoint &screenPos, bool hasSelection,
          const std::function<void(QKeySequence::StandardKey)> &perform, const Spelling &spelling = {});
// Replaces word with replacement through cursor (an undoable edit of the field).
void replaceWord(QTextCursor &cursor, const SpellCheck::Word &word, const QString &replacement);
// Right-click on the field (and its viewport) opens the menu instead of the system one; while the
// field has the focus its misspelled words are underlined (FEAT-044).
void attach(QPlainTextEdit *edit);

}  // namespace TextEditMenu
