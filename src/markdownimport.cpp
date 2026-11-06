#include "markdownimport.h"
#include "richtitle.h"
#include "markdownshortcuts.h"

#include "formulatext.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QStringList>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>
#include <cmath>
#include <utility>

namespace MarkdownImport {
namespace {

enum InlineFlag : quint8 { kBold = 1, kItalic = 2, kStrike = 4 };

// Riconoscimento delle righe. Fonte della sintassi: CommonMark, https://spec.commonmark.org/0.31.2/
// (titoli ATX 4.2, interruzioni tematiche 4.1, elenchi 5.2-5.3, blocchi di codice 4.5, citazioni
// 5.1); il convertitore ne copre solo la parte descritta nella scheda FEAT-055.
const QRegularExpression &headingLine() {
    static const QRegularExpression re(QStringLiteral("^ {0,3}(#{1,6})[ \\t]+(.*?)(?:[ \\t]+#+)?[ \\t]*$"));
    return re;
}
const QRegularExpression &listLine() {
    static const QRegularExpression re(QStringLiteral("^([ \\t]*)(?:[-*+]|\\d{1,9}[.)])[ \\t]+(.*)$"));
    return re;
}
const QRegularExpression &ruleLine() {
    static const QRegularExpression re(QStringLiteral("^[ \\t]{0,3}([-*_])(?:[ \\t]*\\1){2,}[ \\t]*$"));
    return re;
}
const QRegularExpression &fenceLine() {
    static const QRegularExpression re(QStringLiteral("^[ \\t]*(```|~~~)"));
    return re;
}
const QRegularExpression &quoteLine() {
    static const QRegularExpression re(QStringLiteral("^[ \\t]*>[ \\t]?(.*)$"));
    return re;
}
const QRegularExpression &imageOnlyLine() {
    static const QRegularExpression re(QStringLiteral("^!\\[[^\\]]*\\]\\([^)]*\\)$"));
    return re;
}
const QRegularExpression &checkbox() {
    static const QRegularExpression re(QStringLiteral("^\\[[ xX]\\][ \\t]+"));
    return re;
}

// Testo e formati di una riga, un flag per carattere di text.
struct InlineText {
    QString text;
    QVector<quint8> flags;
};

bool isAsciiPunctuation(QChar c) { return c.unicode() < 128 && (c.isPunct() || c.isSymbol()); }

// Formati in linea: **grassetto**/__grassetto__, *corsivo*/_corsivo_, ~~barrato~~. Un simbolo apre
// un formato solo se piu' avanti c'e' il simbolo che lo chiude, altrimenti resta testo. Codice in
// linea, formule $...$, collegamenti e immagini (ne resta il testo), <br> = a capo, \* = carattere.
InlineText parseInline(const QString &src) {
    static const QRegularExpression lineBreak(QStringLiteral("<br[ \\t]*/?>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression link(QStringLiteral("!?\\[([^\\]]*)\\]\\([^)\\s]*(?:[ \\t]+\"[^\"]*\")?\\)"));
    constexpr auto kAtOffset = QRegularExpression::AnchorAtOffsetMatchOption;
    InlineText out;
    quint8 state = 0;
    auto emitText = [&](const QString &text) {
        out.text += text;
        for (qsizetype i = 0; i < text.size(); ++i) out.flags.append(state);
    };
    const qsizetype n = src.size();
    for (qsizetype k = 0; k < n;) {
        const QChar c = src.at(k);
        if (c == QLatin1Char('\\') && k + 1 < n && isAsciiPunctuation(src.at(k + 1))) {
            emitText(src.at(k + 1));
            k += 2;
            continue;
        }
        if (c == QLatin1Char('`') || c == QLatin1Char('$')) {
            const qsizetype end = src.indexOf(c, k + 1);
            if (end > k + 1) {
                // Le formule restano con i $ (FormulaText le disegna); il codice perde i backtick.
                emitText(c == QLatin1Char('$') ? src.mid(k, end - k + 1) : src.mid(k + 1, end - k - 1));
                k = end + 1;
                continue;
            }
        }
        if (c == QLatin1Char('<')) {
            if (const auto match = lineBreak.match(src, k, QRegularExpression::NormalMatch, kAtOffset); match.hasMatch()) {
                emitText(QStringLiteral("\n"));
                k += match.capturedLength();
                continue;
            }
        }
        if (c == QLatin1Char('[') || (c == QLatin1Char('!') && k + 1 < n && src.at(k + 1) == QLatin1Char('['))) {
            if (const auto match = link.match(src, k, QRegularExpression::NormalMatch, kAtOffset); match.hasMatch()) {
                emitText(match.captured(1));
                k += match.capturedLength();
                continue;
            }
        }
        if (c == QLatin1Char('~') && k + 1 < n && src.at(k + 1) == QLatin1Char('~')) {
            if ((state & kStrike) || src.indexOf(QStringLiteral("~~"), k + 2) >= 0) {
                state ^= kStrike;
                k += 2;
                continue;
            }
        }
        if (c == QLatin1Char('*') || c == QLatin1Char('_')) {
            qsizetype run = 1;
            while (k + run < n && src.at(k + run) == c) ++run;
            // "_" dentro una parola (snake_case) non e' un formato.
            const bool underscore = c == QLatin1Char('_');
            const bool canOpen = !underscore || k == 0 || !src.at(k - 1).isLetterOrNumber();
            const bool canClose = !underscore || k + run >= n || !src.at(k + run).isLetterOrNumber();
            const QString pair(2, c);
            qsizetype left = run;
            while (left > 0) {
                if (left >= 2 && ((state & kBold) ? canClose : canOpen && src.indexOf(pair, k + run) >= 0)) {
                    state ^= kBold;
                    left -= 2;
                } else if ((state & kItalic) ? canClose : canOpen && src.indexOf(c, k + run) >= 0) {
                    state ^= kItalic;
                    left -= 1;
                } else {
                    break;
                }
            }
            emitText(QString(left, c));
            k += run;
            continue;
        }
        emitText(c);
        ++k;
    }
    return out;
}

QString plainInline(const QString &src) { return parseInline(src).text.trimmed(); }

// FEAT-060: "#etichetta" in fondo alla voce, dopo uno spazio e con una lettera (o "_") come primo
// carattere ("Capitolo #2" resta titolo); "_" al posto degli spazi. Toglie le etichette dal testo.
QStringList takeLabels(QString &source) {
    static const QRegularExpression tail(QStringLiteral(R"(\s#([\p{L}_][^\s#]*)\s*$)"));
    QStringList labels;
    for (QRegularExpressionMatch match = tail.match(source); match.hasMatch(); match = tail.match(source)) {
        labels.prepend(match.captured(1).replace(QLatin1Char('_'), QLatin1Char(' ')));
        source.truncate(match.capturedStart());
    }
    return labels;
}

Item makeItem(const QString &rawSource) {
    QString source = rawSource;
    const QStringList labels = takeLabels(source);
    InlineText parsed = parseInline(source);
    qsizetype first = 0;
    qsizetype last = parsed.text.size();
    while (first < last && parsed.text.at(first).isSpace()) ++first;
    while (last > first && parsed.text.at(last - 1).isSpace()) --last;
    Item item;
    item.labels = labels;
    item.title = parsed.text.mid(first, last - first);
    for (qsizetype i = first; i < last;) {
        const quint8 value = parsed.flags.at(i);
        qsizetype end = i + 1;
        while (end < last && parsed.flags.at(end) == value) ++end;
        if (value) item.spans.push_back({int(i - first), int(end - i), bool(value & kBold), bool(value & kItalic), bool(value & kStrike)});
        i = end;
    }
    return item;
}

// Rientro di una riga: tabulazioni e spazi iniziali contati separatamente.
std::pair<int, int> leadingIndent(const QString &line) {
    int tabs = 0;
    int spaces = 0;
    for (const QChar c : line) {
        if (c == QLatin1Char('\t')) ++tabs;
        else if (c == QLatin1Char(' ')) ++spaces;
        else break;
    }
    return {tabs, spaces};
}

// Livello dal rientro: una tabulazione = un livello, spazi divisi per l'unita' arrotondando al
// livello piu' vicino (rientri irregolari).
int indentLevel(const QString &line, int unit) {
    const auto [tabs, spaces] = leadingIndent(line);
    return tabs + int(std::lround(double(spaces) / unit));
}

// Costruzione dell'albero: ogni voce va al massimo un livello sotto la precedente.
class TreeBuilder {
public:
    explicit TreeBuilder(int maxItems) : m_maxItems(maxItems) {}

