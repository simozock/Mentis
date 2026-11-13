#include "mindmapscene.h"

#include "document.h"

#include <QTimer>
#include <QImage>
#include <QBuffer>
#include "branchpath.h"
#include "mapnavigation.h"
#include "mapvariants.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "maplayout.h"
#include "nodeitem.h"
#include "nodeshape.h"
#include "nodesketch.h"
#include "nodestyle.h"
#include "relationshipitem.h"

#include <QGraphicsPathItem>
#include <QGraphicsSceneMouseEvent>
#include <QGuiApplication>
#include <QGraphicsProxyWidget>
#include <QGraphicsView>
#include <QScrollBar>
#include <QGraphicsPolygonItem>
#include <QGraphicsSimpleTextItem>
#include <QPainterPath>
#include <QPainter>
#include <QGraphicsPixmapItem>
#include <QPainterPathStroker>
#include <QLineF>
#include <functional>
#include <optional>
#include <tuple>
#include <utility>
#include <cmath>
#include <algorithm>

using namespace MapLayoutRules;

namespace {
// Ramo tra due nodi. QGraphicsPathItem::boundingRect() con penna di spessore > 0 ricava il
// riquadro dal contorno del tratto (shape(), costruito con QPainterPathStroker) e lo rifa' dopo
// ogni setPath() (https://code.qt.io/cgit/qt/qtbase.git/tree/src/widgets/graphicsview/qgraphicsitem.cpp):
// con 1.000 rami ridisposti costava circa 20 ms, pagati alla prima richiesta dell'area
// (updateSceneRect, indice della scena o disegno). Una curva di Bezier sta dentro l'inviluppo
// convesso dei suoi punti di controllo, quindi il loro riquadro
// (https://doc.qt.io/qt-6/qpainterpath.html#controlPointRect) allargato di meta' spessore contiene
// tutto il tratto, estremi arrotondati compresi. shape() resta quello di Qt: serve solo a trovare
// il ramo sotto il mouse e si calcola su richiesta.
class EdgeItem final : public QGraphicsPathItem {
public:
    QRectF boundingRect() const override {
        // Half a pixel more: antialiasing, and rounding when the stroke reaches the control points
        // exactly (the quadratic branches from the central topic, FEAT-072 A7).
        const qreal halfWidth = (pen().style() == Qt::NoPen ? 0 : pen().widthF() / 2) + 0.5;
        const QRectF line = path().controlPointRect().adjusted(-halfWidth, -halfWidth, halfWidth, halfWidth);
        if (m_filledMarks.isEmpty() && m_strokedMarks.isEmpty()) return line;
        // An empty path's rectangle is the origin: only the marks that exist count.
        QRectF marks;
        for (const QPainterPath *mark : {&m_filledMarks, &m_strokedMarks})
            if (!mark->isEmpty()) marks |= mark->controlPointRect();
        const qreal markHalf = m_markWidth / 2 + 0.5;
        return line | marks.adjusted(-markHalf, -markHalf, markHalf, markHalf);
    }
    // FEAT-023: line ends, filled with the line colour or, for the open arrow, stroked like the line.
    void setMarks(const QPainterPath &filled, const QPainterPath &stroked, const QColor &color, qreal width) {
        if (filled == m_filledMarks && stroked == m_strokedMarks && color == m_markColor && width == m_markWidth) return;
        prepareGeometryChange();
        m_filledMarks = filled;
        m_strokedMarks = stroked;
        m_markColor = color;
        m_markWidth = width;
        update();
    }
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override {
        QGraphicsPathItem::paint(painter, option, widget);
        if (m_filledMarks.isEmpty() && m_strokedMarks.isEmpty()) return;
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_markColor);
        painter->drawPath(m_filledMarks);
        painter->setPen(QPen(m_markColor, m_markWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(m_strokedMarks);
    }

private:
    QPainterPath m_filledMarks, m_strokedMarks;
    QColor m_markColor;
    qreal m_markWidth = 0;
};

constexpr qreal kRootSquareInsetPx = 50;  // FEAT-072: right-angle lines from the central topic (reference 51)
// FEAT-072: curves to underlined main topics start inside the corner by 0.75 x the child's distance above or
// below the central topic (reference photos 2026-10-23: 0.73-0.75 over 7-123 px; a fixed 35 px fitted only close topics).
constexpr qreal kRootUnderlineInsetPerPx = 0.75;
constexpr qreal kTimelineObliqueSlope = 0.577;  // variant 3: the last stretch to a subtopic rises at 30 degrees
// FEAT-028 (reference live 2026-10-25): below a main topic every cell takes its colour mixed 80% with
// white (#FF6B6B -> #FFE1E1).
constexpr qreal kTableTintWhite = 0.8;
QColor solidFill(const MindNode *node) { return node->fill.alpha() > 0 ? node->fill : node->branchColor; }
}

MindMapScene::MindMapScene(MindMapModel *model, QObject *parent) : QGraphicsScene(parent), m_model(model), m_layout(model) {
    // FEAT-096 stage 2: the layout reads the items' measures and, for the summaries, the last placement.
    m_layout.geometryOf = [this](const MindNode *node) -> const NodeGeometry * {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
        return item ? &item->geometry() : nullptr;
    };
    m_layout.nodeById = [this](const QString &id) -> const MindNode * {
        const NodeItem *item = m_itemsById.value(id);
        return item ? item->node() : nullptr;
    };
    m_layout.branchesCovered = [this](const QStringList &topics) { return branchesCovered(topics); };
    setBackgroundBrush(QColor("#f6f7fb"));
    connect(model, &MindMapModel::modelReset, this, &MindMapScene::rebuild);
    // Si aggiungono/rimuovono solo gli item dei nodi coinvolti (prima ogni inserimento o
    // rimozione ricostruiva tutta la scena).
    connect(model, &MindMapModel::rowsInserted, this, &MindMapScene::onRowsInserted);
    connect(model, &MindMapModel::rowsAboutToBeRemoved, this, &MindMapScene::onRowsAboutToBeRemoved);
    connect(model, &MindMapModel::rowsRemoved, this, &MindMapScene::onRowsRemoved);
    // B8 (2026-10-13): rientro, riduci rientro e trascinamento nell'outliner ora spostano le
    // righe invece di resettare il modello. Gli item restano gli stessi: basta ricollegare
    // il ramo entrante dei nodi spostati al nuovo genitore.
    connect(model, &MindMapModel::rowsMoved, this, &MindMapScene::onRowsMoved);
    // FEAT-052: a summary topic's branch has no rows.
    connect(model, &MindMapModel::summaryBranchAboutToChange, this, &MindMapScene::onSummaryBranchAboutToChange);
    connect(model, &MindMapModel::summaryBranchChanged, this, &MindMapScene::onSummaryBranchChanged);
    // Modifica del titolo da outliner o editor: aggiorna solo il nodo cambiato.
    connect(model, &MindMapModel::dataChanged, this, [this](const QModelIndex &topLeft, const QModelIndex &bottomRight) {
        for (int row = topLeft.row(); row <= bottomRight.row(); ++row)
            updateNode(m_model->nodeFromIndex(topLeft.siblingAtRow(row)));
    });
    // FEAT-019: colori o rami cambiati dalla cascata (palette, stile scelto, nodi spostati). Gli
    // aggiornamenti dei rami, delle annotazioni e dell'area della scena si fanno una volta sola
    // alla fine, non per ogni nodo (setDeferredUpdates), anche con migliaia di nodi.
    connect(model, &MindMapModel::stylesChanged, this, &MindMapScene::refreshChangedNodes);
    // FEAT-027: un'icona di collegamento in piu' o in meno cambia la misura del nodo.
    connect(model, &MindMapModel::linksEdited, this, &MindMapScene::refreshChangedNodes);
    rebuild();
}

void MindMapScene::refreshChangedNodes(const QList<MindNode *> &nodes) {
    const bool alreadyDeferred = m_deferredUpdates;
    if (!alreadyDeferred) setDeferredUpdates(true);
    // FEAT-020 parte B: forma e spessore del bordo cambiano la misura del nodo; allora la mappa
    // si ridispone, come dopo un ridimensionamento a mano.
    bool resized = false;
    for (MindNode *node : nodes) {
        updateNode(node);
        if (const NodeItem *item = m_items.value(node); item && item->lastSyncChangedGeometry()) resized = true;
        // FEAT-072: a new branch shape moves the children (fans sit further out) and redraws their lines.
        if (node->branchShape != m_laidOutShapes.value(node, BranchShape::Auto)) resized = true;
    }
    // Con la ridisposizione i rami si ricalcolano tutti in autoLayout: non serve farlo due volte.
    if (resized && !alreadyDeferred) m_deferredNodes.clear();
    if (!alreadyDeferred) setDeferredUpdates(false);
    if (resized) autoLayout();
}

void MindMapScene::refreshTopicLinkStates(const std::function<bool(const QString &targetId)> &exists) {
    for (NodeItem *item : std::as_const(m_items)) {
        const QString &target = item->node()->topicLink;
        item->setTopicLinkBroken(!target.isEmpty() && !exists(target));
    }
}

QRectF MindMapScene::nodeSceneRect(const MindNode *node) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item ? item->sceneBoundingRect() : QRectF();
}

QRectF MindMapScene::nodeShapeSceneRect(const MindNode *node) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item && item->isVisible() ? item->mapRectToScene(item->shapeRect()) : QRectF();
}

void MindMapScene::refreshTableCells() {
    if (!m_layout.tableCells().isEmpty()) computeAutoPositions();  // with no table, applyTableCells finds a new one
    applyTableCells();
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) updateEdge(it.key());
}

QRectF MindMapScene::nodeOutlineSceneRect(const MindNode *node) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    if (item && item->isVisible() && item->tableCell()) return item->mapRectToScene(item->tableCell()->rect);
    return nodeShapeSceneRect(node);
}

bool MindMapScene::isEditing(const MindNode *node) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item && item->isEditing();
}

