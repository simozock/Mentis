#include "reordertarget.h"

#include "maplayout.h"
#include "mindmapmodel.h"

#include <algorithm>

ReorderTarget ReorderTarget::at(const Scene &scene, const MindNode *dragged, const QPointF &point) {
    ReorderTarget result;
    if (!dragged || !dragged->parent || !scene.model || !scene.layout) return result;  // the central topic does not move
    MindNode *root = scene.model->root();
    const MapLayout &layout = *scene.layout;
    const auto &boxOf = scene.box;
    // Candidates: laid-out topics shown, outside the dragged branch (free topics and their branches not).
    const auto candidate = [&](const MindNode *node) {
        if (!scene.shown(node) || !layout.autoPositions().contains(const_cast<MindNode *>(node))) return false;
        for (const MindNode *up = node; up; up = up->parent) if (up == dragged) return false;
        return true;
    };
    // The side a topic's children grow towards; for the central topic the pointer's.
    const auto outward = [&](const MindNode *node) {
        if (node == root) return point.x() < boxOf(root).center().x() ? -1 : 1;
        return layout.sideOf(node) < 0 ? -1 : 1;
    };
    // The parent's laid-out children on side s, top to bottom, without the dragged one (floating
    // topics are not branches of the central topic).
    const auto visualChildren = [&](const MindNode *parent, int s) {
        QList<MindNode *> list;
        for (const auto &child : parent->children)
            if (child.get() != dragged && !child->floating && candidate(child.get()) && (parent != root || outward(child.get()) == s)) list.append(child.get());
        std::sort(list.begin(), list.end(), [&](const MindNode *x, const MindNode *y) { return boxOf(x).center().y() < boxOf(y).center().y(); });
        return list;
    };
    // 1. On a topic: its child, at the end. 2. Beyond its outer side: the nearest.
    MindNode *parent = nullptr;
    bool onBody = false;
    qreal bestDistance = 0;
    for (MindNode *node : scene.nodes) {
        if (!candidate(node)) continue;
        const QRectF box = boxOf(node);
        if (box.adjusted(-kBodyMarginPx, -kBodyMarginPx, kBodyMarginPx, kBodyMarginPx).contains(point)) {
            parent = node;
            onBody = true;
            break;
        }
        const int s = outward(node);
        const qreal distance = (point.x() - (s > 0 ? box.right() : box.left())) * s;
        if (distance < 0) continue;
        const QList<MindNode *> children = visualChildren(node, s);
        QRectF span = box;
        qreal reach = kLeafReachPx;
        if (!children.isEmpty()) {
            reach = 0;
            for (const MindNode *child : children) {
                const QRectF childBox = boxOf(child);
                span = span.united(childBox);
                reach = std::max(reach, (s > 0 ? childBox.right() - box.right() : box.left() - childBox.left()));
            }
        }
        if (distance > reach || point.y() < span.top() - kVerticalReachPx || point.y() > span.bottom() + kVerticalReachPx) continue;
        if (!parent || distance < bestDistance) { parent = node; bestDistance = distance; }
    }
    if (!parent) {
        // On the background: a floating topic, but not over the topic itself.
        const QRectF own = boxOf(dragged);
        if (!own.adjusted(-kBodyMarginPx, -kBodyMarginPx, kBodyMarginPx, kBodyMarginPx).contains(point)) result.kind = Kind::Float;
        return result;
    }
    const int s = outward(parent);
    const QList<MindNode *> children = visualChildren(parent, s);
    const auto rowOf = [](const MindNode *node) {
        const auto &siblings = node->parent->children;
        return int(std::find_if(siblings.cbegin(), siblings.cend(), [node](const auto &child) { return child.get() == node; }) - siblings.cbegin());
    };
    // The place among the visible children (from the top): on the topic = last in the order.
    const bool mirrored = parent == root && s < 0;  // on the left the main branches go bottom to top
    int index = int(children.size());
    if (!onBody) {
        index = 0;
        while (index < children.size() && boxOf(children[index]).center().y() < point.y()) ++index;
    } else if (mirrored) {
        index = 0;  // last in the order = top left
    }
    if (children.isEmpty()) result.row = int(parent->children.size());
    else if (index < children.size()) result.row = rowOf(children[index]) + (mirrored ? 1 : 0);
    else result.row = rowOf(children.back()) + (mirrored ? 0 : 1);
    if (onBody && !mirrored) result.row = int(parent->children.size());
    if (parent == root) result.row = std::min(result.row, scene.model->firstFloatingRow());  // before the floating topics
    result.parent = parent;
    if (parent == root && s != layout.sideOf(dragged)) result.side = s;
    // Same parent and same place (as MindMapModel::moveNode): no effect.
    if (parent == dragged->parent && result.side == 0) {
        const int from = rowOf(dragged);
        const int destination = from < result.row ? result.row - 1 : result.row;
        if (destination == from) return {};
    }
    // The slot: at the children's distance from the parent, between the near siblings (or at the
    // parent's middle when it has no children on that side).
    const QRectF box = boxOf(parent);
    const qreal gap = parent == root ? layout.spacing().rootLevel : layout.spacing().level;
    qreal y = box.center().y();
    if (!children.isEmpty()) {
        if (index == 0) y = boxOf(children.front()).top() - layout.spacing().sibling / 2 - kSlotPx.height() / 2;
        else if (index == children.size()) y = boxOf(children.back()).bottom() + layout.spacing().sibling / 2 + kSlotPx.height() / 2;
        else y = (boxOf(children[index - 1]).bottom() + boxOf(children[index]).top()) / 2;
    }
    const qreal x = s > 0 ? box.right() + gap : box.left() - gap - kSlotPx.width();
    result.slot = QRectF(QPointF(x, y - kSlotPx.height() / 2), kSlotPx);
    result.from = scene.branchStart(parent, s > 0);
    result.kind = onBody ? Kind::Child : Kind::Insert;
    return result;
}