    // false oltre il limite di voci.
    bool add(Item item, int depth) {
        if (m_maxItems > 0 && m_result.itemCount >= m_maxItems) {
            m_result.tooLarge = true;
            return false;
        }
        depth = std::max(0, std::min(depth, m_stack.empty() ? 0 : m_stack.back().first + 1));
        while (!m_stack.empty() && m_stack.back().first >= depth) m_stack.pop_back();
        // I puntatori nello stack sono solo antenati: aggiungere un fratello non li invalida.
        std::vector<Item> &siblings = m_stack.empty() ? m_result.items : m_stack.back().second->children;
        siblings.push_back(std::move(item));
        m_stack.emplace_back(depth, &siblings.back());
        ++m_result.itemCount;
        m_blankPending = false;
        return true;
    }

    bool hasItem() const { return !m_stack.empty(); }
    int lastDepth() const { return m_stack.empty() ? -1 : m_stack.back().first; }

    void addNoteLine(const QString &line, const QString &raw = QString()) {
        if (m_stack.empty() || line.isEmpty()) return;
        Item *item = m_stack.back().second;
        // FEAT-064: una riga che e' solo "$$codice$$" (scritta dall'esportazione) e' la formula del nodo.
        static const QRegularExpression block(QStringLiteral(R"(^\s*\$\$(.+)\$\$\s*$)"));
        if (const QRegularExpressionMatch match = block.match(raw.isEmpty() ? line : raw); match.hasMatch() && item->formula.isEmpty()) {
            item->formula = match.captured(1).trimmed();
            return;
        }
        const QString separator = m_blankPending ? QStringLiteral("\n\n") : QStringLiteral("\n");
        if (!item->notes.isEmpty()) item->notes += separator;
        item->notes += line;
        // FEAT-059: "• " e "1\. " scritti dall'esportazione tornano elenchi della nota.
        QString markdown = raw.isEmpty() ? line : raw.trimmed();
        static const QRegularExpression bullet(QStringLiteral("^• "));
        static const QRegularExpression number(QStringLiteral("^(\\d+)\\\\\\. "));
        markdown.replace(bullet, QStringLiteral("- "));
        markdown.replace(number, QStringLiteral("\\1. "));
        if (!item->notesMarkdown.isEmpty()) item->notesMarkdown += separator;
        item->notesMarkdown += markdown;
        m_blankPending = false;
    }

