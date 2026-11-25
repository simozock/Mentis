#pragma once

#include "formulafielddocument.h"

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPointer>
#include <QQuickTextDocument>
#include <QtQml/qqmlregistration.h>

class QTextDocument;

// FEAT-116: the formulas of a plain title in the phone's title field (FormulaFieldDocument): each
// one a picture in the line, the one the cursor is in or touches its code. layoutSource is the
// title with that formula's delimiters escaped (\$): the topic is laid out as the field shows it,
// its code as text, and the box fits the field while the formula is typed.
class FormulaField : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QQuickTextDocument *document READ document WRITE setDocument NOTIFY documentChanged)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY fontChanged)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY colorChanged)
    // Pixels per field unit of the pictures (the field is scaled by the zoom).
    Q_PROPERTY(qreal pixelRatio READ pixelRatio WRITE setPixelRatio NOTIFY pixelRatioChanged)
    // True while the field holds a formula, as a picture or as code.
    Q_PROPERTY(bool hasMath READ hasMath NOTIFY hasMathChanged)
    // The field's widest line unwrapped, pictures included (the field's own implicitWidth follows
    // its width when it wraps: a binding loop).
    Q_PROPERTY(qreal naturalWidth READ naturalWidth NOTIFY naturalWidthChanged)

public:
    explicit FormulaField(QObject *parent = nullptr);

    QQuickTextDocument *document() const { return m_document; }
    void setDocument(QQuickTextDocument *document);
    QFont font() const { return m_font; }
    void setFont(const QFont &font);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    qreal pixelRatio() const { return m_pixelRatio; }
    void setPixelRatio(qreal ratio);
    bool hasMath() const { return m_hasMath; }
    qreal naturalWidth() const { return m_naturalWidth; }

    // The title in the field, every formula a picture.
    Q_INVOKABLE void load(const QString &source);
    // The formulas laid out again for the cursor; the cursor's new position, or -1 when unchanged.
    Q_INVOKABLE int refresh(int cursor);
    // Undo or Redo by one change of the title (FormulaFieldDocument::undo); the cursor's new position.
    Q_INVOKABLE int undo(bool redo);
    // The title: each picture its code, preedit (the word being composed) at cursor.
    Q_INVOKABLE QString source(const QString &preedit = QString(), int cursor = 0) const;
    // The same, the formulas typed as code with their $ escaped (the title the topic is laid out with).
    Q_INVOKABLE QString layoutSource(const QString &preedit = QString(), int cursor = 0) const;

signals:
    void documentChanged();
    void fontChanged();
    void colorChanged();
    void pixelRatioChanged();
    void hasMathChanged();
    void naturalWidthChanged();

private:
    QTextDocument *textDocument() const;
    void updateLook();
    void updateState();

    QPointer<QQuickTextDocument> m_document;
    FormulaFieldDocument m_field;
    QFont m_font;
    QColor m_color = Qt::black;
    qreal m_pixelRatio = 2;
    bool m_hasMath = false;
    qreal m_naturalWidth = 0;
};
