#pragma once

// FEAT-020 parte B (docs/features/FEAT-020-stile-forma-nodo.md): geometria delle forme Base del
// nodo. Nessuna dipendenza da widget o dal modello: misure, contorni e punti di attacco dei rami
// a partire dal riquadro del contenuto (oggi il testo con il suo padding; con FEAT-037 anche
// immagine e link). Tutto e' centrato sul centro del riquadro del contenuto.

#include "nodestyle.h"

#include <QLineF>
#include <QList>
#include <QPainterPath>
#include <QRectF>

namespace NodeShapes {

// Rettangolo esterno della forma che contiene il riquadro del contenuto con lo stesso margine.
// borderWidth serve alla doppia linea sotto (lo spessore vale per ogni linea). textWidth: the text
// alone, without padding; the circle is sized on it (0 = unknown).
QRectF outerRect(NodeShape shape, const QRectF &content, qreal borderWidth, qreal textWidth = 0);

// Contorno chiuso della forma dentro outer: riempimento, bordo e area cliccabile. Per "Linea
// sotto" e "Doppia linea sotto" e' il rettangolo (il riempimento non si disegna).
// roundedRadius: corner of the rounded rectangle, < 0 = the usual one (FEAT-052: summary topics 4).
QPainterPath outline(NodeShape shape, const QRectF &outer, qreal roundedRadius = -1);

// Lines that draw the border of an open shape ("Linea sotto", "Doppia linea sotto", square brackets)
// in place of the outline; empty for the other shapes.
QList<QLineF> borderLines(NodeShape shape, const QRectF &outer, qreal borderWidth);
bool hasOpenBorder(NodeShape shape);
bool isUnderline(NodeShape shape);

// FEAT-030: lines drawn inside the outline with the border's pen (the fold of the note, the front card of
// the stack, the side bar). Each path is either closed or a single line; empty for the other shapes.
QList<QPainterPath> innerLines(NodeShape shape, const QRectF &outer, qreal roundedRadius = -1);

// FEAT-030 stage B: the Puntuale group (square brackets included). Marks drawn with the border's
// pen beside the text instead of a closed outline; the fill covers the whole rectangle.
bool isPunctual(NodeShape shape);
// The open strokes of the marks (empty for square brackets, drawn by borderLines, and quotes).
QList<QPainterPath> marks(NodeShape shape, const QRectF &outer);
// Marks filled with the border's colour: the quotation marks.
QPainterPath filledMarks(NodeShape shape, const QRectF &outer);
// How far the marks reach above and below the rectangle (round and curly brackets).
qreal markOverhang(NodeShape shape);

// FEAT-030 stage C: the connectors draw a small mark above the text instead of around it.
bool hasTextBelow(NodeShape shape);
// Choosing these shapes turns a missing border into a thin one, so the marks or inner lines show.
bool showsBorderWhenChosen(NodeShape shape);

// Punto in cui il ramo si attacca al lato destro (right) o sinistro, sul contorno all'altezza del
// centro; per le linee sotto all'altezza della (prima) linea, alla sua estremita'.
QPointF branchAnchor(NodeShape shape, const QRectF &outer, qreal borderWidth, bool right);

// FEAT-030: branches from the central topic start on its box for the rectangles and the open shapes
// (FEAT-072 A7, measured on the reference), on the outline for the others.
bool startsOnBox(NodeShape shape);
// Where the ray from `from` towards `target` last crosses the outline; `from` when it never does.
QPointF rayExit(const QPainterPath &outline, const QPointF &from, const QPointF &target);

}  // namespace NodeShapes
