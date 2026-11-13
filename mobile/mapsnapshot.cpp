#include "mapsnapshot.h"

#include "branchpath.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "nodeshape.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainterPathStroker>

#include <algorithm>
#include <functional>
#include <utility>

std::unique_ptr<NodeGeometry> MapSnapshot::measure(const MindNode *node) {
    // NodeItem's measuring steps, in its order (constructor and syncFromNode), as
    // tests/domain_layout_test.cpp does.
    auto geometry = std::make_unique<NodeGeometry>(node);
    geometry->setHasImage(!node->imagePath.isEmpty() || !node->formula.isEmpty());
    geometry->updateText();
    geometry->updateNotePreview(QFont());
    geometry->updateLabelLayout();
    return geometry;
}

MapSnapshot::MapSnapshot(MindMapModel *model, bool writePositions) : m_writePositions(writePositions) {
    if (!model || !model->root()) return;
    m_background = model->backdrop().color;
    const std::function<void(const MindNode *)> measureBranch = [&](const MindNode *node) {
        m_geometries[node] = measure(node);
        for (const auto &child : node->children) measureBranch(child.get());
    };
    measureBranch(model->root());
    for (const MindNode *topic : model->summaryTopics()) measureBranch(topic);  // FEAT-115 step 2
    m_layout = std::make_unique<MapLayout>(model);
    m_layout->geometryOf = [this](const MindNode *node) -> const NodeGeometry * {
        const auto it = m_geometries.find(node);
        return it == m_geometries.end() ? nullptr : it->second.get();
    };
    m_layout->nodeById = [model](const QString &id) -> const MindNode * { return model->nodeById(id); };
    // FEAT-115 step 2, MindMapScene::branchesCovered from the last placement: the model's positions
    // (written by the last edit's layout, or the file's), the branches below a collapsed topic left out.
    m_layout->branchesCovered = [this, model](const QStringList &ids) {
        QRectF box;
        const std::function<void(const MindNode *)> add = [&](const MindNode *node) {
            const auto geometry = m_geometries.find(node);
            if (geometry == m_geometries.end()) return;
            const QRectF shape = geometry->second->shapeRect().translated(node->position);
            box = box.isNull() ? shape : box.united(shape);
            if (MapLayout::childrenHidden(node)) return;
            for (const auto &child : node->children)
                if (!child->floating) add(child.get());
        };
        for (const QString &id : ids) {
            const MindNode *node = model->nodeById(id);
            bool hidden = false;
            for (const MindNode *up = node ? node->parent : nullptr; up && !hidden; up = up->parent) hidden = MapLayout::childrenHidden(up);
            if (node && !hidden) add(node);
        }
        return box;
    };
    m_layout->collectBoundaryRuns();
    m_layout->computeAutoPositions();
    place(model->root(), QPointF(), false);
    addSummaries(model);  // before the boundaries, which may hold a summary topic's subtopics
    addBoundaries(model);
    addRelations(model);
}

QRectF MapSnapshot::drawnCovered(const MindMapModel *model, const QStringList &ids) const {
    QRectF box;
    const std::function<void(const MindNode *)> add = [&](const MindNode *node) {
        const Topic *topic = topicOf(node);
        if (!topic) return;  // hidden: so are its branches
        const QRectF shape = topic->geometry->shapeRect().translated(topic->position);
        box = box.isNull() ? shape : box.united(shape);
        for (const auto &child : node->children)
            if (!child->floating) add(child.get());
    };
    for (const QString &id : ids)
        if (const MindNode *node = model->nodeById(id)) add(node);
    return box;
}

