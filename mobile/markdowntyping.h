#pragma once

#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QTextCharFormat>
#include <QtQml/qqmlregistration.h>

class QTextDocument;

// FEAT-124: the desktop's Markdown shortcuts (FEAT-068, MarkdownShortcuts) in a phone field. The
// soft keyboard sends no key events, so the shortcut is read from the document: after a change the
// user typed (an Undo step and no Redo: not a document loaded nor an Undo; not our own), the
// character before the field's cursor is passed to MarkdownShortcuts::afterTyping, on the next turn of the
// event loop (the field is not changed while it handles its own change). The field's own cursor
// cannot be given a format from here, so the text typed right after a format (as on the desktop: not
// in it) is given the format the shortcut left the cursor, once inserted.
class MarkdownTyping : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    // False while the field is not typed in (nothing happens).
    Q_PROPERTY(bool active MEMBER m_active NOTIFY activeChanged)
    // A note (lists, headings, links, code) rather than a title.
    Q_PROPERTY(bool note MEMBER m_note NOTIFY noteChanged)
    // The bold weight of the field's text (900 where the text is bold already, OutlineFormats).
    Q_PROPERTY(int boldWeight MEMBER m_boldWeight NOTIFY boldWeightChanged)
    // A note's heading and text sizes (MarkdownShortcuts::Options).
    Q_PROPERTY(int headingPx MEMBER m_headingPx NOTIFY headingPxChanged)
    Q_PROPERTY(int bodyPx MEMBER m_bodyPx NOTIFY bodyPxChanged)

public:
    explicit MarkdownTyping(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);

signals:
    void documentChanged();
    void activeChanged();
    void noteChanged();
    void boldWeightChanged();
    void headingPxChanged();
    void bodyPxChanged();
    // A shortcut became its format.
    void applied();

private:
    QTextDocument *textDocument() const;
    void apply();

    QPointer<QQuickTextDocument> m_document;
    QMetaObject::Connection m_changes;
    bool m_active = false;
    bool m_note = false;
    int m_boldWeight = 700;
    int m_headingPx = 18;
    int m_bodyPx = 14;
    bool m_scheduled = false;
    bool m_applying = false;  // our own change
    int m_nextAt = -1;        // where the text typed next takes m_nextFormat (after a shortcut)
    int m_lengthAt = 0;       // the document's length then
    QTextCharFormat m_nextFormat;
};
