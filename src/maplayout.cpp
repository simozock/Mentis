#include "maplayout.h"

#include "mapvariants.h"
#include "nodestyle.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

using namespace MapLayoutRules;

BranchShape MapLayout::effectiveBranchShape(const MindNode *parent) const {
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

QMarginsF MapLayout::boundaryPad(const QList<MindNode *> &children, int index, bool alongX, bool ascending) const {
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

QRectF MapLayout::summaryRoom(const MindNode *node, const QList<MindNode *> &children, const QList<QRectF> &placed,
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
        const NodeGeometry *item = geometry(summary.topic.get());
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

void MapLayout::collectBoundaryRuns() {
    m_boundaryRuns.clear();
    m_boundaryRunsOf.clear();
    m_summaryBranchBounds.clear();
    const auto addRun = [this](const QStringList &ids, qreal above, qreal below) {
        // Room only for a run of siblings; topics moved under other parents are just surrounded.
        QList<const MindNode *> topics;
        for (const QString &id : ids)
            if (const MindNode *topic = nodeById ? nodeById(id) : nullptr) topics.append(topic);
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
        const NodeGeometry *item = geometry(summary.topic.get());
        const QRectF covered = branchesCovered ? branchesCovered(summary.topics) : QRectF();
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

QRectF MapLayout::layoutSummaryBranch(MindNode *topic, int sign) {
    // As a floating topic's branch: the children in a column beside the topic, centred on it. Run
    // after the main layout and more than once per layout: the boundary runs in it start afresh.
    for (BoundaryRun &run : m_boundaryRuns) run.visited = 0;
    m_autoPositions[topic] = topic->position;
    m_sides[topic] = sign;
    const NodeGeometry *topicItem = geometry(topic);
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
            if (const NodeGeometry *item = geometry(child.get()))
                bounds = bounds.united(item->shapeRect().translated(m_autoPositions.value(child.get())));
            unite(child.get());
        }
    };
    unite(topic);
    return bounds;
}

qreal MapLayout::layoutSubtree(MindNode *node, int depth, qreal &cursor, int sign) {
    // FEAT-052: room for the boundaries starting here; the guard adds the room after the last
    // topic of a run once this subtree is placed (it runs after the return value is computed).
    const QList<int> runs = m_boundaryRunsOf.value(node);
    for (const int run : runs) if (m_boundaryRuns[run].visited == 0) cursor += m_boundaryRuns[run].above;
    struct RoomAfter {
        MapLayout *scene; const QList<int> &runs; qreal &cursor;
        ~RoomAfter() {
            for (const int run : runs)
                if (++scene->m_boundaryRuns[run].visited == scene->m_boundaryRuns[run].size) cursor += scene->m_boundaryRuns[run].below;
        }
    } roomAfter{this, runs, cursor};
    // La distanza orizzontale tiene conto della larghezza reale dei nodi (i nodi con formule possono
    // essere piu' larghi) e quella verticale della loro altezza (distanze: costanti k*GapPx).
    auto *item = geometry(node);
    // B7: il risultato va in m_autoPositions; node->position lo imposta autoLayout().
    QPointF &autoPosition = m_autoPositions[node];
    const qreal halfWidth = item ? item->anchorHalfWidth() : 105.0;
    if (node->parent) {
        auto *parentItem = geometry(node->parent);
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
        const NodeGeometry *atItem = geometry(const_cast<MindNode *>(at));
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

bool MapLayout::childrenHidden(const MindNode *node) { return node->parent && node->collapsed; }

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
QRectF MapLayout::layoutBlock(MindNode *node, int inheritedSide, QHash<MindNode *, Placement> &rel) {
    using MapStructure::Type;
    using MapStructure::Direction;
    const NodeGeometry *item = geometry(node);
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
                const NodeGeometry *childItem = geometry(const_cast<MindNode *>(child));
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
                const NodeGeometry *childItem = geometry(children[i]);
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
                const NodeGeometry *childItem = geometry(children[i]);
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
QRectF MapLayout::layoutTable(MindNode *owner, QHash<MindNode *, Placement> &rel) {
    const auto shapeOf = [this](const MindNode *node) {
        const NodeGeometry *item = geometry(const_cast<MindNode *>(node));
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

// FEAT-026 phase E (reference live 2026-10-24): main topics left to right along the axis, the first one's
// children above, the next one's below, and so on; their descendants beside them like a logic chart.
QRectF MapLayout::layoutTimeline(MindNode *node, const QRectF &self, const QList<MindNode *> &mains,
                                    QHash<MindNode *, Placement> &rel) {
    const bool offAxis = MapStructure::directionOf(node) == MapStructure::Direction::OffAxis;
    const qreal gap = (offAxis ? kTimelineOffAxisGapPx : kTimelineGapPx) * m_spacing.block;
    const auto halfHeightOf = [this](const MindNode *at) {
        const NodeGeometry *atItem = geometry(const_cast<MindNode *>(at));
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
        const NodeGeometry *mainItem = geometry(main);
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
                const NodeGeometry *subItem = geometry(subs[j]);
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
QRectF MapLayout::layoutFishbone(MindNode *node, const QRectF &self, const QList<MindNode *> &mains,
                                    QHash<MindNode *, Placement> &rel) {
    const int s = MapStructure::directionOf(node) == MapStructure::Direction::Left ? -1 : 1;
    const auto mirrored = [s](const QRectF &r) { return s > 0 ? r : QRectF(-r.right(), r.top(), r.width(), r.height()); };
    const auto halfHeightOf = [this](const MindNode *at) {
        const NodeGeometry *atItem = geometry(const_cast<MindNode *>(at));
        return atItem ? std::max<qreal>(0, atItem->layoutHeight() / 2 - kLayoutMarginPx) : 20.0;
    };
    const auto halfWidthOf = [this](const MindNode *at) {
        const NodeGeometry *atItem = geometry(const_cast<MindNode *>(at));
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
        const NodeGeometry *mainItem = geometry(main);
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

void MapLayout::placeBlock(MindNode *node, const QHash<MindNode *, Placement> &rel) {
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

void MapLayout::shiftSubtree(MindNode *node, qreal dy) {
    m_autoPositions[node].ry() += dy;
    if (childrenHidden(node)) return;  // FEAT-016: i figli nascosti non hanno un posto
    for (const auto &child : node->children) if (!child->floating) shiftSubtree(child.get(), dy);
}

// FEAT-012 (passo 4a): Mappa mentale su due lati. Il centrale sta in (0, 0); i rami principali si
// dividono fra destra e sinistra (MapStructure::balancedSides sui loro ingombri verticali, oppure i
// lati scelti); a destra nell'ordine dall'alto in basso, a sinistra gli altri dal basso in alto (in
// senso orario); su due lati i rami principali di ogni lato si dispongono attorno al
// centrale, con una direzione sola il lato e' centrato sul centrale.
void MapLayout::computeAutoPositions() {
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
    const NodeGeometry *rootItem = geometry(root);
    const qreal rootHalfHeight = rootItem ? rootItem->layoutHeight() / 2 : 0;
    // FEAT-072 A2, live: with two or more main topics on a side, the first and last may reach
    // into the central box by half its vertical padding (two per side: centres 130 px apart). A lone
    // topic was not measured live and keeps the rule from the photos.
    const qreal rootClearance = rootItem ? rootHalfHeight - kLayoutMarginPx - NodeStyle::textPadding(root).y() / 2 : 0;
    const auto halfHeight = [this](MindNode *node) {
        const NodeGeometry *item = geometry(node);
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
            const NodeGeometry *item = geometry(node);
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
}
