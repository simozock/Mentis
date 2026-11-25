#include "outlineformats.h"

#include "formulatext.h"
#include "markdownshortcuts.h"
#include "mindmapmodel.h"
#include "richtitle.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <algorithm>

namespace OutlineFormats {

quint8 inlineFlags(const QTextCharFormat &format, int boldWeight) {
    return (format.fontItalic() ? kInlineItalic : 0) | (format.fontStrikeOut() ? kInlineStrike : 0) |
           (format.fontUnderline() ? kInlineUnderline : 0) |
           (format.fontWeight() >= boldWeight ? kInlineBold : 0) |
           (MarkdownShortcuts::isHighlight(format) ? kInlineHighlight : 0);  // FEAT-068 "==testo=="
}

// Formato da disegnare: il peso si imposta solo per il grassetto, il resto eredita quello della
// riga (impostarlo a 400 toglierebbe il grassetto delle voci di primo livello).
QTextCharFormat inlineFormat(quint8 flags, int boldWeight) {
    QTextCharFormat format = flags & kInlineHighlight ? MarkdownShortcuts::highlightFormat() : QTextCharFormat();
    format.setFontItalic(flags & kInlineItalic);
    format.setFontStrikeOut(flags & kInlineStrike);
    format.setFontUnderline(flags & kInlineUnderline);
    if (flags & kInlineBold) format.setFontWeight(boldWeight);
    return format;
}

// Formato da salvare in titleHtml: il peso è sempre esplicito, così il grassetto si può anche togliere.
QTextCharFormat inlineStoredFormat(quint8 flags, BoldWeights weights) {
    QTextCharFormat format = inlineFormat(flags & ~kInlineBold);
    format.setFontWeight(flags & kInlineBold ? weights.bold : weights.regular);
    return format;
}

// Flag di ogni carattere del documento, indicizzati come le posizioni di QTextCursor e come
// toPlainText() (il separatore tra paragrafi occupa una posizione e vale 0).
QVector<quint8> inlineFlagsPerChar(const QTextDocument &document, int boldWeight) {
    QVector<quint8> flags(document.characterCount(), 0);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid()) continue;
            const quint8 value = inlineFlags(fragment.charFormat(), boldWeight);
            for (int i = 0; i < fragment.length(); ++i) flags[fragment.position() + i] = value;
        }
    return flags;
}

// Tratti consecutivi con gli stessi flag, a partire da offset e per length caratteri.
QList<QTextLayout::FormatRange> inlineRanges(const QVector<quint8> &flags, int offset, int length,
                                             int boldWeight) {
    QList<QTextLayout::FormatRange> ranges;
    for (int i = 0; i < length;) {
        const quint8 value = flags.value(offset + i);
        int end = i + 1;
        while (end < length && flags.value(offset + end) == value) ++end;
        if (value) ranges.append({i, end - i, inlineFormat(value, boldWeight)});
        i = end;
    }
    return ranges;
}

// Inizio del titolo nel testo del documento: NodeItem::finishEditing salva come titolo il testo
// senza spazi iniziali e finali, mentre l'HTML li conserva. -1 se l'HTML non corrisponde al titolo.
int titleOffsetIn(const QString &plain, const QString &title) {
    int offset = 0;
    while (offset < plain.size() && plain.at(offset).isSpace()) ++offset;
    return plain.mid(offset, title.size()) == title ? offset : -1;
}

// Corsivo, barrato e sottolineato del titolo, nelle posizioni del testo semplice (title).
// Vuoto se non ce ne sono, se il titolo ha formule o se l'HTML non corrisponde al titolo.
// storedBold: peso del grassetto nel titolo salvato (Black per il nodo principale, già in grassetto
// sulla mappa); displayBold: peso con cui disegnarlo nella riga dell'outliner (2026-10-14).
QList<QTextLayout::FormatRange> titleInlineRanges(const QString &title, const QString &html,
                                                  int storedBold, int displayBold) {
    if (html.isEmpty() || FormulaText::containsMath(title)) return {};
    QTextDocument document;
    document.setHtml(RichTitle::withoutDocumentFont(html));  // FEAT-022: vale il font del nodo
    const int offset = titleOffsetIn(document.toPlainText(), title);
    if (offset < 0) return {};
    return inlineRanges(inlineFlagsPerChar(document, storedBold), offset, int(title.size()), displayBold);
}

