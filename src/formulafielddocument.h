#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QPair>
#include <QString>
#include <QVector>

class QTextDocument;

// FEAT-116: the formulas of a title while it is typed, in the field's document (the desktop's
// InlineTitleEditor, the phone's title field). Each $...$ (or $$...$$, \(...\)) is a picture in the
// line, as on the topic, except the one the cursor is in or touches (at a delimiter): that one is
// its code, typed as text. A picture keeps its code in its character, so the title's source is read
// back from any state of the document, also from its undo history (sourceOf). Only the formulas
// that change are replaced: the rest of the text keeps its formatting.
class FormulaFieldDocument {
public:
    void setDocument(QTextDocument *document) { m_document = document; }
    QTextDocument *document() const { return m_document; }
    // The pictures' look: the title's font and colour, pixels per unit (sharp when zoomed in).
    // A change redraws the pictures in the document.
    void setLook(const QFont &font, const QColor &color, qreal pixelRatio);
    QFont font() const { return m_font; }

    // The formulas laid out again for the cursor (a document position; -1: none, all pictures).
    // Returns the cursor's new position, or -1 when nothing changed.
    int refresh(int cursor);
    // The field's Undo (redo: Redo) by one change of the source: the steps that only turned formulas
    // into pictures or code are passed over (refresh would make them again and Undo would never get
    // past them). The cursor's new position, or -1 when there was nothing to undo. Refresh only at
    // the next move or key, so Redo stays possible.
    int undo(bool redo);
    // True when the document holds a formula, as a picture or as code (after refresh).
    bool hasMath() const { return m_hasMath; }

    // The title: the text, each picture its code; preedit (a word being composed) put at cursor.
    static QString sourceOf(const QTextDocument *document, const QString &preedit = QString(), int cursor = -1);
    // The source of the document positions [from, to) (a selection copied).
    static QString sourceBetween(const QTextDocument *document, int from, int to);
    // The same with the formulas typed as code escaped (\$): read as text, the topic lays them out
    // as the field shows them (the phone, whose topic does not grow with the field).
    QString layoutSource(const QString &preedit, int cursor) const;
    // The widest line unwrapped, pictures included.
    qreal naturalWidth() const;

private:
    struct Flat {
        QString source;
        QVector<QPair<int, int>> pictures;  // source ranges shown as pictures
        int sourceCursor = -1;
    };
    static Flat flatten(const QTextDocument *document, int cursor, int from = 0, int to = -1);
    QImage picture(const QString &formula);
    void apply(const Flat &flat, const QVector<QPair<int, int>> &pictures);

    QTextDocument *m_document = nullptr;
    QFont m_font;
    QColor m_color = Qt::black;
    qreal m_pixelRatio = 2;
    bool m_hasMath = false;
    QHash<QString, QImage> m_pictures;
    int m_counter = 0;
};
