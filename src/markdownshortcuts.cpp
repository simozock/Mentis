#include "markdownshortcuts.h"

#include "nodelinks.h"

#include <QFont>
#include <QFontDatabase>
#include <QRegularExpression>
#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextList>

namespace MarkdownShortcuts {

namespace {
constexpr int kCodeProperty = QTextFormat::UserProperty + 68;  // riconosce il codice nell'HTML riletto

// Scorciatoia in linea: simbolo di apertura e chiusura, formato da applicare, contesti.
struct Inline {
    QChar trigger;
    QRegularExpression pattern;  // sul testo del blocco fino al cursore, ancorato alla fine
    bool notesOnly;
    int kind;  // 0 grassetto, 1 corsivo, 2 barrato, 3 evidenziato, 4 codice
};

const QList<Inline> &inlines() {
    // (?<![x\w]) prima dell'apertura: niente simbolo doppio ne' lettera attaccata; (?!\s) e (?<!\s):
    // il contenuto tocca i simboli. Il primo carattere del contenuto non e' il simbolo stesso.
    static const QList<Inline> list{
        {QLatin1Char('*'), QRegularExpression(QStringLiteral(R"((?<![\*\w])\*\*(?![\s\*])(.+?)(?<![\s\*])\*\*$)")), false, 0},
        {QLatin1Char('*'), QRegularExpression(QStringLiteral(R"((?<![\*\w])\*(?![\s\*])(.+?)(?<![\s\*])\*$)")), false, 1},
        {QLatin1Char('~'), QRegularExpression(QStringLiteral(R"((?<![~\w])~~(?![\s~])(.+?)(?<![\s~])~~$)")), false, 2},
        {QLatin1Char('='), QRegularExpression(QStringLiteral(R"((?<![=\w])==(?![\s=])(.+?)(?<![\s=])==$)")), false, 3},
        {QLatin1Char('`'), QRegularExpression(QStringLiteral(R"((?<![`\w])`([^`]+)`$)")), true, 4},
    };
    return list;
}

// Dentro una formula se prima del punto ci sono "$" non chiusi (conteggio dispari).
bool insideMath(const QString &before) {
    int dollars = 0;
    for (const QChar c : before)
        if (c == QLatin1Char('$')) ++dollars;
    return dollars % 2 == 1;
}

QTextCharFormat formatFor(int kind, const Options &options) {
    QTextCharFormat format;
    switch (kind) {
    case 0: format.setFontWeight(options.boldWeight); break;
    case 1: format.setFontItalic(true); break;
    case 2: format.setFontStrikeOut(true); break;
    case 3: format = highlightFormat(); break;
    default: format = codeFormat(); break;
    }
    return format;
}

// Toglie i simboli attorno al contenuto [contentStart, contentEnd) e lo formatta.
void wrap(QTextCursor &cursor, int blockStart, int openStart, int contentStart, int contentEnd, int closeEnd,
          const QTextCharFormat &format) {
    QTextCursor edit(cursor);
    edit.setPosition(blockStart + contentStart);
    const QTextCharFormat before = edit.charFormat();  // formato del testo prima dei simboli
    edit.beginEditBlock();
    edit.setPosition(blockStart + contentEnd);
    edit.setPosition(blockStart + closeEnd, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.setPosition(blockStart + openStart);
    edit.setPosition(blockStart + contentStart, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    const int length = contentEnd - contentStart;
    edit.setPosition(blockStart + openStart);
    edit.setPosition(blockStart + openStart + length, QTextCursor::KeepAnchor);
    edit.mergeCharFormat(format);
    edit.endEditBlock();
    cursor.setPosition(blockStart + openStart + length);
    // Il testo scritto dopo non continua il formato (come Typora).
    QTextCharFormat next = before;
    next.setAnchor(before.isAnchor());
    cursor.setCharFormat(next);
}

bool applyInline(QTextCursor &cursor, QChar typed, const Options &options) {
    const QTextBlock block = cursor.block();
    const int column = cursor.position() - block.position();
    const QString before = block.text().left(column);
    for (const Inline &rule : inlines()) {
        if (rule.trigger != typed || (rule.notesOnly && options.context != Context::Note)) continue;
        const QRegularExpressionMatch match = rule.pattern.match(before);
        if (!match.hasMatch()) continue;
        const int openStart = int(match.capturedStart(0));
        if (insideMath(before.left(openStart))) return false;
        // Niente scorciatoie dentro il codice (il simbolo di apertura e' gia' codice).
        QTextCursor probe(cursor);
        probe.setPosition(block.position() + openStart + 1);
        if (isCode(probe.charFormat())) return false;
        const int contentStart = int(match.capturedStart(1));
        const int contentEnd = int(match.capturedEnd(1));
        wrap(cursor, block.position(), openStart, contentStart, contentEnd, column, formatFor(rule.kind, options));
        return true;
    }
    return false;
}

// "[testo](indirizzo)" nelle note: collegamento come quello del bottone della catena.
bool applyLink(QTextCursor &cursor) {
    static const QRegularExpression pattern(QStringLiteral(R"((?<!!)\[([^\]\[]+)\]\(([^\s()]+)\)$)"));
    const QTextBlock block = cursor.block();
    const int column = cursor.position() - block.position();
    const QString before = block.text().left(column);
    const QRegularExpressionMatch match = pattern.match(before);
    if (!match.hasMatch() || insideMath(before.left(int(match.capturedStart(0))))) return false;
    const QString address = NodeLinks::webAddress(match.captured(2));
    if (address.isEmpty()) return false;
    QTextCharFormat link;
    link.setAnchor(true);
    link.setAnchorHref(address);
    link.setFontUnderline(true);
    link.setForeground(QColor("#4f8cff"));  // NoteEditor::linkColor
    const int openStart = int(match.capturedStart(0));
    wrap(cursor, block.position(), openStart, int(match.capturedStart(1)), int(match.capturedEnd(1)), column, link);
    // Dopo il collegamento si scrive testo normale.
    QTextCharFormat plain = cursor.charFormat();
    plain.setAnchor(false);
    plain.setAnchorHref(QString());
    plain.setFontUnderline(false);
    plain.clearForeground();
    cursor.setCharFormat(plain);
    return true;
}

// Inizio di riga seguito dallo spazio appena scritto.
Result applyBlockStart(QTextCursor &cursor, const Options &options) {
    Result result;
    const QTextBlock block = cursor.block();
    const int column = cursor.position() - block.position();
    const QString before = block.text().left(column);
    const auto removePrefix = [&](int length) {
        cursor.setPosition(block.position());
        cursor.setPosition(block.position() + length, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    };
    if (options.context == Context::Title) {
        // Solo all'inizio del titolo (primo paragrafo): "- [ ] ", "[ ] ", "- [x] ", "[x] ".
        static const QRegularExpression box(QStringLiteral(R"(^(?:- )?\[([ xX])\] $)"));
        const QRegularExpressionMatch match = box.match(before);
        if (!match.hasMatch() || block.blockNumber() != 0) return result;
        cursor.beginEditBlock();
        removePrefix(int(before.size()));
        cursor.endEditBlock();
        result.changed = true;
        result.task = match.captured(1).trimmed().isEmpty() ? 1 : 2;
        return result;
    }
    if (cursor.currentList()) return result;
    if (before == QLatin1String("- ") || before == QLatin1String("* ") || before == QLatin1String("1. ")) {
        cursor.beginEditBlock();
        removePrefix(int(before.size()));
        QTextListFormat list;
        list.setStyle(before == QLatin1String("1. ") ? QTextListFormat::ListDecimal : QTextListFormat::ListDisc);
        cursor.createList(list);
        cursor.endEditBlock();
        result.changed = true;
    } else if (before == QLatin1String("## ") || before == QLatin1String("# ")) {
        // Come il bottone "Titolo" di NoteEditor: livello 2, grassetto, 18 px.
        cursor.beginEditBlock();
        removePrefix(int(before.size()));
        QTextBlockFormat blockFormat = cursor.blockFormat();
        blockFormat.setHeadingLevel(2);
        cursor.setBlockFormat(blockFormat);
        QTextCharFormat heading;
        heading.setFontWeight(QFont::Bold);
        heading.setProperty(QTextFormat::FontPixelSize, options.headingPx);
        QTextCursor whole(cursor);
        whole.select(QTextCursor::BlockUnderCursor);
        whole.mergeCharFormat(heading);
        cursor.mergeCharFormat(heading);
        cursor.endEditBlock();
        result.changed = true;
    }
    return result;
}
}  // namespace

bool isTrigger(QChar typed) {
    return typed == QLatin1Char('*') || typed == QLatin1Char('~') || typed == QLatin1Char('=') ||
           typed == QLatin1Char('`') || typed == QLatin1Char(')') || typed == QLatin1Char(' ');
}

Result afterTyping(QTextCursor &cursor, QChar typed, const Options &options) {
    Result result;
    if (!enabled() || cursor.isNull() || cursor.hasSelection() || !isTrigger(typed)) return result;
    if (typed == QLatin1Char(' ')) return applyBlockStart(cursor, options);
    if (typed == QLatin1Char(')')) {
        result.changed = options.context == Context::Note && applyLink(cursor);
        return result;
    }
    result.changed = applyInline(cursor, typed, options);
    return result;
}

bool applyToDocument(QTextDocument *document, const Options &options) {
    bool changed = false;
    // Il codice letto dal Markdown di Qt (solo spaziatura fissa) prende l'aspetto di quello scritto.
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !fragment.charFormat().fontFixedPitch() || fragment.charFormat().boolProperty(kCodeProperty)) continue;
            QTextCursor cursor(document);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            cursor.mergeCharFormat(codeFormat());
            changed = true;
        }
    for (QTextBlock block = document->begin(); block.isValid(); block = block.next()) {
        // Si riprova sullo stesso blocco finche' qualcosa cambia (piu' scorciatoie sulla riga).
        for (bool again = true; again;) {
            again = false;
            const QString text = block.text();
            for (int end = 1; end <= text.size() && !again; ++end) {
                const QChar c = text.at(end - 1);
                if (c != QLatin1Char('*') && c != QLatin1Char('~') && c != QLatin1Char('=') && c != QLatin1Char('`')) continue;
                QTextCursor cursor(document);
                cursor.setPosition(block.position() + end);
                if (applyInline(cursor, c, options)) again = changed = true;
            }
        }
    }
    return changed;
}

QTextCharFormat highlightFormat() {
    QTextCharFormat format;
    format.setBackground(QColor("#fff3a3"));
    format.setForeground(QColor("#1f2937"));  // leggibile sul giallo anche su nodi e riquadri scuri
    return format;
}

QTextCharFormat codeFormat() {
    QTextCharFormat format;
    format.setFontFamilies({QFontDatabase::systemFont(QFontDatabase::FixedFont).family()});
    format.setFontFixedPitch(true);
    format.setBackground(QColor(127, 127, 127, 40));
    format.setProperty(kCodeProperty, true);
    return format;
}

bool isHighlight(const QTextCharFormat &format) {
    return format.background().style() != Qt::NoBrush && format.background().color() == QColor("#fff3a3");
}

bool isCode(const QTextCharFormat &format) {
    // Dopo l'HTML riletto resta solo la famiglia del font (proprieta' e fixedPitch si perdono).
    if (format.fontFixedPitch() || format.boolProperty(kCodeProperty)) return true;
    const QStringList families = format.fontFamilies().toStringList();
    return !families.isEmpty() && families.front() == QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

namespace {
QTextCharFormat withoutHighlight(QTextCharFormat format) {
    format.clearBackground();
    if (format.foreground().color() == QColor("#1f2937")) format.clearForeground();  // testo messo dall'evidenziato
    return format;
}
QTextCharFormat withoutCode(QTextCharFormat format) {
    format.clearBackground();
    format.clearProperty(QTextFormat::FontFamilies);
    format.clearProperty(QTextFormat::FontFamily);
    format.clearProperty(QTextFormat::FontFixedPitch);
    format.clearProperty(kCodeProperty);
    return format;
}

// Formato tolto pezzo per pezzo (mergeCharFormat non sa togliere proprieta').
void removeFormat(QTextCursor &cursor, QTextCharFormat (*strip)(QTextCharFormat)) {
    if (!cursor.hasSelection()) {
        cursor.setCharFormat(strip(cursor.charFormat()));
        return;
    }
    const int start = cursor.selectionStart(), end = cursor.selectionEnd();
    QTextCursor edit(cursor);
    edit.beginEditBlock();
    for (QTextBlock block = cursor.document()->findBlock(start); block.isValid() && block.position() <= end; block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const int from = std::max(start, fragment.position());
            const int to = std::min(end, fragment.position() + fragment.length());
            if (from >= to) continue;
            edit.setPosition(from);
            edit.setPosition(to, QTextCursor::KeepAnchor);
            edit.setCharFormat(strip(fragment.charFormat()));
        }
    edit.endEditBlock();
}
}  // namespace

void setHighlight(QTextCursor &cursor, bool on) {
    if (on) cursor.mergeCharFormat(highlightFormat());
    else removeFormat(cursor, withoutHighlight);
}

void setCode(QTextCursor &cursor, bool on) {
    if (on) cursor.mergeCharFormat(codeFormat());
    else removeFormat(cursor, withoutCode);
}

bool enabled() { return QSettings().value(QStringLiteral("editor/markdownShortcuts"), true).toBool(); }
void setEnabled(bool on) { QSettings().setValue(QStringLiteral("editor/markdownShortcuts"), on); }

}  // namespace MarkdownShortcuts