// Nuovo titleHtml dopo una modifica fatta nell'outliner. Si parte dal titolo formattato salvato
// (oldHtml), si sostituisce solo il tratto di testo cambiato (prefisso e suffisso comuni restano
// con la loro formattazione completa) e si applicano corsivo, barrato e sottolineato del campo.
// I caratteri nuovi prendono le altre proprietà (grassetto, colore...) dal carattere precedente,
// come quando si scrive sulla mappa. Titolo senza HTML e senza formattazione: resta testo semplice
// (stringa vuota). Nessun cambiamento: restituisce oldHtml così com'è (niente "Modificato").
// editedBold: peso del grassetto nel campo dell'outliner; stored: pesi da scrivere nel titolo
// salvato (kHeavyBold per il nodo principale, kNormalBold per gli altri). 2026-10-14.
QString mergeOutlineFormats(const QString &oldTitle, const QString &oldHtml, const QTextDocument &edited,
                            const QFont &font, int editedBold, BoldWeights stored) {
    const QString newText = edited.toPlainText();
    // Le formule restano testo semplice, come sulla mappa (NodeItem::finishEditing).
    if (FormulaText::containsMath(newText)) return {};
    const QVector<quint8> wanted = inlineFlagsPerChar(edited, editedBold);
    const bool anyWanted = std::any_of(wanted.cbegin(), wanted.cend(), [](quint8 value) { return value != 0; });
    if (oldHtml.isEmpty() && !anyWanted) return {};

    QTextDocument document;
    document.setUndoRedoEnabled(false);
    document.setDefaultFont(font);
    if (oldHtml.isEmpty()) document.setPlainText(oldTitle);
    else document.setHtml(RichTitle::withoutDocumentFont(oldHtml));  // FEAT-022: vale il font del nodo
    // Tratto del documento che corrisponde al titolo (gli spazi attorno restano dove sono).
    const QString oldPlain = document.toPlainText();
    int offset = titleOffsetIn(oldPlain, oldTitle);
    const QString oldText = offset < 0 ? oldPlain : oldTitle;
    if (offset < 0) offset = 0;

    const int common = int(qMin(oldText.size(), newText.size()));
    int prefix = 0;
    while (prefix < common && oldText.at(prefix) == newText.at(prefix)) ++prefix;
    int suffix = 0;
    while (suffix < common - prefix && oldText.at(oldText.size() - 1 - suffix) == newText.at(newText.size() - 1 - suffix)) ++suffix;
    bool changed = prefix + suffix != oldText.size() || prefix + suffix != newText.size();
    QTextCursor cursor(&document);
    if (changed) {
        cursor.setPosition(offset + prefix);
        cursor.setPosition(offset + int(oldText.size()) - suffix, QTextCursor::KeepAnchor);
        // "\n" diventa un nuovo paragrafo (https://doc.qt.io/qt-6/qtextcursor.html#insertText).
        cursor.insertText(newText.mid(prefix, newText.size() - prefix - suffix));
        if (document.toPlainText().mid(offset, newText.size()) != newText) {
            // Caso imprevisto (caratteri che toPlainText converte): si riparte dal testo nuovo.
            document.setPlainText(newText);
            offset = 0;
        }
    }
    const QVector<quint8> current = inlineFlagsPerChar(document, stored.bold);
    for (int i = 0; i < newText.size();) {
        const quint8 value = wanted.value(i);
        if (newText.at(i) == QLatin1Char('\n') || current.value(offset + i) == value) { ++i; continue; }
        int end = i + 1;
        while (end < newText.size() && newText.at(end) != QLatin1Char('\n') && wanted.value(end) == value &&
               current.value(offset + end) != value) ++end;
        cursor.setPosition(offset + i);
        cursor.setPosition(offset + end, QTextCursor::KeepAnchor);
        cursor.mergeCharFormat(inlineStoredFormat(value, stored));
        // FEAT-068: l'evidenziato tolto nel campo si toglie anche dal titolo salvato.
        if (!(value & kInlineHighlight) && (current.value(offset + i) & kInlineHighlight)) MarkdownShortcuts::setHighlight(cursor, false);
        changed = true;
        i = end;
    }
    if (!changed) return oldHtml;
    return document.toHtml();
}
BoldWeights storedBoldWeights(const MindNode *node) {
    return {node->fontWeight >= QFont::Bold ? int(QFont::Black) : int(QFont::Bold), node->fontWeight};
}

}  // namespace OutlineFormats
