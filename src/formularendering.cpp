#include "formularendering.h"

namespace FormulaRendering {

Hooks &hooks() {
    static Hooks installed;
    return installed;
}

QImage boxImage(const QString &code, const QFont &font, const QColor &background, const QColor &text,
                const QSizeF &size, qreal ratio) {
    return hooks().boxImage ? hooks().boxImage(code, font, background, text, size, ratio) : QImage();
}

}  // namespace FormulaRendering
