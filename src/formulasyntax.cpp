#include "formulatext.h"

#include <cmath>

// FEAT-096 stage 4: FormulaText's text parsing and colour, without JKQTMathText (formulatext.cpp),
// so the core (MentisDomain) reads formulas without rendering them.

namespace {
bool isSpace(QChar c) { return c.isSpace(); }
}  // namespace

QVector<FormulaText::Segment> FormulaText::split(const QString &text) {
    QVector<Segment> segments;
    QString plain;
    int plainFrom = 0;
    int i = 0;
    auto flushPlain = [&] { if (!plain.isEmpty()) { segments.append({plain, false, plainFrom, i}); plain.clear(); } };
    const int n = text.size();
    while (i < n) {
        const QChar c = text[i];
        if (plain.isEmpty()) plainFrom = i;
        // \$ = dollaro letterale.
        if (c == '\\' && i + 1 < n && text[i + 1] == '$') { plain += '$'; i += 2; continue; }
        // \( ... \)
        if (c == '\\' && i + 1 < n && text[i + 1] == '(') {
            const int end = text.indexOf(QStringLiteral("\\)"), i + 2);
            if (end > i + 2) { flushPlain(); segments.append({text.mid(i + 2, end - i - 2), true, i, end + 2}); i = end + 2; continue; }
        }
        if (c == '$') {
            const bool display = i + 1 < n && text[i + 1] == '$';
            const int open = display ? 2 : 1;
            const int start = i + open;
            if (start < n && !isSpace(text[start])) {
                // Cerca il $ (o $$) di chiusura valido, saltando i \$ interni.
                int j = start;
                int end = -1;
                while (j < n) {
                    if (text[j] == '\\' && j + 1 < n) { j += 2; continue; }
                    if (text[j] == '$') {
                        const bool closes = display ? (j + 1 < n && text[j + 1] == '$') : true;
                        const bool validEnd = !isSpace(text[j - 1]) &&
                                              (display || j + 1 >= n || !text[j + 1].isDigit());
                        if (closes && validEnd && j > start) { end = j; break; }
                    }
                    ++j;
                }
                if (end > 0) {
                    flushPlain();
                    segments.append({text.mid(start, end - start), true, i, end + open});
                    i = end + open;
                    continue;
                }
            }
        }
        plain += c;
        ++i;
    }
    flushPlain();
    return segments;
}

bool FormulaText::containsMath(const QString &text) {
    if (!text.contains('$') && !text.contains(QStringLiteral("\\("))) return false;
    for (const auto &segment : split(text)) if (segment.math) return true;
    return false;
}

QColor FormulaText::textColorOn(const QColor &background, const QColor &nodeText) {
    if (background.alpha() < 128) return nodeText;
    // Contrasto semplice: luminanza relativa (WCAG) sopra meta' = testo scuro.
    const auto channel = [](qreal c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    const qreal luminance = 0.2126 * channel(background.redF()) + 0.7152 * channel(background.greenF()) + 0.0722 * channel(background.blueF());
    return luminance > 0.35 ? QColor("#172033") : QColor(Qt::white);
}
