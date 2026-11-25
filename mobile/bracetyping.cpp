#include "bracetyping.h"

#include "bracecompletion.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

BraceTyping::BraceTyping(QObject *parent) : QObject(parent) {}

QTextDocument *BraceTyping::textDocument() const { return m_document ? m_document->textDocument() : nullptr; }

void BraceTyping::setDocument(QQuickTextDocument *document) {
    if (m_document == document) return;
    disconnect(m_changes);
    m_document = document;
    if (QTextDocument *text = textDocument()) {
        m_last = text->toRawText();
        m_changes = connect(text, &QTextDocument::contentsChanged, this, [this] {
            if (m_applying) return;
            if (!m_scheduled) {
                m_before = m_last;  // the text before the changes of this turn
                if (m_active) {
                    m_scheduled = true;
                    QTimer::singleShot(0, this, &BraceTyping::apply);
                }
            }
            if (QTextDocument *changed = textDocument()) m_last = changed->toRawText();
        });
    }
    emit documentChanged();
}

void BraceTyping::apply() {
    m_scheduled = false;
    QTextDocument *text = textDocument();
    QObject *field = m_document ? m_document->parent() : nullptr;
    if (!text || !field || !m_active || !text->isUndoAvailable() || text->isRedoAvailable()) return;
    if (!field->property("preeditText").toString().isEmpty()) return;  // a word being composed
    const QString after = text->toRawText();
    const qsizetype grown = after.size() - m_before.size();
    // Text typed right before the cursor (several characters when keys came faster than this turn:
    // the last one counts), or one character removed right after it. Found from the cursor: the
    // first difference of the texts would put a "}" typed before a "}" after it.
    if (grown == 0 || grown < -1) return;
    const qsizetype end = field->property("cursorPosition").toInt();
    const qsizetype at = grown > 0 ? end - grown : end;  // where the change starts
    const qsizetype length = grown > 0 ? grown : 1;
    const QString &longer = grown > 0 ? after : m_before;
    const QString &other = grown > 0 ? m_before : after;
    if (at < 0 || at + length > longer.size() || longer.left(at) != other.left(at) || longer.mid(at + length) != other.mid(at))
        return;
    const BraceCompletion::Scope scope = m_code ? BraceCompletion::Scope::Code : BraceCompletion::Scope::Text;
    QTextCursor cursor(text);
    cursor.setPosition(int(end));
    m_applying = true;
    const bool changed = grown > 0 ? BraceCompletion::afterTyping(cursor, after[end - 1], scope)
                                   : BraceCompletion::afterDeleting(cursor, m_before[at], scope);
    m_applying = false;
    m_last = text->toRawText();
    if (!changed) return;
    field->setProperty("cursorPosition", cursor.position());
    QGuiApplication::inputMethod()->update(Qt::ImQueryInput);
}