void MapSnapshot::addSummaries(MindMapModel *model) {
    // MindMapScene::syncSummaryItems and summarySide.
    const QColor ink = NodeStyle::annotationColor(Qt::black, model->backdrop());
    for (const Summary &summary : std::as_const(model->summaries)) {
        MindNode *topic = summary.topic.get();
        const MindNode *parent = topic->parent;
        const Topic *parentTopic = topicOf(parent);
        const QRectF covered = drawnCovered(model, summary.topics);
        // The summaries of a tree table, and of its cells, are hidden.
        if (!parentTopic || covered.isNull() || MapStructure::typeOf(parent) == MapStructure::Type::TreeTable
            || MapStructure::insideTreeTable(parent))
            continue;
        // A row (org chart, timeline, fishbone) or a column: the run's own topics tell, else the
        // siblings on the first one's side (the two sides of a mind map are not one row).
        const auto centres = [&](const QStringList &ids) {
            QList<QPointF> list;
            for (const QString &id : ids)
                if (const Topic *drawn = topicOf(model->nodeById(id))) list.append(drawn->position);
            return list;
        };
        QList<QPointF> points = centres(summary.topics);
        if (points.size() < 2) {
            const MindNode *first = model->nodeById(summary.topics.front());
            const int side = first ? m_layout->sideOf(first) : 1;
            QStringList siblings;
            for (const auto &child : parent->children)
                if (!child->floating && m_layout->sideOf(child.get()) == side) siblings.append(child->id);
            points = centres(siblings);
        }
        const Summaries::Side side = Summaries::sideFor(points, covered, parentTopic->position, MapLayoutRules::setsFishbone(parent));
        const QPointF position = MapLayoutRules::summaryTopicPosition(covered, side, m_geometries.at(topic)->shapeRect());
        // The branch grows away from the bracket (to the right below and above a row). The layout
        // reads the topic's place from the model: it is put back when the positions are not written.
        const QPointF stored = std::exchange(topic->position, position);
        m_layout->layoutSummaryBranch(topic, side == Summaries::Side::Left ? -1 : 1);
        place(topic, QPointF(), false);
        if (!m_writePositions) topic->position = stored;
        m_summaries.append({summary.id, topic, Summaries::bracket(covered, side), ink});
        const qreal half = Summaries::kLineWidth / 2;
        m_bounds = m_bounds.united(m_summaries.last().bracket.boundingRect().adjusted(-half, -half, half, half));
    }
}

void MapSnapshot::addBoundaries(const MindMapModel *model) {
    // MindMapScene::boundaryCovered: the drawn shapes of the topics' branches; none inside a tree table.
    const auto covered = [&](const Boundary &boundary) {
        for (const QString &id : boundary.topics)
            if (const MindNode *node = model->nodeById(id); node && MapStructure::insideTreeTable(node)) return QRectF();
        return drawnCovered(model, boundary.topics);
    };
    const QList<Boundary> &boundaries = model->boundaries;
    QList<QRectF> covers;
    for (const Boundary &boundary : boundaries) covers.append(covered(boundary));
    Boundaries::widenAroundNested(boundaries, covers, [model](const Boundary &inner, const Boundary &outer) {
        return std::all_of(inner.topics.cbegin(), inner.topics.cend(), [&](const QString &id) {
            for (const MindNode *node = model->nodeById(id); node; node = node->parent)
                if (outer.topics.contains(node->id)) return true;
            return false;
        });
    });
    const NodeStyle::Backdrop backdrop = model->backdrop();
    for (qsizetype i = 0; i < boundaries.size(); ++i) {
        if (covers[i].isNull()) continue;
        BoundaryShape shape{boundaries[i], Boundaries::strokeRect(covers[i]), {},
                            Boundaries::look(boundaries[i], backdrop, model->sheetStyle.fontFamily), {}};
        if (!shape.boundary.title.isEmpty())
            shape.titleRect = Boundaries::titleRect(shape.stroke, QFontMetricsF(shape.look.font).horizontalAdvance(shape.boundary.title));
        shape.outline = Boundaries::outline(shape.boundary.shape, shape.stroke);
        if (!shape.titleRect.isEmpty()) shape.outline = shape.outline.united(Boundaries::titlePath(shape.titleRect));
        const qreal half = shape.look.pen.widthF() / 2;
        m_bounds = m_bounds.united(shape.frame().adjusted(-half, -half, half, half));
        m_boundaries.append(shape);
    }
    // MindMapScene: a boundary inside another one is drawn above it, by how many hold it.
    QHash<QString, int> depth;
    for (const BoundaryShape &shape : std::as_const(m_boundaries))
        for (const BoundaryShape &other : std::as_const(m_boundaries))
            if (&other != &shape && other.stroke.contains(shape.stroke) && other.stroke != shape.stroke) ++depth[shape.boundary.id];
    std::stable_sort(m_boundaries.begin(), m_boundaries.end(), [&depth](const BoundaryShape &a, const BoundaryShape &b) {
        return depth.value(a.boundary.id) < depth.value(b.boundary.id);
    });
}