QRectF MindMapScene::collapseSceneRect(const MindNode *node, int side) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    if (!item || !item->isVisible()) return {};
    const QRectF rect = item->collapseRect(side);
    return rect.isEmpty() ? QRectF() : item->mapRectToScene(rect);
}

bool MindMapScene::isTopicLinkBroken(const MindNode *node) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item && item->isTopicLinkBroken();
}

QRectF MindMapScene::linkIconSceneRect(const MindNode *node, NodeItem::LinkKind kind) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    if (!item) return {};
    const QRectF icon = item->linkIconRect(kind);
    return icon.isEmpty() ? icon : item->mapRectToScene(icon);
}

MindNode *MindMapScene::selectedNode() const {
    for (auto *item : selectedItems()) if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item)) return nodeItem->node();
    return nullptr;
}

QList<MindNode *> MindMapScene::selectedNodes() const {
    QList<MindNode *> nodes;
    for (auto *item : selectedItems()) if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item)) nodes.append(nodeItem->node());
    return nodes;
}

void MindMapScene::rebuild() {
    m_labelHighlight.clear();  // FEAT-060: gli elementi nuovi nascono opachi al 100%
    clear();
    m_reorderGhost = nullptr;  // FEAT-015: distrutti da clear()
    m_reorderIndicator = nullptr;
    m_items.clear();
    m_itemsById.clear();
    m_edgeByChild.clear();
    m_edges.clear();
    m_annotations.clear();
    m_relationItems.clear();
    m_boundaryItems.clear();  // FEAT-052: deleted by clear()
    m_summaryItems.clear();
    addNodeRecursive(m_model->root());
    refreshStacking();
    // A saved map keeps its positions, but sides and collapse controls come from the layout: without
    // this an opened file showed no collapse circle (and its knob side) until the next autoLayout.
    computeAutoPositions();
    applyTableCells();  // FEAT-028: before the lines, which a table leaves out
    drawConnections(m_model->root());
    drawAnnotations();
    applyCollapse();
    setSceneRect(itemsBoundingRect().adjusted(-300, -300, 300, 300));
}

void MindMapScene::addNodeRecursive(MindNode *node) {
    auto *item = new NodeItem(node);
    item->setStackZ(stackZ(node));  // FEAT-057: a topic added to a branch joins its place in the stack
    addItem(item);
    m_items.insert(node, item);
    m_itemsById.insert(node->id, item);
    connect(item, &NodeItem::activated, this, [this](NodeItem *i) { emit nodeSelected(i->node()); });
    // Prima: refreshConnections() ricreava tutti i rami e le annotazioni a ogni
    // movimento del mouse. Ora si aggiornano solo i rami del nodo spostato.
    connect(item, &NodeItem::moved, this, &MindMapScene::onNodeMoved);
    connect(item, &NodeItem::dragFinished, this, &MindMapScene::onDragFinished);  // B7
    item->reorderDrag = [this](const NodeItem *i) { return isReorderDrag(i->node()); };  // FEAT-015
    connect(item, &NodeItem::reorderMoved, this, &MindMapScene::onReorderMoved);
    connect(item, &NodeItem::reorderFinished, this, &MindMapScene::onReorderFinished);
    connect(item, &NodeItem::collapseToggled, this, [this](NodeItem *i) { emit collapseRequested(i->node()); });
    // Durante il trascinamento anche i rami del nodo (entrante e uscenti) diventano
    // trasparenti come il nodo; al rilascio tornano opachi.
    connect(item, &NodeItem::dragFadeChanged, this, [this](NodeItem *i, bool active) {
        const qreal opacity = active ? NodeItem::kDragOpacity : 1.0;
        if (auto *edge = m_edgeByChild.value(i->node())) edge->setOpacity(opacity);
        for (const auto &child : i->node()->children)
            if (auto *edge = m_edgeByChild.value(child.get())) edge->setOpacity(opacity);
        // BUG-013: inizio e fine del trascinamento di un nodo (vedi collectFollowers).
        if (active) collectFollowers(i);
        else m_dragFollowers.clear();
    });
    connect(item, &NodeItem::imagePlacementChanged, this, [this](NodeItem *i) {
        updateNode(i->node()); emit nodeChanged();
    });
    connect(item, &NodeItem::historyStepFinished, this, &MindMapScene::historyStepFinished);
    // Larghezza cambiata trascinando un lato (2026-10-13) o tornata automatica con il doppio clic
    // sul bordo (FEAT-018): il nodo può essere diventato più alto o più basso, quindi si ridispone
    // la mappa (come dopo l'aggiunta di un nodo); structureChanged aggiorna storico e stato
    // "Modificato". historyStepFinished registra subito il passo, così una modifica fatta entro
    // l'attesa dello storico non si unisce al ridimensionamento. L'outliner non cambia.
    // BUG-004 (2026-10-15): la vista inquadra il nodo appena ridimensionato. Dopo il ritorno
    // all'automatica il nodo si accorcia e la vista, rimasta dove l'utente l'aveva portata per
    // raggiungere il bordo, lo lasciava fuori o lontano. ensureVisible scorre solo se serve e
    // non usa centerOn() (AGENTS.md); il margine di 24 px e' della griglia di AGENTS.md.
    connect(item, &NodeItem::resizeFinished, this, [this](NodeItem *i) {
        autoLayout();
        emit m_model->structureChanged();
        emit historyStepFinished();
        for (QGraphicsView *view : views()) view->ensureVisible(i, 24, 24);
    });
    connect(item, &NodeItem::titleEdited, this, [this](NodeItem *i, const QString &title, const QString &html) {
        const qreal halfBefore = i->anchorHalfWidth();  // FEAT-034
        const QSizeF sizeBefore = i->shapeRect().size();
        // The document's dataChanged or nodesChanged updates the item (once: P8, 2026-10-13) and its
        // structureChanged the outliner, the history and the "Modified" state.
        if (!m_document || !m_document->editTitle(i->node(), title, html)) return;
        // FEAT-034: con il titolo nuovo il lato verso il genitore resta dov'era (nello stesso passo
        // della modifica; il layout seguente lo tiene li').
        i->keepParentSide(halfBefore);
        // A new size moves the topics around it (FEAT-072): e.g. a central topic going to two
        // lines pushes the main topics clear of it. Before, only its branches followed.
        if (i->shapeRect().size() != sizeBefore) autoLayout();
        else updateEdgesFor(i->node());
    });
    connect(item, &NodeItem::editingResized, this, &MindMapScene::onEditingResized);
    connect(item, &NodeItem::imageSelectionChanged, this, &MindMapScene::imageSelectionChanged);
    connect(item, &NodeItem::taskToggled, this, [this](NodeItem *i) { emit taskToggled(i->node()); });
    connect(item, &NodeItem::noteActivated, this, [this](NodeItem *i) { emit noteActivated(i->node()); });
    connect(item, &NodeItem::labelActivated, this, [this](NodeItem *i) { emit labelActivated(i->node()); });
    connect(item, &NodeItem::taskTyped, this, [this](NodeItem *i, int task) { emit taskTyped(i->node(), task); });
    connect(item, &NodeItem::filesActivated, this, [this](NodeItem *i, const QPoint &screenPos) { emit filesActivated(i->node(), screenPos); });
    connect(item, &NodeItem::formulaActivated, this, [this](NodeItem *i) { emit formulaActivated(i->node()); });
    connect(item, &NodeItem::formulaMenuRequested, this, [this](NodeItem *i, const QPoint &screenPos) { emit formulaMenuRequested(i->node(), screenPos); });
    connect(item, &NodeItem::markerClicked, this, [this](NodeItem *i, const QString &marker) { emit markerClicked(i->node(), marker); });
    connect(item, &NodeItem::imageMenuRequested, this, [this](NodeItem *i, const QPoint &screenPos) { emit imageMenuRequested(i->node(), screenPos); });
    connect(item, &NodeItem::markerMenuRequested, this, [this](NodeItem *i, const QString &marker, const QPoint &screenPos) {
        emit markerMenuRequested(i->node(), marker, screenPos);
    });
    connect(item, &NodeItem::labelMenuRequested, this, [this](NodeItem *i, const QString &label, const QPoint &screenPos) {
        emit labelMenuRequested(i->node(), label, screenPos);
    });
    connect(item, &NodeItem::taskMenuRequested, this, [this](NodeItem *i, const QPoint &screenPos) { emit taskMenuRequested(i->node(), screenPos); });
    connect(item, &NodeItem::nodeMenuRequested, this, [this](NodeItem *i, const QPoint &screenPos) { emit nodeMenuRequested(i->node(), screenPos); });
    connect(item, &NodeItem::linkActivated, this, [this](NodeItem *i, NodeItem::LinkKind kind) { emit linkActivated(i->node(), kind); });
    connect(item, &NodeItem::linkMenuRequested, this, [this](NodeItem *i, NodeItem::LinkKind kind, const QPoint &screenPos) {
        emit linkMenuRequested(i->node(), kind, screenPos);
    });
    for (const auto &child : node->children) addNodeRecursive(child.get());
}

void MindMapScene::drawConnections(MindNode *node) {
    auto *from = m_items.value(node);
    for (const auto &child : node->children) {
        if (child->floating) { drawConnections(child.get()); continue; }
        Q_UNUSED(from);
        // La geometria del ramo è calcolata in updateEdge(), condivisa con gli
        // aggiornamenti incrementali.
        updateEdge(child.get());
        drawConnections(child.get());
    }
}

// Non più chiamata (vedi updateEdgesFor). Attenzione: non aggiorna m_edgeByChild,
// quindi non va riutilizzata così com'è. Da rimuovere dopo conferma.
void MindMapScene::refreshConnections() {
    for (auto *edge : std::as_const(m_edges)) delete edge;
    for (auto *item : std::as_const(m_annotations)) delete item;
    m_edges.clear();
    m_annotations.clear();
    drawConnections(m_model->root());
    drawAnnotations();
    invalidate(sceneRect(), QGraphicsScene::AllLayers);
}

