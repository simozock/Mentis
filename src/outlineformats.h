#pragma once

// FEAT-121: the outliner's title formats, shared by the desktop's outliner (FEAT-003) and the
// phone's (moved unchanged from mainwindow.cpp). Qt Gui only.

#include <QFont>
#include <QList>
#include <QString>
#include <QTextLayout>
#include <QVector>

class QTextCharFormat;
class QTextDocument;
struct MindNode;

namespace OutlineFormats {

// Formattazione condivisa tra mappa e outliner (richieste 2026-10-13 e 2026-10-14). L'outliner
// mostra e modifica grassetto, corsivo, barrato e sottolineato del titolo formattato
// (MindNode::titleHtml); font, dimensione e colore restano nell'HTML e si vedono solo sulla mappa.
// Il peso dipende dal contesto (BoldWeights): dove il testo è già in grassetto (voci di primo
// livello e titolo principale nell'outliner, nodo principale sulla mappa) il grassetto è Black
// (900), perché Segoe UI non ha l'800 e lo rende come Bold 700 (misurato con QFontInfo).
enum InlineFlag : quint8 { kInlineItalic = 1, kInlineStrike = 2, kInlineUnderline = 4, kInlineBold = 8, kInlineHighlight = 16 };

// Peso del testo in grassetto e del testo normale in un contesto (riga dell'outliner o titolo
// salvato per la mappa). Un carattere è "in grassetto" se il suo peso arriva almeno a bold.
struct BoldWeights {
    int bold = QFont::Bold;
    int regular = QFont::Normal;
};
// Testo normale a peso 400 (voci dell'outliner sotto il primo livello, nodi della mappa).
constexpr BoldWeights kNormalBold{QFont::Bold, QFont::Normal};
// Testo già in grassetto (voci di primo livello e titolo principale nell'outliner, nodo principale).
constexpr BoldWeights kHeavyBold{QFont::Black, QFont::Bold};

quint8 inlineFlags(const QTextCharFormat &format, int boldWeight = QFont::Bold);

// Formato da disegnare: il peso si imposta solo per il grassetto, il resto eredita quello della
// riga (impostarlo a 400 toglierebbe il grassetto delle voci di primo livello).
QTextCharFormat inlineFormat(quint8 flags, int boldWeight = QFont::Bold);

// Formato da salvare in titleHtml: il peso è sempre esplicito, così il grassetto si può anche togliere.
QTextCharFormat inlineStoredFormat(quint8 flags, BoldWeights weights);

// Flag di ogni carattere del documento, indicizzati come le posizioni di QTextCursor e come
// toPlainText() (il separatore tra paragrafi occupa una posizione e vale 0).
QVector<quint8> inlineFlagsPerChar(const QTextDocument &document, int boldWeight = QFont::Bold);

// Tratti consecutivi con gli stessi flag, a partire da offset e per length caratteri.
QList<QTextLayout::FormatRange> inlineRanges(const QVector<quint8> &flags, int offset, int length,
                                             int boldWeight = QFont::Bold);

// Inizio del titolo nel testo del documento: NodeItem::finishEditing salva come titolo il testo
// senza spazi iniziali e finali, mentre l'HTML li conserva. -1 se l'HTML non corrisponde al titolo.
int titleOffsetIn(const QString &plain, const QString &title);

// Corsivo, barrato e sottolineato del titolo, nelle posizioni del testo semplice (title).
// Vuoto se non ce ne sono, se il titolo ha formule o se l'HTML non corrisponde al titolo.
// storedBold: peso del grassetto nel titolo salvato (Black per il nodo principale, già in grassetto
// sulla mappa); displayBold: peso con cui disegnarlo nella riga dell'outliner (2026-10-14).
QList<QTextLayout::FormatRange> titleInlineRanges(const QString &title, const QString &html,
                                                  int storedBold = QFont::Bold, int displayBold = QFont::Bold);

// Nuovo titleHtml dopo una modifica fatta nell'outliner. Si parte dal titolo formattato salvato
// (oldHtml), si sostituisce solo il tratto di testo cambiato (prefisso e suffisso comuni restano
// con la loro formattazione completa) e si applicano corsivo, barrato e sottolineato del campo.
// I caratteri nuovi prendono le altre proprietà (grassetto, colore...) dal carattere precedente,
// come quando si scrive sulla mappa. Titolo senza HTML e senza formattazione: resta testo semplice
// (stringa vuota). Nessun cambiamento: restituisce oldHtml così com'è (niente "Modificato").
// editedBold: peso del grassetto nel campo dell'outliner; stored: pesi da scrivere nel titolo
// salvato (kHeavyBold per il nodo principale, kNormalBold per gli altri). 2026-10-14.
QString mergeOutlineFormats(const QString &oldTitle, const QString &oldHtml, const QTextDocument &edited,
                            const QFont &font, int editedBold = QFont::Bold, BoldWeights stored = kNormalBold);

// FEAT-022: pesi del titolo salvato di un nodo: normale = peso del nodo, grassetto = 700 o 900
// (NodeItem::boldWeightFor). Con i predefiniti sono kNormalBold e, per il nodo principale, kHeavyBold.
BoldWeights storedBoldWeights(const MindNode *node);

}  // namespace OutlineFormats
