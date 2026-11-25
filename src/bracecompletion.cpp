#include "bracecompletion.h"

#include "formulatext.h"

#include <QKeyEvent>
#include <QString>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace BraceCompletion {

namespace {

struct Range {
    int from = 0;  // the LaTeX code, in the text's positions
    int to = 0;
    bool valid = false;
};

// True when the character at index is escaped by an odd run of backslashes before it.
bool escaped(const QString &text, int index) {
    int count = 0;
    for (int i = index - 1; i >= 0 && text[i] == QLatin1Char('\\'); --i) ++count;
    return count % 2 == 1;
}

// The formula's code around position in a line of text: a closed $...$, $$...$$ or \(...\), or the
// code after a "$" or "\(" not closed yet, up to the next formula or the end.
Range formulaAt(const QString &text, int position) {
    for (const FormulaText::Segment &segment : FormulaText::split(text)) {
        if (segment.math) {
            const int delimiter = (segment.to - segment.from - segment.text.size()) / 2;
            const int from = segment.from + delimiter;
            const int to = segment.to - delimiter;
            if (position >= from && position <= to) return {from, to, true};
            continue;
        }
        if (position < segment.from || position > segment.to) continue;
        // The last opening delimiter of this plain text before position: the formula being typed.
        int open = -1;
        for (int i = segment.from; i < position; ++i) {
            if (text[i] == QLatin1Char('\\') && i + 1 < text.size()) {
                if (text[i + 1] == QLatin1Char('(')) open = i + 2;
                ++i;
                continue;
            }
            if (text[i] != QLatin1Char('$')) continue;
            const int start = i + 1 < text.size() && text[i + 1] == QLatin1Char('$') ? i + 2 : i + 1;
            // As in split: "$ " opens no formula (a price, "5 $ each").
            if (start < text.size() && !text[start].isSpace()) open = start;
            i = start - 1;
        }
        if (open >= 0 && open <= position) return {open, segment.to, true};
    }
    return {};
}

// The text and range the change at position is in: the whole document (Code), else its line.
struct Context {
    QString text;
    int offset = 0;  // document position of text[0]
    Range range;
};

Context contextAt(const QTextCursor &cursor, Scope scope) {
    Context context;
    if (scope == Scope::Code) {
        context.text = cursor.document()->toRawText();
        context.range = {0, int(context.text.size()), true};
        return context;
    }
    const QTextBlock block = cursor.block();
    context.text = block.text();
    context.offset = block.position();
    context.range = formulaAt(context.text, cursor.position() - context.offset);
    return context;
}

// Closing braces minus opening braces in the code (escaped \{ \} not counted).
int surplusOfClosing(const QString &text, const Range &range) {
    int surplus = 0;
    for (int i = range.from; i < range.to; ++i) {
        if (text[i] == QLatin1Char('\\')) { ++i; continue; }
        if (text[i] == QLatin1Char('{')) --surplus;
        else if (text[i] == QLatin1Char('}')) ++surplus;
    }
    return surplus;
}

// Outside an edit block: QTextDocument merges it with the typed (or removed) character's own
// command, adjacent and with the same format, so one Undo takes both back (an edit block joined to
// it would not: the typed character is no block of its own).
void edit(QTextCursor &cursor, int at, bool insert) {
    cursor.setPosition(at);
    if (insert) cursor.insertText(QStringLiteral("}"));
    else cursor.deleteChar();
}

// True when a "}" typed at the cursor would only close a "{" already closed by the "}" after it.
bool closedAfter(const Context &context, int at) {
    const QString &text = context.text;
    return context.range.valid && at >= context.range.from && at < context.range.to && at < text.size() &&
           text[at] == QLatin1Char('}') && !escaped(text, at) && surplusOfClosing(text, context.range) >= 0;
}

}  // namespace

bool afterTyping(QTextCursor &cursor, QChar typed, Scope scope) {
    if (cursor.hasSelection() || (typed != QLatin1Char('{') && typed != QLatin1Char('}'))) return false;
    const Context context = contextAt(cursor, scope);
    const int at = cursor.position() - context.offset;  // after the typed character
    const QString &text = context.text;
    if (at <= 0 || at > text.size() || text[at - 1] != typed || !context.range.valid) return false;
    if (at - 1 < context.range.from || escaped(text, at - 1)) return false;
    const QChar next = at < text.size() ? text[at] : QChar();
    const int position = cursor.position();
    if (typed == QLatin1Char('{')) {
        // Not before a letter or digit: "{" typed to wrap the text after it stays alone.
        if (next.isLetterOrNumber()) return false;
        edit(cursor, position, true);
        cursor.setPosition(position);
        return true;
    }
    // "}" before a "}" the formula has no "{" for: the typed one steps over it (on the phone; the
    // desktop moves the cursor before the key is typed, stepOver).
    if (next != QLatin1Char('}') || at >= context.range.to || surplusOfClosing(text, context.range) <= 0) return false;
    edit(cursor, position, false);
    cursor.setPosition(position);
    return true;
}

bool afterDeleting(QTextCursor &cursor, QChar removed, Scope scope) {
    if (cursor.hasSelection() || removed != QLatin1Char('{')) return false;
    const Context context = contextAt(cursor, scope);
    const int at = cursor.position() - context.offset;
    const QString &text = context.text;
    if (at < 0 || at >= text.size() || text[at] != QLatin1Char('}') || !context.range.valid) return false;
    // A removed "\{" is no brace of the formula.
    if (at < context.range.from || at >= context.range.to || escaped(text, at)) return false;
    if (surplusOfClosing(text, context.range) <= 0) return false;
    const int position = cursor.position();
    edit(cursor, position, false);
    cursor.setPosition(position);
    return true;
}

bool stepOver(QTextCursor &cursor, Scope scope) {
    if (cursor.hasSelection()) return false;
    const Context context = contextAt(cursor, scope);
    if (!closedAfter(context, cursor.position() - context.offset)) return false;
    cursor.movePosition(QTextCursor::NextCharacter);
    return true;
}

// No modifier test for the braces: AltGr (Ctrl+Alt on Windows) types them on many layouts.
bool KeyTyping::before(QTextCursor &cursor, const QKeyEvent *event, Scope scope) {
    m_scope = scope;
    m_removed = QChar();
    m_done = false;
    if (event->key() == Qt::Key_Backspace) {
        if (event->modifiers() == Qt::NoModifier && !cursor.hasSelection() && cursor.position() > 0)
            m_removed = cursor.document()->characterAt(cursor.position() - 1);
        return false;
    }
    m_done = event->text() == QLatin1String("}") && stepOver(cursor, scope);
    return m_done;
}

bool KeyTyping::after(QTextCursor &cursor, const QKeyEvent *event) const {
    if (m_done) return false;
    if (event->key() == Qt::Key_Backspace) return !m_removed.isNull() && afterDeleting(cursor, m_removed, m_scope);
    return event->text() == QLatin1String("{") && afterTyping(cursor, QLatin1Char('{'), m_scope);
}

}  // namespace BraceCompletion
