#include "markdowntyping.h"

#include "formulafielddocument.h"
#include "formulatext.h"
#include "markdownshortcuts.h"

#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

#include <utility>

MarkdownTyping::MarkdownTyping(QObject *parent) : QObject(parent) {}

QTextDocument *MarkdownTyping::textDocument() const { return m_document ? m_document->textDocument() : nullptr; }

void MarkdownTyping::setDocument(QQuickTextDocument *document) {
    if (m_document == document) return;
    disconnect(m_changes);
    m_document = document;
    m_nextAt = -1;
    if (QTextDocument *text = textDocument()) {
        m_changes = connect(text, &QTextDocument::contentsChange, this, [this](int, int, int added) {
            if (m_applying || !m_active || added <= 0 || m_scheduled) return;
            m_scheduled = true;
            QTimer::singleShot(0, this, &MarkdownTyping::apply);
        });
    }
    emit documentChanged();
}

void MarkdownTyping::apply() {
    m_scheduled = false;
    const int nextAt = std::exchange(m_nextAt, -1);
    QTextDocument *text = textDocument();
    // The field's cursor, after what was typed: an input method's commit is reported as a change of
    // the whole document, so the change's own range does not say where the text went.
    const QObject *field = m_document ? m_document->parent() : nullptr;
    const int end = field ? field->property("cursorPosition").toInt() : -1;
    // Typed: the field has an Undo step and no Redo. A loaded title or note has no Undo; an Undo
    // leaves a Redo, so the symbols it gives back are not turned into the format again.
    if (!text || !m_active || !text->isUndoAvailable() || text->isRedoAvailable() || end <= 0 || end >= text->characterCount())
        return;
    if (nextAt >= 0 && end > nextAt && end - nextAt == text->characterCount() - m_lengthAt) {
        // Only text typed right after a shortcut's format: given the format before it, in its step.
        QTextCursor typedText(text);
        typedText.setPosition(nextAt);
        typedText.setPosition(end, QTextCursor::KeepAnchor);
        m_applying = true;
        typedText.joinPreviousEditBlock();
        typedText.setCharFormat(m_nextFormat);
        typedText.endEditBlock();
        m_applying = false;
    }
    const QChar last = text->characterAt(end - 1);
    if (!MarkdownShortcuts::isTrigger(last)) return;
    // As the desktop: a title holding a formula stays plain text.
    if (!m_note && FormulaText::containsMath(FormulaFieldDocument::sourceOf(text))) return;
    QTextCursor cursor(text);
    cursor.setPosition(end);
    MarkdownShortcuts::Options options;
    options.context = m_note ? MarkdownShortcuts::Context::Note : MarkdownShortcuts::Context::Title;
    options.boldWeight = m_boldWeight;
    options.headingPx = m_headingPx;
    options.bodyPx = m_bodyPx;
    m_applying = true;
    const MarkdownShortcuts::Result result = MarkdownShortcuts::afterTyping(cursor, last, options);
    m_applying = false;
    if (!result.changed) return;
    // The format the next text takes, when it is not the one of the character before.
    QTextCursor before(text);
    before.setPosition(cursor.position());
    if (before.charFormat() != cursor.charFormat()) {
        m_nextAt = cursor.position();
        m_lengthAt = text->characterCount();
        m_nextFormat = cursor.charFormat();
    }
    emit applied();
}
