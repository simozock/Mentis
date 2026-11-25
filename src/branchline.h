#pragma once

#include <QBrush>
#include <QColor>
#include <QPainterPath>
#include <QPen>
#include <QPointF>

#include <functional>

class MapLayout;
class MindMapModel;
class NodeGeometry;
struct MindNode;

// FEAT-119 stage 0: the line into a topic, as MindMapScene::updateEdge draws it, without scene items,
// so the desktop scene and the phone (MapSnapshot) draw the same line: the structure's line (timeline,
// fishbone, org chart, tree chart, brace, the variant's line or the parent's branch shape), the
// freehand wobble, the tapered branch from the central topic and the marks at both ends (FEAT-023,
// FEAT-026, FEAT-072). Moved unchanged from mindmapscene.cpp.
namespace BranchLine {

// Where a topic is drawn: its position (map px) and measures; geometry null when it is not drawn.
struct Placed {
    QPointF pos;
    const NodeGeometry *geometry = nullptr;
    explicit operator bool() const { return geometry != nullptr; }
};
using PlacedOf = std::function<Placed(const MindNode *)>;

struct Line {
    QPainterPath path;  // stroked with pen, or (tapered) filled with brush
    QPen pen;
    QBrush brush;
    QPainterPath filledMarks;   // filled with the line colour
    QPainterPath strokedMarks;  // stroked with a solid round pen markWidth wide (the open arrow)
    QColor markColor;
    qreal markWidth = 0;
    qreal z = -1;  // -2 for a timeline's axis and a fishbone's spine; the caller adds the stack's z
};

// True when the child has a line in: not the root, a floating or summary topic, nor (`tableCell`) a
// table's cell (MindMapScene::isTableCell), and both ends drawn.
bool hasLine(const MindNode *child, bool tableCell, const PlacedOf &placed);
// The line into `child`; call only when hasLine.
Line line(const MindMapModel &model, const MapLayout &layout, const MindNode *child, const PlacedOf &placed);

}  // namespace BranchLine
