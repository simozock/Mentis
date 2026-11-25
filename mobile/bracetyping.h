#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QTextDocument;

// FEAT-133: LaTeX braces closed while typing in a phone field (BraceCompletion). The soft keyboard
// sends no key events and an input method's commit is reported as a change of the whole document,
// so the change is found by comparing the text with the one before; a single character typed or
// removed (an Undo step and no Redo, as MarkdownTyping) is passed to BraceCompletion on the next
// turn of the event loop, and the field's cursor is put where BraceCompletion leaves it.
class BraceTyping : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    // False while the field is not typed in (nothing happens).
    Q_PROPERTY(bool active MEMBER m_active NOTIFY activeChanged)
    // The whole field is LaTeX (the formula modal), not only its formulas.
    Q_PROPERTY(bool code MEMBER m_code NOTIFY codeChanged)

public:
    explicit BraceTyping(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);

signals:
    void documentChanged();
    void activeChanged();
    void codeChanged();

private:
    QTextDocument *textDocument() const;
    void apply();

    QPointer<QQuickTextDocument> m_document;
    QMetaObject::Connection m_changes;
    bool m_active = false;
    bool m_code = false;
    bool m_scheduled = false;
    bool m_applying = false;  // our own change
    QString m_before;         // the text before the change
    QString m_last;           // the text after the last change seen
};
