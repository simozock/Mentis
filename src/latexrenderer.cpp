#include "latexrenderer.h"

#include <QHash>
#include <QRegularExpression>
#include <functional>

namespace {
QString convertScript(const QString &value, bool superscript) {
    static const QHash<QChar, QChar> superscripts{
        {'0', QChar(0x2070)}, {'1', QChar(0x00B9)}, {'2', QChar(0x00B2)}, {'3', QChar(0x00B3)},
        {'4', QChar(0x2074)}, {'5', QChar(0x2075)}, {'6', QChar(0x2076)}, {'7', QChar(0x2077)},
        {'8', QChar(0x2078)}, {'9', QChar(0x2079)}, {'+', QChar(0x207A)}, {'-', QChar(0x207B)},
        {'=', QChar(0x207C)}, {'(', QChar(0x207D)}, {')', QChar(0x207E)}, {'n', QChar(0x207F)},
        {'i', QChar(0x2071)}
    };
    static const QHash<QChar, QChar> subscripts{
        {'0', QChar(0x2080)}, {'1', QChar(0x2081)}, {'2', QChar(0x2082)}, {'3', QChar(0x2083)},
        {'4', QChar(0x2084)}, {'5', QChar(0x2085)}, {'6', QChar(0x2086)}, {'7', QChar(0x2087)},
        {'8', QChar(0x2088)}, {'9', QChar(0x2089)}, {'+', QChar(0x208A)}, {'-', QChar(0x208B)},
        {'=', QChar(0x208C)}, {'(', QChar(0x208D)}, {')', QChar(0x208E)}, {'a', QChar(0x2090)},
        {'e', QChar(0x2091)}, {'h', QChar(0x2095)}, {'i', QChar(0x1D62)}, {'j', QChar(0x2C7C)},
        {'k', QChar(0x2096)}, {'l', QChar(0x2097)}, {'m', QChar(0x2098)}, {'n', QChar(0x2099)},
        {'o', QChar(0x2092)}, {'p', QChar(0x209A)}, {'r', QChar(0x1D63)}, {'s', QChar(0x209B)},
        {'t', QChar(0x209C)}, {'u', QChar(0x1D64)}, {'v', QChar(0x1D65)}, {'x', QChar(0x2093)}
    };
    const auto &table = superscript ? superscripts : subscripts;
    QString result;
    for (const QChar character : value) result += table.value(character, character);
    return result;
}

void replaceGroups(QString &text, const QRegularExpression &expression,
                   const std::function<QString(const QRegularExpressionMatch &)> &replacement) {
    QRegularExpressionMatch match;
    int offset = 0;
    while ((match = expression.match(text, offset)).hasMatch()) {
        const QString value = replacement(match);
        text.replace(match.capturedStart(), match.capturedLength(), value);
        offset = match.capturedStart() + value.size();
    }
}

QString combineCharacters(const QString &value, QChar mark) {
    QString result;
    for (const QChar character : value) {
        result += character;
        if (!character.isSpace()) result += mark;
    }
    return result;
}
}