const MapSnapshot::BoundaryShape *MapSnapshot::boundaryAt(const QPointF &mapPoint, qreal tolerance) const {
    // BoundaryItem::shape: the filled outline, a band along the border and the tab.
    for (auto it = m_boundaries.crbegin(); it != m_boundaries.crend(); ++it) {
        const qreal band = std::max(tolerance, it->look.pen.widthF() / 2);
        if (it->stroke.adjusted(-band, -band, band, band).contains(mapPoint) || it->titleRect.contains(mapPoint)) return &*it;
    }
    return nullptr;
}

const MapSnapshot::BoundaryShape *MapSnapshot::boundaryOf(const QString &id) const {
    for (const BoundaryShape &shape : m_boundaries)
        if (shape.boundary.id == id) return &shape;
    return nullptr;
}

void MapSnapshot::addRelations(const MindMapModel *model) {
    // MindMapScene::relationEnd: a topic's end is the outline of its shape and its centre.
    QHash<QString, Relationships::End> ends;
    for (const Topic &topic : std::as_const(m_topics)) {
        const QRectF box = topic.geometry->shapeRect().translated(topic.position);
        ends.insert(topic.node->id, {NodeShapes::outline(topic.node->shape, box, topic.geometry->cornerRadius()), box.center()});
    }
    // FEAT-115, MindMapScene::relationEnd: a boundary's end is its drawn outline, tab included.
    for (const BoundaryShape &shape : std::as_const(m_boundaries)) ends.insert(shape.boundary.id, {shape.outline, shape.stroke.center()});
    const NodeStyle::Backdrop backdrop = model->backdrop();
    for (const Relationship &relationship : model->relations) {
        const auto from = ends.constFind(relationship.from);
        const auto to = ends.constFind(relationship.to);
        if (from == ends.cend() || to == ends.cend()) continue;  // hidden
        Relation relation{relationship, Relationships::geometry(relationship, *from, *to),
                          Relationships::look(relationship, backdrop, model->sheetStyle.fontFamily), {}, *from, *to};
        if (!relationship.label.isEmpty())
            relation.labelRect = Relationships::labelRect(relation.geometry.labelPoint, Relationships::labelSize(relation.look.font, relationship.label));
        const qreal margin = relation.look.lineWidth * 3 + BranchPath::kMinEndMarkPx;  // the end marks
        m_bounds = m_bounds.united(relation.geometry.path.boundingRect().adjusted(-margin, -margin, margin, margin)).united(relation.labelRect);
        m_relations.append(relation);
    }
}

const MapSnapshot::Relation *MapSnapshot::relationAt(const QPointF &mapPoint, qreal tolerance) const {
    QPainterPathStroker stroker;
    for (auto it = m_relations.crbegin(); it != m_relations.crend(); ++it) {
        if (it->labelRect.contains(mapPoint)) return &*it;
        stroker.setWidth(2 * tolerance + it->look.lineWidth);
        if (it->geometry.path.boundingRect().adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(mapPoint)
            && stroker.createStroke(it->geometry.path).contains(mapPoint))
            return &*it;
    }
    return nullptr;
}

