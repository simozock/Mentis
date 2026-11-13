#pragma once

#include "mapstructure.h"
#include "mindmapmodel.h"
#include "nodegeometry.h"
#include "summary.h"

#include <QHash>
#include <QList>
#include <QMarginsF>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <functional>

// FEAT-096 stage 2: distances and helpers of the map layout, shared with the drawing of the lines
// (MindMapScene::updateEdge). Moved unchanged from mindmapscene.cpp.
namespace MapLayoutRules {
// Distanze del layout, misurate sulle foto di riferimento dell'utente (2026-10-21) e approvate: il riferimento
// stacca i rami principali e compatta i loro discendenti.
// Valori: dal centrale ai rami principali, da un nodo ai suoi figli (dal ramo principale in giu'), fra
// i rami principali (fra i loro ingombri), fra due foglie vicine dentro lo stesso ramo principale, e
// fattore per le distanze di organigramma e albero. Live reference values (FEAT-072, user decisions
// 2026-10-21/22): central to main topics 54 (was 80), a node to its children 28 (was 32), 35 between
// the shapes of main topics (was 32 plus the hidden 4 px margins). "Mappa compatta" (FEAT-043, richiesta
// dell'utente 2026-10-21: "diminuisce leggermente gli spazi"): circa tre quarti, sulla griglia di 4 px.
inline constexpr qreal kNormalSpacing[] = {54, 28, 35, 6, 1};
inline constexpr qreal kCompactSpacing[] = {40, 24, 24, 4, 0.75};
inline constexpr qreal kFanLevelFactor = 54.0 / 28.0;  // FEAT-072: fans put the children 54 px out (reference live)
inline constexpr qreal kLogicRootFactor = 100.0 / 54.0;  // FEAT-072: logic chart, main topics 100 px from the central (reference live)
// FEAT-026 phase E, brace map (reference live 2026-10-24): cusp 12 px out from the parent, arcs of radius
// 18.5 from the central topic and 7 below, children 17 px past the arms' ends.
inline constexpr qreal kBraceCuspGapPx = 12;
inline constexpr qreal kBraceRootRadiusPx = 18.5;
inline constexpr qreal kBraceRadiusPx = 7;
inline constexpr qreal kBraceEndGapPx = 17;
inline qreal braceRadius(const MindNode *parent) { return parent->parent ? kBraceRadiusPx : kBraceRootRadiusPx; }
// Shape to shape, from a parent of a brace map to its children.
inline qreal braceLevelGap(const MindNode *parent) { return kBraceCuspGapPx + 2 * braceRadius(parent) + kBraceEndGapPx; }
inline bool isBraceParent(const MindNode *node) { return MapStructure::typeOf(node) == MapStructure::Type::Brace; }
// FEAT-026 phase E, timeline (reference live 2026-10-24). On the axis (variants 1 and 2): 100 px between
// consecutive topics. Off the axis (variant 3): main topics 25 px above and below the axis in turn, the
// first 25 px from the central topic, each centre 37 px past the previous topic's right edge, the axis
// 40 px past the last one, dots of twice the line width in radius. Either way a main topic's branch
// (topic and children) stays that gap from the previous branch on its side, and its children stand in
// a column 25 px above or below it, 27 px beside the stem from the middle of its side.
inline constexpr qreal kTimelineGapPx = 100;
inline constexpr qreal kTimelineOffAxisGapPx = 37;
inline constexpr qreal kTimelineAxisGapPx = 25;
inline constexpr qreal kTimelineAxisOvershootPx = 40;
inline constexpr qreal kTimelineChildGapPx = 25;
inline constexpr qreal kTimelineStemIndentPx = 27;
// FEAT-052: position of a summary topic with this shape beside the bracket of `covered`.
inline QPointF summaryTopicPosition(const QRectF &covered, Summaries::Side side, const QRectF &shape) {
    const QPointF anchor = Summaries::topicAnchor(covered, side);
    switch (side) {
    case Summaries::Side::Right: return {anchor.x() - shape.left(), anchor.y() - shape.center().y()};
    case Summaries::Side::Left: return {anchor.x() - shape.right(), anchor.y() - shape.center().y()};
    case Summaries::Side::Down: return {anchor.x() - shape.center().x(), anchor.y() - shape.top()};
    case Summaries::Side::Up: return {anchor.x() - shape.center().x(), anchor.y() - shape.bottom()};
    }
    return anchor;
}
// The node that sets a timeline: the central topic of a timeline sheet, or a branch with that type.
inline bool setsTimeline(const MindNode *node) {
    return node && MapStructure::typeOf(node) == MapStructure::Type::Timeline
        && (!node->parent || MapStructure::typeFromKey(node->structure) == MapStructure::Type::Timeline);
}
// FEAT-026 phase E, fishbone (reference live 2026-10-24, variant 1 at 100%). The spine runs from the central
// topic's side; main topics stand above and below it in turn, each on a bone at 60 degrees from its foot
// on the spine to the middle of the topic's side facing the spine. Subtopics sit between the topic and the
// spine, one under the other, each on a horizontal tick 36 px long from the bone; their centres are 65 px
// from the spine (the nearest), 37 apart, 39 from the topic (the farthest); a topic without subtopics is
// 50 px from the spine. A branch (topic, subtopics, bone) stays 52 px from the central topic and 35 from
// the previous branch on its side; each foot is at least 35 px past the previous one. The spine ends where
// the farthest branch ends, in the last main topic's colour.
inline constexpr qreal kFishboneSlope = 0.577;  // tan 30 degrees: horizontal run of a bone per pixel of height
inline constexpr qreal kFishboneHeadGapPx = 52;
inline constexpr qreal kFishboneBranchGapPx = 35;
inline constexpr qreal kFishboneFootGapPx = 35;
inline constexpr qreal kFishboneTickPx = 36;
inline constexpr qreal kFishboneSpineCentrePx = 65;  // spine to the nearest subtopic's centre
inline constexpr qreal kFishboneTopicCentrePx = 39;  // farthest subtopic's centre to the main topic
inline constexpr qreal kFishboneSubPitchPx = 37;     // centre to centre; taller subtopics keep the sibling gap
inline constexpr qreal kFishboneBarePx = 50;
inline bool setsFishbone(const MindNode *node) {
    return node && MapStructure::typeOf(node) == MapStructure::Type::Fishbone
        && (!node->parent || MapStructure::typeFromKey(node->structure) == MapStructure::Type::Fishbone);
}
// FEAT-028: the topic that sets a tree table (the central topic of a table sheet, or a branch with
// that type). Reference live 2026-10-25: a row at least 48 px tall (46.5 plus the border).
inline bool setsTreeTable(const MindNode *node) {
    return node && MapStructure::typeOf(node) == MapStructure::Type::TreeTable
        && (!node->parent || MapStructure::typeFromKey(node->structure) == MapStructure::Type::TreeTable);
}
inline constexpr qreal kTableRowPx = 48;
inline constexpr qreal kTableInsetPx = 16;  // stage 2: a branch inside a cell from its edges (reference live: 15-16)
inline constexpr qreal kMinMainLeafHalfPx = 16;  // un ramo principale senza figli occupa almeno 32 px (passo 64)
// NodeGeometry::normalBoundingRect allarga la forma di 4 px per lato: lo spazio fra le foglie si misura
// sulla forma, come appare.
inline constexpr qreal kLayoutMarginPx = 4;
}  // namespace MapLayoutRules

