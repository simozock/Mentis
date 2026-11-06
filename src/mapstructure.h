#pragma once

// FEAT-026 fase B (docs/features/FEAT-026-strutture-mappa.md, passo 4a): tipi di mappa (strutture)
// e direzioni, per foglio (sul nodo centrale) e, facoltativi, per ramo (MindNode::structure e
// structureDirection; vuoti = ereditati). In questa fase si dispone solo la Mappa mentale: gli
// altri tipi, salvati e letti, si dispongono come lei fino alla fase C.
//
// Bilanciamento (FEAT-012): balancedSides non conosce l'asse. Riceve il peso di ogni ramo (il suo
// ingombro lungo l'asse del tipo) e restituisce il lato di ognuno, cosi' i tipi verticali della fase
// C lo riusano senza riscriverlo.

#include <QList>
#include <QString>

struct MindNode;

namespace MapStructure {

enum class Type { MindMap, LogicChart, OrgChart, TreeChart, Brace, Timeline, Fishbone, TreeTable };
// OffAxis: timeline with the main topics above and below the axis in turn.
enum class Direction { BothSides, Right, Left, Down, Up, OffAxis };

QString typeKey(Type type);
Type typeFromKey(const QString &key);            // sconosciuto o vuoto = MindMap
QString directionKey(Direction direction);
Direction directionFromKey(const QString &key, Type type);  // sconosciuta = la prima del tipo
// Direzioni offerte da ogni tipo (decisioni 2026-10-15 e 2026-10-17), la predefinita per prima.
QList<Direction> directions(Type type);
bool isTwoSided(Direction direction);            // il bilanciamento lavora solo su due lati
// FEAT-026 fase C: tipi disposti a blocchi (figli sotto il genitore): organigramma e grafico ad albero.
// Mappa mentale e grafico logico dispongono i figli accanto al genitore.
bool isBlockType(Type type);
// Types laid out by MindMapScene::layoutBlock: the block types, the timeline and the fishbone (FEAT-026
// phase E), the tree table (FEAT-028).
bool laysOutAsBlock(Type type);

// Tipo e direzione che valgono per il nodo: i suoi, o quelli del primo antenato che li ha (il
// centrale ha quelli del foglio).
Type typeOf(const MindNode *node);
// FEAT-028: the node is a cell of a tree table: the nearest ancestor with a type of its own is a tree
// table (a Mind map type does not count inside a table). Under a cell with another type the topics are
// laid out by that type, inside the cell (stage 2).
bool inTreeTable(const MindNode *node);
// FEAT-028: the node is somewhere inside a tree table (a cell, or a topic in a cell's branch).
bool insideTreeTable(const MindNode *node);
Direction directionOf(const MindNode *node);

// Lati: +1 primo lato (destra, o basso nei tipi verticali), -1 secondo (sinistra, o alto).
// fixed[i] = +1 o -1 per un ramo con il lato scelto, 0 = automatico. I rami automatici restano
// nell'ordine: i primi sul primo lato, gli altri sul secondo (in senso orario).
// Regola ricavata dalle foto dell'utente (2026-10-21, zoom 100%): i rami si dividono per
// numero, meta' sul primo lato (dispari: uno in piu' sul primo) ma due rami stanno entrambi sul
// primo; se nessun lato resta vuoto e i lati differiscono di almeno kRebalanceRows righe, il ramo
// al punto di divisione passa al lato piu' leggero, uno alla volta finche' la differenza cala.
// Peso = righe del ramo (foglie visibili). Deterministico.
constexpr double kRebalanceRows = 12.0;
QList<int> balancedSides(const QList<double> &weights, const QList<int> &fixed);

}  // namespace MapStructure
