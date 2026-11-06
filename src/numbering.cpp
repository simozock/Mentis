#include "numbering.h"

#include <QJsonObject>

#include <algorithm>

namespace Numbering {

namespace {

constexpr Symbol kPresetSymbols[] = {Symbol::Arabic, Symbol::UpperLetter, Symbol::LowerLetter, Symbol::UpperRoman};

bool isRoman(const QString &token, bool upper) {
    const QString digits = upper ? QStringLiteral("IVXLCDM") : QStringLiteral("ivxlcdm");
    if (token.isEmpty() || token.front() != (upper ? u'I' : u'i')) return false;
    for (const QChar c : token) if (!digits.contains(c)) return false;
    return true;
}

QString roman(int value) {
    static const std::pair<int, const char *> parts[] = {{1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
                                                        {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
    QString text;
    for (const auto &[amount, letters] : parts)
        for (; value >= amount; value -= amount) text += QLatin1String(letters);
    return text;
}

QString letters(int value) {
    QString text;
    for (; value > 0; value = (value - 1) / 26) text.prepend(QChar(u'A' + (value - 1) % 26));
    return text;
}

}  // namespace

const QList<QChar> &separators() {
    static const QList<QChar> list{u'.', u')', u'-'};
    return list;
}

QStringList presetFormats(QChar separator) {
    QStringList formats;
    for (const char *preset : {"1.2.3", "A.B.C", "a.b.c", "I.II.III"})
        formats.append(QString::fromLatin1(preset).replace(u'.', separator));
    return formats;
}

int presetIndex(const BranchNumbering &numbering) {
    return int(presetFormats(numbering.separator).indexOf(numbering.format));
}

std::optional<QList<Symbol>> parse(const QString &format, QChar separator) {
    if (format.isEmpty()) return std::nullopt;
    QList<Symbol> symbols;
    for (const QString &token : format.split(separator)) {
        if (token.isEmpty()) return std::nullopt;
        bool digits = true, upper = true, lower = true;
        for (const QChar c : token) {
            digits = digits && c.isDigit();
            upper = upper && c.isLetter() && c.isUpper();
            lower = lower && c.isLetter() && c.isLower();
        }
        if (digits) symbols.append(Symbol::Arabic);
        else if (isRoman(token, true)) symbols.append(Symbol::UpperRoman);
        else if (isRoman(token, false)) symbols.append(Symbol::LowerRoman);
        else if (upper) symbols.append(Symbol::UpperLetter);
        else if (lower) symbols.append(Symbol::LowerLetter);
        else return std::nullopt;
    }
    return symbols;
}

bool isValid(const BranchNumbering &numbering) {
    return separators().contains(numbering.separator) && parse(numbering.format, numbering.separator)
           && numbering.levels >= 0 && numbering.levels <= kMaxLevels && numbering.start >= 1 && numbering.start <= kMaxStart;
}

QString symbolText(int value, Symbol symbol) {
    value = std::max(value, 1);
    switch (symbol) {
    case Symbol::Arabic: return QString::number(value);
    case Symbol::UpperLetter: return letters(value);
    case Symbol::LowerLetter: return letters(value).toLower();
    case Symbol::UpperRoman: return value < 4000 ? roman(value) : QString::number(value);
    case Symbol::LowerRoman: return value < 4000 ? roman(value).toLower() : QString::number(value);
    }
    return QString::number(value);
}

QString label(const BranchNumbering &numbering, const QList<int> &path) {
    const qsizetype depth = path.size();
    const std::optional<QList<Symbol>> symbols = parse(numbering.format, numbering.separator);
    if (!symbols || depth == 0 || (numbering.levels > 0 && depth > numbering.levels)) return QString();
    // A preset numbers every level with its symbol; a custom format only the levels it writes.
    const int preset = presetIndex(numbering);
    if (preset < 0 && depth > symbols->size()) return QString();
    const auto symbolAt = [&](qsizetype level) { return preset >= 0 ? kPresetSymbols[preset] : symbols->at(level); };
    QString text;
    if (!numbering.hierarchical) {
        text = symbolText(path.back(), symbolAt(depth - 1));
    } else {
        QStringList parts;
        for (qsizetype level = 0; level < depth; ++level) parts.append(symbolText(path[level], symbolAt(level)));
        text = parts.join(numbering.separator);
    }
    if (numbering.trailingSeparator) text += numbering.separator;
    return text;
}

QJsonValue toJson(const BranchNumbering &numbering) {
    QJsonObject object{{QStringLiteral("format"), numbering.format}};
    if (numbering.separator != u'.') object.insert(QStringLiteral("separator"), QString(numbering.separator));
    if (numbering.trailingSeparator) object.insert(QStringLiteral("trailingSeparator"), true);
    if (!numbering.hierarchical) object.insert(QStringLiteral("byLevel"), true);
    if (numbering.levels > 0) object.insert(QStringLiteral("levels"), numbering.levels);
    if (numbering.start != 1) object.insert(QStringLiteral("start"), numbering.start);
    return object;
}

std::optional<BranchNumbering> fromJson(const QJsonValue &value) {
    if (!value.isObject()) return std::nullopt;
    const QJsonObject object = value.toObject();
    BranchNumbering numbering;
    numbering.format = object.value(QStringLiteral("format")).toString();
    const QString separator = object.value(QStringLiteral("separator")).toString(QStringLiteral("."));
    if (separator.size() != 1) return std::nullopt;
    numbering.separator = separator.front();
    numbering.trailingSeparator = object.value(QStringLiteral("trailingSeparator")).toBool(false);
    numbering.hierarchical = !object.value(QStringLiteral("byLevel")).toBool(false);
    numbering.levels = object.value(QStringLiteral("levels")).toInt(0);
    numbering.start = object.value(QStringLiteral("start")).toInt(1);
    return isValid(numbering) ? std::optional(numbering) : std::nullopt;
}

}  // namespace Numbering
