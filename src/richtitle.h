#pragma once

// Titolo formattato del nodo (MindNode::titleHtml, HTML di QTextDocument) visto dalle proprieta'
// del testo del nodo (FEAT-022, docs/features/FEAT-022-testo-nodo.md). Nessuna dipendenza da widget.

#include "nodestyle.h"

#include <QString>

class QKeyEvent;
class QTextCursor;

namespace RichTitle {

// QTextDocument::toHtml scrive nel <body> il font del documento, cioe' il font del nodo al momento
// della modifica. Il font del nodo ora si sceglie dal pannello e deve valere anche per i titoli
// formattati: chi legge titleHtml toglie prima quel font (famiglia, dimensione, peso e stile del
// <body>), cosi' contano solo le parti formattate a mano.
QString withoutDocumentFont(const QString &html);

// Toglie da ogni parte del titolo la proprieta' indicata (font, dimensione, peso, corsivo,
// sottolineato, barrato, colore del testo), lasciando le altre: scegliere una proprieta' dal
// pannello vale per tutto il titolo. Restituisce html invariato se nessuna parte la aveva.
QString withoutProperty(const QString &html, StyleProperty property);

// Vero per le proprieta' che il titolo formattato puo' avere su singole parole.
bool isInlineProperty(StyleProperty property);

// FEAT-095: the character formats with a key (Ctrl+B, Ctrl+I, Ctrl+U, Ctrl+Shift+X).
enum class CharStyle { Bold, Italic, Underline, StrikeOut };

// The topic's own text style, what a character without that property shows.
struct BaseStyle {
    int weight = 400;
    bool italic = false, underline = false, strikeOut = false;
};

// True if every character of the title shows the style. A character is bold from boldWeight up.
bool hasEverywhere(const QString &title, const QString &html, CharStyle style, const BaseStyle &base, int boldWeight);
// The title's html with the style on (or off) on every character it has now: bold is boldWeight,
// not bold the topic's weight. Text typed later does not get it.
QString withEverywhere(const QString &title, const QString &html, CharStyle style, bool on, const BaseStyle &base, int boldWeight);

// FEAT-095: in a title field the first text typed after opening is the topic's own style, not the
// formatted characters' around it, and the text after it continues that way. A click or a navigation
// key before typing ends the rule.
struct PlainFirstTyping {
    bool active = false;
    // Types the key's text at the cursor without a character format and returns true, or returns false
    // and leaves the key to the field. The rest of the typing joins that text's undo step.
    bool type(const QKeyEvent *event, QTextCursor &cursor);
};

}  // namespace RichTitle
