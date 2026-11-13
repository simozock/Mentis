#pragma once

#include "boundary.h"
#include "maplayout.h"
#include "nodegeometry.h"
#include "relationship.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSet>

#include <memory>
#include <unordered_map>

class MindMapModel;
struct MindNode;

// FEAT-097 stage A: what the mobile canvas draws of one sheet, worked out once when the sheet is
// opened, not at every frame. Every topic is measured with NodeGeometry and laid out with MapLayout
// (FEAT-096), then placed as the desktop scene places it (MindMapScene::placeBranch: automatic place
// plus the hand moves of the topic and its ancestors; floating topics stay where they are; the
// branches below a collapsed topic are hidden). Qt Gui only.
//
// FEAT-097 stage B: with writePositions, like placeBranch, it writes each placed topic's position
// into the model (MindNode::position), so the history step and the file keep what is drawn. Only an
// edit asks for it: on open, sheet switch or Undo the model must stay equal to the history state,
// or Document would record a layout-only step (the phone measures text with its own fonts).
//
// What is drawn of it, and the prototype's limits: MapScene (stage A, read only). Every line is the
// plain curve between two topics.
//
// FEAT-110: the relationships between two drawn elements, with the desktop's geometry and look
// (Relationships::geometry, Relationships::look).
//
// FEAT-115: the boundaries as MindMapScene::syncBoundaryItems places them: around the drawn boxes
// of their topics' branches, widened around the boundaries nested in them, none in a tree table or
// with no topic drawn.
//
// FEAT-115 step 2: the summaries as MindMapScene::syncSummaryItems places them: the bracket beside
// the drawn boxes of the covered branches, the summary topic beside it and its branch laid out away
// from it (MapLayout::layoutSummaryBranch); none in a tree table or over no drawn topic. The layout's
// room for a tall summary reads the covered runs at the model's positions (the last placement), as
// the desktop reads them from its items.
class MapSnapshot {
public:
    struct Topic {
        const MindNode *node = nullptr;
        QPointF position;       // map px, the topic's origin (NodeGeometry's frame)
        QRectF bounds;          // map px, shape and content with the 4 px margin
        const NodeGeometry *geometry = nullptr;
        // FEAT-114: map px, the tag pills under the shape (empty without tags); they belong to the
        // topic, as NodeItem's shape, but not to its selection ring.
        QRectF pills;
        bool contains(const QPointF &mapPoint) const { return bounds.contains(mapPoint) || pills.contains(mapPoint); }
    };
    struct Branch {
        QPainterPath path;      // map px
        QColor color;
        qreal width = 2;
        QRectF bounds;
    };
    struct Relation {
        Relationship relationship;
        Relationships::Geometry geometry;  // map px
        Relationships::Look look;
        QRectF labelRect;  // map px, the label's box; empty without a label
        Relationships::End from, to;  // its elements, for the line bent by a handle (MapCanvas)
    };
    struct BoundaryShape {
        Boundary boundary;
        QRectF stroke;     // map px, where the border runs (BoundaryItem::strokeRect)
        QRectF titleRect;  // map px, the title tab; empty without a title
        Boundaries::Look look;
        QPainterPath outline;  // the border with the tab: a relationship's end (BoundaryItem::outline)
        QRectF frame() const { return titleRect.isEmpty() ? stroke : stroke.united(titleRect); }
    };
    struct SummaryShape {
        QString id;            // Summary::id
        const MindNode *topic = nullptr;
        QPainterPath bracket;  // map px, open (Summaries::bracket)
        QColor color;          // NodeStyle::annotationColor of black on the sheet's backdrop
    };
    // The touch target each side of a relationship's line, screen px: half the 48 px target of
    // ui-mobile.md ("Touch targets"). A topic under the finger is taken first (MobileMap::selectAt).
    // FEAT-115: the same band outside a boundary's border.
    static constexpr qreal kRelationTouchPx = 24;

    // A topic measured with NodeItem's steps, as the snapshot measures every topic.
    static std::unique_ptr<NodeGeometry> measure(const MindNode *node);

    MapSnapshot() = default;
    explicit MapSnapshot(MindMapModel *model, bool writePositions = false);
    MapSnapshot(const MapSnapshot &) = delete;
    MapSnapshot &operator=(const MapSnapshot &) = delete;

    const QList<Topic> &topics() const { return m_topics; }
    const QList<Branch> &branches() const { return m_branches; }
    const QList<Relation> &relations() const { return m_relations; }
    // The relationship whose line is within `tolerance` map px of a point, or whose label holds it
    // (the last drawn on top first); null when none.
    const Relation *relationAt(const QPointF &mapPoint, qreal tolerance) const;
    const Relation *relationOf(const QString &id) const;
    // FEAT-115: the boundaries in drawing order (an outer one before the ones nested in it).
    const QList<BoundaryShape> &boundaries() const { return m_boundaries; }
    // The boundary whose fill or tab holds a point, or whose border is within `tolerance` map px of
    // it (the one drawn on top first: the innermost); null when none.
    const BoundaryShape *boundaryAt(const QPointF &mapPoint, qreal tolerance) const;
    const BoundaryShape *boundaryOf(const QString &id) const;
    // FEAT-115 step 2: the summaries drawn; their topics and branches are among topics().
    const QList<SummaryShape> &summaries() const { return m_summaries; }
    QRectF bounds() const { return m_bounds; }  // map px, every topic and line
    // FEAT-114: topics whose topic link leads nowhere (set by MobileMap), drawn with a grey chain.
    void setBrokenLinks(const QSet<const MindNode *> &nodes) { m_brokenLinks = nodes; }
    bool linkBroken(const MindNode *node) const { return m_brokenLinks.contains(node); }
    QColor background() const { return m_background; }
    // The topic drawn at a map point (the last drawn on top), or null.
    const Topic *topicAt(const QPointF &mapPoint) const;
    // The drawn topic of a node, or null when it is hidden or not in the sheet.
    const Topic *topicOf(const MindNode *node) const;
    // The sheet's layout (automatic places, sides, spacing), for the drop rules of B2.
    const MapLayout *layout() const { return m_layout.get(); }

private:
    void place(MindNode *node, const QPointF &inheritedOffset, bool hidden);
    bool isTableCell(const MindNode *node) const;
    void addBranch(const Topic &parent, const Topic &child, bool fromRoot);
    void addBoundaries(const MindMapModel *model);
    void addSummaries(MindMapModel *model);
    // MindMapScene::branchesCovered: the union of the drawn shapes of these topics and of their
    // branches (not their floating topics); null when none is drawn.
    QRectF drawnCovered(const MindMapModel *model, const QStringList &ids) const;
    void addRelations(const MindMapModel *model);

    std::unordered_map<const MindNode *, std::unique_ptr<NodeGeometry>> m_geometries;
    std::unique_ptr<MapLayout> m_layout;
    QList<Topic> m_topics;
    QList<Branch> m_branches;
    QList<Relation> m_relations;
    QList<BoundaryShape> m_boundaries;  // FEAT-115
    QList<SummaryShape> m_summaries;    // FEAT-115 step 2
    QRectF m_bounds;
    QSet<const MindNode *> m_brokenLinks;
    QHash<const MindNode *, qsizetype> m_topicIndex;  // index in m_topics of each drawn topic
    QColor m_background = Qt::white;
    bool m_writePositions = false;
};
