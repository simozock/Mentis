#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <functional>
#include <memory>

class QPainter;
class QTextDocument;

// FEAT-096 (user's decision 2026-11-09): formulas are rendered by the caller, so the core
// (MentisDomain) links no formula renderer. The desktop connects these hooks to JKQTMathText
// (FormulaText::installRendering, at startup and with the first NodeItem). Without them a title with
// $...$ is laid out as text, a note's $...$ stay text and a formula content has no picture.
namespace FormulaRendering {

// A title with formulas laid out at a width: its size, and the drawing for paint().
class TitleFormula {
public:
    virtual ~TitleFormula() = default;
    virtual QSizeF size() const = 0;
    virtual void draw(QPainter *painter, const QRectF &rect, Qt::Alignment alignment) const = 0;
};

struct Hooks {
    // The title laid out at maxWidth; null when it cannot be rendered (then it is text).
    std::function<std::unique_ptr<TitleFormula>(const QString &text, const QFont &font, const QColor &color, qreal maxWidth)> layoutTitle;
    // The $...$ of a note's document become images in the given font and colour.
    std::function<void(QTextDocument *document, const QFont &font, const QColor &color)> renderInDocument;
    // FEAT-064: a formula content (LaTeX code) in its box at `size`, `ratio` pixels per px.
    std::function<QImage(const QString &code, const QFont &font, const QColor &background, const QColor &text,
                         const QSizeF &size, qreal ratio)> boxImage;
};
Hooks &hooks();

// hooks().boxImage, or a null image without it.
QImage boxImage(const QString &code, const QFont &font, const QColor &background, const QColor &text,
                const QSizeF &size, qreal ratio);

}  // namespace FormulaRendering
