#include "branchline.h"

#include "branchpath.h"
#include "maplayout.h"
#include "mapstructure.h"
#include "mapvariants.h"
#include "mindmapmodel.h"
#include "nodegeometry.h"
#include "nodeshape.h"
#include "nodesketch.h"
#include "nodestyle.h"

#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

using namespace MapLayoutRules;

namespace BranchLine {

namespace {
constexpr qreal kRootSquareInsetPx = 50;  // FEAT-072: right-angle lines from the central topic (reference 51)
// FEAT-072: curves to underlined main topics start inside the corner by 0.75 x the child's distance above or
// below the central topic (reference photos 2026-10-23: 0.73-0.75 over 7-123 px; a fixed 35 px fitted only close topics).
constexpr qreal kRootUnderlineInsetPerPx = 0.75;
constexpr qreal kTimelineObliqueSlope = 0.577;  // variant 3: the last stretch to a subtopic rises at 30 degrees

QRectF shapeOf(const Placed &item) { return item.geometry->shapeRect().translated(item.pos); }
}  // namespace

bool hasLine(const MindNode *child, bool tableCell, const PlacedOf &placed) {
    // The root and floating topics have no line in. FEAT-028: no lines between the cells of a table.
    return child && child->parent && !child->floating && !child->summaryTopic && !tableCell && placed(child->parent) && placed(child);
}

Line line(const MindMapModel &model, const MapLayout &layout, const MindNode *child, const PlacedOf &placed) {
    const Placed from = placed(child->parent);
    const Placed to = placed(child);
    // Prima l'aggancio era fisso a ±105 px; ora segue la larghezza reale del nodo. FEAT-020 parte
    // B: il punto e' sul contorno della forma (con le linee sotto all'altezza della linea).
    // FEAT-014: il lato del figlio segue la meta' della mappa in cui sta (linea verticale del
    // centrale), anche se il nodo e' piu' in alto o in basso del genitore e durante lo spostamento a
    // mano: a destra il ramo entra da sinistra, a sinistra da destra. Nei rami di un argomento
    // mobile conta la posizione rispetto al genitore. Dal centrale il ramo esce dal punto del suo
    // contorno nella direzione del figlio.
    bool inFloatingBranch = false;
    // FEAT-052: a summary topic's branch, like a floating one's, by the place beside its parent.
    for (const MindNode *up = child->parent; up && !inFloatingBranch; up = up->parent) inFloatingBranch = up->floating || up->summaryTopic;
    const Placed rootItem = placed(model.root());
    // FEAT-026 fase C: la meta' della mappa conta solo nella mappa mentale senza tipi propri lungo il
    // ramo; altrove (tipi annidati, grafico logico...) conta la posizione rispetto al genitore.
    bool plainMindMap = !inFloatingBranch && MapStructure::typeOf(model.root()) == MapStructure::Type::MindMap;
    for (const MindNode *up = child->parent; up && up->parent && plainMindMap; up = up->parent) plainMindMap = up->structure.isEmpty();
    const qreal axis = plainMindMap && rootItem ? rootItem.pos.x() : from.pos.x();
    const bool right = to.pos.x() >= axis;
    const QPointF b = to.pos + to.geometry->branchAnchor(!right);
    const MapStructure::Type parentType = MapStructure::typeOf(child->parent);
    QPainterPath path;
    qreal zValue = -1;
    if (setsTimeline(child->parent)) {
        // FEAT-026 phase E (reference live 2026-10-24). On the axis: a line from the previous main topic (or
        // the central one) in this topic's colour. Off the axis: a dot on the axis and a tick to the
        // topic; the last topic also draws the axis, in its colour, under the other dots.
        const QRectF toShape = shapeOf(to);
        const QRectF fromShape = shapeOf(from);
        Placed previous = from;
        // The previous main topic and whether this one is the last (floating topics are not on the timeline).
        bool last = true;
        bool seen = false;
        for (const auto &sibling : child->parent->children) {
            if (sibling->floating) continue;
            if (sibling.get() == child) { seen = true; continue; }
            if (seen) { last = false; break; }
            if (const Placed siblingItem = placed(sibling.get())) previous = siblingItem;
        }
        if (MapStructure::directionOf(child->parent) != MapStructure::Direction::OffAxis) {
            const QRectF previousShape = shapeOf(previous);
            path = BranchPath::straight({previousShape.right(), previousShape.center().y()}, {toShape.left(), toShape.center().y()});
        } else {
            const qreal axisY = fromShape.center().y();
            const qreal x = toShape.center().x();
            const qreal width = child->parent->branchWidth;
            path.moveTo(x, axisY);
            path.lineTo(x, toShape.center().y() < axisY ? toShape.bottom() : toShape.top());
            path.addEllipse(QPointF(x, axisY), width / 2, width / 2);  // stroked twice: a dot of radius 2 x width
            path.addEllipse(QPointF(x, axisY), 1.5 * width, 1.5 * width);
            if (last) {
                path.moveTo(fromShape.right(), axisY);
                path.lineTo(toShape.right() + kTimelineAxisOvershootPx, axisY);
                zValue = -2;
            }
        }
    } else if (setsTimeline(child->parent->parent) && !child->parent->floating) {
        // The stem from the middle of the main topic's side towards its children, then square to each child
        // (variants 1 and 2) or rising at 30 degrees for its last stretch (variant 3).
        const QRectF fromShape = shapeOf(from);
        const QRectF toShape = shapeOf(to);
        const bool up = toShape.center().y() < fromShape.center().y();
        const QPointF start(fromShape.center().x(), up ? fromShape.top() : fromShape.bottom());
        const QPointF end(toShape.left(), toShape.center().y());
        const MapVariants::Variant *variant = MapVariants::find(model.sheetStyle.variant);
        const qreal rise = variant && variant->line == MapVariants::Line::Angled ? (end.x() - start.x()) * kTimelineObliqueSlope : 0;
        const qreal kneeY = up ? std::min(start.y(), end.y() + rise) : std::max(start.y(), end.y() - rise);
        path.moveTo(start);
        path.lineTo(start.x(), kneeY);
        path.lineTo(end);
    } else if ((setsFishbone(child->parent) && !child->floating)
               || (child->parent->parent && setsFishbone(child->parent->parent) && !child->floating && !child->parent->floating)) {
        // FEAT-026 phase E (reference live 2026-10-24). A main topic's bone from its foot on the spine at 60 degrees
        // to the middle of its side facing the spine; the last main topic also draws the spine, in its colour,
        // from the head to the end of the farthest branch. A subtopic's horizontal tick from the bone.
        const bool isMain = setsFishbone(child->parent);
        const MindNode *headNode = isMain ? child->parent : child->parent->parent;
        const Placed head = placed(headNode);
        const Placed mainItem = isMain ? to : from;
        const int s = MapStructure::directionOf(headNode) == MapStructure::Direction::Left ? -1 : 1;
        const QRectF headShape = head ? shapeOf(head) : QRectF();
        const QRectF mainShape = shapeOf(mainItem);
        const qreal axisY = headShape.center().y();
        const bool above = mainShape.center().y() < axisY;
        const QPointF boneEnd(mainShape.center().x(), above ? mainShape.bottom() : mainShape.top());
        const qreal footX = boneEnd.x() - s * std::abs(boneEnd.y() - axisY) * kFishboneSlope;
        if (isMain) {
            path.moveTo(footX, axisY);
            path.lineTo(boneEnd);
            const auto last = std::find_if(child->parent->children.crbegin(), child->parent->children.crend(),
                                           [](const auto &sibling) { return !sibling->floating; });
            if (last->get() == child) {
                path.moveTo(s > 0 ? headShape.right() : headShape.left(), axisY);
                path.lineTo(headShape.center().x() + s * layout.fishboneReach().value(headNode, 0), axisY);
                zValue = -2;
            }
        } else {
            const QRectF toShape = shapeOf(to);
            const qreal y = toShape.center().y();
            path.moveTo(footX + s * std::abs(y - axisY) * kFishboneSlope, y);
            path.lineTo(s > 0 ? toShape.left() : toShape.right(), y);
        }
    } else if (parentType == MapStructure::Type::OrgChart && !child->floating) {
        // Organigramma: dal lato del genitore rivolto al figlio al lato opposto del figlio.
        const bool down = to.pos.y() >= from.pos.y();
        const QRectF fromShape = from.geometry->shapeRect(), toShape = to.geometry->shapeRect();
        const QPointF start = from.pos + QPointF(0, down ? fromShape.bottom() : fromShape.top());
        const QPointF end = to.pos + QPointF(0, down ? toShape.top() : toShape.bottom());
        // FEAT-072: from the central topic slanted lines with a rounded bend; below it the elbow.
        path = child->parent->parent ? BranchPath::elbow(start, end) : BranchPath::orgFan(start, end);
    } else if (parentType == MapStructure::Type::TreeChart && !child->floating
               && (!child->parent->parent || MapStructure::typeFromKey(child->parent->structure) == MapStructure::Type::TreeChart)) {
        // Albero: tronco dal centro del lato basso del genitore al lato del figlio rivolto al tronco.
        path = BranchPath::trunk(from.pos + QPointF(0, from.geometry->shapeRect().bottom()), b);
    } else if (parentType == MapStructure::Type::Brace) {
        // One brace per parent (reference live 2026-10-24): the first child draws the upper arm, the last
        // the lower one, each in its colour; middle children have no line, a single child both arms
        // to its top and bottom.
        const auto isLaidOut = [](const auto &sibling) { return !sibling->floating; };
        const auto first = std::find_if(child->parent->children.cbegin(), child->parent->children.cend(), isLaidOut);
        const auto last = std::find_if(child->parent->children.crbegin(), child->parent->children.crend(), isLaidOut);
        const int dir = right ? 1 : -1;
        const QRectF fromShape = shapeOf(from);
        const QRectF toShape = shapeOf(to);
        const QPointF cusp(right ? fromShape.right() + kBraceCuspGapPx : fromShape.left() - kBraceCuspGapPx, fromShape.center().y());
        const qreal radius = braceRadius(child->parent);
        const bool isFirst = first->get() == child, isLast = last->get() == child;
        if (isFirst && isLast) {
            path = BranchPath::braceArm(cusp, toShape.top(), radius, dir);
            path.addPath(BranchPath::braceArm(cusp, toShape.bottom(), radius, dir));
        } else if (isFirst || isLast) {
            path = BranchPath::braceArm(cusp, toShape.center().y(), radius, dir);
        }
    } else {
        // Dal centrale della mappa mentale il ramo parte dal contorno verso il figlio; altrove (anche nel
        // grafico logico dal centrale) dal lato del genitore. FEAT-026 fase D: forma della variante.
        const MapVariants::Variant *variant = MapVariants::find(model.sheetStyle.variant);
        const MapVariants::Line line = variant ? variant->line : MapVariants::Line::Curve;
        const bool fromRootEdge = child->parent == model.root() && parentType == MapStructure::Type::MindMap;
        // FEAT-030: a shape wider above or below its anchor (heart, cloud, star, gem...) would cover the
        // bars and stubs built from the anchor: the line goes straight out to the shape's side first.
        const QPointF anchor = from.pos + from.geometry->branchAnchor(right);
        const QRectF fromRect = shapeOf(from);
        const QPointF side(right ? std::max(anchor.x(), fromRect.right()) : std::min(anchor.x(), fromRect.left()), anchor.y());
        // FEAT-030: from a central topic that is not a rectangle the branch starts on its outline, where
        // the box rule would have started it (on the box, the line floated beside a star or a heart).
        const bool onOutline = fromRootEdge && !NodeShapes::startsOnBox(child->parent->shape);
        const QPainterPath rootOutline = onOutline ? NodeShapes::outline(child->parent->shape, fromRect) : QPainterPath();
        const auto rootExit = [&](const QPointF &boxPoint, const QPointF &inside) {
            if (!onOutline) return boxPoint;
            const QPointF exit = NodeShapes::rayExit(rootOutline, inside, boxPoint);
            return exit == inside ? boxPoint : exit;
        };
        const QPointF a = fromRootEdge ? rootExit(BranchPath::exitPoint(fromRect, b), fromRect.center()) : side;
        // FEAT-072: shape chosen on the parent, else the map's default (effectiveBranchShape); Auto =
        // the variant's line (curve from the central topic of a mind map = rule A7).
        const BranchShape shape = layout.effectiveBranchShape(child->parent);
        // Measured live 2026-10-23: a child above or below the central topic gets a line from its top or
        // bottom side, inside the side facing the child by `insetPx` plus `perPx` x its distance from that
        // top or bottom side; a child level with it, from that side.
        const auto fromRootSide = [&](qreal insetPx, qreal perPx, QPainterPath (*build)(const QPointF &, const QPointF &)) {
            const QRectF rootRect = shapeOf(from);
            const qreal distance = std::max({rootRect.top() - b.y(), b.y() - rootRect.bottom(), 0.0});
            const qreal inset = std::min(insetPx + perPx * distance, rootRect.width() / 2);
            const qreal x = right ? rootRect.right() - inset : rootRect.left() + inset;
            const qreal middle = rootRect.center().y();
            if (b.y() < rootRect.top()) return build(rootExit(QPointF(x, rootRect.top()), QPointF(x, middle)), b);
            if (b.y() > rootRect.bottom()) return build(rootExit(QPointF(x, rootRect.bottom()), QPointF(x, middle)), b);
            return BranchPath::straight(rootExit(QPointF(right ? rootRect.right() : rootRect.left(), b.y()), QPointF(rootRect.center().x(), b.y())), b);
        };
        if (shape == BranchShape::Auto) {
            switch (line) {
            case MapVariants::Line::Curve:
                // FEAT-072 (user's reference photos 2026-10-23): curves to underlined main topics leave the top
                // or bottom side, further inside the farther the topic, the same above and below. With rule
                // A7 the upper ones, whose underline is nearer the centre, left the side.
                if (fromRootEdge && variant && variant->main.look == MapVariants::Look::Underline)
                    path = fromRootSide(0, kRootUnderlineInsetPerPx, BranchPath::curve);
                else
                    path = fromRootEdge ? BranchPath::curve(a, b) : BranchPath::between(a, b);  // rule A7 from a
                break;
            case MapVariants::Line::Angled: path = BranchPath::angled(a, b); break;
            case MapVariants::Line::Square:
                path = fromRootEdge ? fromRootSide(kRootSquareInsetPx, 0, BranchPath::square) : BranchPath::squareFromStub(a, b);
                break;
            case MapVariants::Line::Straight: path = BranchPath::straight(a, b); break;
            }
        } else if (shape == BranchShape::Curve && fromRootEdge) {
            path = BranchPath::curve(a, b);  // rule A7 (BranchPath::fromRoot), from the outline when not a box
        } else if (shape == BranchShape::RoundedBracket || shape == BranchShape::Bracket) {
            // Every child leaves the bar with a rounded corner; the outermost children's
            // paths draw the bar past the middle ones.
            path = BranchPath::bracket(side, b, shape == BranchShape::RoundedBracket);
        } else {
            path = BranchPath::fan(side, b, static_cast<int>(shape));
        }
        if (!fromRootEdge && side != anchor && !path.isEmpty()) {
            QPainterPath lead(anchor);
            lead.lineTo(side);
            lead.connectPath(path);
            path = lead;
        }
    }
    // BUG-005/006: colore del ramo del figlio; stile e spessore delle linee che partono dal genitore.
    const MindNode *owner = child->parent;
    // FEAT-023: linee a mano libera (il percorso ondulato, la penna continua o tratteggiata) e rami
    // affusolati (un'area piena, dal doppio circa dello spessore al genitore a 1 px al figlio).
    const Qt::PenStyle style = owner->branchStyle;
    const bool freehand = style == NodeStyle::kFreehandSolidLine || style == NodeStyle::kFreehandDashLine;
    const QPainterPath centreLine = path;  // before the wobble and the taper: where the line ends point
    if (freehand) path = BranchPath::freehand(path, NodeSketch::seedFor(child->id));
    const Qt::PenStyle penStyle = style == NodeStyle::kFreehandSolidLine ? Qt::SolidLine
                                : style == NodeStyle::kFreehandDashLine ? Qt::DashLine : style;
    // Foto dell'utente 2026-10-21: affusolati solo i rami dal centrale ai principali, che
    // arrivano al nodo con lo spessore scelto; gli altri sono linee normali.
    // Not on a timeline: its lines join topic to topic and carry dots (FEAT-026 phase E).
    const bool taper = model.sheetStyle.tapered && penStyle != Qt::NoPen && child->parent == model.root() && !child->floating
                    && !setsTimeline(child->parent) && !setsFishbone(child->parent);
    // FEAT-023: marks at the parent's end (start) and the child's end of the line; none without a line.
    // A tapered branch starts wider: the start mark takes that width.
    Line result;
    if (penStyle != Qt::NoPen && !centreLine.isEmpty() && (owner->branchStart != BranchEnd::None || owner->branchEnd != BranchEnd::None)) {
        const auto travel = [&centreLine](qreal percent) {
            const qreal angle = centreLine.angleAtPercent(percent) * std::numbers::pi / 180.0;
            return QPointF(std::cos(angle), -std::sin(angle));
        };
        const qreal startWidth = taper ? NodeStyle::taperedStartWidth(owner->branchWidth) : owner->branchWidth;
        for (const auto &[kind, tip, outward, width] :
             {std::tuple{owner->branchStart, centreLine.pointAtPercent(0), -travel(0), startWidth},
              std::tuple{owner->branchEnd, centreLine.pointAtPercent(1), travel(1), owner->branchWidth}}) {
            if (kind == BranchEnd::None) continue;
            (BranchPath::endIsStroke(kind) ? result.strokedMarks : result.filledMarks).addPath(BranchPath::endMark(kind, tip, outward, width));
        }
    }
    if (taper) {
        const qreal startWidth = NodeStyle::taperedStartWidth(owner->branchWidth);
        const bool fromRoot = child->parent == model.root() && parentType == MapStructure::Type::MindMap;
        const QPointF start = path.elementCount() > 0 ? QPointF(path.elementAt(0)) : QPointF();
        const qreal angle = path.angleAtPercent(0) * std::numbers::pi / 180.0;
        path = BranchPath::tapered(path, startWidth, owner->branchWidth);
        if (fromRoot && !path.isEmpty()) {
            // User's photos 2026-10-23: the branch comes out from under the central topic at full
            // width, cut by its outline. It is prolonged backwards into the box and cut by the real outline:
            // cutting by the rectangle left one edge ending in the empty rounded corner, and the cut square
            // to the curve stuck out above the border.
            const QRectF rootRect = shapeOf(from);
            const qreal depth = std::min(rootRect.width(), rootRect.height()) / 2;
            const QPointF back = QPointF(-std::cos(angle), std::sin(angle)) * depth;
            const QPointF half = QPointF(std::sin(angle), std::cos(angle)) * (startWidth / 2);
            QPainterPath under;
            under.addPolygon(QPolygonF{start + half, start - half, start - half + back, start + half + back});
            path = path.united(under).subtracted(NodeShapes::outline(child->parent->shape, rootRect));
        }
    }
    result.path = path;
    result.pen = taper ? QPen(Qt::NoPen) : QPen(child->lineColor, owner->branchWidth, penStyle, Qt::RoundCap);  // FEAT-043
    result.brush = taper ? QBrush(child->lineColor) : QBrush(Qt::NoBrush);
    result.markColor = child->lineColor;
    result.markWidth = owner->branchWidth;
    result.z = zValue;
    return result;
}

}  // namespace BranchLine
