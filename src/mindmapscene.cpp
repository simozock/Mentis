#include "mindmapscene.h"

#include <QTimer>
#include <QImage>
#include <QBuffer>
#include "richresources.h"
#include "branchpath.h"
#include "mapvariants.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
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

// FEAT-015 (foto dell'utente 2026-10-21): segnaposto azzurro dove comparira' il nodo,
// raggio 4; oltre il lato esterno di un nodo senza figli il rilascio vale fino a questa distanza.
constexpr QSizeF kReorderSlotPx{36, 12};
constexpr qreal kReorderLeafReachPx = 200;
constexpr qreal kReorderBodyMarginPx = 4;   // attorno alla forma: rilascio sopra il nodo
constexpr qreal kReorderVerticalReachPx = 24;  // sopra e sotto il gruppo dei figli
constexpr qreal kReorderGhostOpacity = 0.6;

// Distanze del layout, misurate sulle foto di riferimento dell'utente (2026-10-21) e approvate: il riferimento
// stacca i rami principali e compatta i loro discendenti.
// Valori: dal centrale ai rami principali, da un nodo ai suoi figli (dal ramo principale in giu'), fra
// i rami principali (fra i loro ingombri), fra due foglie vicine dentro lo stesso ramo principale, e
// fattore per le distanze di organigramma e albero. Live reference values (FEAT-072, user decisions
// 2026-10-21/22): central to main topics 54 (was 80), a node to its children 28 (was 32), 35 between
// the shapes of main topics (was 32 plus the hidden 4 px margins). "Mappa compatta" (FEAT-043, richiesta
// dell'utente 2026-10-21: "diminuisce leggermente gli spazi"): circa tre quarti, sulla griglia di 4 px.
constexpr qreal kNormalSpacing[] = {54, 28, 35, 6, 1};
constexpr qreal kCompactSpacing[] = {40, 24, 24, 4, 0.75};
constexpr qreal kFanLevelFactor = 54.0 / 28.0;  // FEAT-072: fans put the children 54 px out (reference live)
constexpr qreal kRootSquareInsetPx = 50;  // FEAT-072: right-angle lines from the central topic (reference 51)
// FEAT-072: curves to underlined main topics start inside the corner by 0.75 x the child's distance above or
// below the central topic (reference photos 2026-10-23: 0.73-0.75 over 7-123 px; a fixed 35 px fitted only close topics).
constexpr qreal kRootUnderlineInsetPerPx = 0.75;
constexpr qreal kLogicRootFactor = 100.0 / 54.0;  // FEAT-072: logic chart, main topics 100 px from the central (reference live)
// FEAT-026 phase E, brace map (reference live 2026-10-24): cusp 12 px out from the parent, arcs of radius
// 18.5 from the central topic and 7 below, children 17 px past the arms' ends.
constexpr qreal kBraceCuspGapPx = 12;
constexpr qreal kBraceRootRadiusPx = 18.5;
constexpr qreal kBraceRadiusPx = 7;
constexpr qreal kBraceEndGapPx = 17;
qreal braceRadius(const MindNode *parent) { return parent->parent ? kBraceRadiusPx : kBraceRootRadiusPx; }
// Shape to shape, from a parent of a brace map to its children.
qreal braceLevelGap(const MindNode *parent) { return kBraceCuspGapPx + 2 * braceRadius(parent) + kBraceEndGapPx; }
bool isBraceParent(const MindNode *node) { return MapStructure::typeOf(node) == MapStructure::Type::Brace; }
// FEAT-026 phase E, timeline (reference live 2026-10-24). On the axis (variants 1 and 2): 100 px between
// consecutive topics. Off the axis (variant 3): main topics 25 px above and below the axis in turn, the
// first 25 px from the central topic, each centre 37 px past the previous topic's right edge, the axis
// 40 px past the last one, dots of twice the line width in radius. Either way a main topic's branch
// (topic and children) stays that gap from the previous branch on its side, and its children stand in
// a column 25 px above or below it, 27 px beside the stem from the middle of its side.
constexpr qreal kTimelineGapPx = 100;
constexpr qreal kTimelineOffAxisGapPx = 37;
constexpr qreal kTimelineAxisGapPx = 25;
constexpr qreal kTimelineAxisOvershootPx = 40;
constexpr qreal kTimelineChildGapPx = 25;
constexpr qreal kTimelineStemIndentPx = 27;
constexpr qreal kTimelineObliqueSlope = 0.577;  // variant 3: the last stretch to a subtopic rises at 30 degrees
// FEAT-052: position of a summary topic with this shape beside the bracket of `covered`.
QPointF summaryTopicPosition(const QRectF &covered, Summaries::Side side, const QRectF &shape) {
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
bool setsTimeline(const MindNode *node) {
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
constexpr qreal kFishboneSlope = 0.577;  // tan 30 degrees: horizontal run of a bone per pixel of height
constexpr qreal kFishboneHeadGapPx = 52;
constexpr qreal kFishboneBranchGapPx = 35;
constexpr qreal kFishboneFootGapPx = 35;
constexpr qreal kFishboneTickPx = 36;
constexpr qreal kFishboneSpineCentrePx = 65;  // spine to the nearest subtopic's centre
constexpr qreal kFishboneTopicCentrePx = 39;  // farthest subtopic's centre to the main topic
constexpr qreal kFishboneSubPitchPx = 37;     // centre to centre; taller subtopics keep the sibling gap
constexpr qreal kFishboneBarePx = 50;
bool setsFishbone(const MindNode *node) {
    return node && MapStructure::typeOf(node) == MapStructure::Type::Fishbone
        && (!node->parent || MapStructure::typeFromKey(node->structure) == MapStructure::Type::Fishbone);
}
// FEAT-028: the topic that sets a tree table (the central topic of a table sheet, or a branch with
// that type). Reference live 2026-10-25: a row at least 48 px tall (46.5 plus the border); below a main
// topic every cell takes its colour mixed 80% with white (#FF6B6B -> #FFE1E1).
bool setsTreeTable(const MindNode *node) {
    return node && MapStructure::typeOf(node) == MapStructure::Type::TreeTable
        && (!node->parent || MapStructure::typeFromKey(node->structure) == MapStructure::Type::TreeTable);
}
constexpr qreal kTableRowPx = 48;
constexpr qreal kTableInsetPx = 16;  // stage 2: a branch inside a cell from its edges (reference live: 15-16)
constexpr qreal kTableTintWhite = 0.8;
QColor solidFill(const MindNode *node) { return node->fill.alpha() > 0 ? node->fill : node->branchColor; }
constexpr qreal kMinMainLeafHalfPx = 16;  // un ramo principale senza figli occupa almeno 32 px (passo 64)
// NodeItem::normalBoundingRect allarga la forma di 4 px per lato: lo spazio fra le foglie si misura
// sulla forma, come appare.
constexpr qreal kLayoutMarginPx = 4;
}

MindMapScene::MindMapScene(MindMapModel *model, QObject *parent) : QGraphicsScene(parent), m_model(model) {
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

BranchShape MindMapScene::effectiveBranchShape(const MindNode *parent) const {
    if (!parent || parent->branchShape != BranchShape::Auto) return parent ? parent->branchShape : BranchShape::Auto;
    const MapVariants::Variant *variant = MapVariants::find(m_model->sheetStyle.variant);
    if (variant && variant->line != MapVariants::Line::Curve) return BranchShape::Auto;
    const MapStructure::Type type = MapStructure::typeOf(parent);
    // Measured live 2026-10-23: in the logic chart curves from the central topic, the rounded bracket below.
    if (type == MapStructure::Type::LogicChart) return parent->parent ? BranchShape::RoundedBracket : BranchShape::Curve;
    if (type == MapStructure::Type::TreeChart) return BranchShape::RoundedBracket;  // below the trunk
    if (type != MapStructure::Type::MindMap) return BranchShape::Auto;
    // Measured live 2026-10-23: variants with underlined subtopics (2, 3, the hand-drawn one) join
    // them with curves, the others with the rounded bracket.
    if (parent->parent) return variant && variant->sub.look == MapVariants::Look::Underline ? BranchShape::Curve : BranchShape::RoundedBracket;
    return BranchShape::Auto;
}

QRectF MindMapScene::nodeShapeSceneRect(const MindNode *node) const {
    const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
    return item && item->isVisible() ? item->mapRectToScene(item->shapeRect()) : QRectF();
}

void MindMapScene::refreshTableCells() {
    if (!m_tableCells.isEmpty()) computeAutoPositions();  // with no table, applyTableCells finds a new one
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
        // testo semplice. Il titolo formattato viene salvato direttamente insieme al plain text.
        const qreal halfBefore = i->anchorHalfWidth();  // FEAT-034
        const QSizeF sizeBefore = i->shapeRect().size();
        i->node()->title = title;
        i->node()->titleHtml = RichResources::withoutExternal(html);  // BUG-044
        // P8 (2026-10-13): per i figli il dataChanged emesso qui sotto aggiorna già il nodo
        // (vedi il costruttore): prima l'aggiornamento avveniva due volte. La radice non ha
        // indice nel modello, quindi per lei si aggiorna qui.
        if (!i->node()->parent || summaryTopicOf(i->node())) updateNode(i->node());  // FEAT-052: no index either
        const QModelIndex index = m_model->indexFromNode(i->node());
        if (index.isValid()) emit m_model->dataChanged(index, index, {Qt::EditRole, Qt::DisplayRole});
        // FEAT-034: con il titolo nuovo il lato verso il genitore resta dov'era (nello stesso passo
        // della modifica; il layout seguente lo tiene li').
        i->keepParentSide(halfBefore);
        i->node()->position = i->pos();
        // A new size moves the topics around it (FEAT-072): e.g. a central topic going to two
        // lines pushes the main topics clear of it. Before, only its branches followed.
        if (i->shapeRect().size() != sizeBefore) autoLayout();
        else updateEdgesFor(i->node());
        emit m_model->structureChanged();  // aggiorna outliner, storico e stato "Modificato"
        // selezionato da solo; riselezionarlo toglierebbe la selezione al nodo appena cliccato)
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

QMarginsF MindMapScene::boundaryPad(const QList<MindNode *> &children, int index, bool alongX, bool ascending) const {
    QMarginsF pad;
    const QString &id = children[index]->id;
    for (const Boundary &boundary : std::as_const(m_model->boundaries)) {
        if (!boundary.topics.contains(id)) continue;
        QList<int> run;
        for (int i = 0; i < children.size(); ++i) if (boundary.topics.contains(children[i]->id)) run.append(i);
        if (run.size() != boundary.topics.size()) continue;  // topics under other parents: just surrounded
        const int low = ascending ? run.front() : run.back();  // the smallest coordinate
        const int high = ascending ? run.back() : run.front();
        const qreal side = Boundaries::padBelow(boundary);  // margin and half the stroke
        const qreal above = Boundaries::padAbove(boundary);  // with the title tab
        if (alongX) {
            pad.setTop(std::max(pad.top(), above));
            pad.setBottom(std::max(pad.bottom(), side));
            if (index == low) pad.setLeft(std::max(pad.left(), side));
            if (index == high) pad.setRight(std::max(pad.right(), side));
        } else {
            pad.setLeft(std::max(pad.left(), side));
            pad.setRight(std::max(pad.right(), side));
            if (index == low) pad.setTop(std::max(pad.top(), above));
            if (index == high) pad.setBottom(std::max(pad.bottom(), side));
        }
    }
    return pad;
}

QRectF MindMapScene::summaryRoom(const MindNode *node, const QList<MindNode *> &children, const QList<QRectF> &placed,
                                 const QList<QPointF> &centres, const QPointF &nodeAt, bool row) const {
    QRectF room;
    for (const Summary &summary : std::as_const(m_model->summaries)) {
        if (summary.topic->parent != node) continue;
        QRectF covered;
        QList<QPointF> points;
        for (int i = 0; i < children.size(); ++i)
            if (summary.topics.contains(children[i]->id)) {
                covered = covered.isNull() ? placed[i] : covered.united(placed[i]);
                points.append(centres[i]);
            }
        if (covered.isNull()) continue;
        if (points.size() < 2) points = centres;
        const NodeItem *item = m_items.value(summary.topic.get());
        const QSizeF size = item ? item->shapeRect().size() : QSizeF(80, 22);
        const Summaries::Side side = Summaries::sideFor(points, covered, nodeAt, row);
        QRectF extent = Summaries::extent(covered, side, size);
        // With the topic's branch, from where the topic stands in this frame.
        if (const auto bounds = m_summaryBranchBounds.constFind(summary.topic.get()); item && bounds != m_summaryBranchBounds.cend())
            extent = extent.united(bounds->translated(summaryTopicPosition(covered, side, item->shapeRect())));
        room = room.isNull() ? extent : room.united(extent);
    }
    return room;
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
        layoutSummaryBranch(items.topic.get(), sign);
        const std::function<void(MindNode *)> place = [&](MindNode *node) {
            for (const auto &child : node->children) {
                if (NodeItem *childItem = m_items.value(child.get())) {
                    childItem->setGrowSide(m_sides.value(child.get(), sign));
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

void MindMapScene::collectBoundaryRuns() {
    m_boundaryRuns.clear();
    m_boundaryRunsOf.clear();
    m_summaryBranchBounds.clear();
    const auto addRun = [this](const QStringList &ids, qreal above, qreal below) {
        // Room only for a run of siblings; topics moved under other parents are just surrounded.
        QList<const MindNode *> topics;
        for (const QString &id : ids)
            if (const NodeItem *item = m_itemsById.value(id)) topics.append(item->node());
        if (topics.isEmpty() || std::any_of(topics.cbegin(), topics.cend(), [&](const MindNode *t) { return t->parent != topics.front()->parent; }))
            return;
        const int index = int(m_boundaryRuns.size());
        m_boundaryRuns.append({above, below, int(topics.size()), 0});
        for (const MindNode *topic : std::as_const(topics)) m_boundaryRunsOf[topic].append(index);
    };
    for (const Boundary &boundary : std::as_const(m_model->boundaries))
        addRun(boundary.topics, Boundaries::padAbove(boundary), Boundaries::padBelow(boundary));
    // FEAT-052: a summary topic taller than its run moves the siblings apart (the run's height from the
    // last layout: the room lies outside it, so the result does not change it).
    for (const Summary &summary : std::as_const(m_model->summaries)) {
        const NodeItem *item = m_items.value(summary.topic.get());
        const QRectF covered = branchesCovered(summary.topics);
        if (!item || covered.isNull()) continue;
        // With the topic's branch, laid out where the topic stands (only its extent counts here).
        const QRectF bounds = layoutSummaryBranch(summary.topic.get(), sideOf(summary.topic.get()) < 0 ? -1 : 1)
                                  .translated(-summary.topic->position);
        m_summaryBranchBounds.insert(summary.topic.get(), bounds);
        const qreal centre = item->shapeRect().center().y();
        const qreal half = covered.height() / 2 + Summaries::kOverhangPx;
        const qreal above = centre - bounds.top() - half;
        const qreal below = bounds.bottom() - centre - half;
        if (above > 0 || below > 0) addRun(summary.topics, std::max<qreal>(0, above), std::max<qreal>(0, below));
    }
    for (BoundaryRun &run : m_boundaryRuns) run.visited = 0;  // for the main layout
}

QRectF MindMapScene::layoutSummaryBranch(MindNode *topic, int sign) {
    // As a floating topic's branch: the children in a column beside the topic, centred on it. Run
    // after the main layout and more than once per layout: the boundary runs in it start afresh.
    for (BoundaryRun &run : m_boundaryRuns) run.visited = 0;
    m_autoPositions[topic] = topic->position;
    m_sides[topic] = sign;
    const NodeItem *topicItem = m_items.value(topic);
    QRectF bounds = topicItem ? topicItem->shapeRect().translated(topic->position) : QRectF();
    QList<MindNode *> laid;
    qreal cursor = 0;
    m_lastLeafStride = -1;
    m_leafYs.clear();
    m_startsBranch = true;
    for (const auto &child : topic->children)
        if (!child->floating) { layoutSubtree(child.get(), 2, cursor, sign); laid.append(child.get()); }
    if (laid.isEmpty()) return bounds;
    // The topic halfway between its first and last child, as layoutSubtree centres a parent.
    const qreal middle = (m_autoPositions.value(laid.front()).y() + m_autoPositions.value(laid.back()).y()) / 2;
    for (MindNode *child : std::as_const(laid)) shiftSubtree(child, topic->position.y() - middle);
    const std::function<void(const MindNode *)> unite = [&](const MindNode *node) {
        if (childrenHidden(node)) return;
        for (const auto &child : node->children) {
            if (const NodeItem *item = m_items.value(child.get()))
                bounds = bounds.united(item->shapeRect().translated(m_autoPositions.value(child.get())));
            unite(child.get());
        }
    };
    unite(topic);
    return bounds;
}

void MindMapScene::onSummaryBranchAboutToChange(MindNode *topic) {
    for (const auto &child : topic->children) removeNodeItems(child.get());
}

void MindMapScene::onSummaryBranchChanged(MindNode *topic) {
    if (!m_items.contains(topic)) return;  // its items come with the summary's (syncSummaryItems)
    for (const auto &child : topic->children) addNodeRecursive(child.get());
}

qreal MindMapScene::layoutSubtree(MindNode *node, int depth, qreal &cursor, int sign) {
    // FEAT-052: room for the boundaries starting here; the guard adds the room after the last
    // topic of a run once this subtree is placed (it runs after the return value is computed).
    const QList<int> runs = m_boundaryRunsOf.value(node);
    for (const int run : runs) if (m_boundaryRuns[run].visited == 0) cursor += m_boundaryRuns[run].above;
    struct RoomAfter {
        MindMapScene *scene; const QList<int> &runs; qreal &cursor;
        ~RoomAfter() {
            for (const int run : runs)
                if (++scene->m_boundaryRuns[run].visited == scene->m_boundaryRuns[run].size) cursor += scene->m_boundaryRuns[run].below;
        }
    } roomAfter{this, runs, cursor};
    // La distanza orizzontale tiene conto della larghezza reale dei nodi (i nodi con formule possono
    // essere piu' larghi) e quella verticale della loro altezza (distanze: costanti k*GapPx).
    auto *item = m_items.value(node);
    // B7: il risultato va in m_autoPositions; node->position lo imposta autoLayout().
    QPointF &autoPosition = m_autoPositions[node];
    const qreal halfWidth = item ? item->anchorHalfWidth() : 105.0;
    if (node->parent) {
        auto *parentItem = m_items.value(node->parent);
        const qreal parentHalf = parentItem ? parentItem->anchorHalfWidth() : 105.0;
        // FEAT-012: a sinistra lo specchio della destra. Il lato verso il genitore resta alla stessa
        // distanza dal genitore: il nodo si allarga dal lato libero (FEAT-034). 80 px dal centrale,
        // 32 dagli altri genitori (2026-10-21; prima 80 per tutti i livelli).
        // FEAT-072: under a fan-shaped parent the children sit 54 px out, not 28. Since
        // 2026-10-26 (user's decision) also the default curves of the underlined variants: they have the
        // fan's stub too (before, they left the underline end directly at the usual gap).
        const qreal levelGap = NodeStyle::isFanShape(effectiveBranchShape(node->parent)) ? m_spacing.level * kFanLevelFactor : m_spacing.level;
        const bool logic = MapStructure::typeOf(node->parent) == MapStructure::Type::LogicChart;
        const qreal gap = isBraceParent(node->parent) ? braceLevelGap(node->parent)
                        : node->parent->parent ? levelGap : m_spacing.rootLevel * (logic ? kLogicRootFactor : 1.0);
        autoPosition.setX(m_autoPositions.value(node->parent).x() + sign * (parentHalf + gap + halfWidth));
        m_sides[node] = sign;
    } else {
        autoPosition.setX(0);
    }
    const bool hasLayoutChildren = !childrenHidden(node) &&
        std::any_of(node->children.cbegin(), node->children.cend(), [](const auto &child) { return !child->floating; });
    // FEAT-026 fase C: un ramo con tipo a blocchi (organigramma, albero) occupa il suo ingombro,
    // anche asimmetrico (i figli sotto); il nodo resta alla sua x come gli altri.
    if (hasLayoutChildren && node->parent && !node->structure.isEmpty() &&
        MapStructure::laysOutAsBlock(MapStructure::typeFromKey(node->structure))) {
        QHash<MindNode *, Placement> rel;
        const QRectF block = layoutBlock(node, sign, rel);
        // L'ingombro non torna verso il genitore oltre il bordo interno del nodo (una riga di
        // organigramma centrata sotto il nodo coprirebbe il centrale): il ramo si sposta in fuori.
        const qreal halfWidth = item ? item->anchorHalfWidth() : 105.0;
        const qreal outward = sign > 0 ? std::max<qreal>(0, -halfWidth - block.left()) : std::max<qreal>(0, block.right() - halfWidth);
        const qreal x = m_autoPositions.value(node).x() + sign * outward;
        const qreal gap = m_startsBranch ? m_spacing.branch : m_spacing.sibling;
        const qreal y = m_lastLeafStride < 0 ? cursor - block.top() : cursor + gap - block.top();
        m_autoPositions[node] = QPointF(x, y);
        placeBlock(node, rel);
        m_lastLeafY = y;
        m_lastLeafStride = block.bottom();
        m_startsBranch = false;
        m_leafYs.append(y);
        cursor = y + block.bottom();
        return y;
    }
    if (!hasLayoutChildren) {
        // (2026-10-13, testi lunghi a capo senza altezza massima: due foglie vicine distano meta'
        // altezza dell'una piu' meta' altezza dell'altra piu' lo spazio, cosi' una foglia alta non
        // copre la precedente.) cursor e' il bordo inferiore dell'ultima foglia.
        // 2026-10-21 (foto di riferimento): dentro un ramo principale 8 px fra le forme, fra i rami
        // principali 32 px come prima; un ramo principale senza figli conta come prima
        // (passo max(64, altezza + 32)). Prima 32 px a ogni livello, con il minimo di 64.
        const qreal height = item ? item->layoutHeight() : 0;
        // Gaps between the visible shapes at every level (FEAT-072: before, main topics also counted
        // the 4 px layout margins, so two main topics were 40 px apart instead of the gap).
        const bool mainTopic = depth <= 1;
        const qreal half = std::max(mainTopic ? kMinMainLeafHalfPx : 0.0, height / 2 - kLayoutMarginPx);
        const qreal gap = m_startsBranch ? m_spacing.branch : m_spacing.sibling;
        const qreal y = m_lastLeafStride < 0 ? cursor + half : cursor + gap + half;
        autoPosition.setY(y);
        m_lastLeafY = y;
        m_lastLeafStride = half;
        m_startsBranch = false;
        m_leafYs.append(y);
        cursor = y + half;
        return autoPosition.y();
    }
    // La Y della prima foglia del ramo si conosce solo dopo averla posizionata (vedi sopra).
    const qsizetype firstLeaf = m_leafYs.size();
    // Top edge of this subtree: where its first leaf starts (the same gap rule as the leaves).
    const qreal blockTop = m_lastLeafStride < 0 ? cursor : cursor + (m_startsBranch ? m_spacing.branch : m_spacing.sibling);
    for (const auto &child : node->children) if (!child->floating) layoutSubtree(child.get(), depth + 1, cursor, sign);
    // FEAT-072, live 2026-10-22: centred between its first and last direct child, then
    // at the height of the nearest child when one is clearly nearest, so that line is straight (one
    // child, the middle of an odd count, or a group pushed by a large subtree). Before: centred on
    // all the leaves of its branch.
    // The snap lines up the branch's own points, not the centres (FEAT-072, reference live 2026-10-23):
    // with underlined topics the line leaves the parent's underline and reaches the child's, so the
    // middle child's underline sits exactly at the parent's (a straight line).
    const auto anchorDy = [this](const MindNode *at, bool right) {
        const NodeItem *atItem = m_items.value(const_cast<MindNode *>(at));
        return atItem ? atItem->branchAnchor(right).y() : 0.0;
    };
    const qreal ownAnchor = anchorDy(node, sign > 0);
    QList<std::pair<qreal, qreal>> childYs;  // centre, centre moved so its entry point meets ownAnchor
    for (const auto &child : node->children)
        if (!child->floating) {
            const qreal childY = m_autoPositions.value(child.get()).y();
            childYs.append({childY, childY + anchorDy(child.get(), sign < 0) - ownAnchor});
        }
    // A brace's cusp stays halfway between its arms, at the centres (reference live 2026-10-24): no snap.
    const bool brace = isBraceParent(node);
    // Elsewhere the branch's points: the parent's line point halfway between the first and the last
    // child's (user's photos 2026-10-26: with two children the centres were halfway, so with underlined
    // topics the junction sat off the middle of the two lines).
    const auto point = [brace](const std::pair<qreal, qreal> &child) { return brace ? child.first : child.second; };
    qreal y = childYs.isEmpty() ? m_lastLeafY : (point(childYs.front()) + point(childYs.back())) / 2;
    if (childYs.size() > 1 && !brace) {
        std::sort(childYs.begin(), childYs.end(), [y](const auto &a, const auto &b) { return std::abs(a.second - y) < std::abs(b.second - y); });
        if (std::abs(childYs[1].second - y) - std::abs(childYs[0].second - y) > 0.5) y = childYs[0].second;
    } else if (childYs.size() == 1 && !brace) {
        y = childYs.front().second;
    }
    // FEAT-072, live: a topic taller than its children's block keeps its own height,
    // so the next topic does not come closer (before, a 41 px main topic with one 28 px child let
    // the next main topic come 6 px nearer).
    const qreal ownHalf = item ? std::max<qreal>(0, item->layoutHeight() / 2 - kLayoutMarginPx) : 0;
    if (y - ownHalf < blockTop) {
        const qreal shift = blockTop - (y - ownHalf);
        for (const auto &child : node->children) if (!child->floating) shiftSubtree(child.get(), shift);
        for (qsizetype i = firstLeaf; i < m_leafYs.size(); ++i) m_leafYs[i] += shift;
        m_lastLeafY += shift;
        cursor += shift;
        y += shift;
    }
    cursor = std::max(cursor, y + ownHalf);
    m_autoPositions[node].setY(y);  // il riferimento può essere invalidato dai figli
    return y;
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
    collectBoundaryRuns();  // FEAT-052: after the sizes, which a summary topic's branch needs
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

bool MindMapScene::childrenHidden(const MindNode *node) { return node->parent && node->collapsed; }

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

// FEAT-026 fase C (foto di riferimento dell'utente 2026-10-21). Ogni figlio ha il suo ingombro (il nodo
// con i suoi discendenti) e si mette accanto ai fratelli senza sovrapporsi:
// - organigramma: figli in riga sotto il genitore (sopra con la direzione "alto"), la riga centrata
//   sul genitore; 48 px dal centrale, 32 dagli altri; 32 px fra i rami principali, 8 fra gli altri;
// - grafico ad albero: figli in colonna sotto il genitore, a 24 px a destra (o a sinistra) del
//   tronco che scende dal suo centro; 16 px sotto il genitore, 32 fra i rami principali, 8 fra gli altri;
// - mappa mentale e grafico logico dentro questi tipi: figli in colonna accanto al genitore, come nella
//   mappa (32 px, 8 fra i fratelli).
// Su due lati (solo il centrale) i rami principali si dividono con balancedSides: in basso e in alto,
// o a destra e a sinistra. Ogni nodo si allarga dal lato libero (growSide).
QRectF MindMapScene::layoutBlock(MindNode *node, int inheritedSide, QHash<MindNode *, Placement> &rel) {
    using MapStructure::Type;
    using MapStructure::Direction;
    const NodeItem *item = m_items.value(node);
    const qreal halfWidth = item ? item->anchorHalfWidth() : 105.0;
    const qreal halfHeight = item ? std::max<qreal>(0, item->layoutHeight() / 2 - kLayoutMarginPx) : 20.0;
    const QRectF self(-halfWidth, -halfHeight, 2 * halfWidth, 2 * halfHeight);
    if (setsTreeTable(node)) return layoutTable(node, rel);
    QList<MindNode *> children;
    if (!childrenHidden(node))
        for (const auto &child : node->children) if (!child->floating) children.append(child.get());
    if (children.isEmpty()) return self;
    if (setsTimeline(node)) return layoutTimeline(node, self, children, rel);
    if (setsFishbone(node)) return layoutFishbone(node, self, children, rel);
    const Type type = MapStructure::typeOf(node);
    const Direction direction = MapStructure::directionOf(node);
    const bool isRoot = !node->parent;
    const bool org = type == Type::OrgChart;
    // FEAT-072 (reference live 2026-10-23): the trunk is only for the children of the topic that sets the
    // tree chart (the central topic, or a branch with that type); further down the children sit
    // beside their parent on the rounded bracket, like a logic chart.
    const bool tree = type == Type::TreeChart && (isRoot || MapStructure::typeFromKey(node->structure) == Type::TreeChart);
    // Lato dei figli: per i tipi a blocchi dalla direzione, per gli altri dal ramo (o dalla direzione).
    const auto sideFor = [&](Direction d) {
        if (d == Direction::Up || d == Direction::Left) return -1;
        if (d == Direction::Down || d == Direction::Right) return 1;
        return inheritedSide < 0 ? -1 : 1;
    };
    QList<int> sides(children.size(), sideFor(direction));
    if (isRoot && direction == Direction::BothSides && (org || tree)) {
        QList<double> weights;
        QList<int> fixed;
        const std::function<int(const MindNode *)> rows = [&rows](const MindNode *at) {
            int count = 0;
            if (!childrenHidden(at)) for (const auto &child : at->children) if (!child->floating) count += rows(child.get());
            return std::max(1, count);
        };
        for (MindNode *child : std::as_const(children)) {
            weights.append(rows(child));
            fixed.append(child->side == MindNode::Side::Right ? 1 : child->side == MindNode::Side::Left ? -1 : 0);
        }
        sides = MapStructure::balancedSides(weights, fixed);
    }
    QList<QRectF> blocks;
    for (int i = 0; i < children.size(); ++i)
        blocks.append(layoutBlock(children[i], org ? inheritedSide : sides[i], rel));
    // FEAT-052: boundaries on these children keep their room (user 2026-10-25; before, a known limit).
    for (int i = 0; i < children.size(); ++i) blocks[i] = blocks[i].marginsAdded(boundaryPad(children, i, org, true));
    QRectF block = self;
    for (const int side : {1, -1}) {
        QList<int> group;
        for (int i = 0; i < children.size(); ++i) if (sides[i] == side) group.append(i);
        if (group.isEmpty()) continue;
        if (org) {
            // Measured live 2026-10-23: main topics 100 px under the central topic, the others 40 (was 48 and 32).
            const qreal levelGap = (isRoot ? 100 : 40) * m_spacing.block;
            const qreal siblingGap = isRoot ? m_spacing.branch : m_spacing.sibling;  // foto di riferimento: circa 8 fra le foglie
            qreal total = -siblingGap;
            for (const int i : std::as_const(group)) total += blocks[i].width() + siblingGap;
            // Centrata sul genitore; in un ramo con tipo proprio dentro una mappa la riga parte dal
            // bordo del nodo rivolto al genitore, cosi' non copre il centrale ne' il ramo che arriva.
            qreal cursor = -total / 2;
            if (node->parent && !node->structure.isEmpty())
                cursor = inheritedSide < 0 ? std::min(cursor, self.right() - total) : std::max(cursor, self.left());
            // I nodi della riga sono in linea (foto dell'utente 2026-10-21): bordi allineati, e tutta la
            // riga si allontana quanto serve perche' nessun ramo (un grafico logico si allarga sopra e
            // sotto il suo nodo) arrivi alla linea che li collega al genitore.
            const auto halfHeightOf = [this](const MindNode *child) {
                const NodeItem *childItem = m_items.value(const_cast<MindNode *>(child));
                return childItem ? std::max<qreal>(0, childItem->layoutHeight() / 2 - kLayoutMarginPx) : 20.0;
            };
            qreal overhang = 0;
            for (const int i : std::as_const(group))
                overhang = std::max(overhang, side > 0 ? -blocks[i].top() - halfHeightOf(children[i]) : blocks[i].bottom() - halfHeightOf(children[i]));
            for (const int i : std::as_const(group)) {
                const qreal childHalfHeight = halfHeightOf(children[i]);
                const qreal x = cursor - blocks[i].left();
                const qreal y = side > 0 ? self.bottom() + levelGap + overhang + childHalfHeight : self.top() - levelGap - overhang - childHalfHeight;
                rel[children[i]] = {QPointF(x, y), 0};
                block = block.united(blocks[i].translated(x, y));
                cursor += blocks[i].width() + siblingGap;
            }
        } else if (tree) {
            const qreal siblingGap = isRoot ? m_spacing.branch : m_spacing.sibling;
            // Measured live 2026-10-23: the first branch starts 56 px under the parent, each topic 50 px
            // beside the trunk (was 16 and 24).
            qreal cursor = self.bottom() + 56 * m_spacing.block;
            for (const int i : std::as_const(group)) {
                const NodeItem *childItem = m_items.value(children[i]);
                const qreal childHalf = childItem ? childItem->anchorHalfWidth() : 105.0;
                const qreal x = side * (50 * m_spacing.block + childHalf);
                const qreal y = cursor - blocks[i].top();
                rel[children[i]] = {QPointF(x, y), side};
                block = block.united(blocks[i].translated(x, y));
                cursor = y + blocks[i].bottom() + siblingGap;
            }
        } else {
            const qreal levelGap = type == Type::Brace ? braceLevelGap(node) : isRoot ? m_spacing.rootLevel : m_spacing.level;
            const qreal siblingGap = isRoot ? m_spacing.branch : m_spacing.sibling;
            qreal total = -siblingGap;
            for (const int i : std::as_const(group)) total += blocks[i].height() + siblingGap;
            qreal cursor = -total / 2;
            for (const int i : std::as_const(group)) {
                const NodeItem *childItem = m_items.value(children[i]);
                const qreal childHalf = childItem ? childItem->anchorHalfWidth() : 105.0;
                const qreal x = side * (halfWidth + levelGap + childHalf);
                const qreal y = cursor - blocks[i].top();
                rel[children[i]] = {QPointF(x, y), side};
                block = block.united(blocks[i].translated(x, y));
                cursor += blocks[i].height() + siblingGap;
            }
        }
    }
    // FEAT-052: the summaries over these children count in the branch, so its neighbours keep clear.
    QList<QRectF> placed;
    QList<QPointF> centres;
    for (int i = 0; i < children.size(); ++i) {
        const QPointF at = rel.value(children[i]).offset;
        placed.append(blocks[i].translated(at));
        centres.append(at);
    }
    if (const QRectF room = summaryRoom(node, children, placed, centres, QPointF()); !room.isNull()) block = block.united(room);
    return block;
}

// FEAT-028: the header across the table, the main topics in rows under it, each child in the next
// column beside its parent. A column is as wide as the widest topic of its level; a cell is as tall
// as its rows (a leaf at least kTableRowPx) and a leaf reaches the table's right edge. Every topic
// keeps its own shape's left edge on the cell's (text on the left) and its centre on the cell's
// middle row; the header's text stays centred. Topics of a table are all cells (stage 1: a type of
// their own does not apply yet).
QRectF MindMapScene::layoutTable(MindNode *owner, QHash<MindNode *, Placement> &rel) {
    const auto shapeOf = [this](const MindNode *node) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
        return item ? item->shapeRect() : QRectF(-105, -20, 210, 40);
    };
    // Stage 2: a topic with a type of its own (not Mind map) holds its branch in its cell,
    // laid out by that type; the cell is a leaf of the table.
    const auto holdsBranch = [owner](const MindNode *node) {
        if (node == owner || node->structure.isEmpty() || childrenHidden(node)) return false;
        if (MapStructure::typeFromKey(node->structure) == MapStructure::Type::MindMap) return false;
        return std::any_of(node->children.cbegin(), node->children.cend(), [](const auto &child) { return !child->floating; });
    };
    const auto rowsOf = [&holdsBranch](const MindNode *node) {
        QList<MindNode *> rows;
        if (!childrenHidden(node) && !holdsBranch(node))
            for (const auto &child : node->children) if (!child->floating) rows.append(child.get());
        return rows;
    };
    QList<qreal> columns;
    struct Held { QRectF block; qreal leftEdge = 0; int depth = 0; };  // block in the topic's coordinates
    QHash<const MindNode *, Held> held;
    const std::function<void(const MindNode *, int)> measure = [&](const MindNode *node, int depth) {
        for (MindNode *row : rowsOf(node)) {
            if (columns.size() <= depth) columns.resize(depth + 1, 0.0);
            columns[depth] = std::max(columns[depth], shapeOf(row).width());
            if (holdsBranch(row)) {
                const QRectF block = layoutBlock(row, 1, rel);
                // The topic stays on the cell's left edge, as the other cells; a branch reaching further
                // left (an org chart under it, a table's header) keeps the inset there too.
                const qreal shapeLeft = shapeOf(row).left();
                held.insert(row, {block, block.left() < shapeLeft - 0.5 ? block.left() - kTableInsetPx : shapeLeft, depth});
            }
            measure(row, depth + 1);
        }
    };
    measure(owner, 0);
    const QRectF ownShape = shapeOf(owner);
    qreal width = 0;
    for (const qreal column : std::as_const(columns)) width += column;
    if (!columns.isEmpty() && ownShape.width() > width) columns.back() += ownShape.width() - width;
    width = std::max(width, ownShape.width());
    // A branch wider than the columns widens the table (its last column).
    for (const Held &branch : std::as_const(held)) {
        qreal start = 0;
        for (int d = 0; d < branch.depth; ++d) start += columns[d];
        const qreal need = start + branch.block.right() + kTableInsetPx - branch.leftEdge;
        if (need > width) {
            columns.back() += need - width;
            width = need;
        }
    }
    const qreal headerHeight = columns.isEmpty() ? ownShape.height() : std::max(kTableRowPx, ownShape.height());
    const QRectF header(ownShape.center().x() - width / 2, ownShape.center().y() - headerHeight / 2, width, headerHeight);
    m_tableCells.insert(owner->id, {header, owner->id, {}});
    QList<qreal> lefts{header.left()};
    for (const qreal column : std::as_const(columns)) lefts.append(lefts.back() + column);
    const std::function<qreal(const MindNode *)> heightOf = [&](const MindNode *node) {
        qreal rows = 0;
        for (const MindNode *row : rowsOf(node)) rows += heightOf(row);
        if (const auto it = held.constFind(node); it != held.cend()) rows = it->block.height() + 2 * kTableInsetPx;
        return std::max({rows, kTableRowPx, shapeOf(node).height()});
    };
    // A cell taller than its rows gives the rest to its last row.
    const std::function<void(MindNode *, int, qreal, qreal, QPointF, const QString &)> place =
        [&](MindNode *node, int depth, qreal top, qreal height, QPointF parentAt, const QString &levelOne) {
            const QRectF shape = shapeOf(node);
            const QList<MindNode *> rows = rowsOf(node);
            const qreal left = lefts[depth];
            const QRectF cell(left, top, rows.isEmpty() ? header.right() - left : columns[depth], height);
            QPointF at(left - shape.left(), cell.center().y() - shape.center().y());
            if (const auto it = held.constFind(node); it != held.cend()) {
                // The branch inside the cell, centred in its height.
                at = QPointF(left - it->leftEdge, cell.center().y() - it->block.center().y());
                if (setsTreeTable(node)) {  // its own table: this cell around it, the topic its header
                    rel[node] = {at - parentAt, 1};
                    m_tableFrames.insert(node->id, {cell.translated(-at), owner->id, levelOne, true});
                    return;
                }
            }
            rel[node] = {at - parentAt, 1};
            m_tableCells.insert(node->id, {cell.translated(-at), owner->id, levelOne, held.contains(node)});
            qreal y = top;
            for (int i = 0; i < rows.size(); ++i) {
                const qreal rowHeight = i + 1 < rows.size() ? heightOf(rows[i]) : top + height - y;
                place(rows[i], depth + 1, y, rowHeight, at, levelOne);
                y += rowHeight;
            }
        };
    qreal y = header.bottom();
    for (MindNode *main : rowsOf(owner)) {
        const qreal height = heightOf(main);
        place(main, 0, y, height, QPointF(), main->id);
        y += height;
    }
    return QRectF(header.left(), header.top(), width, y - header.top());
}

bool MindMapScene::isTableCell(const MindNode *node) const {
    const auto it = m_tableCells.constFind(node->id);
    return (it != m_tableCells.cend() && it->owner != node->id) || m_tableFrames.contains(node->id);
}

// FEAT-028: colours from the topics as they are now (a fill set by hand on a cell wins), so a style
// change needs no new layout. A step of the history restores positions without a layout: when the
// cells no longer match the types, they are worked out again (the auto positions only).
bool MindMapScene::tablePlaceStale(const MindNode *node) const {
    const auto place = m_tableCells.constFind(node->id);
    if (place == m_tableCells.cend()) {
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
        const auto place = m_tableCells.constFind(it.key()->id);
        if (place == m_tableCells.cend()) {
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
            for (auto frame = m_tableFrames.constFind(outer); frame != m_tableFrames.cend(); frame = m_tableFrames.constFind(outer))
                outer = frame->owner;
            const NodeItem *outerItem = m_itemsById.value(outer);
            const MindNode *borderFrom = outerItem ? outerItem->node() : ownerItem ? ownerItem->node() : node;
            return std::pair{fill, solidFill(borderFrom)};
        };
        const auto [fill, border] = colours(*place);
        NodeItem::TableCell cell{place->rect, fill, border, place->holdsBranch};
        if (const auto frame = m_tableFrames.constFind(node->id); frame != m_tableFrames.cend()) {
            std::tie(cell.frameFill, cell.frameBorder) = colours(*frame);
            cell.frame = frame->rect;
            cell.holdsBranch = true;
        }
        it.value()->setTableCell(cell);
    }
}

// FEAT-026 phase E (reference live 2026-10-24): main topics left to right along the axis, the first one's
// children above, the next one's below, and so on; their descendants beside them like a logic chart.
QRectF MindMapScene::layoutTimeline(MindNode *node, const QRectF &self, const QList<MindNode *> &mains,
                                    QHash<MindNode *, Placement> &rel) {
    const bool offAxis = MapStructure::directionOf(node) == MapStructure::Direction::OffAxis;
    const qreal gap = (offAxis ? kTimelineOffAxisGapPx : kTimelineGapPx) * m_spacing.block;
    const auto halfHeightOf = [this](const MindNode *at) {
        const NodeItem *atItem = m_items.value(const_cast<MindNode *>(at));
        return atItem ? std::max<qreal>(0, atItem->layoutHeight() / 2 - kLayoutMarginPx) : 20.0;
    };
    QRectF block = self;
    qreal previousRight = self.right();
    std::optional<QRectF> lastOnSide[2];  // the previous branch above, below
    QList<QRectF> mainBranches;  // FEAT-052: for the summaries over the main topics
    QList<QPointF> mainCentres;
    for (int i = 0; i < mains.size(); ++i) {
        MindNode *main = mains[i];
        const int side = i % 2 == 0 ? -1 : 1;
        const NodeItem *mainItem = m_items.value(main);
        const qreal halfWidth = mainItem ? mainItem->anchorHalfWidth() : 105.0;
        const qreal halfHeight = halfHeightOf(main);
        QRectF branch(-halfWidth, -halfHeight, 2 * halfWidth, 2 * halfHeight);
        if (!childrenHidden(main)) {
            QList<MindNode *> subs;
            for (const auto &child : main->children) if (!child->floating) subs.append(child.get());
            QList<QRectF> blocks;
            qreal total = -m_spacing.sibling;
            for (int j = 0; j < subs.size(); ++j) {
                blocks.append(layoutBlock(subs[j], 1, rel).marginsAdded(boundaryPad(subs, j, false, true)));  // FEAT-052
                total += blocks.back().height() + m_spacing.sibling;
            }
            qreal cursor = side < 0 ? -halfHeight - kTimelineChildGapPx - total : halfHeight + kTimelineChildGapPx;
            QList<QRectF> placed;
            QList<QPointF> centres;
            for (int j = 0; j < subs.size(); ++j) {
                const NodeItem *subItem = m_items.value(subs[j]);
                const qreal x = kTimelineStemIndentPx + (subItem ? subItem->anchorHalfWidth() : 105.0);
                const qreal y = cursor - blocks[j].top();
                rel[subs[j]] = {QPointF(x, y), 1};
                placed.append(blocks[j].translated(x, y));
                centres.append(QPointF(x, y));
                branch = branch.united(placed.back());
                cursor += blocks[j].height() + m_spacing.sibling;
            }
            // FEAT-052: room for the summaries over the subtopics.
            if (const QRectF room = summaryRoom(main, subs, placed, centres, QPointF()); !room.isNull()) branch = branch.united(room);
        }
        branch = branch.marginsAdded(boundaryPad(mains, i, true, true));  // FEAT-052
        const qreal y = offAxis ? side * (kTimelineAxisGapPx + halfHeight) : 0;
        qreal x = !offAxis ? previousRight + gap + halfWidth
                : i == 0   ? previousRight + kTimelineAxisGapPx + halfWidth
                           : previousRight + gap;
        std::optional<QRectF> &previous = lastOnSide[side < 0 ? 0 : 1];
        if (previous) x = std::max(x, previous->right() + gap - branch.left());
        rel[main] = {QPointF(x, y), 1};
        previous = branch.translated(x, y);
        block = block.united(*previous);
        previousRight = x + halfWidth;
        mainBranches.append(*previous);
        mainCentres.append(QPointF(x, y));
    }
    if (offAxis) block.setRight(std::max(block.right(), previousRight + kTimelineAxisOvershootPx));
    // FEAT-052: room for the summaries over the main topics (below or above the axis).
    if (const QRectF room = summaryRoom(node, mains, mainBranches, mainCentres, QPointF()); !room.isNull()) block = block.united(room);
    return block;
}

// FEAT-026 phase E (reference live 2026-10-24): main topics along the spine, above and below in turn, their
// subtopics on the bone, their descendants beside them like a logic chart. Worked out with the head on the
// left (direction Right) and mirrored for Left.
QRectF MindMapScene::layoutFishbone(MindNode *node, const QRectF &self, const QList<MindNode *> &mains,
                                    QHash<MindNode *, Placement> &rel) {
    const int s = MapStructure::directionOf(node) == MapStructure::Direction::Left ? -1 : 1;
    const auto mirrored = [s](const QRectF &r) { return s > 0 ? r : QRectF(-r.right(), r.top(), r.width(), r.height()); };
    const auto halfHeightOf = [this](const MindNode *at) {
        const NodeItem *atItem = m_items.value(const_cast<MindNode *>(at));
        return atItem ? std::max<qreal>(0, atItem->layoutHeight() / 2 - kLayoutMarginPx) : 20.0;
    };
    const auto halfWidthOf = [this](const MindNode *at) {
        const NodeItem *atItem = m_items.value(const_cast<MindNode *>(at));
        return atItem ? atItem->anchorHalfWidth() : 105.0;
    };
    const qreal headGap = kFishboneHeadGapPx * m_spacing.block;
    const qreal branchGap = kFishboneBranchGapPx * m_spacing.block;
    const qreal footGap = kFishboneFootGapPx * m_spacing.block;
    QRectF block = self;  // symmetric, the same in either frame
    QList<QRectF> mainBranches;  // FEAT-052: for the summaries over the main topics
    QList<QPointF> mainCentres;
    std::optional<qreal> previousFoot;
    std::optional<QRectF> lastOnSide[2];  // the previous branch above, below (head-left frame)
    for (int i = 0; i < mains.size(); ++i) {
        MindNode *main = mains[i];
        const int v = i % 2 == 0 ? -1 : 1;  // above, below
        // The bone ends on the shape, which a thick border makes taller than the layout height.
        const NodeItem *mainItem = m_items.value(main);
        const QRectF mainShape = mainItem ? mainItem->shapeRect() : QRectF(-105, -20, 210, 40);
        const qreal halfWidth = halfWidthOf(main);
        // Subtopics outward from the spine; distances from the spine to their centres.
        QList<MindNode *> subs;
        if (!childrenHidden(main))
            for (const auto &child : main->children) if (!child->floating) subs.append(child.get());
        if (v < 0) std::reverse(subs.begin(), subs.end());
        QList<QRectF> blocks;
        QList<qreal> centres;
        qreal distance = kFishboneBarePx;
        qreal previousCentre = 0, previousAway = 0;
        for (int j = 0; j < subs.size(); ++j) {
            // FEAT-052: the list runs outward from the spine, upward above it.
            blocks.append(mirrored(layoutBlock(subs[j], s, rel)).marginsAdded(boundaryPad(subs, j, false, v > 0)));
            const QRectF &b = blocks.back();
            const qreal toward = v > 0 ? -b.top() : b.bottom();
            const qreal away = v > 0 ? b.bottom() : -b.top();
            const qreal nodeHalf = halfHeightOf(subs[j]);
            const qreal centre = j == 0 ? kFishboneSpineCentrePx + std::max<qreal>(0, toward - nodeHalf)
                                        : std::max(previousCentre + kFishboneSubPitchPx, previousAway + m_spacing.sibling + toward);
            centres.append(centre);
            previousCentre = centre;
            previousAway = centre + away;
            distance = std::max(centre + kFishboneTopicCentrePx, previousAway + m_spacing.sibling);
        }
        // The branch with its foot at x = 0 (head-left frame).
        const QPointF mainAt(distance * kFishboneSlope, v * distance + (v < 0 ? -mainShape.bottom() : -mainShape.top()));
        QRectF branch(mainAt.x() - halfWidth, mainAt.y() + mainShape.top(), 2 * halfWidth, mainShape.height());
        QList<QPointF> subAt;
        QList<QRectF> placed;
        for (int j = 0; j < subs.size(); ++j) {
            subAt.append(QPointF(centres[j] * kFishboneSlope + kFishboneTickPx + halfWidthOf(subs[j]), v * centres[j]));
            placed.append(blocks[j].translated(subAt.back()));
            branch = branch.united(placed.back());
        }
        // FEAT-052: room for the summaries over the subtopics (user's photo 2026-10-25: one covered
        // the next main topic).
        if (const QRectF room = summaryRoom(main, subs, placed, subAt, mainAt); !room.isNull()) branch = branch.united(room);
        branch = branch.marginsAdded(boundaryPad(mains, i, true, true));  // FEAT-052: head-left frame
        branch.setLeft(std::min<qreal>(branch.left(), 0));
        if (v < 0) branch.setBottom(std::max<qreal>(branch.bottom(), 0));
        else branch.setTop(std::min<qreal>(branch.top(), 0));
        qreal foot = self.right() + headGap - branch.left();
        if (previousFoot) foot = std::max(foot, *previousFoot + footGap);
        std::optional<QRectF> &previous = lastOnSide[v < 0 ? 0 : 1];
        if (previous) foot = std::max(foot, previous->right() + branchGap - branch.left());
        rel[main] = {QPointF(s * (foot + mainAt.x()), mainAt.y()), s};
        for (int j = 0; j < subs.size(); ++j) rel[subs[j]] = {QPointF(s * (subAt[j].x() - mainAt.x()), subAt[j].y() - mainAt.y()), s};
        previous = branch.translated(foot, 0);
        previousFoot = foot;
        block = block.united(*previous);
        mainBranches.append(*previous);
        mainCentres.append(QPointF(foot + mainAt.x(), mainAt.y()));
    }
    m_fishboneReach.insert(node, block.right());
    // FEAT-052: room for the summaries over the main topics, along the spine.
    if (const QRectF room = summaryRoom(node, mains, mainBranches, mainCentres, QPointF(), true); !room.isNull()) block = block.united(room);
    return mirrored(block);
}

void MindMapScene::placeBlock(MindNode *node, const QHash<MindNode *, Placement> &rel) {
    if (childrenHidden(node)) return;
    const QPointF origin = m_autoPositions.value(node);
    for (const auto &child : node->children) {
        const auto it = rel.constFind(child.get());
        if (child->floating || it == rel.cend()) continue;
        m_autoPositions[child.get()] = origin + it->offset;
        m_sides[child.get()] = it->growSide;
        placeBlock(child.get(), rel);
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
        if (node->parent && !node->summaryTopic && firstChild != node->children.cend()) {  // FEAT-052: a summary topic stays open
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

void MindMapScene::placeBranch(MindNode *node, QPointF inheritedOffset) {
    const auto autoPosition = m_autoPositions.constFind(node);
    if (autoPosition == m_autoPositions.cend()) return;  // nodo flottante: resta dov'e', con il suo ramo
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

void MindMapScene::shiftSubtree(MindNode *node, qreal dy) {
    m_autoPositions[node].ry() += dy;
    if (childrenHidden(node)) return;  // FEAT-016: i figli nascosti non hanno un posto
    for (const auto &child : node->children) if (!child->floating) shiftSubtree(child.get(), dy);
}

// FEAT-012 (passo 4a): Mappa mentale su due lati. Il centrale sta in (0, 0); i rami principali si
// dividono fra destra e sinistra (MapStructure::balancedSides sui loro ingombri verticali, oppure i
// lati scelti); a destra nell'ordine dall'alto in basso, a sinistra gli altri dal basso in alto (in
// senso orario); su due lati i rami principali di ogni lato si dispongono attorno al
// centrale, con una direzione sola il lato e' centrato sul centrale.
void MindMapScene::computeAutoPositions() {
    const qreal *spacing = m_model->sheetStyle.compact ? kCompactSpacing : kNormalSpacing;
    m_spacing = {spacing[0], spacing[1], spacing[2], spacing[3], spacing[4]};
    m_autoPositions.clear();
    m_sides.clear();
    m_fishboneReach.clear();
    m_tableCells.clear();
    m_tableFrames.clear();
    MindNode *root = m_model->root();
    m_autoPositions[root] = QPointF(0, 0);
    m_sides[root] = 0;
    if (MapStructure::laysOutAsBlock(MapStructure::typeOf(root))) {  // FEAT-026 fasi C ed E
        QHash<MindNode *, Placement> rel;
        layoutBlock(root, 1, rel);
        placeBlock(root, rel);
    } else {
    QList<MindNode *> mains;
    for (const auto &child : root->children) if (!child->floating) mains.append(child.get());
    // Peso di ogni ramo: le sue righe, cioe' le foglie disposte (un ramo senza figli = 1), come
    // Foto di riferimento (dell'utente 2026-10-21; prima l'ingombro in pixel, che con nodi di misure diverse
    // da quelle del riferimento spostava i rami molto prima).
    const std::function<int(const MindNode *)> rows = [&rows](const MindNode *node) {
        int count = 0;
        if (!childrenHidden(node))
            for (const auto &child : node->children) if (!child->floating) count += rows(child.get());
        return std::max(1, count);
    };
    QList<double> weights;
    QList<int> fixed;
    for (MindNode *main : std::as_const(mains)) {
        weights.append(rows(main));
        fixed.append(main->side == MindNode::Side::Right ? 1 : main->side == MindNode::Side::Left ? -1 : 0);
    }
    QList<int> sides;
    const MapStructure::Direction direction = MapStructure::directionOf(root);
    if (direction == MapStructure::Direction::Right || direction == MapStructure::Direction::Left) {
        sides = QList<int>(mains.size(), direction == MapStructure::Direction::Right ? 1 : -1);
    } else if (m_model->balanced && !m_model->sheetStyle.freeBranches) {
        sides = MapStructure::balancedSides(weights, fixed);
    } else {
        // Bilanciamento spento: lati fissi. Un ramo nuovo (automatico) va dal lato con meno rami
        // principali, a parita' a destra, e il lato gli resta (contratto, "Nodi nuovi").
        int right = 0, left = 0;
        for (const int side : std::as_const(fixed)) (side > 0 ? right : left) += side != 0;
        for (int i = 0; i < mains.size(); ++i) {
            if (fixed[i] == 0) {
                fixed[i] = left < right ? -1 : 1;
                (fixed[i] > 0 ? right : left)++;
                mains[i]->side = fixed[i] > 0 ? MindNode::Side::Right : MindNode::Side::Left;
            }
        }
        sides = fixed;
    }
    const NodeItem *rootItem = m_items.value(root);
    const qreal rootHalfHeight = rootItem ? rootItem->layoutHeight() / 2 : 0;
    // FEAT-072 A2, live: with two or more main topics on a side, the first and last may reach
    // into the central box by half its vertical padding (two per side: centres 130 px apart). A lone
    // topic was not measured live and keeps the rule from the photos.
    const qreal rootClearance = rootItem ? rootHalfHeight - kLayoutMarginPx - NodeStyle::textPadding(root).y() / 2 : 0;
    const auto halfHeight = [this](MindNode *node) {
        const NodeItem *item = m_items.value(node);
        return item ? item->layoutHeight() / 2 : 0.0;
    };
    for (const int sign : {1, -1}) {
        QList<MindNode *> group;
        for (int i = 0; i < mains.size(); ++i) if (sides[i] == sign) group.append(mains[i]);
        for (MindNode *main : std::as_const(group)) m_sides[main] = sign;  // anche nascosti (FEAT-016)
        if (group.isEmpty()) continue;
        if (sign < 0) std::reverse(group.begin(), group.end());  // in senso orario: dal basso in alto
        qreal cursor = 0;
        m_lastLeafStride = -1;
        m_leafYs.clear();
        for (MindNode *main : std::as_const(group)) {
            m_startsBranch = true;
            layoutSubtree(main, 1, cursor, sign);
        }
        if (direction != MapStructure::Direction::BothSides) {
            // The brace's cusp: halfway between the first and last main topic (reference live 2026-10-24).
            const qreal middle = isBraceParent(root)
                ? (m_autoPositions.value(group.front()).y() + m_autoPositions.value(group.back()).y()) / 2
                : (m_leafYs.value(0, m_lastLeafY) + m_lastLeafY) / 2;
            for (MindNode *main : std::as_const(group)) shiftSubtree(main, -middle);
            continue;
        }
        // Due lati (foto dell'utente 2026-10-21): conta la posizione dei rami principali,
        // non l'ingombro dei loro figli. Il primo e l'ultimo del lato stanno alla stessa distanza dal
        // centrale (con un numero dispari quello di mezzo e' alla sua altezza) e non lo affiancano:
        // il primo sta sopra il suo bordo superiore, l'ultimo sotto quello inferiore. Se i rami sono
        // piu' vicini di cosi', lo spazio in piu' si divide fra loro. Un ramo da solo segue il senso
        // orario: a destra sopra il centrale, a sinistra sotto (foto di riferimento 2026-10-21).
        MindNode *first = group.front();
        MindNode *last = group.back();
        if (group.size() == 1) {
            const qreal y = sign > 0 ? -rootHalfHeight - halfHeight(first) : rootHalfHeight + halfHeight(first);
            shiftSubtree(first, y - m_autoPositions.value(first).y());
            continue;
        }
        const qreal span = m_autoPositions.value(last).y() - m_autoPositions.value(first).y();
        const qreal neededSpan = 2 * rootClearance + halfHeight(first) + halfHeight(last);
        if (span < neededSpan) {
            const qreal extra = (neededSpan - span) / (group.size() - 1);
            for (int i = 1; i < group.size(); ++i) shiftSubtree(group[i], i * extra);
        }
        // Centred by the branch points, not the centres (FEAT-072, user's reference photos 2026-10-23):
        // underlines sit as far above the central topic as below it.
        const auto branchY = [this, sign](MindNode *node) {
            const NodeItem *item = m_items.value(node);
            return m_autoPositions.value(node).y() + (item ? item->branchAnchor(sign < 0).y() : 0.0);
        };
        const qreal middle = (branchY(first) + branchY(last)) / 2;
        for (MindNode *main : std::as_const(group)) shiftSubtree(main, -middle);
    }
    }  // mappa mentale e grafico logico
    // Argomenti mobili: restano dove sono e i loro figli si dispongono a destra, centrati su di loro
    // (prima i figli restavano dove erano nati).
    for (const auto &child : root->children) {
        MindNode *floatingTopic = child.get();
        if (!floatingTopic->floating) continue;
        m_autoPositions[floatingTopic] = floatingTopic->position;
        m_sides[floatingTopic] = 1;
        qreal cursor = 0;
        m_lastLeafStride = -1;
        m_leafYs.clear();
        m_startsBranch = true;
        QList<MindNode *> laid;
        if (floatingTopic->collapsed) continue;  // FEAT-016
        for (const auto &grandchild : floatingTopic->children)
            if (!grandchild->floating) { layoutSubtree(grandchild.get(), 2, cursor, 1); laid.append(grandchild.get()); }
        if (laid.isEmpty()) continue;
        const qreal middle = (m_leafYs.value(0, m_lastLeafY) + m_lastLeafY) / 2;
        for (MindNode *node : std::as_const(laid)) shiftSubtree(node, floatingTopic->position.y() - middle);
    }
    // FEAT-034: ogni nodo si allarga dal lato lontano dal genitore (centrale e nodi liberi dai due lati).
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        const MindNode *node = it.key();
        it.value()->setGrowSide(node->floating || !node->parent ? 0 : m_sides.value(node, 1));
    }
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
        for (auto *nodeItem : std::as_const(moved)) {
            MindNode *node = nodeItem->node();
            const auto autoPosition = m_autoPositions.constFind(node);
            if (node->floating || autoPosition == m_autoPositions.cend()) continue;
            node->manualOffset = node->position - autoPosition.value() - inheritedOffset(node);
        }
    }
    updateSceneRect();
}

MindNode *MindMapScene::neighbour(MindNode *node, Qt::Key key) const {
    using MapStructure::Type;
    using MapStructure::Direction;
    if (!node) return nullptr;
    const auto visible = [this](const MindNode *at) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(at));
        return item && item->isVisible();
    };
    const auto centre = [this](const MindNode *at) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(at));
        return item ? item->pos() : QPointF();
    };
    const auto sideOfNode = [this](const MindNode *at) { return at->floating ? 1 : sideOf(at); };
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
        if (!childrenHidden(at))
            for (const auto &child : at->children) if (!child->floating && visible(child.get())) list.append(child.get());
        return list;
    };
    const QList<MindNode *> children = shownChildren(node);
    // FEAT-028 (user's request 2026-10-25): in a tree table Up and Down go to the cell drawn right above
    // or below, whatever its level (a level-5 cell to the level-3 leaf under it); among the cells
    // touching that edge, the one starting nearest on the left. Left and Right follow the usual rules
    // (Right to the first child, Left to the parent).
    if ((key == Qt::Key_Up || key == Qt::Key_Down) && m_tableCells.contains(node->id)) {
        const QString owner = m_tableCells.value(node->id).owner;
        const QRectF from = nodeOutlineSceneRect(node);
        MindNode *best = nullptr;
        qreal bestGap = 0, bestLeft = 0;
        for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
            MindNode *other = it.key();
            const auto place = m_tableCells.constFind(other->id);
            if (other == node || place == m_tableCells.cend() || place->owner != owner || !visible(other)) continue;
            const QRectF to = nodeOutlineSceneRect(other);
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
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        MindNode *other = it.key();
        if (other == node || !other->parent || !visible(other) || depthOf(other) != depth || sideOfNode(other) != side) continue;
        const qreal there = alongX ? centre(other).x() : centre(other).y();
        const qreal distance = forward ? there - here : here - there;
        if (distance <= 0.5) continue;
        if (!best || distance < bestDistance) { best = other; bestDistance = distance; }
    }
    if (best) return best;
    // No topic of the same level that way (user's request 2026-10-25): the nearest of any level on
    // the same side, so a deep topic still reaches the branch below it.
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        MindNode *other = it.key();
        if (other == node || !other->parent || !visible(other) || sideOfNode(other) != side) continue;
        const qreal there = alongX ? centre(other).x() : centre(other).y();
        const qreal distance = forward ? there - here : here - there;
        if (distance <= 0.5) continue;
        if (!best || distance < bestDistance) { best = other; bestDistance = distance; }
    }
    return best;
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

MindMapScene::ReorderTarget MindMapScene::reorderTargetAt(const MindNode *dragged, const QPointF &scenePos) const {
    using Kind = ReorderTarget::Kind;
    ReorderTarget result;
    if (!dragged || !dragged->parent) return result;  // il centrale non si sposta
    MindNode *root = m_model->root();
    const auto boxOf = [this](const MindNode *node) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
        if (item && item->tableCell()) return item->mapRectToScene(item->tableCell()->rect);  // FEAT-028
        return item ? item->mapRectToScene(item->shapeRect()) : QRectF();
    };
    // Candidati: nodi disposti e visibili, fuori dal ramo trascinato (i nodi liberi e i loro rami no).
    const auto candidate = [&](const MindNode *node) {
        const NodeItem *item = m_items.value(const_cast<MindNode *>(node));
        if (!item || !item->isVisible() || !m_autoPositions.contains(const_cast<MindNode *>(node))) return false;
        for (const MindNode *up = node; up; up = up->parent) if (up == dragged) return false;
        return true;
    };
    // Lato verso cui crescono i figli di un nodo; per il centrale quello del mouse.
    const auto outward = [&](const MindNode *node) {
        if (node == root) return scenePos.x() < boxOf(root).center().x() ? -1 : 1;
        return sideOf(node) < 0 ? -1 : 1;
    };
    // Figli disposti del genitore dal lato s, dall'alto in basso, senza il trascinato (gli argomenti
    // mobili non sono rami del centrale).
    const auto visualChildren = [&](const MindNode *parent, int s) {
        QList<MindNode *> list;
        for (const auto &child : parent->children)
            if (child.get() != dragged && !child->floating && candidate(child.get()) && (parent != root || outward(child.get()) == s)) list.append(child.get());
        std::sort(list.begin(), list.end(), [&](const MindNode *x, const MindNode *y) { return boxOf(x).center().y() < boxOf(y).center().y(); });
        return list;
    };
    // 1. Sopra un nodo: suo figlio, in fondo. 2. Oltre il suo lato esterno: il piu' vicino.
    MindNode *parent = nullptr;
    bool onBody = false;
    qreal bestDistance = 0;
    for (auto it = m_items.cbegin(); it != m_items.cend(); ++it) {
        MindNode *node = it.key();
        if (!candidate(node)) continue;
        const QRectF box = boxOf(node);
        if (box.adjusted(-kReorderBodyMarginPx, -kReorderBodyMarginPx, kReorderBodyMarginPx, kReorderBodyMarginPx).contains(scenePos)) {
            parent = node;
            onBody = true;
            break;
        }
        const int s = outward(node);
        const qreal distance = (scenePos.x() - (s > 0 ? box.right() : box.left())) * s;
        if (distance < 0) continue;
        const QList<MindNode *> children = visualChildren(node, s);
        QRectF span = box;
        qreal reach = kReorderLeafReachPx;
        if (!children.isEmpty()) {
            reach = 0;
            for (const MindNode *child : children) {
                const QRectF childBox = boxOf(child);
                span = span.united(childBox);
                reach = std::max(reach, (s > 0 ? childBox.right() - box.right() : box.left() - childBox.left()));
            }
        }
        if (distance > reach || scenePos.y() < span.top() - kReorderVerticalReachPx || scenePos.y() > span.bottom() + kReorderVerticalReachPx) continue;
        if (!parent || distance < bestDistance) { parent = node; bestDistance = distance; }
    }
    if (!parent) {
        // Sullo sfondo: argomento mobile, ma non sopra il nodo stesso.
        const QRectF own = boxOf(dragged);
        if (!own.adjusted(-kReorderBodyMarginPx, -kReorderBodyMarginPx, kReorderBodyMarginPx, kReorderBodyMarginPx).contains(scenePos))
            result.kind = Kind::Float;
        return result;
    }
    const int s = outward(parent);
    const QList<MindNode *> children = visualChildren(parent, s);
    const auto rowOf = [](const MindNode *node) {
        const auto &siblings = node->parent->children;
        return int(std::find_if(siblings.cbegin(), siblings.cend(), [node](const auto &child) { return child.get() == node; }) - siblings.cbegin());
    };
    // Posto fra i figli visibili (dall'alto): sopra il nodo = in fondo nell'ordine.
    const bool mirrored = parent == root && s < 0;  // a sinistra i rami principali vanno dal basso in alto
    int index = int(children.size());
    if (!onBody) {
        index = 0;
        while (index < children.size() && boxOf(children[index]).center().y() < scenePos.y()) ++index;
    } else if (mirrored) {
        index = 0;  // in fondo nell'ordine = in alto a sinistra
    }
    if (children.isEmpty()) result.row = int(parent->children.size());
    else if (index < children.size()) result.row = rowOf(children[index]) + (mirrored ? 1 : 0);
    else result.row = rowOf(children.back()) + (mirrored ? 0 : 1);
    if (onBody && !mirrored) result.row = int(parent->children.size());
    if (parent == root) result.row = std::min(result.row, m_model->firstFloatingRow());  // prima degli argomenti mobili
    result.parent = parent;
    if (parent == root && s != sideOf(dragged)) result.side = s;
    // Stesso genitore e stesso posto (come MindMapModel::moveNode): nessun effetto.
    if (parent == dragged->parent && result.side == 0) {
        const int from = rowOf(dragged);
        const int destination = from < result.row ? result.row - 1 : result.row;
        if (destination == from) return {};
    }
    // Segnaposto: alla distanza dei figli dal genitore, fra i fratelli vicini (o al centro del
    // genitore se non ha figli da quel lato).
    const QRectF box = boxOf(parent);
    const qreal gap = parent == root ? m_spacing.rootLevel : m_spacing.level;
    qreal y = box.center().y();
    if (!children.isEmpty()) {
        if (index == 0) y = boxOf(children.front()).top() - m_spacing.sibling / 2 - kReorderSlotPx.height() / 2;
        else if (index == children.size()) y = boxOf(children.back()).bottom() + m_spacing.sibling / 2 + kReorderSlotPx.height() / 2;
        else y = (boxOf(children[index - 1]).bottom() + boxOf(children[index]).top()) / 2;
    }
    const qreal x = s > 0 ? box.right() + gap : box.left() - gap - kReorderSlotPx.width();
    result.slot = QRectF(QPointF(x, y - kReorderSlotPx.height() / 2), kReorderSlotPx);
    const NodeItem *parentItem = m_items.value(parent);
    result.from = parentItem->pos() + parentItem->branchAnchor(s > 0);
    result.kind = onBody ? Kind::Child : Kind::Insert;
    return result;
}

void MindMapScene::onReorderMoved(NodeItem *item, const QPointF &scenePos) {
    const QColor color = NodeItem::selectionStyle().selectedColor;
    if (!m_reorderGhost) {
        // Il nodo vero, semitrasparente, sotto il mouse. Doppia risoluzione: resta
        // nitido fino allo zoom 200%.
        auto *ghost = new QGraphicsPixmapItem(item->dragPreview(2.0));
        ghost->setOffset(item->boundingRect().topLeft());
        ghost->setOpacity(kReorderGhostOpacity);
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
        slot.addRoundedRect(target.slot, 4, 4);
        path = QPainterPathStroker(QPen(color, 2, Qt::SolidLine, Qt::RoundCap)).createStroke(branch).united(slot);
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
                path.lineTo(headShape.center().x() + s * m_fishboneReach.value(headNode, 0), axisY);
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
    const NodeStyle::Backdrop backdrop = m_model->backdrop();
    RelationshipItem::Look look;
    const QColor line = relationship.lineColor.value_or(NodeStyle::annotationColor(Relationships::kDefaultLineColor, backdrop));
    look.pen = QPen(line, relationship.lineWidth.value_or(Relationships::kDefaultLineWidth),
                    static_cast<Qt::PenStyle>(relationship.linePattern.value_or(Relationships::kDefaultLinePattern)),
                    Qt::FlatCap, Qt::RoundJoin);
    const QString family = relationship.fontFamily.isEmpty() ? m_model->sheetStyle.fontFamily : relationship.fontFamily;
    look.font.setFamily(family.isEmpty() ? NodeStyle::kDefaultMapFontFamily : family);  // as the topics
    look.font.setPixelSize(qRound(relationship.fontSize.value_or(Relationships::kDefaultFontSize)));
    look.font.setWeight(QFont::DemiBold);  // default label
    look.textColor = relationship.textColor.value_or(NodeStyle::annotationColor(QColor("#303640"), backdrop));
    look.backdrop = backdrop.color;
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
            item->elementAt = [this](const QPointF &scenePos) { return elementAt(scenePos); };
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
    // Black alphas on the default canvas; on a dark backdrop the same alphas of its readable color.
    const QColor ink = NodeStyle::annotationColor(Qt::black, m_model->backdrop());
    QColor line = ink;
    line.setAlphaF(Boundaries::kLineAlpha);
    QColor fill = ink;
    fill.setAlphaF(Boundaries::kFillAlpha);
    BoundaryItem::Look look;
    const qreal width = boundary.lineWidth.value_or(Boundaries::kDefaultLineWidth);
    look.pen = QPen(boundary.lineColor.value_or(line), width, Qt::SolidLine, Qt::FlatCap, Qt::MiterJoin);
    const int pattern = boundary.linePattern.value_or(Boundaries::kDefaultLinePattern);
    if (pattern == Qt::DashLine) look.pen.setDashPattern({Boundaries::kDashOnPx / width, Boundaries::kDashOffPx / width});
    else look.pen.setStyle(static_cast<Qt::PenStyle>(pattern));
    look.fill = boundary.fillColor.value_or(fill);
    look.titleText = NodeStyle::annotationColor(QColor("#303640"), m_model->backdrop());
    const QString family = m_model->sheetStyle.fontFamily;
    look.font.setFamily(family.isEmpty() ? NodeStyle::kDefaultMapFontFamily : family);  // as the topics
    look.font.setPixelSize(qRound(Boundaries::kTitleFontPx));
    look.placeholder = tr("Title");
    return look;
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
    for (bool grown = true; grown;) {
        grown = false;
        for (qsizetype o = 0; o < boundaries.size(); ++o)
            for (qsizetype i = 0; i < boundaries.size(); ++i) {
                if (i == o || covers[i].isNull() || covers[o].isNull() || !inside(boundaries[i], boundaries[o])) continue;
                if (inside(boundaries[o], boundaries[i]) && o < i) continue;  // same topics: the later one is outside
                QRectF need = Boundaries::strokeRect(covers[i]);
                const qreal half = boundaries[i].lineWidth.value_or(Boundaries::kDefaultLineWidth) / 2;
                need.adjust(-half, -half - (boundaries[i].title.isEmpty() ? 0 : Boundaries::kTitleHeightPx), half, half);
                if (covers[o].contains(need)) continue;
                covers[o] = covers[o].united(need);
                grown = true;
            }
    }
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
    clearSelection();
    for (QGraphicsItem *item : std::as_const(items)) item->setSelected(true);
    setFocusItem(items.first());
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