// FEAT-096 stage 2: the automatic layout of a sheet without the scene. From the model and each topic's
// measures (NodeGeometry) it works out every laid-out topic's place without the moves made by hand
// (autoPositions), its side, the tree tables' cells and the fishbones' spines. The scene applies them
// (manual offsets, items, collapse). No QWidget, no QGraphicsItem. Moved unchanged from MindMapScene
// (see tests/layout_regression_test.cpp).
class MapLayout {
public:
    explicit MapLayout(MindMapModel *model) : m_model(model) {}

    // A topic's measures; null for a topic without them (the layout then uses default sizes).
    std::function<const NodeGeometry *(const MindNode *node)> geometryOf;
    // The topic with that id among the ones laid out (summary topics included), or null.
    std::function<const MindNode *(const QString &id)> nodeById;
    // Union of the visible shapes of these topics and their branches, from the last placement (empty:
    // none visible). A summary taller than its run keeps the run's height from the last layout.
    std::function<QRectF(const QStringList &topics)> branchesCovered;

    // FEAT-052: room for boundaries and tall summaries; first, after the measures.
    void collectBoundaryRuns();
    void computeAutoPositions();
    // The branch of a summary topic standing at its node position, laid out as a logic chart away
    // from the bracket (sign +1 right, -1 left) into autoPositions(). Returns the shapes' bounds with
    // the topic's, in scene px.
    QRectF layoutSummaryBranch(MindNode *topic, int sign);

    static bool childrenHidden(const MindNode *node);  // nodo non centrale chiuso
    // FEAT-072: the lines from parent to its children, Auto resolved for the sheet's variant and type.
    BranchShape effectiveBranchShape(const MindNode *parent) const;

