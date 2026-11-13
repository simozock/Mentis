#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <functional>

class MapLayout;
class MindMapModel;
struct MindNode;

// FEAT-015: where a topic dragged in the structure goes, for any front end (the desktop scene and
// the mobile canvas, FEAT-097 B2). The front end gives what it draws: each topic's box and whether
// it is shown; the rules are the same (user's photo 2026-10-21): on a topic = its child, at the end
// (Child); beyond its outer side, up to its children (kLeafReachPx for a leaf) = its child among
// the siblings at the pointer's height (Insert); the central topic counts on both sides. Kind::None
// = a drop without effect (the topic itself or its branch, the same place); Float = on the
// background (a floating topic, on the desktop).
struct ReorderTarget {
    enum class Kind { None, Child, Insert, Float };
    Kind kind = Kind::None;
    MindNode *parent = nullptr;
    int row = -1;
    // For a main topic, the side to take (+1 right, -1 left) when it changes; 0 = as it is.
    int side = 0;
    QRectF slot;   // where the topic will appear (the blue placeholder), map px
    QPointF from;  // the point of the parent its branch starts from

    static constexpr QSizeF kSlotPx{36, 12};
    static constexpr qreal kSlotRadiusPx = 4;
    static constexpr qreal kLeafReachPx = 200;
    static constexpr qreal kBodyMarginPx = 4;      // around the shape: a drop on the topic
    static constexpr qreal kVerticalReachPx = 24;  // above and below the children
    static constexpr qreal kGhostOpacity = 0.6;    // the dragged topic's copy
    static constexpr qreal kBranchWidthPx = 2;     // the blue branch to the slot

    // What the front end draws. nodes: the topics in the order they are tried for a drop on a body
    // (the first that contains the point wins). box: a topic's shape (or table cell) in map px.
    // shown: drawn and visible. branchStart: the parent's point a branch on that side starts from.
    struct Scene {
        MindMapModel *model = nullptr;
        const MapLayout *layout = nullptr;
        QList<MindNode *> nodes;
        std::function<QRectF(const MindNode *)> box;
        std::function<bool(const MindNode *)> shown;
        std::function<QPointF(const MindNode *, bool right)> branchStart;
    };
    static ReorderTarget at(const Scene &scene, const MindNode *dragged, const QPointF &point);
};