    void blankLine() { m_blankPending = true; }

    Result take() {
        if (m_result.tooLarge) m_result.items.clear();
        return std::move(m_result);
    }

private:
    int m_maxItems = 0;
    Result m_result;
    std::vector<std::pair<int, Item *>> m_stack;
    bool m_blankPending = false;
};

// Righe utili: senza commenti HTML (anche quelli scritti da "Esporta Markdown…"). I blocchi di
// codice restano, con i loro delimitatori, e li riconosce chi legge.
QStringList contentLines(const QString &markdown) {
    QString text = markdown;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QStringList lines;
    bool inComment = false;
    bool inFence = false;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (fenceLine().match(line).hasMatch() && !inComment) inFence = !inFence;
        const QString trimmed = line.trimmed();
        if (!inFence && !inComment && trimmed.startsWith(QStringLiteral("<!--"))) {
            inComment = !trimmed.contains(QStringLiteral("-->"));
            continue;
        }
        if (inComment) {
            if (trimmed.contains(QStringLiteral("-->"))) inComment = false;
            continue;
        }
        lines.append(line);
    }
    return lines;
}

// Unita' del rientro: il rientro di spazi piu' piccolo maggiore di zero (2 se non ce n'e').
int indentUnit(const QStringList &lines, const QRegularExpression *only) {
    int unit = 0;
    bool inFence = false;
    for (const QString &line : lines) {
        if (fenceLine().match(line).hasMatch()) { inFence = !inFence; continue; }
        if (inFence || line.trimmed().isEmpty() || (only && !only->match(line).hasMatch())) continue;
        const int spaces = leadingIndent(line).second;
        if (spaces > 0 && (unit == 0 || spaces < unit)) unit = spaces;
    }
    return unit > 0 ? unit : 2;
}

// Testo senza titoli ne' voci d'elenco: una voce per riga, livelli dal rientro.
Result parsePlain(const QStringList &lines, int maxItems) {
    TreeBuilder tree(maxItems);
    const int unit = indentUnit(lines, nullptr);
    for (const QString &line : lines) {
        if (line.trimmed().isEmpty()) continue;
        Item item = makeItem(line);
        if (item.title.isEmpty()) continue;
        if (!tree.add(std::move(item), indentLevel(line, unit))) break;
    }
    return tree.take();
}

}  // namespace

