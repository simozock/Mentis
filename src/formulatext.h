#pragma once

#include <QColor>
#include <QImage>
#include <QStringList>
#include <QFont>
#include <QPicture>
#include <QSizeF>
#include <QString>
#include <QVector>

// Testo di un argomento con formule LaTeX inline.
//
// - Le parti tra $...$ (oppure \(...\) o $$...$$) sono formule; il resto è testo
//   normale. Basta scrivere la formula nel titolo: la conversione è automatica.
// - Un dollaro letterale si scrive \$. Come in Pandoc, il $ di apertura deve essere
//   seguito da un carattere non spazio e quello di chiusura preceduto da un non
//   spazio e non seguito da una cifra, così "costa 5$ o 10$" resta testo.
// - Il rendering usa JKQTMathText (third_party/jkqtmathtext): frazioni, radici,
//   pedici/apici, integrali e sommatorie con estremi, matrici, \left...\right, ecc.
// - Il risultato viene registrato una sola volta in un QPicture (comandi di disegno
//   vettoriali) e poi solo riprodotto a ogni paint: resta nitido a ogni zoom e non
//   richiede di rifare il parsing a ogni ridisegno.
class QTextDocument;

class FormulaText {
public:
    // from/to: the segment in the source text, delimiters included (FEAT-044 skips formulas).
    struct Segment { QString text; bool math = false; int from = 0; int to = 0; };

    // Vero se il testo contiene almeno una formula da rendere.
    static bool containsMath(const QString &text);
    // Divide il testo in segmenti di testo normale e formule.
    static QVector<Segment> split(const QString &text);

    // Prepara il rendering. Ricalcola solo se testo, font, colore o larghezza cambiano.
    void update(const QString &text, const QFont &font, const QColor &color, qreal maxWidth);
    bool isEmpty() const { return m_picture.isNull(); }
    QSizeF size() const { return m_size; }
    bool hadErrors() const { return m_hadErrors; }
    QStringList errors() const { return m_errors; }  // FEAT-064: messaggi del parser, per il modale
    // FEAT-064: errori di scrittura che il parser accetta in silenzio (graffe, \left senza \right,
    // \begin senza \end, ^ o _ senza argomento); vuoto se il codice e' corretto. Messaggi gia' tradotti.
    static QStringList syntaxErrors(const QString &code);
    // FEAT-064: formula a blocco (contenuto del nodo): il codice LaTeX senza i $, senza a capo.
    void updateBlock(const QString &code, const QFont &font, const QColor &color) { update(QStringLiteral("$") + code.trimmed() + QStringLiteral("$"), font, color, 0); }
    // Immagine trasparente della formula (Copia come immagine, formule nelle note), scale x la misura.
    QImage toImage(qreal scale) const;
    // FEAT-064 (rivista il 2026-10-19 con l'utente): la formula del nodo e' un contenuto come
    // l'immagine, un riquadro con lo sfondo (bianco di base) e la formula dentro, 12 px di margine.
    static constexpr qreal kBoxPaddingPx = 12;
    static QSizeF boxSize(const QString &code, const QFont &font);
    // Riquadro alla misura size (la formula si scala in proporzione), pixel per pixel ratio.
    static QImage boxImage(const QString &code, const QFont &font, const QColor &background, const QColor &text,
                           const QSizeF &size, qreal ratio);
    // Colore della formula: leggibile sullo sfondo; con uno sfondo trasparente quello del testo.
    static QColor textColorOn(const QColor &background, const QColor &nodeText);
    // FEAT-064: le formule $...$ di un documento (note) diventano immagini nel testo, nel colore
    // e nel font dati; il testo intorno resta com'e'. Vero se ne ha trovate.
    static bool renderInDocument(QTextDocument *document, const QFont &font, const QColor &color);
    // FEAT-096: connects FormulaRendering's hooks to this renderer, once (the core uses them).
    static void installRendering();
    // Disegna centrato (o allineato a sinistra) dentro rect.
    void draw(QPainter *painter, const QRectF &rect, Qt::Alignment alignment = Qt::AlignCenter) const;

private:
    QString m_text;
    QFont m_font;
    QColor m_color;
    qreal m_maxWidth = 0;
    QPicture m_picture;
    QSizeF m_size;
    bool m_hadErrors = false;
    QStringList m_errors;
};