void MindMapScene::drawAnnotations() {
    syncBoundaryItems();  // before the relationships, whose ends may be boundaries or summary topics
    syncSummaryItems();
    syncRelationItems();
}

Summaries::Side MindMapScene::summarySide(const Summary &summary, const QRectF &covered, const QPointF &parentCentre) const {
    // The siblings form a row (org chart, timeline, fishbone) or a column (the other structures): the
    // run's own topics tell, else all the siblings, else where the one child is from its parent.
    const auto centres = [this](const QStringList &ids) {
        QList<QPointF> list;
        for (const QString &id : ids)
            if (const NodeItem *item = m_itemsById.value(id); item && item->isVisible()) list.append(item->pos());
        return list;
    };
    QList<QPointF> points = centres(summary.topics);
    if (points.size() < 2) {
        // Only the siblings on the same side: the two sides of a mind map are not one row.
        const NodeItem *first = m_itemsById.value(summary.topics.front());
        const int side = first ? sideOf(first->node()) : 1;
        QStringList siblings;
        for (const auto &child : summary.topic->parent->children)
            if (!child->floating && sideOf(child.get()) == side) siblings.append(child->id);
        points = centres(siblings);
    }
    // A fishbone's main topics sit above and below the spine in turn: they are one row along it.
    return Summaries::sideFor(points, covered, parentCentre, setsFishbone(summary.topic->parent));
}

void MindMapScene::syncSummaryItems() {
    QSet<QString> present;
    // Placing a summary topic is not a move by the user: no nodeChanged, no annotation refresh.
    const bool wasBatch = std::exchange(m_batchUpdate, true);
    // FEAT-036: on a backdrop other than white the bracket is light or dark by contrast.
    const QColor ink = NodeStyle::annotationColor(Qt::black, m_model->backdrop());
    for (const Summary &summary : std::as_const(m_model->summaries)) {
        present.insert(summary.id);
        SummaryItems &items = m_summaryItems[summary.id];
        if (items.topic != summary.topic) {
            if (items.topic) removeNodeItems(items.topic.get());
            items.topic = summary.topic;
            addNodeRecursive(items.topic.get());
        }
        if (!items.bracket) {
            items.bracket = addPath(QPainterPath());
            items.bracket->setZValue(-2);  // under the topics, above the boundaries
        }
        NodeItem *item = m_items.value(items.topic.get());
        const QRectF covered = branchesCovered(summary.topics);
        const NodeItem *parentItem = m_items.value(items.topic->parent);
        // The summaries of a tree table are hidden (and none can be added).
        const bool shown = item && parentItem && !covered.isNull()
                        && MapStructure::typeOf(items.topic->parent) != MapStructure::Type::TreeTable
                        && !MapStructure::insideTreeTable(items.topic->parent);  // FEAT-028 stage 2: nor in its cells
        items.bracket->setVisible(shown);
        if (!shown) {
            if (item) collapseBranch(items.topic.get(), true);
            continue;
        }
        const Summaries::Side side = summarySide(summary, covered, parentItem->pos());
        items.bracket->setPen(QPen(ink, Summaries::kLineWidth, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin));
        items.bracket->setPath(Summaries::bracket(covered, side));
        item->syncFromNode();  // its size, for the place
        items.topic->position = summaryTopicPosition(covered, side, item->shapeRect());
        item->syncFromNode();
        // Its branch grows away from the bracket (to the right below and above a row).
        const int sign = side == Summaries::Side::Left ? -1 : 1;
        m_layout.layoutSummaryBranch(items.topic.get(), sign);
        const std::function<void(MindNode *)> place = [&](MindNode *node) {
            for (const auto &child : node->children) {
                if (NodeItem *childItem = m_items.value(child.get())) {
                    childItem->setGrowSide(m_layout.sides().value(child.get(), sign));
                    childItem->syncFromNode();
                }
                updateEdge(child.get());
                place(child.get());
            }
        };
        for (const auto &child : items.topic->children) placeBranch(child.get(), QPointF());
        place(items.topic.get());
        collapseBranch(items.topic.get(), false);
    }
    for (auto it = m_summaryItems.begin(); it != m_summaryItems.end();) {
        if (present.contains(it.key())) { ++it; continue; }
        removeNodeItems(it->topic.get());
        delete it->bracket;
        it = m_summaryItems.erase(it);
    }
    m_batchUpdate = wasBatch;
}

void MindMapScene::onSummaryBranchAboutToChange(MindNode *topic) {
    for (const auto &child : topic->children) removeNodeItems(child.get());
}

void MindMapScene::onSummaryBranchChanged(MindNode *topic) {
    if (!m_items.contains(topic)) return;  // its items come with the summary's (syncSummaryItems)
    for (const auto &child : topic->children) addNodeRecursive(child.get());
}

QByteArray MindMapScene::homePreviewJpeg(const QHash<QString, QJsonObject> &sheet) {
    MindMapModel previewModel;
    if (!previewModel.fromFlatSnapshot(sheet)) return {};
    MindMapScene previewScene(&previewModel);
    // The sheet's background, as on screen: a variant with a dark page (Green tea, dark green) was
    // drawn on white before (user, 2026-11-02).
    const QColor page = previewModel.backdrop().color;
    previewScene.setBackgroundBrush(page);
    const QRectF content = previewScene.itemsBoundingRect();
    if (content.isEmpty()) return {};
    QImage preview(kPreviewWidth, kPreviewHeight, QImage::Format_RGB32);
    preview.fill(page);
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    previewScene.render(&painter, QRectF(16, 16, kPreviewWidth - 32, kPreviewHeight - 32), content, Qt::KeepAspectRatio);
    painter.end();
    return previewJpeg(preview);
}

QByteArray MindMapScene::previewJpeg(const QImage &preview) {
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    preview.save(&buffer, "JPG", 82);
    return jpeg;
}

void MindMapScene::autoLayout() {
    // Le dimensioni dei nodi (che dipendono dalle formule) devono essere aggiornate
    // prima di calcolare le distanze del layout.
    // P2: syncFromNode() ora rimisura solo i nodi il cui contenuto è cambiato.
    m_laidOutShapes.clear();
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
        if (it.key()->branchShape != BranchShape::Auto) m_laidOutShapes.insert(it.key(), it.key()->branchShape);
    syncAllItems();
    justifyLevels();
    m_layout.collectBoundaryRuns();  // FEAT-052: after the sizes, which a summary topic's branch needs
    // B7 (2026-10-13): il layout calcola il posto "automatico" di ogni nodo; poi si aggiunge lo
    // spostamento fatto a mano (manualOffset), così un nodo trascinato resta dove l'hai messo
    // rispetto al suo posto anche quando la mappa cambia. I nodi flottanti non sono toccati.
    // BUG-013 (2026-10-16): lo spostamento vale per tutto il ramo. Prima si aggiungeva solo quello
    // del nodo, cosi' figli e nipoti di un nodo trascinato restavano al posto automatico e i figli
    // nuovi nascevano lontano dal genitore.
    computeAutoPositions();
    placeBranch(m_model->root(), QPointF());
    refreshAll();
    applyCollapse();
}

// FEAT-043 Justify (user's decision 2026-10-24): the shown nodes of each level are as wide as the
// widest of that level across the sheet, up to the hand-width cap; a width set by hand is widened
// too. Floating topics, and the nodes an Org chart or Tree table lays out (the grid will join them),
// are left out with their whole subtree.
void MindMapScene::justifyLevels() {
    QHash<MindNode *, qreal> widths;
    if (m_model->sheetStyle.justify) {
        QList<QList<std::pair<NodeItem *, qreal>>> levels;
        const std::function<void(MindNode *, int)> visit = [&](MindNode *node, int depth) {
            const MapStructure::Type type = MapStructure::typeOf(node);
            if (childrenHidden(node) || type == MapStructure::Type::OrgChart || type == MapStructure::Type::TreeTable) return;
            for (const auto &child : node->children) {
                NodeItem *item = m_items.value(child.get());
                if (child->floating || !item) continue;
                if (levels.size() <= depth) levels.resize(depth + 1);
                levels[depth].append({item, item->naturalWidth()});
                visit(child.get(), depth + 1);
            }
        };
        visit(m_model->root(), 0);
        for (const auto &level : std::as_const(levels)) {
            qreal widest = 0;
            for (const auto &[item, natural] : level) widest = std::max(widest, natural);
            widest = std::min(widest, kMaxManualNodeWidthPx);
            // The widest node keeps its own layout: as a hand width its text would wrap 10 px earlier.
            for (const auto &[item, natural] : level)
                if (widest > natural + 0.5) widths.insert(item->node(), widest);
        }
    }
    m_batchUpdate = true;  // as in syncAllItems: one nodeChanged() for the whole pass
    m_batchMoved = false;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) it.value()->setJustifiedWidth(widths.value(it.key(), 0));
    m_batchUpdate = false;
    if (m_batchMoved) emit nodeChanged();
}

bool MindMapScene::isTableCell(const MindNode *node) const {
    const auto it = m_layout.tableCells().constFind(node->id);
    return (it != m_layout.tableCells().cend() && it->owner != node->id) || m_layout.tableFrames().contains(node->id);
}

// FEAT-028: colours from the topics as they are now (a fill set by hand on a cell wins), so a style
// change needs no new layout. A step of the history restores positions without a layout: when the
// cells no longer match the types, they are worked out again (the auto positions only).
bool MindMapScene::tablePlaceStale(const MindNode *node) const {
    const auto place = m_layout.tableCells().constFind(node->id);
    if (place == m_layout.tableCells().cend()) {
        if (!MapStructure::inTreeTable(node)) return false;
        for (const MindNode *up = node; up; up = up->parent)
            if (up->floating || (up != node && childrenHidden(up))) return false;  // not laid out in the table
        return true;  // a laid-out cell without its place
    }
    return place->owner == node->id ? !setsTreeTable(node) : !MapStructure::inTreeTable(node);
}