Result parse(const QString &markdown, int maxItems) {
    const QStringList lines = contentLines(markdown);
    bool structured = false;
    int minHeading = 7;
    bool inFence = false;
    for (const QString &line : lines) {
        if (fenceLine().match(line).hasMatch()) { inFence = !inFence; continue; }
        if (inFence || ruleLine().match(line).hasMatch()) continue;
        if (const auto heading = headingLine().match(line); heading.hasMatch()) {
            structured = true;
            minHeading = std::min(minHeading, int(heading.capturedLength(1)));
        } else if (listLine().match(line).hasMatch()) {
            structured = true;
        }
    }
    if (!structured) return parsePlain(lines, maxItems);

    TreeBuilder tree(maxItems);
    const int unit = indentUnit(lines, &listLine());
    int listBase = 0;  // profondita' delle voci d'elenco senza rientro: sotto l'ultimo titolo
    inFence = false;
    for (const QString &line : lines) {
        if (fenceLine().match(line).hasMatch()) { inFence = !inFence; continue; }
        if (inFence) {
            tree.addNoteLine(line);  // codice: testo cosi' com'e'
            continue;
        }
        if (line.trimmed().isEmpty()) { tree.blankLine(); continue; }
        if (ruleLine().match(line).hasMatch() || imageOnlyLine().match(line.trimmed()).hasMatch()) continue;
        if (const auto heading = headingLine().match(line); heading.hasMatch()) {
            Item item = makeItem(heading.captured(2));
            if (item.title.isEmpty()) continue;
            if (!tree.add(std::move(item), int(heading.capturedLength(1)) - minHeading)) break;
            listBase = tree.lastDepth() + 1;
            continue;
        }
        if (const auto list = listLine().match(line); list.hasMatch()) {
            QString text = list.captured(2);
            // FEAT-048: "[ ]" e "[x]" diventano la casella di spunta del nodo.
            int task = 0;
            if (const auto box = checkbox().match(text); box.hasMatch()) {
                task = box.captured(0).contains(QLatin1Char('x'), Qt::CaseInsensitive) ? 2 : 1;
                text.remove(0, box.capturedLength());
            }
            Item item = makeItem(text);
            item.task = task;
            // BUG-020: una voce d'elenco vuota ("- ") e' un nodo senza testo, come la scrive
            // l'esportazione; saltarla spostava i suoi figli di un livello.
            if (!tree.add(std::move(item), listBase + indentLevel(line, unit))) break;
            continue;
        }
        const auto quote = quoteLine().match(line);
        const QString text = plainInline(quote.hasMatch() ? quote.captured(1) : line);
        if (tree.hasItem()) {
            tree.addNoteLine(text, quote.hasMatch() ? quote.captured(1) : line);
        } else {
            // Testo prima della prima voce: una voce a se'; le righe seguenti sono la sua nota.
            Item item = makeItem(quote.hasMatch() ? quote.captured(1) : line);
            if (!item.title.isEmpty() && !tree.add(std::move(item), 0)) break;
        }
    }
    return tree.take();
}

