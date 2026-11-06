#pragma once

// Conversione di testo Markdown in rami della mappa (FEAT-055,
// docs/features/FEAT-055-importazione.md): usata dall'incolla nell'outliner e da
// "Importa Markdown…", con le stesse regole. Nessuna dipendenza da widget.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace MarkdownImport {

// Limite di voci per un solo incolla (decisione dell'utente 2026-10-18); il file non ne ha.
inline constexpr int kMaxPastedItems = 5000;

// Tratto del titolo con grassetto, corsivo o barrato, nelle posizioni di Item::title.
struct Span {
    int start = 0;
    int length = 0;
    bool bold = false;
    bool italic = false;
    bool strikeOut = false;
};

struct Item {
    QString title;            // testo semplice, senza simboli Markdown; "\n" = a capo
    std::vector<Span> spans;  // vuoto = nessun formato
    QString notes;
    QString notesMarkdown;  // FEAT-059: le stesse righe con i simboli Markdown (formati della nota)
    int task = 0;  // FEAT-048: 0 nessuna casella, 1 "- [ ]", 2 "- [x]"
    QStringList labels;  // FEAT-060: "#etichetta" in fondo alla voce
    QString formula;     // FEAT-064: riga "$$codice$$" sotto la voce
    std::vector<Item> children;
};

struct Result {
    std::vector<Item> items;  // voci di primo livello del testo
    int itemCount = 0;        // tutte le voci, a ogni livello
    bool tooLarge = false;    // oltre maxItems: items vuoto
};

// maxItems <= 0: nessun limite.
Result parse(const QString &markdown, int maxItems = 0);

// Ramo nel formato JSON di MindMapModel::insertSubtree (titolo, titleHtml, note, figli).
// root: pesi del nodo centrale, che sulla mappa e' gia' in grassetto (900 per il grassetto,
// 700 per il resto; kHeavyBold in mainwindow.cpp); altrimenti 700 e 400.
QJsonObject toSubtreeJson(const Item &item, bool root = false);

}  // namespace MarkdownImport