void MindMapScene::applyTableCells() {
    const bool stale = std::any_of(m_items.cbegin(), m_items.cend(), [this](const NodeItem *item) { return tablePlaceStale(item->node()); });
    if (stale) computeAutoPositions();
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        const auto place = m_layout.tableCells().constFind(it.key()->id);
        if (place == m_layout.tableCells().cend()) {
            it.value()->setTableCell(std::nullopt);
            continue;
        }
        const MindNode *node = it.key();
        const auto colours = [this, node](const TablePlace &at) {
            const NodeItem *ownerItem = m_itemsById.value(at.owner);
            const NodeItem *levelOneItem = m_itemsById.value(at.levelOne);
            QColor fill = solidFill(node);
            if (levelOneItem && levelOneItem->node() != node && !node->style.fill) {
                const QColor main = solidFill(levelOneItem->node());
                const auto tint = [](qreal channel) { return channel + (1 - channel) * kTableTintWhite; };
                fill = QColor::fromRgbF(tint(main.redF()), tint(main.greenF()), tint(main.blueF()));
            }
            // A table in a cell keeps the outer table's borders: its own colour is the cell's, so
            // its borders would not show.
            QString outer = at.owner;
            for (auto frame = m_layout.tableFrames().constFind(outer); frame != m_layout.tableFrames().cend(); frame = m_layout.tableFrames().constFind(outer))
                outer = frame->owner;
            const NodeItem *outerItem = m_itemsById.value(outer);
            const MindNode *borderFrom = outerItem ? outerItem->node() : ownerItem ? ownerItem->node() : node;
            return std::pair{fill, solidFill(borderFrom)};
        };
        const auto [fill, border] = colours(*place);
        NodeItem::TableCell cell{place->rect, fill, border, place->holdsBranch};
        if (const auto frame = m_layout.tableFrames().constFind(node->id); frame != m_layout.tableFrames().cend()) {
            std::tie(cell.frameFill, cell.frameBorder) = colours(*frame);
            cell.frame = frame->rect;
            cell.holdsBranch = true;
        }
        it.value()->setTableCell(cell);
    }
}

// FEAT-016: un nodo e' nascosto se sta sotto un nodo chiuso; con lui
// il suo ramo. Ogni nodo con figli riceve il controllo dal lato dei figli: vuoto, oppure con il
// numero dei discendenti nascosti (tutti, a ogni livello). Costo: una visita della mappa per layout.
void MindMapScene::applyCollapse() {
    collapseBranch(m_model->root(), false);
    syncSummaryItems();  // FEAT-052: a summary over hidden topics is hidden with them
    syncRelationItems();  // FEAT-053: a relationship to a hidden topic is hidden with it
}

void MindMapScene::collapseBranch(MindNode *root, bool rootHidden) {
    const std::function<int(const MindNode *)> descendants = [&descendants](const MindNode *node) {
        int count = 0;
        for (const auto &child : node->children) if (!child->floating) count += 1 + descendants(child.get());
        return count;
    };
    const std::function<void(MindNode *, bool)> visit = [&](MindNode *node, bool hidden) {
        NodeItem *item = m_items.value(node);
        if (item) {
            if (hidden && item->isSelected()) item->setSelected(false);
            item->setVisible(!hidden);
        }
        if (auto *edge = m_edgeByChild.value(node)) edge->setVisible(!hidden);
        NodeItem::CollapseControl left, right;
        // Il centrale non si chiude (decisione dell'utente 2026-10-21).
        const auto firstChild = std::find_if(node->children.cbegin(), node->children.cend(), [](const auto &child) { return !child->floating; });
        if (node->parent && firstChild != node->children.cend()) {  // FEAT-052: a summary topic closes too (user, 2026-11-12)
            // Children above or below (org chart, tree trunk; 2026-10-24): the control sits on the junction
            // there, on the elbow's bar in the org chart. Elsewhere beside the topic, on the children's side.
            const MapStructure::Type type = MapStructure::typeOf(node);
            const bool org = type == MapStructure::Type::OrgChart;
            const bool trunk = (type == MapStructure::Type::TreeChart && MapStructure::typeFromKey(node->structure) == MapStructure::Type::TreeChart)
                            || setsTimeline(node->parent);  // FEAT-026 phase E: on the timeline's stem
            bool down = true;
            if (const NodeItem *childItem = m_items.value(firstChild->get()); childItem && item) down = childItem->pos().y() >= item->pos().y();
            NodeItem::CollapseControl &control = (org || trunk) ? (down ? right : left)
                                                                : ((node->floating || sideOf(node) > 0) ? right : left);
            control.vertical = org || trunk;
            control.reach = org ? BranchPath::kElbowRunPx : NodeItem::kCollapseOpenOffsetPx;
            control.shown = true;
            control.color = node->lineColor;
            if (node->collapsed) control.hidden = descendants(node);
        }
        if (item) item->setCollapseControls(hidden ? NodeItem::CollapseControl{} : left, hidden ? NodeItem::CollapseControl{} : right);
        for (const auto &child : node->children) visit(child.get(), hidden || (!child->floating && childrenHidden(node)));
    };
    visit(root, rootHidden);
}

void MindMapScene::computeAutoPositions() {
    m_layout.computeAutoPositions();
    // FEAT-034: ogni nodo si allarga dal lato lontano dal genitore (centrale e nodi liberi dai due lati).
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        const MindNode *node = it.key();
        it.value()->setGrowSide(node->floating || !node->parent ? 0 : m_layout.sideOf(node));
    }
}

void MindMapScene::placeBranch(MindNode *node, QPointF inheritedOffset) {
    const auto autoPosition = m_layout.autoPositions().constFind(node);
    if (autoPosition == m_layout.autoPositions().cend()) return;  // nodo flottante: resta dov'e', con il suo ramo
    // FEAT-028: a cell stays in its table (no free position there).
    const QPointF offset = inheritedOffset + (isTableCell(node) ? QPointF() : node->manualOffset);
    node->position = autoPosition.value() + offset;
    for (const auto &child : node->children) placeBranch(child.get(), offset);
}

QPointF MindMapScene::inheritedOffset(const MindNode *node) {
    QPointF offset;
    for (const MindNode *ancestor = node->parent; ancestor; ancestor = ancestor->parent) offset += ancestor->manualOffset;
    return offset;
}

void MindMapScene::collectFollowers(NodeItem *dragged) {
    // BUG-013: mentre si trascina, i discendenti seguono il nodo selezionato piu' vicino sopra di
    // loro, alla stessa distanza (onNodeMoved). Quelli selezionati li muove gia' Qt insieme agli
    // altri selezionati, e i loro discendenti seguono loro. I nodi flottanti restano fermi.
    QList<Follower> &followers = m_dragFollowers[dragged->node()];
    followers.clear();
    std::function<void(const MindNode *)> visit = [&](const MindNode *node) {
        for (const auto &child : node->children) {
            NodeItem *item = m_items.value(child.get());
            if (child->floating || !item || item->isSelected()) continue;
            followers.append({item, item->pos() - dragged->pos()});
            visit(child.get());
        }
    };
    visit(dragged->node());
}

void MindMapScene::onDragFinished(NodeItem *item) {
    // B7: un trascinamento (non un clic) diventa uno spostamento manuale per tutti i nodi
    // selezionati, che si muovono insieme. Si salva la distanza dal posto automatico.
    if (item->wasDragged()) {
        computeAutoPositions();
        QList<NodeItem *> moved{item};
        for (auto *selected : selectedItems())
            if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(selected); nodeItem && nodeItem != item) moved.append(nodeItem);
        // BUG-013: la distanza si misura dal posto automatico gia' spostato con gli antenati, quindi
        // prima gli antenati (un nodo selezionato insieme a un suo discendente).
        auto depth = [](const MindNode *node) { int d = 0; for (; node->parent; node = node->parent) ++d; return d; };
        std::sort(moved.begin(), moved.end(), [&](const NodeItem *a, const NodeItem *b) { return depth(a->node()) < depth(b->node()); });
        // Each offset is written before the next one is measured: a descendant's inherited offset
        // reads its ancestors' new ones.
        for (auto *nodeItem : std::as_const(moved)) {
            MindNode *node = nodeItem->node();
            const auto autoPosition = m_layout.autoPositions().constFind(node);
            const QPointF offset = node->floating || autoPosition == m_layout.autoPositions().cend()
                ? node->manualOffset
                : node->position - autoPosition.value() - inheritedOffset(node);
            if (m_document) m_document->placeTopics({{node, node->position, offset}});
        }
    }
    updateSceneRect();
}

MindNode *MindMapScene::neighbour(MindNode *node, Qt::Key key) const {
    MapNavigation::Scene scene;
    scene.layout = &m_layout;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) scene.nodes.append(it.key());
    scene.shown = [this](const MindNode *at) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(at));
        return item && item->isVisible();
    };
    scene.centre = [this](const MindNode *at) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(at));
        return item ? item->pos() : QPointF();
    };
    scene.box = [this](const MindNode *at) { return nodeOutlineSceneRect(at); };
    return MapNavigation::neighbour(scene, node, key);
}

QPainterPath MindMapScene::edgePath(const MindNode *child) const {
    const QGraphicsPathItem *edge = m_edgeByChild.value(const_cast<MindNode *>(child));
    return edge && edge->isVisible() ? edge->path() : QPainterPath();
}

bool MindMapScene::isReorderDrag(const MindNode *node) const {
    // FEAT-043: con "Rami in posizione libera" il trascinamento e' libero anche con il bilanciamento.
    // FEAT-028: a table cell has no free position, so dragging it always moves it in the structure.
    if (node && isTableCell(node)) return true;
    return node && !node->floating && m_model->balanced && !m_model->sheetStyle.freeBranches &&
           MapStructure::directionOf(m_model->root()) == MapStructure::Direction::BothSides;
}