    // B7 (2026-10-13): posizioni calcolate dal layout automatico, senza spostamenti manuali.
    const QHash<MindNode *, QPointF> &autoPositions() const { return m_autoPositions; }
    // FEAT-012: lato di ogni nodo disposto (+1 destra, -1 sinistra, 0 centrale), dal layout.
    const QHash<const MindNode *, int> &sides() const { return m_sides; }
    int sideOf(const MindNode *node) const { return m_sides.value(node, 1); }
    // FEAT-028: the table set by owner (the header) and its cells, by node id. rect in the node's
    // coordinates; levelOne = the main row the cell belongs to (empty for the header).
    struct TablePlace { QRectF rect; QString owner; QString levelOne; bool holdsBranch = false; };
    const QHash<QString, TablePlace> &tableCells() const { return m_tableCells; }
    const QHash<QString, TablePlace> &tableFrames() const { return m_tableFrames; }  // stage 2: the outer cell of a table in a cell
    // FEAT-026 phase E: how far each fishbone's spine runs from its head's centre (towards the tail).
    const QHash<const MindNode *, qreal> &fishboneReach() const { return m_fishboneReach; }
    // Distanze del layout (kNormalSpacing e kCompactSpacing): "Mappa compatta" del foglio le riduce
    // (FEAT-043). Scelte all'inizio di computeAutoPositions.
    struct Spacing { qreal rootLevel, level, branch, sibling, block; };
    const Spacing &spacing() const { return m_spacing; }

private:
    const NodeGeometry *geometry(const MindNode *node) const { return geometryOf ? geometryOf(node) : nullptr; }
    // sign: +1 ramo a destra del genitore, -1 a sinistra (FEAT-012).
    qreal layoutSubtree(MindNode *node, int depth, qreal &cursor, int sign = 1);
    void shiftSubtree(MindNode *node, qreal dy);
    // FEAT-026 fase C: organigramma e grafico ad albero (anche dentro un ramo). Dispone i figli di
    // node in posizioni relative al genitore (rel) e restituisce l'ingombro del ramo rispetto al
    // centro di node; inheritedSide = lato del ramo per i tipi senza direzione propria.
    struct Placement { QPointF offset; int growSide = 0; };
    QRectF layoutBlock(MindNode *node, int inheritedSide, QHash<MindNode *, Placement> &rel);
    QRectF layoutTimeline(MindNode *node, const QRectF &self, const QList<MindNode *> &mains, QHash<MindNode *, Placement> &rel);
    QRectF layoutFishbone(MindNode *node, const QRectF &self, const QList<MindNode *> &mains, QHash<MindNode *, Placement> &rel);
    QRectF layoutTable(MindNode *owner, QHash<MindNode *, Placement> &rel);
    void placeBlock(MindNode *node, const QHash<MindNode *, Placement> &rel);
    // Room for the summaries over node's children in a block layout: `placed` are the children's
    // branches and `centres` their centres, in the frame where node sits at nodeAt.
    QRectF summaryRoom(const MindNode *node, const QList<MindNode *> &children, const QList<QRectF> &placed,
                       const QList<QPointF> &centres, const QPointF &nodeAt, bool row = false) const;
    // FEAT-052: room a block layout keeps around children[index] for the boundaries on runs of these
    // children, like the mind map's: across the run on every topic, along it before the first and
    // after the last. alongX: the children follow each other left to right (a row) or top to bottom;
    // ascending: the list runs towards larger coordinates.
    QMarginsF boundaryPad(const QList<MindNode *> &children, int index, bool alongX, bool ascending) const;

    MindMapModel *m_model;
    QHash<MindNode *, QPointF> m_autoPositions;
    QHash<const MindNode *, int> m_sides;
    QHash<QString, TablePlace> m_tableCells;
    QHash<QString, TablePlace> m_tableFrames;
    QHash<const MindNode *, qreal> m_fishboneReach;
    Spacing m_spacing{54, 28, 35, 6, 1};
    // Layout room for boundaries: the cursor moves by `above` before the first topic of a
    // run is laid out and by `below` after the last one, whatever the order of the side.
    struct BoundaryRun { qreal above = 0; qreal below = 0; int size = 0; int visited = 0; };
    QList<BoundaryRun> m_boundaryRuns;
    QHash<const MindNode *, QList<int>> m_boundaryRunsOf;
    // Bounds of each summary topic with its branch, relative to the topic's position (collectBoundaryRuns).
    QHash<const MindNode *, QRectF> m_summaryBranchBounds;
    // Y dell'ultima foglia posizionata da layoutSubtree (per centrare il genitore).
    qreal m_lastLeafY = 0;
    // Meta' altezza dell'ultima foglia, -1 se non ce n'è ancora una, e Y di tutte le foglie in
    // ordine: servono a distanziare foglie di altezza diversa (2026-10-13).
    qreal m_lastLeafStride = -1;
    QList<qreal> m_leafYs;
    // La prossima foglia apre un nuovo ramo principale: dalla precedente dista kBranchGapPx invece
    // di kSiblingGapPx (distanze di riferimento, 2026-10-21).
    bool m_startsBranch = true;
};