QJsonObject toSubtreeJson(const Item &item, bool root) {
    QJsonObject json{{QStringLiteral("title"), item.title}};
    if (!item.notes.isEmpty()) json.insert(QStringLiteral("notes"), item.notes);
    // FEAT-059: nota con formati, solo se il Markdown ha simboli oltre al testo semplice.
    // FEAT-068: anche "==evidenziato==", che il Markdown di Qt non conosce (lo converte il motore
    // delle scorciatoie, come se fosse stato scritto).
    if (!item.notes.isEmpty() && (item.notesMarkdown != item.notes || item.notesMarkdown.contains(QStringLiteral("==")))) {
        QTextDocument note;
        note.setMarkdown(item.notesMarkdown);
        MarkdownShortcuts::Options options;
        options.context = MarkdownShortcuts::Context::Note;
        MarkdownShortcuts::applyToDocument(&note, options);
        json.insert(QStringLiteral("notes"), note.toPlainText());
        json.insert(QStringLiteral("notesHtml"), RichTitle::withoutDocumentFont(note.toHtml()));
    }
    if (item.task) json.insert(QStringLiteral("task"), item.task == 2 ? QStringLiteral("done") : QStringLiteral("open"));  // FEAT-048
    if (!item.labels.isEmpty()) json.insert(QStringLiteral("labels"), QJsonArray::fromStringList(item.labels));  // FEAT-060
    if (!item.formula.isEmpty()) json.insert(QStringLiteral("formula"), item.formula);  // FEAT-064
    // I titoli con formule restano testo semplice, come sulla mappa (NodeItem::finishEditing).
    if (!item.spans.empty() && !FormulaText::containsMath(item.title)) {
        // Pesi come i titoli salvati dall'outliner (kNormalBold/kHeavyBold in mainwindow.cpp):
        // sempre espliciti, cosi' il grassetto si puo' anche togliere.
        const int boldWeight = root ? QFont::Black : QFont::Bold;
        const int regularWeight = root ? QFont::Bold : QFont::Normal;
        QTextDocument document;
        document.setUndoRedoEnabled(false);
        document.setPlainText(item.title);
        QTextCursor cursor(&document);
        cursor.select(QTextCursor::Document);
        QTextCharFormat regular;
        regular.setFontWeight(regularWeight);
        cursor.mergeCharFormat(regular);
        for (const Span &span : item.spans) {
            cursor.setPosition(span.start);
            cursor.setPosition(span.start + span.length, QTextCursor::KeepAnchor);
            QTextCharFormat format;
            format.setFontWeight(span.bold ? boldWeight : regularWeight);
            format.setFontItalic(span.italic);
            format.setFontStrikeOut(span.strikeOut);
            cursor.mergeCharFormat(format);
        }
        json.insert(QStringLiteral("titleHtml"), document.toHtml());
    }
    QJsonArray children;
    for (const Item &child : item.children) children.append(toSubtreeJson(child));
    if (!children.isEmpty()) json.insert(QStringLiteral("children"), children);
    return json;
}

}  // namespace MarkdownImport