ReorderTarget MindMapScene::reorderTargetAt(const MindNode *dragged, const QPointF &scenePos) const {
    ReorderTarget::Scene scene;
    scene.model = m_model;
    scene.layout = &m_layout;
    scene.nodes = m_items.keys();
    scene.box = [this](const MindNode *node) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
        if (item && item->tableCell()) return item->mapRectToScene(item->tableCell()->rect);  // FEAT-028
        return item ? item->mapRectToScene(item->shapeRect()) : QRectF();
    };
    scene.shown = [this](const MindNode *node) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
        return item && item->isVisible();
    };
    scene.branchStart = [this](const MindNode *node, bool right) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
        return item->pos() + item->branchAnchor(right);
    };
    return ReorderTarget::at(scene, dragged, scenePos);
}

void MindMapScene::onReorderMoved(NodeItem *item, const QPointF &scenePos) {
    const QColor color = NodeItem::selectionStyle().selectedColor;
    if (!m_reorderGhost) {
        // Il nodo vero, semitrasparente, sotto il mouse. Doppia risoluzione: resta
        // nitido fino allo zoom 200%.
        auto *ghost = new QGraphicsPixmapItem(item->dragPreview(2.0));
        ghost->setOffset(item->boundingRect().topLeft());
        ghost->setOpacity(ReorderTarget::kGhostOpacity);
        ghost->setZValue(1000);
        ghost->setAcceptedMouseButtons(Qt::NoButton);
        m_reorderGhost = ghost;
        addItem(m_reorderGhost);
        // L'anteprima resta attaccata al mouse nel punto in cui il nodo e' stato preso
        // (il primo movimento e' appena oltre la soglia di trascinamento).
        m_reorderGrabOffset = item->pos() - scenePos;
    }
    m_reorderGhost->setPos(scenePos + m_reorderGrabOffset);
    if (!m_reorderIndicator) {
        m_reorderIndicator = addPath(QPainterPath(), Qt::NoPen, color);  // ramo e segnaposto gia' pieni
        m_reorderIndicator->setZValue(1001);  // sopra l'anteprima: il segnaposto resta visibile
        m_reorderIndicator->setAcceptedMouseButtons(Qt::NoButton);
    }
    const ReorderTarget target = reorderTargetAt(item->node(), scenePos);
    QPainterPath path;
    if (target.kind == ReorderTarget::Kind::Child || target.kind == ReorderTarget::Kind::Insert) {
        // Ramo azzurro dal genitore al segnaposto (curva come i rami) e segnaposto pieno.
        const bool right = target.slot.center().x() > target.from.x();
        const QPointF end(right ? target.slot.left() : target.slot.right(), target.slot.center().y());
        QPainterPath branch(target.from);
        const qreal middle = (target.from.x() + end.x()) / 2;
        branch.cubicTo({middle, target.from.y()}, {middle, end.y()}, end);
        QPainterPath slot;
        slot.addRoundedRect(target.slot, ReorderTarget::kSlotRadiusPx, ReorderTarget::kSlotRadiusPx);
        path = QPainterPathStroker(QPen(color, ReorderTarget::kBranchWidthPx, Qt::SolidLine, Qt::RoundCap)).createStroke(branch).united(slot);
    }
    m_reorderIndicator->setPath(path);
}

void MindMapScene::onReorderFinished(NodeItem *item, const QPointF &scenePos, bool dropped) {
    delete std::exchange(m_reorderGhost, nullptr);
    delete std::exchange(m_reorderIndicator, nullptr);
    if (!dropped) return;
    const ReorderTarget target = reorderTargetAt(item->node(), scenePos);
    if (target.kind == ReorderTarget::Kind::Float) emit floatRequested(item->node(), scenePos + m_reorderGrabOffset);
    else if (target.kind != ReorderTarget::Kind::None) emit reorderRequested(item->node(), target.parent, target.row, target.side);
}

void MindMapScene::setDeferredUpdates(bool deferred) {
    m_deferredUpdates = deferred;
    if (deferred) return;
    for (auto *node : std::as_const(m_deferredNodes)) if (m_items.contains(node)) updateEdgesFor(node);
    m_deferredNodes.clear();
    refreshAnnotations();
    updateSceneRect();
}