const MapSnapshot::Relation *MapSnapshot::relationOf(const QString &id) const {
    for (const Relation &relation : m_relations)
        if (relation.relationship.id == id) return &relation;
    return nullptr;
}

void MapSnapshot::place(MindNode *node, const QPointF &inheritedOffset, bool hidden) {
    // MindMapScene::placeBranch: a table cell takes no hand move; a topic the layout does not place
    // (a floating one) keeps its stored position and passes no move on.
    const auto autoPosition = m_layout->autoPositions().constFind(node);
    const bool tableCell = isTableCell(node);
    QPointF offset;
    QPointF position = node->position;
    if (autoPosition != m_layout->autoPositions().cend()) {
        // FEAT-115 step 2: a summary topic stands where its bracket puts it (syncSummaryItems).
        offset = inheritedOffset + (tableCell || node->summaryTopic ? QPointF() : node->manualOffset);
        position = autoPosition.value() + offset;
        if (m_writePositions) node->position = position;
    }
    const qsizetype self = m_topics.size();
    if (!hidden) {
        const NodeGeometry *geometry = m_geometries.at(node).get();
        QRectF pills;  // FEAT-114
        for (const QRectF &pill : geometry->labelRects()) pills = pills.united(pill.translated(position));
        m_topicIndex.insert(node, m_topics.size());
        m_topics.append({node, position, geometry->normalBoundingRect().translated(position), geometry, pills});
        m_bounds = m_bounds.united(pills);
        m_bounds = m_bounds.united(m_topics.last().bounds);
    }
    for (const auto &child : node->children) {
        // MindMapScene::applyCollapse: a collapsed topic hides its branches, not its floating topics.
        const bool childHidden = hidden || (!child->floating && MapLayout::childrenHidden(node));
        const qsizetype childIndex = m_topics.size();
        place(child.get(), offset, childHidden);
        // updateEdge: no line into a floating topic, a summary topic or a table's cell.
        if (!childHidden && !child->floating && !child->summaryTopic && !isTableCell(child.get()))
            addBranch(m_topics.at(self), m_topics.at(childIndex), !node->parent);
    }
}

const MapSnapshot::Topic *MapSnapshot::topicAt(const QPointF &mapPoint) const {
    for (auto it = m_topics.crbegin(); it != m_topics.crend(); ++it)
        if (it->contains(mapPoint)) return &*it;
    return nullptr;
}

const MapSnapshot::Topic *MapSnapshot::topicOf(const MindNode *node) const {
    const auto index = m_topicIndex.constFind(node);
    return index == m_topicIndex.cend() ? nullptr : &m_topics.at(*index);
}

bool MapSnapshot::isTableCell(const MindNode *node) const {
    // MindMapScene::isTableCell.
    const auto cell = m_layout->tableCells().constFind(node->id);
    return (cell != m_layout->tableCells().cend() && cell->owner != node->id) || m_layout->tableFrames().contains(node->id);
}

void MapSnapshot::addBranch(const Topic &parent, const Topic &child, bool fromRoot) {
    // Prototype: the desktop's line for a plain mind map (MindMapScene::updateEdge). The child's side
    // is the half of the map it stands in, the line enters it on the side facing the parent.
    const bool right = child.position.x() >= parent.position.x();
    const QPointF entry = child.position + child.geometry->branchAnchor(!right);
    QPainterPath path;
    if (fromRoot) {
        path = BranchPath::fromRoot(parent.geometry->shapeRect().translated(parent.position), entry);
    } else {
        path = BranchPath::between(parent.position + parent.geometry->branchAnchor(right), entry);
    }
    // The line takes the child's branch colour and the parent's width (nodestyle.h).
    const qreal width = parent.node->branchWidth;
    const QRectF bounds = path.boundingRect().adjusted(-width, -width, width, width);
    m_bounds = m_bounds.united(bounds);
    m_branches.append({path, child.node->lineColor, width, bounds});
}
