#pragma once

// FEAT-133: LaTeX braces closed while typing, on the desktop and the phone. Typing "{" in LaTeX adds
// the "}" after the cursor; typing "}" right before a "}" the formula does not need steps over it;
// Backspace on a "{" right before its "}" removes both. The added "}" merges into the typed
// character's undo step: one Ctrl+Z takes the pair back.
//
// LaTeX is the whole field in the formula modal (Scope::Code); in the other fields (titles, outliner,
// notes, Gantt names) only a formula: inside $...$, $$...$$, \(...\), or after a "$" or "\(" not yet
// closed (the formula being typed).

#include <QChar>

class QKeyEvent;
class QTextCursor;

namespace BraceCompletion {

enum class Scope { Code, Text };

// After the character typed was inserted before cursor (no selection). Returns true when it changed
// the document; cursor is then where the field's cursor goes.
bool afterTyping(QTextCursor &cursor, QChar typed, Scope scope);
// After Backspace removed the character removed before cursor.
bool afterDeleting(QTextCursor &cursor, QChar removed, Scope scope);

// Before a "}" is typed: true when the "}" after cursor already closes it; cursor then steps over it.
bool stepOver(QTextCursor &cursor, Scope scope);

// Desktop key handling, around the field's own: before (with the field's cursor) returns true when
// the key is done (a "}" stepped over: set the cursor, the field does not take the key); after (with
// the field's cursor once it took the key) returns true when the cursor must be set.
class KeyTyping {
public:
    bool before(QTextCursor &cursor, const QKeyEvent *event, Scope scope);
    bool after(QTextCursor &cursor, const QKeyEvent *event) const;

private:
    Scope m_scope = Scope::Text;
    QChar m_removed;  // the character a Backspace removes
    bool m_done = false;
};

}  // namespace BraceCompletion