void MindMapScene::updateEdge(MindNode *child) {
    if (!child) return;
    auto *edge = m_edgeByChild.value(child);
    auto *from = child->parent ? m_items.value(child->parent) : nullptr;
    auto *to = m_items.value(child);
    // I nodi flottanti e la radice non hanno un ramo entrante.
    // FEAT-028: no lines between the cells of a table.
    if (child->floating || child->summaryTopic || !from || !to || isTableCell(child)) {
        if (edge) { m_edges.removeOne(edge); m_edgeByChild.remove(child); delete edge; }
        return;
    }
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
    const NodeItem *rootItem = m_items.value(m_model->root());
    // FEAT-026 fase C: la meta' della mappa conta solo nella mappa mentale senza tipi propri lungo il
    // ramo; altrove (tipi annidati, grafico logico...) conta la posizione rispetto al genitore.
    bool plainMindMap = !inFloatingBranch && MapStructure::typeOf(m_model->root()) == MapStructure::Type::MindMap;
    for (const MindNode *up = child->parent; up && up->parent && plainMindMap; up = up->parent) plainMindMap = up->structure.isEmpty();
    const qreal axis = plainMindMap && rootItem ? rootItem->pos().x() : from->pos().x();
    const bool right = to->pos().x() >= axis;
    const QPointF b = to->pos() + to->branchAnchor(!right);
    const MapStructure::Type parentType = MapStructure::typeOf(child->parent);
    QPainterPath path;
    qreal zValue = -1;
    if (setsTimeline(child->parent)) {
        // FEAT-026 phase E (reference live 2026-10-24). On the axis: a line from the previous main topic (or
        // the central one) in this topic's colour. Off the axis: a dot on the axis and a tick to the
        // topic; the last topic also draws the axis, in its colour, under the other dots.
        const QRectF toShape = to->mapRectToScene(to->shapeRect());
        const QRectF fromShape = from->mapRectToScene(from->shapeRect());
        const NodeItem *previous = from;
        // The previous main topic and whether this one is the last (floating topics are not on the timeline).
        bool last = true;
        bool seen = false;
        for (const auto &sibling : child->parent->children) {
            if (sibling->floating) continue;
            if (sibling.get() == child) { seen = true; continue; }
            if (seen) { last = false; break; }
            if (const NodeItem *siblingItem = m_items.value(sibling.get())) previous = siblingItem;
        }
        if (MapStructure::directionOf(child->parent) != MapStructure::Direction::OffAxis) {
            const QRectF previousShape = previous->mapRectToScene(previous->shapeRect());
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
        const QRectF fromShape = from->mapRectToScene(from->shapeRect());
        const QRectF toShape = to->mapRectToScene(to->shapeRect());
        const bool up = toShape.center().y() < fromShape.center().y();
        const QPointF start(fromShape.center().x(), up ? fromShape.top() : fromShape.bottom());
        const QPointF end(toShape.left(), toShape.center().y());
        const MapVariants::Variant *variant = MapVariants::find(m_model->sheetStyle.variant);
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
        const NodeItem *head = m_items.value(const_cast<MindNode *>(headNode));
        const NodeItem *mainItem = isMain ? to : from;
        const int s = MapStructure::directionOf(headNode) == MapStructure::Direction::Left ? -1 : 1;
        const QRectF headShape = head ? head->mapRectToScene(head->shapeRect()) : QRectF();
        const QRectF mainShape = mainItem->mapRectToScene(mainItem->shapeRect());
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
                path.lineTo(headShape.center().x() + s * m_layout.fishboneReach().value(headNode, 0), axisY);
                zValue = -2;
            }
        } else {
            const QRectF toShape = to->mapRectToScene(to->shapeRect());
            const qreal y = toShape.center().y();
            path.moveTo(footX + s * std::abs(y - axisY) * kFishboneSlope, y);
            path.lineTo(s > 0 ? toShape.left() : toShape.right(), y);
        }
    } else if (parentType == MapStructure::Type::OrgChart && !child->floating) {
        // Organigramma: dal lato del genitore rivolto al figlio al lato opposto del figlio.
        const bool down = to->pos().y() >= from->pos().y();
        const QRectF fromShape = from->shapeRect(), toShape = to->shapeRect();
        const QPointF start = from->pos() + QPointF(0, down ? fromShape.bottom() : fromShape.top());
        const QPointF end = to->pos() + QPointF(0, down ? toShape.top() : toShape.bottom());
        // FEAT-072: from the central topic slanted lines with a rounded bend; below it the elbow.
        path = child->parent->parent ? BranchPath::elbow(start, end) : BranchPath::orgFan(start, end);
    } else if (parentType == MapStructure::Type::TreeChart && !child->floating
               && (!child->parent->parent || MapStructure::typeFromKey(child->parent->structure) == MapStructure::Type::TreeChart)) {
        // Albero: tronco dal centro del lato basso del genitore al lato del figlio rivolto al tronco.
        path = BranchPath::trunk(from->pos() + QPointF(0, from->shapeRect().bottom()), b);
    } else if (parentType == MapStructure::Type::Brace) {
        // One brace per parent (reference live 2026-10-24): the first child draws the upper arm, the last
        // the lower one, each in its colour; middle children have no line, a single child both arms
        // to its top and bottom.
        const auto isLaidOut = [](const auto &sibling) { return !sibling->floating; };
        const auto first = std::find_if(child->parent->children.cbegin(), child->parent->children.cend(), isLaidOut);
        const auto last = std::find_if(child->parent->children.crbegin(), child->parent->children.crend(), isLaidOut);
        const int dir = right ? 1 : -1;
        const QRectF fromShape = from->mapRectToScene(from->shapeRect());
        const QRectF toShape = to->mapRectToScene(to->shapeRect());
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
        const MapVariants::Variant *variant = MapVariants::find(m_model->sheetStyle.variant);
        const MapVariants::Line line = variant ? variant->line : MapVariants::Line::Curve;
        const bool fromRootEdge = child->parent == m_model->root() && parentType == MapStructure::Type::MindMap;
        // FEAT-030: a shape wider above or below its anchor (heart, cloud, star, gem...) would cover the
        // bars and stubs built from the anchor: the line goes straight out to the shape's side first.
        const QPointF anchor = from->pos() + from->branchAnchor(right);
        const QRectF fromRect = from->mapRectToScene(from->shapeRect());
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
        const BranchShape shape = effectiveBranchShape(child->parent);
        // Measured live 2026-10-23: a child above or below the central topic gets a line from its top or
        // bottom side, inside the side facing the child by `insetPx` plus `perPx` x its distance from that
        // top or bottom side; a child level with it, from that side.
        const auto fromRootSide = [&](qreal insetPx, qreal perPx, QPainterPath (*build)(const QPointF &, const QPointF &)) {
            const QRectF rootRect = from->mapRectToScene(from->shapeRect());
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
    const MindNode *owner = child->parent ? child->parent : child;
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
    const bool taper = m_model->sheetStyle.tapered && penStyle != Qt::NoPen && child->parent == m_model->root() && !child->floating
                    && !setsTimeline(child->parent) && !setsFishbone(child->parent);
    // FEAT-023: marks at the parent's end (start) and the child's end of the line; none without a line.
    // A tapered branch starts wider: the start mark takes that width.
    QPainterPath filledMarks, strokedMarks;
    if (penStyle != Qt::NoPen && !centreLine.isEmpty() && (owner->branchStart != BranchEnd::None || owner->branchEnd != BranchEnd::None)) {
        const auto travel = [&centreLine](qreal percent) {
            const qreal angle = centreLine.angleAtPercent(percent) * M_PI / 180.0;
            return QPointF(std::cos(angle), -std::sin(angle));
        };
        const qreal startWidth = taper ? NodeStyle::taperedStartWidth(owner->branchWidth) : owner->branchWidth;
        for (const auto &[kind, tip, outward, width] :
             {std::tuple{owner->branchStart, centreLine.pointAtPercent(0), -travel(0), startWidth},
              std::tuple{owner->branchEnd, centreLine.pointAtPercent(1), travel(1), owner->branchWidth}}) {
            if (kind == BranchEnd::None) continue;
            (BranchPath::endIsStroke(kind) ? strokedMarks : filledMarks).addPath(BranchPath::endMark(kind, tip, outward, width));
        }
    }
    if (taper) {
        const qreal startWidth = NodeStyle::taperedStartWidth(owner->branchWidth);
        const bool fromRoot = child->parent == m_model->root() && parentType == MapStructure::Type::MindMap;
        const QPointF start = path.elementCount() > 0 ? QPointF(path.elementAt(0)) : QPointF();
        const qreal angle = path.angleAtPercent(0) * M_PI / 180.0;
        path = BranchPath::tapered(path, startWidth, owner->branchWidth);
        if (fromRoot && !path.isEmpty()) {
            // User's photos 2026-10-23: the branch comes out from under the central topic at full
            // width, cut by its outline. It is prolonged backwards into the box and cut by the real outline:
            // cutting by the rectangle left one edge ending in the empty rounded corner, and the cut square
            // to the curve stuck out above the border.
            const QRectF rootRect = from->mapRectToScene(from->shapeRect());
            const qreal depth = std::min(rootRect.width(), rootRect.height()) / 2;
            const QPointF back = QPointF(-std::cos(angle), std::sin(angle)) * depth;
            const QPointF half = QPointF(std::sin(angle), std::cos(angle)) * (startWidth / 2);
            QPainterPath under;
            under.addPolygon(QPolygonF{start + half, start - half, start - half + back, start + half + back});
            path = path.united(under).subtracted(NodeShapes::outline(child->parent->shape, rootRect));
        }
    }
    const QPen pen = taper ? QPen(Qt::NoPen) : QPen(child->lineColor, owner->branchWidth, penStyle, Qt::RoundCap);  // FEAT-043
    const QBrush brush = taper ? QBrush(child->lineColor) : QBrush(Qt::NoBrush);
    zValue += stackZ(child);  // FEAT-057: still under every topic, above the lines of lower branches
    if (!edge) {
        edge = new EdgeItem;
        edge->setPath(path);
        edge->setPen(pen);
        edge->setBrush(brush);
        edge->setZValue(zValue);
        static_cast<EdgeItem *>(edge)->setMarks(filledMarks, strokedMarks, child->lineColor, owner->branchWidth);
        addItem(edge);
        m_edges.append(edge);
        m_edgeByChild.insert(child, edge);
        return;
    }
    // setPath/setPen invalidano da soli la vecchia e la nuova area del ramo.
    if (edge->zValue() != zValue) edge->setZValue(zValue);
    if (edge->path() != path) edge->setPath(path);
    if (edge->pen() != pen) edge->setPen(pen);
    if (edge->brush() != brush) edge->setBrush(brush);
    static_cast<EdgeItem *>(edge)->setMarks(filledMarks, strokedMarks, child->lineColor, owner->branchWidth);  // every edge is an EdgeItem
}

void MindMapScene::updateEdgesFor(MindNode *node) {
    if (!node) return;
    updateEdge(node);
    for (const auto &child : node->children) updateEdge(child.get());
    // A fishbone's head: the subtopics' ticks start on bones that depend on the spine (FEAT-026 phase E).
    if (setsFishbone(node))
        for (const auto &child : node->children)
            for (const auto &grandchild : child->children) updateEdge(grandchild.get());
    // A timeline's main topic: the next one's line starts at it, the last one draws the axis from the
    // central topic past itself (only those two, so a long timeline does not redraw every line).
    if (setsTimeline(node->parent)) {
        MindNode *next = nullptr, *last = nullptr;
        bool seen = false;
        for (const auto &sibling : node->parent->children) {
            if (sibling->floating) continue;
            if (seen && !next) next = sibling.get();
            if (sibling.get() == node) seen = true;
            last = sibling.get();
        }
        if (next) updateEdge(next);
        if (last && last != next && last != node) updateEdge(last);
    }
}

void MindMapScene::onNodeMoved(NodeItem *item) {
    if (m_batchUpdate) { m_batchMoved = true; return; }  // vedi syncAllItems
    updateEdgesFor(item->node());
    // BUG-013: il ramo segue il nodo trascinato (i discendenti spostati qui aggiornano i loro rami
    // passando di nuovo da onNodeMoved).
    if (const auto followers = m_dragFollowers.constFind(item->node()); followers != m_dragFollowers.cend())
        for (const Follower &follower : followers.value()) follower.item->setPos(item->pos() + follower.offset);
    // Le annotazioni sono poche e dipendono dalle posizioni: si ridisegnano solo
    // se la mappa ne contiene.
    if (!m_model->relations.isEmpty() || !m_model->summaries.isEmpty() || !m_model->boundaries.isEmpty())
        refreshAnnotations();
    emit nodeChanged();
}

// While the title is typed the box changes size: its branches follow, and (BUG-039, user's photos
// 2026-11-04) the map is laid out again so a longer title pushes its children and siblings instead
// of covering them. One layout per turn of the event loop, only when the size changed (autoLayout
// costs 5-6 ms with 1,000 topics, FEAT-018). The edit ends with a layout for the title kept or
// dropped (Esc), so nothing stays where the preview had put it.
void MindMapScene::onEditingResized(NodeItem *item) {
    updateEdgesFor(item->node());
    if (!item->isEditing()) {
        if (item != m_editingItem) return;
        m_editingItem = nullptr;
        if (std::exchange(m_editingLaidOut, false)) autoLayout();
        return;
    }
    const QSizeF size = item->shapeRect().size();
    if (item != m_editingItem) {
        // A change made just before (a new topic, say) keeps its own Ctrl+Z.
        m_editingItem = item;
        m_editingLayoutSize = size;
        m_editingLaidOut = false;
        emit historyStepFinished();
        return;
    }
    if (size == m_editingLayoutSize || m_editingLayoutQueued) return;
    m_editingLayoutQueued = true;
    QTimer::singleShot(0, this, [this] {
        m_editingLayoutQueued = false;
        if (!editingTitle()) return;
        m_editingLayoutSize = m_editingItem->shapeRect().size();
        m_editingLaidOut = true;
        autoLayout();
    });
}

void MindMapScene::syncAllItems() {
    // Prima ogni nodo spostato emetteva nodeChanged() (MainWindow::setDirty: timer di storico e
    // salvataggio riavviati e titolo della finestra aggiornato per ogni nodo); ora una volta sola
    // alla fine della passata, con lo stesso risultato.
    m_batchUpdate = true;
    m_batchMoved = false;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) it.value()->syncFromNode();
    m_batchUpdate = false;
    if (m_batchMoved) emit nodeChanged();
}

void MindMapScene::updateNode(MindNode *node) {
    auto *item = m_items.value(node);
    if (!item) return;
    item->syncFromNode();
    if (node->parent == m_model->root()) refreshStacking();  // FEAT-057: its rank may have changed (Undo)
    // FEAT-028: a step of the history that adds or removes a table (its type, or rows) changes the
    // cells and the lines of the whole table.
    if (tablePlaceStale(node)) {
        applyTableCells();
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) updateEdge(it.key());
    }
    if (m_deferredUpdates) { m_deferredNodes.insert(node); return; }  // P3: vedi setDeferredUpdates
    updateEdgesFor(node);
    refreshAnnotations();
    updateSceneRect();
}

void MindMapScene::refreshSelectionStyle() {
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) it.value()->selectionStyleChanged();
}

void MindMapScene::refreshAll() {
    syncAllItems();
    applyTableCells();
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) updateEdge(it.key());
    refreshAnnotations();
    updateSceneRect();
}

std::optional<Relationships::End> MindMapScene::relationEnd(const QString &id) const {
    // FEAT-052: a boundary end attaches to its drawn outline (title tab included).
    if (const BoundaryItem *boundary = m_boundaryItems.value(id); boundary && boundary->isVisible())
        return Relationships::End{boundary->outline(), boundary->strokeRect().center()};
    const NodeItem *item = m_itemsById.value(id);
    if (!item || !item->isVisible()) return std::nullopt;  // FEAT-016: hidden in a collapsed branch
    const QRectF box = item->shapeRect();
    return Relationships::End{item->mapToScene(NodeShapes::outline(item->node()->shape, box, item->cornerRadius())), item->mapToScene(box.center())};
}

