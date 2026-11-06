#pragma once

// FEAT-020 parte C (docs/features/FEAT-020-stile-forma-nodo.md): riempimenti a motivo e tratti a
// mano libera del nodo. Nessuna dipendenza da widget o dal modello: si parte dal contorno della
// forma (NodeShapes::outline) e da un seme. Lo stesso seme da' sempre lo stesso tratto, su ogni
// piattaforma: il generatore pseudo-casuale e' scritto qui (le distribuzioni di <random> non
// danno gli stessi numeri con librerie standard diverse).

#include "nodestyle.h"

#include <QColor>
#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QString>

namespace NodeSketch {

// Di quanto un tratto a mano libera si scosta al massimo dal contorno (stima dalle foto 05 e 06).
inline constexpr qreal kBorderWobble = 1.5;
// Di quanto "Pieno a mano libera" esce al massimo dal contorno (foto 13).
inline constexpr qreal kFillOvershoot = 2;
// Di quanto i motivi escono al massimo dal contorno, spessore delle pennellate compreso (foto
// 24-27: gli estremi sporgono come denti). L'anello di selezione e l'area del nodo se ne tengono
// fuori.
inline constexpr qreal kPatternReach = 6;

// Seme stabile ricavato dall'id del nodo (FNV-1a a 64 bit sui caratteri UTF-16).
quint64 seedFor(const QString &id);

bool isFreehand(NodeBorderStyle style);

// Contorno a mano libera, aperto: fa il giro completo e ripassa per un breve tratto l'inizio, come
// un tratto di penna che si chiude sovrapponendosi.
QPainterPath roughOutline(const QPainterPath &outline, quint64 seed);
// Linea a mano libera (le linee sotto delle forme "Linea sotto" e "Doppia linea sotto").
QPainterPath roughLine(const QLineF &line, quint64 seed);
// FEAT-030: an open path drawn freehand (the marks of the Puntuale shapes), corners kept.
QPainterPath roughPath(const QPainterPath &path, quint64 seed);
// Area chiusa con il bordo irregolare per "Pieno a mano libera".
QPainterPath roughArea(const QPainterPath &outline, quint64 seed);
// Motivo dentro il contorno: segmenti da disegnare con QPainter::drawLines, una penna di penWidth
// con estremi piatti e un colore opaco (opaqueOver), perche' le pennellate si sovrappongono. Per i
// segmenti dritti il motore raster di Qt ha una via rapida, mentre riempire un unico tracciato
// con tutte le linee costava circa il doppio.
struct Pattern {
    QList<QLineF> lines;
    qreal penWidth = 0;
};
Pattern pattern(NodeFillStyle style, const QPainterPath &outline, quint64 seed);
// Colore opaco che sullo sfondo appare come color (anche semitrasparente, per esempio il secondo
// livello al 20%): le parti sovrapposte delle pennellate non diventano piu' scure.
QColor opaqueOver(const QColor &color, const QColor &background);

}  // namespace NodeSketch
