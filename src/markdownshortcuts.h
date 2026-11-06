#pragma once

// FEAT-068 (docs/features/FEAT-068-scorciatoie-markdown.md), tappa 1: i simboli Markdown scritti
// in un titolo o in una nota diventano subito il formato e spariscono, come in Notion e Typora.
// Un solo motore per il campo del titolo sulla mappa, quelli dell'outliner e NoteEditor. Ogni
// trasformazione e' un blocco di modifica del documento: un Ctrl+Z subito dopo rimette i simboli.
//
// Regola standard di Markdown contro i falsi positivi: il simbolo deve toccare il testo
// ("*parola*" si', "2 * 3 * 4" no) e non stare attaccato a una lettera fuori ("a*b*" no); "_" non
// formatta mai; niente trasformazioni dentro una formula "$...$" o dentro il codice.

#include <QTextCharFormat>

class QTextCursor;
class QTextDocument;

namespace MarkdownShortcuts {

enum class Context { Title, Note };

struct Options {
    Context context = Context::Title;
    int boldWeight = 700;   // peso del grassetto nel campo (900 dove il testo e' gia' in grassetto)
    int headingPx = 18;     // nota: "## "
    int bodyPx = 14;
};

struct Result {
    bool changed = false;
    int task = 0;  // titolo: 1 = "- [ ] " (casella vuota), 2 = "- [x] " (spuntata)
};

// Da chiamare subito dopo che l'utente ha scritto il carattere typed con il cursore cursor
// (posizione dopo il carattere). Applica al massimo una trasformazione; alla fine il cursore resta
// dove scrivere e il suo formato e' quello di prima (il testo che segue non e' in grassetto).
Result afterTyping(QTextCursor &cursor, QChar typed, const Options &options);

// Caratteri che possono chiudere una scorciatoia (per non controllare ogni tasto).
bool isTrigger(QChar typed);

// Applica le scorciatoie in linea gia' scritte in tutto il documento (per esempio "==" nelle note
// importate da Markdown). Restituisce true se ha cambiato qualcosa.
bool applyToDocument(QTextDocument *document, const Options &options);

// Formati nuovi della tappa 1.
QTextCharFormat highlightFormat();  // "==testo==": fondo giallo, testo scuro leggibile
QTextCharFormat codeFormat();       // "`testo`": spaziatura fissa, fondo grigio leggero
bool isHighlight(const QTextCharFormat &format);
bool isCode(const QTextCharFormat &format);
// Bottoni Evidenzia e Codice (2026-10-19: il Markdown mette il formato, i bottoni lo tolgono).
// Con una selezione accendono o spengono il formato su tutta la selezione (un passo di Ctrl+Z);
// senza selezione cambiano il formato del testo che si scrive dopo.
void setHighlight(QTextCursor &cursor, bool on);
void setCode(QTextCursor &cursor, bool on);

// Interruttore in Impostazioni > Editor (acceso di base), chiave "editor/markdownShortcuts".
bool enabled();
void setEnabled(bool on);

}  // namespace MarkdownShortcuts
