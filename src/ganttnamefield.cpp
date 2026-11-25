#include "ganttnamefield.h"

#include "ganttnames.h"
#include "bracecompletion.h"
#include "markdownshortcuts.h"
#include "richtitle.h"
#include "shortcuts.h"

#include <QAbstractTextDocumentLayout>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCursor>

GanttNameField::GanttNameField(QWidget *parent) : QTextEdit(parent) {
    setAcceptRichText(false);
    setLineWrapMode(QTextEdit::NoWrap);
    setWordWrapMode(QTextOption::NoWrap);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    document()->setDocumentMargin(0);
}

void GanttNameField::setText(const QString &text) {
    setPlainText(text);
    moveCursor(QTextCursor::End);
    document()->clearUndoRedoStacks();
    document()->setModified(false);
}

void GanttNameField::load(const MindNode *topic, bool boldRow) {
    m_boldRow = boldRow;
    m_formattable = GanttNames::formattable(topic);
    GanttNames::load(document(), topic, boldRow);
    document()->clearUndoRedoStacks();  // the loaded formats are no edit
    moveCursor(QTextCursor::End);
}

bool GanttNameField::result(const MindNode *topic, QString *title, QString *html) const {
    return GanttNames::edited(topic, *document(), m_boldRow, title, html);
}

// The size of a line edit with the same font and frame.
QSize GanttNameField::sizeHint() const {
    const int line = fontMetrics().height();
    const QMargins frame = contentsMargins();
    return {QTextEdit::sizeHint().width(), line + 4 + frame.top() + frame.bottom()};
}
QSize GanttNameField::minimumSizeHint() const { return {fontMetrics().averageCharWidth() * 4, sizeHint().height()}; }

bool GanttNameField::event(QEvent *event) {
    // The format keys stay with the field (the window's would format the whole topic while typing).
    if (event->type() == QEvent::ShortcutOverride && styleKey(static_cast<QKeyEvent *>(event)) >= 0) {
        event->accept();
        return true;
    }
    return QTextEdit::event(event);
}

void GanttNameField::keyPressEvent(QKeyEvent *event) {
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        emit returnPressed();
        emit editingFinished();
        return;
    case Qt::Key_Up:
    case Qt::Key_Down:
        event->ignore();  // one line: the panel steps through the tasks
        return;
    default: break;
    }
    if (const int style = styleKey(event); style >= 0) {
        if (m_formattable) toggle(style);
        return;
    }
    // FEAT-133: the braces of a formula closed while typing.
    BraceCompletion::KeyTyping braces;
    if (QTextCursor cursor = textCursor(); braces.before(cursor, event, BraceCompletion::Scope::Text)) {
        setTextCursor(cursor);
        return;
    }
    QTextEdit::keyPressEvent(event);
    if (QTextCursor cursor = textCursor(); braces.after(cursor, event)) {
        setTextCursor(cursor);
        return;
    }
    if (!m_formattable) return;  // a formula's title stays plain
    // FEAT-068: the Markdown shortcut closed by the character just typed.
    if (event->text().size() != 1 || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) return;
    QTextCursor cursor = textCursor();
    MarkdownShortcuts::Options options;
    options.boldWeight = GanttNames::rowWeights(m_boldRow).bold;
    const MarkdownShortcuts::Result shortcut = MarkdownShortcuts::afterTyping(cursor, event->text().front(), options);
    if (shortcut.changed) setTextCursor(cursor);
}

int GanttNameField::styleKey(const QKeyEvent *event) const {
    if (!shortcuts) return -1;
    const std::pair<const char *, RichTitle::CharStyle> styles[] = {{"bold", RichTitle::CharStyle::Bold},
                                                                     {"italic", RichTitle::CharStyle::Italic},
                                                                     {"underline", RichTitle::CharStyle::Underline},
                                                                     {"strikeOut", RichTitle::CharStyle::StrikeOut}};
    for (const auto &[id, style] : styles)
        if (shortcuts->matches(QString::fromLatin1(id), event)) return int(style);
    return -1;
}

// As the format bar's button: on unless the selection has it; bold at the row's weights.
void GanttNameField::toggle(int style) {
    QTextCursor cursor = textCursor();
    if (!cursor.hasSelection()) return;
    const QTextCharFormat now = cursor.charFormat();
    const OutlineFormats::BoldWeights weights = GanttNames::rowWeights(m_boldRow);
    QTextCharFormat format;
    switch (RichTitle::CharStyle(style)) {
    case RichTitle::CharStyle::Bold: format.setFontWeight(now.fontWeight() >= weights.bold ? weights.regular : weights.bold); break;
    case RichTitle::CharStyle::Italic: format.setFontItalic(!now.fontItalic()); break;
    case RichTitle::CharStyle::Underline: format.setFontUnderline(!now.fontUnderline()); break;
    case RichTitle::CharStyle::StrikeOut: format.setFontStrikeOut(!now.fontStrikeOut()); break;
    default: return;
    }
    cursor.mergeCharFormat(format);
}

void GanttNameField::focusOutEvent(QFocusEvent *event) {
    QTextEdit::focusOutEvent(event);
    // A menu over the field (its own context menu) does not end the editing.
    if (event->reason() != Qt::PopupFocusReason) emit editingFinished();
}

void GanttNameField::insertFromMimeData(const QMimeData *source) {
    if (!source || !source->hasText()) return;
    QString text = source->text();
    text.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
    textCursor().insertText(text);
}

void GanttNameField::resizeEvent(QResizeEvent *event) {
    QTextEdit::resizeEvent(event);
    centreLine();
}

// The line in the middle of the field's height, as a line edit's text.
void GanttNameField::centreLine() {
    const int line = fontMetrics().height();
    const int top = std::max(0, (viewport()->height() + viewportMargins().top() + viewportMargins().bottom() - line) / 2);
    setViewportMargins(0, top, 0, 0);
}
