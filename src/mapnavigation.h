#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <functional>

class MapLayout;
struct MindNode;

// The arrows on the map (user's request 2026-10-21): from a topic to the one drawn that way, for
// any front end (the desktop scene and the mobile canvas, FEAT-097). The front end gives what it
// draws; the rules follow the structure (mind map, org chart, timeline, fishbone, tree table).
struct MapNavigation {
    // nodes: every topic drawn; shown: drawn and visible; centre: its centre and box: its shape (or
    // table cell), in map px.
    struct Scene {
        const MapLayout *layout = nullptr;
        QList<MindNode *> nodes;
        std::function<bool(const MindNode *)> shown;
        std::function<QPointF(const MindNode *)> centre;
        std::function<QRectF(const MindNode *)> box;
    };
    // The topic key (Left, Right, Up, Down) leads to from node; nullptr when there is none.
    static MindNode *neighbour(const Scene &scene, MindNode *node, Qt::Key key);
};