std::optional<Relationships::Geometry> MindMapScene::relationGeometry(const Relationship &relationship) const {
    const auto from = relationEnd(relationship.from);
    const auto to = relationEnd(relationship.to);
    if (!from || !to) return std::nullopt;
    return Relationships::geometry(relationship, *from, *to);
}

RelationshipItem::Look MindMapScene::relationLook(const Relationship &relationship) const {
    const Relationships::Look shared = Relationships::look(relationship, m_model->backdrop(), m_model->sheetStyle.fontFamily);
    RelationshipItem::Look look;
    look.pen = QPen(shared.lineColor, shared.lineWidth, static_cast<Qt::PenStyle>(shared.linePattern), Qt::FlatCap, Qt::RoundJoin);
    look.font = shared.font;
    look.textColor = shared.textColor;
    look.backdrop = shared.backdrop;
    look.placeholder = tr("Relationship");
    return look;
}

void MindMapScene::syncRelationItems() {
    QSet<QString> present;
    for (const Relationship &relationship : std::as_const(m_model->relations)) {
        const auto geometry = relationGeometry(relationship);
        if (!geometry) continue;
        present.insert(relationship.id);
        RelationshipItem *item = m_relationItems.value(relationship.id);
        if (!item) {
            item = new RelationshipItem(relationship.id);
            addItem(item);
            m_relationItems.insert(relationship.id, item);
            item->geometryFor = [this](const Relationship &changed) {
                return relationGeometry(changed).value_or(Relationships::Geometry());
            };
            connect(item, &RelationshipItem::edited, this, &MindMapScene::relationEdited);
            connect(item, &RelationshipItem::menuRequested, this, &MindMapScene::relationMenuRequested);
        }
        item->setContent(relationship, *geometry, relationLook(relationship));
    }
    for (auto it = m_relationItems.begin(); it != m_relationItems.end();) {
        if (present.contains(it.key())) { ++it; continue; }
        delete it.value();
        it = m_relationItems.erase(it);
    }
}

void MindMapScene::isolateBranch(const MindNode *top) {
    QSet<QString> kept;
    const std::function<void(const MindNode *)> keep = [&](const MindNode *node) {
        kept.insert(node->id);
        for (const auto &child : node->children) keep(child.get());
    };
    keep(top);
    const auto allKept = [&kept](const QStringList &topics) {
        return std::all_of(topics.cbegin(), topics.cend(), [&kept](const QString &id) { return kept.contains(id); });
    };
    QSet<QString> keptSummaries;
    for (const Summary &summary : std::as_const(m_model->summaries))
        if (allKept(summary.topics)) keptSummaries.insert(summary.id);
    for (auto it = m_summaryItems.cbegin(); it != m_summaryItems.cend(); ++it) {
        if (keptSummaries.contains(it.key())) { keep(it->topic.get()); continue; }
        if (it->bracket) it->bracket->setVisible(false);
    }
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
        if (!kept.contains(it.key()->id)) it.value()->setVisible(false);
    // The branch into `top` belongs to its parent.
    for (auto it = m_edgeByChild.cbegin(); it != m_edgeByChild.cend(); ++it)
        if (it.key() == top || !kept.contains(it.key()->id)) it.value()->setVisible(false);
    for (const Boundary &boundary : std::as_const(m_model->boundaries)) {
        if (allKept(boundary.topics)) kept.insert(boundary.id);  // a relationship may end on it
        else if (BoundaryItem *item = m_boundaryItems.value(boundary.id)) item->setVisible(false);
    }
    for (const Relationship &relationship : std::as_const(m_model->relations))
        if (!kept.contains(relationship.from) || !kept.contains(relationship.to))
            if (RelationshipItem *item = m_relationItems.value(relationship.id)) item->setVisible(false);
}

QRectF MindMapScene::branchesCovered(const QStringList &topics) const {
    QRectF covered;
    const std::function<void(const MindNode *)> add = [&](const MindNode *node) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
        if (!item || !item->isVisible()) return;  // FEAT-016: a collapsed branch hides its topics
        const QRectF box = item->mapRectToScene(item->shapeRect());
        covered = covered.isNull() ? box : covered.united(box);
        for (const auto &child : node->children) if (!child->floating) add(child.get());
    };
    for (const QString &id : topics)
        if (const NodeItem *item = m_itemsById.value(id)) add(item->node());
    return covered;
}

BoundaryItem::Look MindMapScene::boundaryLook(const Boundary &boundary) const {
    const Boundaries::Look shared = Boundaries::look(boundary, m_model->backdrop(), m_model->sheetStyle.fontFamily);
    return BoundaryItem::Look{shared.pen, shared.fill, shared.titleText, shared.font, tr("Title")};
}

QRectF MindMapScene::boundaryCovered(const Boundary &boundary) const {
    for (const QString &id : boundary.topics)
        if (const NodeItem *item = m_itemsById.value(id); item && MapStructure::insideTreeTable(item->node())) return {};
    return branchesCovered(boundary.topics);
}

void MindMapScene::syncBoundaryItems() {
    // A boundary inside another one (its topics in the other's branches) widens it by its own
    // outline; repeated so a boundary nested twice reaches the outermost one.
    const QList<Boundary> &boundaries = m_model->boundaries;
    QList<QRectF> covers;
    for (const Boundary &boundary : boundaries) covers.append(boundaryCovered(boundary));
    const auto inside = [this](const Boundary &inner, const Boundary &outer) {
        return std::all_of(inner.topics.cbegin(), inner.topics.cend(), [&](const QString &id) {
            const NodeItem *item = m_itemsById.value(id);
            for (const MindNode *node = item ? item->node() : nullptr; node; node = node->parent)
                if (outer.topics.contains(node->id)) return true;
            return false;
        });
    };
    Boundaries::widenAroundNested(boundaries, covers, inside);
    QSet<QString> present;
    QList<std::pair<BoundaryItem *, QRectF>> shown;
    for (qsizetype index = 0; index < boundaries.size(); ++index) {
        const Boundary &boundary = boundaries[index];
        const QRectF covered = covers[index];
        if (covered.isNull()) continue;
        present.insert(boundary.id);
        BoundaryItem *item = m_boundaryItems.value(boundary.id);
        if (!item) {
            item = new BoundaryItem(boundary.id);
            addItem(item);
            m_boundaryItems.insert(boundary.id, item);
            connect(item, &BoundaryItem::edited, this, &MindMapScene::boundaryEdited);
            connect(item, &BoundaryItem::menuRequested, this, &MindMapScene::boundaryMenuRequested);
        }
        item->setContent(boundary, covered, boundaryLook(boundary));
        shown.append({item, item->strokeRect()});
    }
    // A boundary inside another one is drawn above it (its fill darkens).
    for (const auto &[item, rect] : std::as_const(shown)) {
        int depth = 0;
        for (const auto &[other, otherRect] : std::as_const(shown))
            if (other != item && otherRect.contains(rect) && otherRect != rect) ++depth;
        item->setZValue(-3 - 1.0 / (depth + 2));
    }
    for (auto it = m_boundaryItems.begin(); it != m_boundaryItems.end();) {
        if (present.contains(it.key())) { ++it; continue; }
        delete it.value();
        it = m_boundaryItems.erase(it);
    }
}

QStringList MindMapScene::selectedBoundaryIds() const {
    QStringList ids;
    for (auto *item : selectedItems())
        if (item->type() == BoundaryItem::Type) ids.append(static_cast<BoundaryItem *>(item)->id());
    return ids;
}

BoundaryItem *MindMapScene::boundaryItem(const QString &id) const { return m_boundaryItems.value(id); }

void MindMapScene::selectBoundary(const QString &id) {
    BoundaryItem *item = m_boundaryItems.value(id);
    if (!item) return;
    clearSelection();
    item->setSelected(true);
    item->setFocus();
}

int MindMapScene::selectTargets(const SelectionCommands::Targets &targets, bool apply) {
    QList<QGraphicsItem *> items;
    for (MindNode *node : targets.nodes)
        if (NodeItem *item = m_items.value(node); item && item->isVisible()) items.append(item);
    for (const QString &id : targets.relations)
        if (RelationshipItem *item = m_relationItems.value(id); item && item->isVisible()) items.append(item);
    for (const QString &id : targets.boundaries)
        if (BoundaryItem *item = m_boundaryItems.value(id); item && item->isVisible()) items.append(item);
    if (!apply || items.isEmpty()) return int(items.size());
    clearSelection();  // with its signals: an editor that loses the selection finishes its edit
    {
        // One selectionChanged for the whole set, not one per item: each refreshes the side panel,
        // which made a thousand items cost seconds (FEAT-109). Selecting emits nothing else but the
        // topics' nodeSelected, sent after in the same order.
        const QSignalBlocker blocker(this);
        for (QGraphicsItem *item : std::as_const(items)) item->setSelected(true);
    }
    for (QGraphicsItem *item : std::as_const(items))
        if (auto *nodeItem = qgraphicsitem_cast<NodeItem *>(item)) emit nodeSelected(nodeItem->node());
    setFocusItem(items.first());
    emit selectionChanged();
    return int(items.size());
}

QString MindMapScene::elementAt(const QPointF &scenePos) const {
    if (const MindNode *node = nodeAt(scenePos)) return node->id;
    for (QGraphicsItem *item : items(scenePos))  // topmost first: the innermost boundary
        if (item->type() == BoundaryItem::Type) return static_cast<BoundaryItem *>(item)->id();
    return QString();
}

QStringList MindMapScene::selectedRelationIds() const {
    QStringList ids;
    for (auto *item : selectedItems())
        if (item->type() == RelationshipItem::Type) ids.append(static_cast<RelationshipItem *>(item)->id());
    return ids;
}

