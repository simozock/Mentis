#pragma once

// FEAT-014 (docs/features/FEAT-014-collegamenti-radiali.md): geometria dei rami, senza scena ne'
// widget, per poterla provare con rettangoli e punti.

#include <QPainterPath>
#include <QPointF>
#include <QRectF>

enum class BranchEnd;  // nodestyle.h

namespace BranchPath {

// Punto in cui la semiretta dal centro di box verso target esce dal bordo di box (il riquadro
// logico del nodo centrale, testo e padding compresi). Target dentro box o nel centro: il centro.
QPointF exitPoint(const QRectF &box, const QPointF &target);

// Branch from the central topic (curved lines, FEAT-072 A7): from exitPoint towards the
// child's entry point (on its side facing the central topic), a quadratic curve that arrives
// horizontal; straight when the child is level with the start.
QPainterPath fromRoot(const QRectF &rootBox, const QPointF &entry);
// The A7 curve from any start: quadratic, control point at the child's height 20% of the way across.
QPainterPath curve(const QPointF &start, const QPointF &entry);

// Ramo fra due nodi non centrali: esce ed entra in orizzontale (curva con le maniglie a meta').
QPainterPath between(const QPointF &from, const QPointF &to);

// FEAT-026 fase C, angoli arrotondati di raggio kCornerPx (foto di riferimento 2026-10-21).
constexpr qreal kCornerPx = 6;
constexpr qreal kElbowRunPx = 16;  // dal genitore alla linea orizzontale dell'organigramma
// Organigramma: dal centro del lato del genitore verso i figli, in orizzontale a kElbowRunPx (o a
// meta' strada, se piu' vicino) fino sopra il figlio, poi al centro del suo lato.
QPainterPath elbow(const QPointF &from, const QPointF &to);
// Grafico ad albero: tronco verticale dal genitore fino all'altezza del figlio, poi in orizzontale
// fino al suo lato.
QPainterPath trunk(const QPointF &from, const QPointF &to);
// FEAT-072 (org chart, measured live 2026-10-23): from the middle of the central topic's bottom side
// kOrgStubPx down to one point, then a straight slant to kOrgRunPx above the child, a rounded bend
// (kOrgCornerPx) and down into the middle of its top side (mirrored when the chart grows upwards).
constexpr qreal kOrgStubPx = 15;
constexpr qreal kOrgRunPx = 47;
constexpr qreal kOrgCornerPx = 16;
QPainterPath orgFan(const QPointF &from, const QPointF &to);

// FEAT-026 fase D, linee delle varianti (foto di riferimento 2026-10-21). Tratto obliquo fino all'altezza
// del figlio, poi orizzontale (foto di riferimento 09).
QPainterPath angled(const QPointF &from, const QPointF &to);
// Ad angolo retto: in verticale fino all'altezza del figlio, poi in orizzontale (foto di riferimento 10).
QPainterPath square(const QPointF &from, const QPointF &to);
// The same with kStubPx straight out first, so the vertical bar and the open branch's circle sit on
// the same point (user's photos 2026-10-23); the central topic of a mind map keeps square().
QPainterPath squareFromStub(const QPointF &from, const QPointF &to);
// Dritta.
QPainterPath straight(const QPointF &from, const QPointF &to);

// FEAT-072 (branch shapes, measured live 2026-10-22). from = middle of the parent's side
// facing the child, to = the child's entry point. kStubPx out from the parent sits the bracket's
// vertical bar or the fans' common point.
constexpr qreal kStubPx = 12;
constexpr qreal kFanRunPx = 15;  // angled fans: horizontal run into the child
// Bracket: stub, vertical bar to the child's height, horizontal into the child; the corner is
// rounded (kCornerPx) for every child: the bar goes on past the middle ones.
QPainterPath bracket(const QPointF &from, const QPointF &to, bool roundCorner);
// Fans from the common point: straight, curve (quadratic, control at the child's height 20% of the
// way, like the central topic's branches), S-curve, angled and angled with a rounded bend.
QPainterPath fan(const QPointF &from, const QPointF &to, int shape);

// FEAT-026 phase E (brace map, measured live 2026-10-24). One arm of a brace, from the
// cusp to `endY`, `dir` = +1 to the right, -1 to the left: quarter arcs of `radius` joined by a vertical
// bar `radius` out from the cusp, ending 2 x radius out. Short arms shrink the arcs; a level one is
// straight.
QPainterPath braceArm(const QPointF &cusp, qreal endY, qreal radius, int dir);

// FEAT-023 (foto dell'utente 2026-10-21). Linea a mano libera: il percorso con piccole
// ondulazioni regolari, sempre uguali per lo stesso seme.
QPainterPath freehand(const QPainterPath &path, quint64 seed);
// Ramo affusolato: area chiusa attorno al percorso, larga startWidth all'inizio e endWidth alla fine.
QPainterPath tapered(const QPainterPath &path, qreal startWidth, qreal endWidth);

// FEAT-023 (user's approval 2026-10-24): mark of a line end, about 3x the line width and at least
// kMinEndMarkPx, ending at `tip` and pointing along `outward` (unit vector, away from the line).
// The open arrow is a stroke (endIsStroke), the others are filled areas.
constexpr qreal kMinEndMarkPx = 6;
QPainterPath endMark(BranchEnd kind, const QPointF &tip, const QPointF &outward, qreal lineWidth);
bool endIsStroke(BranchEnd kind);

}  // namespace BranchPath