QString LatexRenderer::toDisplayText(const QString &source) {
    QString text = source.trimmed();
    if (text.size() >= 2 && text.startsWith('$') && text.endsWith('$')) text = text.mid(1, text.size() - 2);
    else if (text.startsWith("\\(") && text.endsWith("\\)")) text = text.mid(2, text.size() - 4);
    else return source;

    // P8 (2026-10-13): prima ogni chiamata compilava di nuovo una dozzina di espressioni
    // regolari (QRegularExpression(...) scritte dentro le chiamate sotto). L'outliner chiama
    // questa funzione per ogni riga con formule a ogni ridisegno: ora si compilano una volta sola.
    // I testi delle espressioni sono identici a prima, solo spostati qui.
    static const QRegularExpression kFraction(R"(\\frac\s*\{([^{}]*)\}\s*\{([^{}]*)\})");
    static const QRegularExpression kSquareRoot(R"(\\sqrt\s*\{([^{}]*)\})");
    static const QRegularExpression kOverline(R"(\\(?:overline|bar)\s*\{([^{}]*)\})");
    static const QRegularExpression kUnderline(R"(\\underline\s*\{([^{}]*)\})");
    static const QRegularExpression kHat(R"(\\hat\s*\{([^{}]*)\})");
    static const QRegularExpression kVector(R"(\\vec\s*\{([^{}]*)\})");
    static const QRegularExpression kSuperscriptGroup(R"(\^\{([^{}]*)\})");
    static const QRegularExpression kSubscriptGroup(R"(_\{([^{}]*)\})");
    static const QRegularExpression kSuperscriptChar(R"(\^([A-Za-z0-9+\-=()]))");
    static const QRegularExpression kSubscriptChar(R"(_([A-Za-z0-9+\-=()]))");
    static const QRegularExpression kLeftRight(R"(\\left|\\right)");
    static const QRegularExpression kSpacing(R"(\\,|\\;|\\!)");

    replaceGroups(text, kFraction,
                  [](const auto &match) { return match.captured(1) + QChar(0x2044) + match.captured(2); });
    replaceGroups(text, kSquareRoot,
                  [](const auto &match) { return QString(QChar(0x221A)) + '(' + match.captured(1) + ')'; });

    // I marcatori privati proteggono i comandi con argomento dalla successiva
    // sostituzione dei comandi semplici. Il contenuto può così includere altri
    // simboli LaTeX, che vengono convertiti prima di applicare l'accento.
    constexpr QChar overlineStart(0xE000), overlineEnd(0xE001);
    constexpr QChar underlineStart(0xE002), underlineEnd(0xE003);
    constexpr QChar hatStart(0xE004), hatEnd(0xE005);
    constexpr QChar vectorStart(0xE006), vectorEnd(0xE007);
    replaceGroups(text, kOverline,
                  [=](const auto &match) { return overlineStart + match.captured(1) + overlineEnd; });
    replaceGroups(text, kUnderline,
                  [=](const auto &match) { return underlineStart + match.captured(1) + underlineEnd; });
    replaceGroups(text, kHat,
                  [=](const auto &match) { return hatStart + match.captured(1) + hatEnd; });
    replaceGroups(text, kVector,
                  [=](const auto &match) { return vectorStart + match.captured(1) + vectorEnd; });
    replaceGroups(text, kSuperscriptGroup,
                  [](const auto &match) { return convertScript(match.captured(1), true); });
    replaceGroups(text, kSubscriptGroup,
                  [](const auto &match) { return convertScript(match.captured(1), false); });
    replaceGroups(text, kSuperscriptChar,
                  [](const auto &match) { return convertScript(match.captured(1), true); });
    replaceGroups(text, kSubscriptChar,
                  [](const auto &match) { return convertScript(match.captured(1), false); });

    static const QHash<QString, QString> commands{
        {"alpha", "α"}, {"beta", "β"}, {"gamma", "γ"}, {"delta", "δ"}, {"epsilon", "ε"},
        {"varepsilon", "ϵ"}, {"zeta", "ζ"}, {"eta", "η"}, {"theta", "θ"}, {"vartheta", "ϑ"},
        {"iota", "ι"}, {"kappa", "κ"}, {"lambda", "λ"}, {"mu", "μ"}, {"nu", "ν"},
        {"xi", "ξ"}, {"pi", "π"}, {"varpi", "ϖ"}, {"rho", "ρ"}, {"sigma", "σ"},
        {"tau", "τ"}, {"upsilon", "υ"}, {"phi", "φ"}, {"varphi", "ϕ"}, {"chi", "χ"},
        {"psi", "ψ"}, {"omega", "ω"}, {"Gamma", "Γ"}, {"Delta", "Δ"}, {"Theta", "Θ"},
        {"Lambda", "Λ"}, {"Xi", "Ξ"}, {"Pi", "Π"}, {"Sigma", "Σ"}, {"Phi", "Φ"},
        {"Psi", "Ψ"}, {"Omega", "Ω"}, {"times", "×"}, {"cdot", "·"}, {"pm", "±"},
        {"mp", "∓"}, {"div", "÷"}, {"neq", "≠"}, {"leq", "≤"}, {"geq", "≥"},
        {"approx", "≈"}, {"equiv", "≡"}, {"infty", "∞"}, {"sum", "∑"}, {"prod", "∏"},
        {"int", "∫"}, {"partial", "∂"}, {"nabla", "∇"}, {"in", "∈"}, {"notin", "∉"},
        {"subset", "⊂"}, {"supset", "⊃"}, {"cup", "∪"}, {"cap", "∩"}, {"forall", "∀"},
        {"exists", "∃"}, {"rightarrow", "→"}, {"leftarrow", "←"}, {"leftrightarrow", "↔"}
    };
    text.replace(kLeftRight, "");
    text.replace(kSpacing, " ");
    static const QRegularExpression command(R"(\\([A-Za-z]+))");
    QRegularExpressionMatch match;
    int offset = 0;
    while ((match = command.match(text, offset)).hasMatch()) {
        const QString name = match.captured(1);
        const QString value = commands.value(name, name);
        text.replace(match.capturedStart(), match.capturedLength(), value);
        offset = match.capturedStart() + value.size();
    }

    auto applyMarkedGroup = [&](QChar start, QChar end, QChar combiningMark) {
        int begin = 0;
        while ((begin = text.indexOf(start, begin)) >= 0) {
            const int finish = text.indexOf(end, begin + 1);
            if (finish < 0) break;
            const QString value = combineCharacters(text.mid(begin + 1, finish - begin - 1), combiningMark);
            text.replace(begin, finish - begin + 1, value);
            begin += value.size();
        }
    };
    applyMarkedGroup(overlineStart, overlineEnd, QChar(0x0305));
    applyMarkedGroup(underlineStart, underlineEnd, QChar(0x0332));
    applyMarkedGroup(hatStart, hatEnd, QChar(0x0302));
    applyMarkedGroup(vectorStart, vectorEnd, QChar(0x20D7));
    text.remove('{');
    text.remove('}');
    return text;
}
