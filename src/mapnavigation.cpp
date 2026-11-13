#include "mapnavigation.h"

#include "maplayout.h"
#include "mapstructure.h"
#include "mindmapmodel.h"

#include <cmath>

MindNode *MapNavigation::neighbour(const Scene &scene, MindNode *node, Qt::Key key) {
    using MapStructure::Type;
    using MapStructure::Direction;
    using MapLayoutRules::setsFishbone;
    using MapLayoutRules::setsTimeline;
    if (!node || !scene.layout) return nullptr;
    const MapLayout &layout = *scene.layout;
    const auto visible = [&scene](const MindNode *at) { return scene.shown(at); };
    const auto &centre = scene.centre;
    const auto sideOfNode = [&layout](const MindNode *at) { return at->floating ? 1 : layout.sideOf(at); };
    // Tasto che porta ai figli di "at": in un organigramma in basso (o in alto), altrove verso l'esterno.
    const auto childKey = [&](const MindNode *at, int side) {
        if (MapStructure::typeOf(at) == Type::OrgChart) return MapStructure::directionOf(at) == Direction::Up ? Qt::Key_Up : Qt::Key_Down;
        return side < 0 ? Qt::Key_Left : Qt::Key_Right;
    };
    const auto opposite = [](Qt::Key k) {
        switch (k) {
        case Qt::Key_Left: return Qt::Key_Right;
        case Qt::Key_Right: return Qt::Key_Left;
        case Qt::Key_Up: return Qt::Key_Down;
        default: return Qt::Key_Up;
        }
    };
    const auto shownChildren = [&](const MindNode *at) {
        QList<MindNode *> list;
        if (!MapLayout::childrenHidden(at))
            for (const auto &child : at->children) if (!child->floating && visible(child.get())) list.append(child.get());
        return list;
    };
    const QList<MindNode *> children = shownChildren(node);
    // FEAT-028 (user's request 2026-10-25): in a tree table Up and Down go to the cell drawn right above
    // or below, whatever its level (a level-5 cell to the level-3 leaf under it); among the cells
    // touching that edge, the one starting nearest on the left. Left and Right follow the usual rules
    // (Right to the first child, Left to the parent).
    if ((key == Qt::Key_Up || key == Qt::Key_Down) && layout.tableCells().contains(node->id)) {
        const QString owner = layout.tableCells().value(node->id).owner;
        const QRectF from = scene.box(node);
        MindNode *best = nullptr;
        qreal bestGap = 0, bestLeft = 0;
        for (MindNode *other : scene.nodes) {
            const auto place = layout.tableCells().constFind(other->id);
            if (other == node || place == layout.tableCells().cend() || place->owner != owner || !visible(other)) continue;
            const QRectF to = scene.box(other);
            if (std::min(from.right(), to.right()) - std::max(from.left(), to.left()) <= 1) continue;  // not above or below
            const qreal gap = key == Qt::Key_Down ? to.top() - from.bottom() : from.top() - to.bottom();
            if (gap < -0.5) continue;
            const qreal left = std::abs(to.left() - from.left());
            if (!best || gap < bestGap - 0.5 || (gap < bestGap + 0.5 && left < bestLeft)) { best = other; bestGap = gap; bestLeft = left; }
        }
        if (best) return best;
    }
    // FEAT-026 phase E: a timeline or a fishbone runs along its axis or spine (from topic to topic, away
    // from the central topic) and each column of subtopics across it (Up/Down), so the keys follow what is
    // drawn, not the mind map's rule.
    const auto setsAxis = [](const MindNode *at) { return setsTimeline(at) || setsFishbone(at); };
    const auto alongKey = [](const MindNode *at) {
        return setsFishbone(at) && MapStructure::directionOf(at) == Direction::Left ? Qt::Key_Left : Qt::Key_Right;
    };
    if (setsAxis(node) && key == alongKey(node) && !children.isEmpty()) return children.front();
    // Nearest of the candidates above (Up) or below (Down) node.
    const auto nearestVertically = [&](const QList<MindNode *> &candidates) {
        MindNode *best = nullptr;
        qreal bestDistance = 0;
        for (MindNode *other : candidates) {
            const qreal dy = centre(other).y() - centre(node).y();
            const qreal distance = key == Qt::Key_Up ? -dy : dy;
            if (other == node || distance <= 0.5) continue;
            if (!best || distance < bestDistance) { best = other; bestDistance = distance; }
        }
        return best;
    };
    if (node->parent && !node->floating && setsAxis(node->parent)) {
        const QList<MindNode *> mains = shownChildren(node->parent);
        const qsizetype index = mains.indexOf(node);
        const Qt::Key along = alongKey(node->parent);
        if (key == along) return index >= 0 && index + 1 < mains.size() ? mains[index + 1] : nullptr;
        if (key == opposite(along)) return index > 0 ? mains[index - 1] : node->parent;
        return nearestVertically(children);  // the column on that side, if the children are there
    }
    if (node->parent && node->parent->parent && !node->floating && !node->parent->floating && setsAxis(node->parent->parent)
        && (key == Qt::Key_Up || key == Qt::Key_Down)) {
        // Within the column only; past its end towards the axis, the main topic.
        if (MindNode *sibling = nearestVertically(shownChildren(node->parent))) return sibling;
        return nearestVertically({node->parent});
    }
    // Centrale: i primi rami di ogni lato (in senso orario: a destra il piu' alto, a sinistra il piu' basso).
    if (!node->parent) {
        const bool org = MapStructure::typeOf(node) == Type::OrgChart;
        for (MindNode *child : std::as_const(children)) {
            const QPointF at = centre(child) - centre(node);
            const Qt::Key towards = org ? (at.y() < 0 ? Qt::Key_Up : Qt::Key_Down) : (at.x() < 0 ? Qt::Key_Left : Qt::Key_Right);
            if (towards == key) return child;
        }
        return nullptr;
    }
    const int side = sideOfNode(node);
    if (key == childKey(node, side)) {
        // Il figlio piu' vicino lungo l'altro asse (la riga o la colonna dei figli).
        MindNode *best = nullptr;
        qreal bestDistance = 0;
        const bool vertical = key == Qt::Key_Up || key == Qt::Key_Down;
        for (MindNode *child : std::as_const(children)) {
            const QPointF delta = centre(child) - centre(node);
            const qreal distance = std::abs(vertical ? delta.x() : delta.y());
            if (!best || distance < bestDistance - 0.5) { best = child; bestDistance = distance; }
        }
        return best;
    }
    if (!node->floating && key == opposite(childKey(node->parent, node->parent->parent ? sideOfNode(node->parent) : side)))
        return node->parent;
    // Stesso livello e stesso lato, nell'ordine in cui si vedono lungo l'altro asse.
    const bool alongX = MapStructure::typeOf(node->parent) == Type::OrgChart && !node->floating;
    if (alongX ? (key != Qt::Key_Left && key != Qt::Key_Right) : (key != Qt::Key_Up && key != Qt::Key_Down)) return nullptr;
    const auto depthOf = [](const MindNode *at) { int depth = 0; for (; at->parent; at = at->parent) ++depth; return depth; };
    const int depth = depthOf(node);
    const qreal here = alongX ? centre(node).x() : centre(node).y();
    const bool forward = key == Qt::Key_Down || key == Qt::Key_Right;
    MindNode *best = nullptr;
    qreal bestDistance = 0;
    for (MindNode *other : scene.nodes) {
        if (other == node || !other->parent || !visible(other) || depthOf(other) != depth || sideOfNode(other) != side) continue;
        const qreal there = alongX ? centre(other).x() : centre(other).y();
        const qreal distance = forward ? there - here : here - there;
        if (distance <= 0.5) continue;
        if (!best || distance < bestDistance) { best = other; bestDistance = distance; }
    }
    if (best) return best;
    // No topic of the same level that way (user's request 2026-10-25): the nearest of any level on
    // the same side, so a deep topic still reaches the branch below it.
    for (MindNode *other : scene.nodes) {
        if (other == node || !other->parent || !visible(other) || sideOfNode(other) != side) continue;
        const qreal there = alongX ? centre(other).x() : centre(other).y();
        const qreal distance = forward ? there - here : here - there;
        if (distance <= 0.5) continue;
        if (!best || distance < bestDistance) { best = other; bestDistance = distance; }
    }
    return best;
}
