#include "formulafield.h"

#include <QTextDocument>

FormulaField::FormulaField(QObject *parent) : QObject(parent) {}

QTextDocument *FormulaField::textDocument() const { return m_document ? m_document->textDocument() : nullptr; }

void FormulaField::setDocument(QQuickTextDocument *document) {
    if (document == m_document) return;
    if (QTextDocument *old = textDocument()) disconnect(old, nullptr, this, nullptr);
    m_document = document;
    m_field.setDocument(textDocument());
    if (QTextDocument *text = textDocument()) connect(text, &QTextDocument::contentsChanged, this, &FormulaField::updateState);
    updateLook();
    emit documentChanged();
}

void FormulaField::setFont(const QFont &font) {
    if (font == m_font) return;
    m_font = font;
    updateLook();
    emit fontChanged();
}

void FormulaField::setColor(const QColor &color) {
    if (color == m_color) return;
    m_color = color;
    updateLook();
    emit colorChanged();
}

void FormulaField::setPixelRatio(qreal ratio) {
    if (qFuzzyCompare(ratio, m_pixelRatio) || ratio <= 0) return;
    m_pixelRatio = ratio;
    updateLook();
    emit pixelRatioChanged();
}

void FormulaField::updateLook() {
    m_field.setLook(m_font, m_color, m_pixelRatio);
    updateState();
}

void FormulaField::load(const QString &source) {
    QTextDocument *document = textDocument();
    if (!document) return;
    document->setPlainText(source);
    m_field.refresh(-1);
    document->clearUndoRedoStacks();
    document->setModified(false);
    updateState();
}

int FormulaField::refresh(int cursor) {
    const int moved = m_field.refresh(cursor);
    updateState();
    return moved;
}

int FormulaField::undo(bool redo) {
    const int at = m_field.undo(redo);
    updateState();
    return at;
}

QString FormulaField::source(const QString &preedit, int cursor) const {
    return FormulaFieldDocument::sourceOf(textDocument(), preedit, cursor);
}

QString FormulaField::layoutSource(const QString &preedit, int cursor) const { return m_field.layoutSource(preedit, cursor); }

void FormulaField::updateState() {
    if (const bool math = m_field.hasMath(); math != m_hasMath) {
        m_hasMath = math;
        emit hasMathChanged();
    }
    if (const qreal width = m_field.naturalWidth(); !qFuzzyCompare(width + 1, m_naturalWidth + 1)) {
        m_naturalWidth = width;
        emit naturalWidthChanged();
    }
}