RelationshipItem *MindMapScene::relationItem(const QString &id) const { return m_relationItems.value(id); }

void MindMapScene::selectRelation(const QString &id) {
    RelationshipItem *item = m_relationItems.value(id);
    if (!item) return;
    clearSelection();
    item->setSelected(true);
    item->setFocus();
}

void MindMapScene::selectRelationAfterClick(const QString &id) {
    if (QGuiApplication::mouseButtons() == Qt::NoButton) selectRelation(id);
    else m_relationToSelect = id;
}

void MindMapScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    QGraphicsScene::mouseReleaseEvent(event);
    if (!m_relationToSelect.isEmpty()) selectRelation(std::exchange(m_relationToSelect, QString()));
}

void MindMapScene::refreshAnnotations() {
    for (auto *item : std::as_const(m_annotations)) delete item;
    m_annotations.clear();
    drawAnnotations();
}

void MindMapScene::onRowsInserted(const QModelIndex &parent, int first, int last) {
    auto *parentNode = parent.isValid() ? m_model->nodeFromIndex(parent) : m_model->root();
    if (!parentNode) return;
    for (int row = first; row <= last && row < static_cast<int>(parentNode->children.size()); ++row) {
        auto *child = parentNode->children[row].get();
        addNodeRecursive(child);
        drawConnections(child);
        updateEdge(child);
    }
    if (parentNode == m_model->root()) refreshStacking();
    updateSceneRect();
}

void MindMapScene::onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last) {
    auto *parentNode = parent.isValid() ? m_model->nodeFromIndex(parent) : m_model->root();
    if (!parentNode) return;
    for (int row = first; row <= last && row < static_cast<int>(parentNode->children.size()); ++row)
        removeNodeItems(parentNode->children[row].get());
}

void MindMapScene::onRowsMoved(const QModelIndex &, int, int, const QModelIndex &destination, int) {
    auto *newParent = destination.isValid() ? m_model->nodeFromIndex(destination) : m_model->root();
    if (!newParent) return;
    // Si ricalcolano i rami di tutti i figli del nuovo genitore (pochi): così non serve
    // ricavare la posizione finale delle righe spostate. updateEdge() non fa nulla se il
    // ramo è già corretto.
    for (const auto &child : newParent->children) updateEdge(child.get());
    // FEAT-057: a moved branch takes the place of its new main branch, or its own as a main branch.
    if (newParent == m_model->root()) refreshStacking();
    else for (const auto &child : newParent->children) setStackZBelow(child.get(), stackZ(newParent));
    // Un sommario disegnato accanto al nodo spostato va riposizionato.
    refreshAnnotations();
}

void MindMapScene::onRowsRemoved() {
    // Relazioni, sommari e confini che puntavano ai nodi rimossi spariscono.
    refreshAnnotations();
}

void MindMapScene::refreshStacking() {
    const QList<MindNode *> order = m_model->stackingOrder();
    // Bands in (0, 1): above the central topic (0), under relationships (5) and the topic being edited.
    // Only branches whose place changed are walked: this runs on every update of a main branch.
    for (qsizetype rank = 0; rank < order.size(); ++rank) {
        const qreal z = qreal(rank + 1) / qreal(order.size() + 1);
        if (const NodeItem *item = m_items.value(order[rank]); item && item->stackZ() != z) setStackZBelow(order[rank], z);
    }
}

qreal MindMapScene::stackZ(const MindNode *node) const {
    while (node && node->parent && node->parent->parent) node = node->parent;
    if (!node || !node->parent) return 0;
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item ? item->stackZ() : 0;
}

void MindMapScene::setStackZBelow(MindNode *node, qreal z) {
    NodeItem *item = m_items.value(node);
    if (!item) return;
    const qreal shift = z - item->stackZ();
    if (shift != 0) {
        item->setStackZ(z);
        if (QGraphicsPathItem *edge = m_edgeByChild.value(node)) edge->setZValue(edge->zValue() + shift);
    }
    for (const auto &child : node->children) setStackZBelow(child.get(), z);
}

void MindMapScene::removeNodeItems(MindNode *node) {
    for (const auto &child : node->children) removeNodeItems(child.get());
    if (auto *edge = m_edgeByChild.take(node)) { m_edges.removeOne(edge); delete edge; }
    if (auto *item = m_items.take(node)) { m_itemsById.remove(node->id); delete item; }
}

void MindMapScene::updateSceneRect() {
    // (bug 2026-10-12: la mappa "saltava" entrando in modifica e tornava al clic. Il rilascio
    // del doppio clic chiama questa funzione mentre la barra di formattazione è nella scena:
    // l'area della scena si allargava e Qt, con la scena più piccola della finestra, ricentrava
    // la mappa; il clic successivo la ricalcolava senza barra e la riportava indietro.)
    // Ora l'area comprende sempre anche la parte di mappa visibile, così Qt non la ricentra, e
    // il punto in alto a sinistra della vista resta nello stesso posto dello schermo.
    // La barra di formattazione (QGraphicsProxyWidget figlio del nodo in modifica) e gli
    // elementi nascosti non contano: sono temporanei e allargherebbero la scena (con le barre
    // di scorrimento che compaiono e restringono la vista) solo mentre si scrive.
    QRectF content;
    for (const QGraphicsItem *item : items())
        if (item->isVisible() && item->type() != QGraphicsProxyWidget::Type) content |= item->sceneBoundingRect();
    QRectF rect = content.adjusted(-300, -300, 300, 300);
    struct Anchor { QGraphicsView *view; QPointF topLeft; };
    QList<Anchor> anchors;
    for (QGraphicsView *view : views()) {
        const QRectF visible = view->mapToScene(view->viewport()->rect()).boundingRect();
        rect |= visible;
        anchors.append({view, visible.topLeft()});
    }
    if (rect == sceneRect()) return;
    setSceneRect(rect);
    for (const Anchor &anchor : std::as_const(anchors)) {
        const QPoint shift = anchor.view->mapFromScene(anchor.topLeft);
        anchor.view->horizontalScrollBar()->setValue(anchor.view->horizontalScrollBar()->value() + shift.x());
        anchor.view->verticalScrollBar()->setValue(anchor.view->verticalScrollBar()->value() + shift.y());
    }
}

bool MindMapScene::commitEditing() {
    // La modifica è al massimo una; si copia l'elenco perché la conferma aggiorna la scena.
    const QList<NodeItem *> items = m_items.values();
    for (NodeItem *item : items)
        if (item->isEditing()) { item->commitEditing(); return true; }
    // FEAT-053: a relationship label being written (it is applied before a sheet change or a save).
    const QList<RelationshipItem *> relations = m_relationItems.values();
    for (RelationshipItem *item : relations)
        if (item->isEditing()) { item->commitEditing(); return true; }
    const QList<BoundaryItem *> boundaries = m_boundaryItems.values();  // FEAT-052: a title being written
    for (BoundaryItem *item : boundaries)
        if (item->isEditing()) { item->commitEditing(); return true; }
    return false;
}

void MindMapScene::setLabelHighlight(const QString &label) {
    m_labelHighlight = label;
    constexpr qreal kFaded = 0.2;
    const auto matches = [&label](const MindNode *node) {
        return std::any_of(node->labels.cbegin(), node->labels.cend(), [&label](const QString &own) { return Labels::same(own, label); });
    };
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
        it.value()->setOpacity(label.isEmpty() || matches(it.key()) ? 1.0 : kFaded);
    // Linee e annotazioni: tenui anche loro, cosi' restano visibili solo i nodi con l'etichetta.
    for (QGraphicsItem *item : items())
        if (!qgraphicsitem_cast<NodeItem *>(item) && !item->parentItem()) item->setOpacity(label.isEmpty() ? 1.0 : kFaded);
}

MindNode *MindMapScene::nodeAt(const QPointF &scenePos) const {
    for (QGraphicsItem *item : items(scenePos)) {
        for (QGraphicsItem *up = item; up; up = up->parentItem())
            if (auto *node = qgraphicsitem_cast<NodeItem *>(up)) return node->node();
    }
    return nullptr;
}

std::pair<MindNode *, QString> MindMapScene::hoveredMarker() const {
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it)
        if (const int index = it.value()->hoveredMarker(); index >= 0) return {it.key(), it.key()->markers.value(index)};
    return {nullptr, QString()};
}

MindNode *MindMapScene::selectedImageNode() const {
    for (QGraphicsItem *selected : selectedItems())
        if (auto *item = qgraphicsitem_cast<NodeItem *>(selected); item && item->isImageSelected()) return item->node();
    return nullptr;
}

QPixmap MindMapScene::nodePicture(const MindNode *node) const {
    NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item ? item->picture() : QPixmap();
}

qreal MindMapScene::imageRoomWidth(const MindNode *node) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item ? item->imageRoomWidth() : 0;
}

qreal MindMapScene::nodeWidth(const MindNode *node) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item ? item->textBoxWidth() : 0;
}

qreal MindMapScene::manualWidthFor(const MindNode *node, qreal requestedPx) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    const qreal requested = std::round(requestedPx);
    if (!item) return std::clamp(requested, 1.0, kMaxManualNodeWidthPx);
    // Il riquadro attuale contiene almeno il carattere piu' largo con i margini, cioe' non e' mai
    // sotto il minimo: chiedendo almeno la larghezza attuale (allargare, frecce su) non serve
    // impaginare il titolo per misurare il minimo (17-28 ms su 1.000 nodi, misura in FEAT-021).
    if (requested >= std::ceil(item->textBoxWidth())) return std::min(requested, kMaxManualNodeWidthPx);
    const qreal minimum = std::min(item->minimumCustomWidth(), kMaxManualNodeWidthPx);
    return std::clamp(requested, minimum, kMaxManualNodeWidthPx);
}

void MindMapScene::selectNode(MindNode *node) {
    if (auto *item = m_items.value(node)) { clearSelection(); item->setSelected(true); setFocusItem(item); }
}

