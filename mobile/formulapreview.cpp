#include "formulapreview.h"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace {
// FormulaDialog::Preview's measures: the formula at 20 px, a box at least 96 px tall with 16 px
// above and below the formula, 12 px at the sides, radius 8, the checkerboard's 8 px squares, the
// placeholder at 13 px.
constexpr int kFormulaPx = 20;
constexpr qreal kMinHeightPx = 96;
constexpr qreal kMarginPx = 16;
constexpr qreal kSidePx = 12;
constexpr qreal kRadiusPx = 8;
constexpr int kSquarePx = 8;
constexpr int kPlaceholderPx = 13;
const QColor kBox(0xff, 0xff, 0xff);
const QColor kSquares(15, 23, 42, 12);
const QColor kFormulaText(0x1f, 0x29, 0x37);
const QColor kPlaceholder(0x6b, 0x72, 0x80);
}  // namespace

FormulaPreview::FormulaPreview(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    refresh();
}

void FormulaPreview::setCode(const QString &code) {
    if (code == m_code) return;
    m_code = code;
    emit codeChanged();
    refresh();
}

void FormulaPreview::setBackground(const QColor &background) {
    if (background == m_background) return;
    m_background = background;
    emit backgroundChanged();
    refresh();
}

void FormulaPreview::setPlaceholder(const QString &placeholder) {
    if (placeholder == m_placeholder) return;
    m_placeholder = placeholder;
    emit placeholderChanged();
    update();
}

qreal FormulaPreview::neededHeight() const {
    return std::max(kMinHeightPx, m_formula.size().height() + 2 * kMarginPx);
}

void FormulaPreview::refresh() {
    const QString code = m_code.trimmed();
    QFont font;
    font.setPixelSize(kFormulaPx);
    if (code.isEmpty()) m_formula = FormulaText();
    else m_formula.updateBlock(code, font, FormulaText::textColorOn(m_background, kFormulaText));
    // FormulaDialog::refresh: the parser accepts many mistakes silently, so the writing checks first.
    QStringList errors = FormulaText::syntaxErrors(code);
    if (m_formula.hadErrors()) errors += m_formula.errors().isEmpty() ? QStringList{tr("check the code.")} : m_formula.errors();
    m_valid = !code.isEmpty() && !m_formula.isEmpty() && errors.isEmpty();
    m_error = code.isEmpty() || errors.isEmpty() ? QString() : tr("The formula is not valid: %1").arg(errors.front());
    emit checked();
    update();
}

void FormulaPreview::paint(QPainter *painter) {
    const QRectF box = boundingRect();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(kBox);
    painter->drawRoundedRect(box, kRadiusPx, kRadiusPx);
    if (m_background.alpha() < 255) {  // a see-through background shows over the squares
        painter->save();
        QPainterPath clip;
        clip.addRoundedRect(box, kRadiusPx, kRadiusPx);
        painter->setClipPath(clip);
        painter->setBrush(kSquares);
        for (int y = 0; y < box.height(); y += kSquarePx)
            for (int x = (y / kSquarePx) % 2 * kSquarePx; x < box.width(); x += 2 * kSquarePx)
                painter->drawRect(x, y, kSquarePx, kSquarePx);
        painter->restore();
    }
    painter->setBrush(m_background);
    painter->drawRoundedRect(box, kRadiusPx, kRadiusPx);
    if (m_formula.isEmpty()) {
        painter->setPen(kPlaceholder);
        QFont hint = painter->font();
        hint.setPixelSize(kPlaceholderPx);
        painter->setFont(hint);
        painter->drawText(box, Qt::AlignCenter | Qt::TextWordWrap, m_placeholder);
        return;
    }
    // Wider than the box: scaled down to fit (the topic keeps the formula's real size).
    const QSizeF size = m_formula.size();
    const qreal scale = std::min<qreal>(1, (box.width() - 2 * kSidePx) / std::max<qreal>(1, size.width()));
    painter->translate(box.center());
    painter->scale(scale, scale);
    m_formula.draw(painter, QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height()), Qt::AlignCenter);
}
