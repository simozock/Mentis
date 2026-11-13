#include "mapcanvas.h"

#include "mobilemap.h"

#include <QLoggingCategory>
#include "branchpath.h"
#include "document.h"
#include "mapscene.h"
#include "mapstructure.h"
#include "mindmapmodel.h"
#include "nodegeometry.h"
#include "performance.h"

#include <QPainterPath>

#include <QFontMetricsF>
#include <QGuiApplication>
#include <QLineF>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGOpacityNode>
#include <QSGRectangleNode>
#include <QSGRendererInterface>
#include <QSet>
#include <QSGTextNode>
#include <QSGTransformNode>

#include <algorithm>
#include <functional>
#include <utility>
#include <cmath>
#include <numbers>

Q_LOGGING_CATEGORY(lcMapCanvas, "mentis.mobile.canvas")

namespace {

// 60 Hz: one refresh is 16.7 ms. A frame interval counts as on time up to one refresh plus the
// display's jitter; a missed refresh shows as about 33 ms.
constexpr qint64 kOnTimeFrameUs = 17000;
// FEAT-110, RelationshipItem::paint: the line from an end to its handle and the handle's ring (map px).
constexpr qreal kHandleGuidePx = 1;
constexpr qreal kHandleRingPx = 1.5;
// FEAT-115, BoundaryItem's selection frame: 3 px outside the border, 1.5 px wide (map px).
constexpr qreal kBoundarySelectionGapPx = 3;
constexpr qreal kBoundarySelectionWidthPx = 1.5;

qint64 percentile(QList<qint64> values, double fraction) {
    if (values.isEmpty()) return 0;
    std::sort(values.begin(), values.end());
    const qsizetype index = std::min<qsizetype>(values.size() - 1, qsizetype(std::ceil(fraction * values.size())) - 1);
    return values.at(std::max<qsizetype>(0, index));
}

QString graphicsApiName(QSGRendererInterface::GraphicsApi api) {
    switch (api) {
    case QSGRendererInterface::OpenGL: return QStringLiteral("OpenGL");
    case QSGRendererInterface::Vulkan: return QStringLiteral("Vulkan");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D 11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D 12");
    case QSGRendererInterface::Metal: return QStringLiteral("Metal");
    case QSGRendererInterface::Software: return QStringLiteral("software");
    default: return QStringLiteral("other");
    }
}

}  // namespace

MapCanvas::MapCanvas(QQuickItem *parent) : QQuickItem(parent) {
    setFlag(ItemHasContents);
    setClip(true);  // the map stays inside the item, under the page's header
    connect(this, &MapCanvas::viewChanged, this, &MapCanvas::editorChanged);  // the field moves with the map
    m_autoScrollTimer.setInterval(kAutoScrollIntervalMs);
    connect(&m_autoScrollTimer, &QTimer::timeout, this, &MapCanvas::autoScrollTick);
}

void MapCanvas::setMap(MobileMap *map) {
    if (m_map == map) return;
    if (m_map) disconnect(m_map, nullptr, this, nullptr);
    m_map = map;
    if (m_map) {
        connect(m_map, &MobileMap::snapshotChanged, this, &MapCanvas::onSnapshotChanged);
        connect(m_map, &MobileMap::contentChanged, this, &MapCanvas::onContentChanged);
        connect(m_map, &MobileMap::selectionChanged, this, [this] {
            // The handle belonged to the relationship or the picture selected before.
            if (m_bentHandle != 0 || m_resizedCorner != 0) endMove(false);
            m_selectionDirty = true;
            if (m_editing) m_sceneDirty = true;  // the title left out follows the selection
            emit editorChanged();
            update();
        });
    }
    emit mapChanged();
    onSnapshotChanged();
}

void MapCanvas::onSnapshotChanged() {
    endMove(false);  // another sheet, or the map closed
    setLabelHighlight(QString());  // as the desktop's scene built again
    m_sceneDirty = true;
    m_fitPending = true;
    fit();
    emit editorChanged();
}

void MapCanvas::onContentChanged() {
    m_sceneDirty = true;
    m_selectionDirty = true;
    if (m_bentHandle != 0) {
        // An Undo with another finger: the bend goes on over the new state, or ends with its line.
        if (!m_map || !m_map->selectedRelation()) endMove(false);
    } else if (m_resizedCorner != 0) {
        if (!m_map || !m_map->pictureSelected()) endMove(false);  // an Undo took the picture off
    } else if (moving()) {
        // Drawn again under a moving finger (an Undo with another finger): the copy and the slot
        // follow the new sheet, or the move ends when its topic is no longer drawn.
        const QString &id = m_movingId.isEmpty() ? m_pictureId : m_movingId;
        const MindNode *node = m_map && m_map->document() ? m_map->document()->model()->nodeById(id) : nullptr;
        if (!node || !m_map->snapshot() || !m_map->snapshot()->topicOf(node)) endMove(false);
        else { m_ghostDirty = true; moveTo(m_moveItemPoint.x(), m_moveItemPoint.y()); }
    }
    if (const QRectF selected = selectedBounds(); !selected.isEmpty() && !m_fitPending) reveal(selected);
    emit editorChanged();
    update();
}

void MapCanvas::setBandTop(qreal top) {
    if (qFuzzyCompare(top, m_bandTop)) return;
    m_bandTop = top;
    emit bandChanged();
}

void MapCanvas::setBandBottom(qreal bottom) {
    if (qFuzzyCompare(bottom, m_bandBottom)) return;
    m_bandBottom = bottom;
    emit bandChanged();
}

void MapCanvas::setEditing(bool editing) {
    if (editing == m_editing) return;
    m_editing = editing;
    m_sceneDirty = true;
    m_selectionDirty = true;  // a selected relationship's placeholder hides under the editor
    emit editingChanged();
    update();
}

QRectF MapCanvas::relationLabelBox(const MapSnapshot::Relation &relation) const {
    if (!relation.labelRect.isEmpty()) return relation.labelRect;
    return Relationships::labelRect(relation.geometry.labelPoint, Relationships::labelSize(relation.look.font, tr("Relationship")));
}

QRectF MapCanvas::boundaryTab(const MapSnapshot::BoundaryShape &shape) const {
    if (!shape.titleRect.isEmpty()) return shape.titleRect;
    return Boundaries::titleRect(shape.stroke, QFontMetricsF(shape.look.font).horizontalAdvance(tr("Title")));
}

QRectF MapCanvas::selectedBounds() const {
    if (const MapSnapshot::Topic *topic = m_map ? m_map->selectedTopic() : nullptr) return topic->bounds;
    if (const MapSnapshot::Relation *relation = m_map ? m_map->selectedRelation() : nullptr) return relationLabelBox(*relation);
    if (const MapSnapshot::BoundaryShape *boundary = m_map ? m_map->selectedBoundary() : nullptr) return boundary->frame();
    return {};
}

QPointF MapCanvas::editorAnchor() const {
    // FEAT-115: a boundary's title starts after the tab's padding, in its middle (BoundaryItem::beginEditing).
    if (const MapSnapshot::BoundaryShape *boundary = m_map ? m_map->selectedBoundary() : nullptr) {
        const QRectF tab = boundaryTab(*boundary);
        return itemPoint(QPointF(tab.left() + Boundaries::kTitlePaddingPx, tab.center().y()));
    }
    // FEAT-110: a relationship's label is centred on its box, as RelationshipItem::beginEditing.
    if (const MapSnapshot::Relation *relation = m_map ? m_map->selectedRelation() : nullptr)
        return itemPoint(relation->geometry.labelPoint);
    const MapSnapshot::Topic *topic = m_map ? m_map->selectedTopic() : nullptr;
    if (!topic) return {};
    // MapScene::build: the text block in the middle of the text area, its widest line as wide as
    // the area inside the padding.
    const NodeGeometry &geometry = *topic->geometry;
    const QRectF box = geometry.textArea().translated(topic->position).adjusted(geometry.padX(), 0, -geometry.padX(), 0);
    const int alignment = editorAlignment();
    const qreal x = alignment == Qt::AlignHCenter ? box.center().x() : alignment == Qt::AlignRight ? box.right() : box.left();
    return itemPoint(QPointF(x, box.center().y()));
}

MapCanvas::Controls MapCanvas::controls() const {
    Controls out;
    const MapSnapshot::Topic *topic = m_map ? m_map->selectedTopic() : nullptr;
    const MapLayout *layout = topic ? m_map->snapshot()->layout() : nullptr;
    if (!layout) return out;
    const MindNode *node = topic->node;
    // The topic's outer border (the bounds keep a 4 px margin), as the selection ring, and its shape.
    const QRectF box(itemPoint(topic->bounds.topLeft() + QPointF(4, 4)), itemPoint(topic->bounds.bottomRight() - QPointF(4, 4)));
    const QRectF shapeRect = topic->geometry->shapeRect().translated(topic->position);
    const QRectF shape(itemPoint(shapeRect.topLeft()), itemPoint(shapeRect.bottomRight()));
    // The knobs and the control follow the zoom as the topic does (BUG-051); the targets keep 48 px.
    const qreal z = m_zoom;
    const qreal knobSize = kKnobSizePx * z, knobGap = kCollapseKnobGapPx * z;
    const qreal openSize = kCollapseOpenSizePx * z, closedSize = kCollapseSizePx * z, gap = kCollapseGapPx * z;

    // MindMapScene::collapseBranch: the control on the children's side, above or below the topic in
    // org charts and tree trunks; not on the central topic.
    Collapse &collapse = out.collapse;
    const auto firstChild = std::find_if(node->children.cbegin(), node->children.cend(), [](const auto &child) { return !child->floating; });
    if (node->parent && firstChild != node->children.cend()) {
        const MapStructure::Type type = MapStructure::typeOf(node);
        const bool org = type == MapStructure::Type::OrgChart;
        const bool trunk = (type == MapStructure::Type::TreeChart && MapStructure::typeFromKey(node->structure) == MapStructure::Type::TreeChart)
                        || MapLayoutRules::setsTimeline(node->parent);
        // The first child's drawn place; a closed branch's children keep their last one in the model.
        const MindNode *first = firstChild->get();
        const MapSnapshot::Topic *firstTopic = m_map->snapshot()->topicOf(first);
        const QPointF firstPosition = firstTopic ? firstTopic->position : first->position;
        collapse.vertical = org || trunk;
        collapse.side = collapse.vertical ? (firstPosition.y() >= topic->position.y() ? 1 : -1)
                                          : ((node->floating || layout->sideOf(node) > 0) ? 1 : -1);
        collapse.color = node->lineColor;
        if (node->collapsed) {
            const std::function<int(const MindNode *)> descendants = [&descendants](const MindNode *n) {
                int count = 0;
                for (const auto &child : n->children) if (!child->floating) count += 1 + descendants(child.get());
                return count;
            };
            collapse.hidden = descendants(node);
        }
        // NodeItem::collapseRect, scaled by the zoom.
        const int side = collapse.side;
        qreal width = closedSize;
        if (collapse.hidden > 0) {
            QFont font = QGuiApplication::font();
            font.setPixelSize(kCollapseCountPx);
            width = std::max(width, QFontMetricsF(font).horizontalAdvance(QString::number(collapse.hidden)) * z + 8 * z);
        }
        // The target runs from the topic's border outwards, 48 px or up to the control's far edge.
        if (collapse.vertical) {
            const qreal centreX = shape.center().x();
            collapse.edge = QPointF(centreX, side > 0 ? shape.bottom() : shape.top());
            if (collapse.hidden == 0) {
                const qreal reach = (org ? BranchPath::kElbowRunPx : kCollapseOpenOffsetPx) * z;
                const qreal centreY = side > 0 ? shape.bottom() + reach : shape.top() - reach;
                collapse.rect = QRectF(centreX - openSize / 2, centreY - openSize / 2, openSize, openSize);
            } else {
                const qreal y = side > 0 ? shape.bottom() + gap : shape.top() - gap - closedSize;
                collapse.rect = QRectF(centreX - width / 2, y, width, closedSize);
            }
            const qreal reachOut = std::max(kKnobTargetPx, side > 0 ? collapse.rect.bottom() - box.bottom() : box.top() - collapse.rect.top());
            collapse.target = QRectF(centreX - kKnobTargetPx / 2, side > 0 ? box.bottom() : box.top() - reachOut, kKnobTargetPx, reachOut);
        } else {
            const qreal centreY = shape.center().y();
            collapse.edge = QPointF(side > 0 ? shape.right() : shape.left(), centreY);
            if (collapse.hidden == 0) {
                const qreal centreX = side > 0 ? shape.right() + kCollapseOpenOffsetPx * z : shape.left() - kCollapseOpenOffsetPx * z;
                collapse.rect = QRectF(centreX - openSize / 2, centreY - openSize / 2, openSize, openSize);
            } else {
                const qreal x = side > 0 ? shape.right() + gap : shape.left() - gap - width;
                collapse.rect = QRectF(x, centreY - closedSize / 2, width, closedSize);
            }
            const qreal reachOut = std::max(kKnobTargetPx, side > 0 ? collapse.rect.right() - box.right() : box.left() - collapse.rect.left());
            collapse.target = QRectF(side > 0 ? box.right() : box.left() - reachOut, centreY - kKnobTargetPx / 2, reachOut, kKnobTargetPx);
        }
    }

    // TopicTools::update: the child knob on the branch side, the sibling one under the topic; in org
    // and tree charts the other way round. Each 48 px target lies outside the topic, on the knob's side.
    const bool block = MapStructure::isBlockType(MapStructure::typeOf(node));
    const bool left = node->parent && layout->sideOf(node) < 0;
    QPointF beside(left ? box.left() - kChildKnobOffsetPx * z : box.right() + kChildKnobOffsetPx * z, box.center().y());
    QPointF below(box.center().x(), box.bottom() + kSiblingKnobOffsetPx * z);
    // Past 48 px a target grows to hold its knob.
    const qreal besideSide = std::max(kKnobTargetPx, (kChildKnobOffsetPx + kKnobSizePx / 2) * z);
    const qreal belowSide = std::max(kKnobTargetPx, (kSiblingKnobOffsetPx + kKnobSizePx / 2) * z);
    QRectF besideTarget(left ? box.left() - besideSide : box.right(), beside.y() - besideSide / 2, besideSide, besideSide);
    QRectF belowTarget(below.x() - belowSide / 2, box.bottom(), belowSide, belowSide);
    // A knob over the collapse control goes just past it, away from the topic. The two targets meet
    // halfway between the control and the knob, so the control's is narrower than 48 px there
    // (user's choice 2026-11-11, ui-foundations.md "Eccezioni approvate").
    if (!collapse.rect.isEmpty()) {
        const QRectF circle = collapse.rect;
        const QRectF zone = circle.adjusted(-knobGap, -knobGap, knobGap, knobGap);
        const qreal clear = knobGap + knobSize / 2;
        const QPointF half(knobSize / 2, knobSize / 2);
        if (zone.intersects(QRectF(beside - half, beside + half))) {
            beside.setX(left ? circle.left() - clear : circle.right() + clear);
            const qreal mid = left ? (circle.left() + beside.x() + knobSize / 2) / 2 : (circle.right() + beside.x() - knobSize / 2) / 2;
            besideTarget.moveLeft(left ? mid - besideTarget.width() : mid);
            if (left) collapse.target.setLeft(mid); else collapse.target.setRight(mid);
        }
        if (zone.intersects(QRectF(below - half, below + half))) {
            below.setY(circle.bottom() + clear);
            const qreal mid = (circle.bottom() + below.y() - knobSize / 2) / 2;
            belowTarget.moveTop(mid);
            collapse.target.setBottom(mid);
        }
    }
    out.child = block ? Knob{below, belowTarget} : Knob{beside, besideTarget};
    if (node->parent && !node->floating && !node->summaryTopic)  // FEAT-052: a summary topic has no row
        out.sibling = block ? Knob{beside, besideTarget} : Knob{below, belowTarget};
    return out;
}

qreal MapCanvas::editorWidth() const {
    if (const MapSnapshot::Relation *relation = m_map ? m_map->selectedRelation() : nullptr)
        return relationLabelBox(*relation).width();  // the page's field does not wrap a label: it grows
    // Nor a title: the field grows with its text from the placeholder's width, as the tab of
    // BoundaryItem's editor.
    if (const MapSnapshot::BoundaryShape *boundary = m_map ? m_map->selectedBoundary() : nullptr)
        return QFontMetricsF(boundary->look.font).horizontalAdvance(tr("Title"));
    const MapSnapshot::Topic *topic = m_map ? m_map->selectedTopic() : nullptr;
    return topic ? topic->geometry->plainLayoutSize().width() : 0;
}

int MapCanvas::editorAlignment() const {
    if (m_map && m_map->selectedRelation()) return Qt::AlignHCenter;
    const MapSnapshot::Topic *topic = m_map ? m_map->selectedTopic() : nullptr;
    if (!topic) return Qt::AlignLeft;
    switch (topic->node->textAlign) {
    case TextAlign::Centro: return Qt::AlignHCenter;
    case TextAlign::Destra: return Qt::AlignRight;
    default: return Qt::AlignLeft;
    }
}

QFont MapCanvas::editorFont() const {
    if (const MapSnapshot::Relation *relation = m_map ? m_map->selectedRelation() : nullptr) return relation->look.font;
    if (const MapSnapshot::BoundaryShape *boundary = m_map ? m_map->selectedBoundary() : nullptr) return boundary->look.font;
    const MapSnapshot::Topic *topic = m_map ? m_map->selectedTopic() : nullptr;
    return topic ? NodeGeometry::titleFont(topic->node) : QFont();
}

QColor MapCanvas::editorBackdrop() const {
    const MapSnapshot::BoundaryShape *boundary = m_map ? m_map->selectedBoundary() : nullptr;
    return boundary ? boundary->look.pen.color() : QColor(Qt::transparent);
}

QColor MapCanvas::editorColor() const {
    if (const MapSnapshot::Relation *relation = m_map ? m_map->selectedRelation() : nullptr) return relation->look.textColor;
    if (const MapSnapshot::BoundaryShape *boundary = m_map ? m_map->selectedBoundary() : nullptr) return boundary->look.titleText;
    const MapSnapshot::Topic *topic = m_map ? m_map->selectedTopic() : nullptr;
    return topic ? topic->node->text : QColor(Qt::black);
}

void MapCanvas::setLabelHighlight(const QString &label) {
    if (label == m_labelHighlight) return;
    m_labelHighlight = label;
    m_sceneDirty = true;
    emit labelHighlightChanged();
    update();
}

QString MapCanvas::labelAt(const QPointF &mapPoint, const MapSnapshot::Topic **found) const {
    const MapSnapshot *snapshot = m_map ? m_map->snapshot() : nullptr;
    if (!snapshot) return QString();
    for (const MapSnapshot::Topic &topic : snapshot->topics()) {
        if (!topic.pills.contains(mapPoint)) continue;
        const QList<QRectF> pills = topic.geometry->labelRects();
        for (qsizetype i = 0; i < pills.size() && i < topic.node->labels.size(); ++i)
            if (pills.at(i).translated(topic.position).contains(mapPoint)) {
                if (found) *found = &topic;
                return topic.node->labels.at(i);
            }
    }
    return QString();
}

bool MapCanvas::labelMenuAt(qreal x, qreal y) {
    setLabelHighlight(QString());
    if (!m_map || m_map->formatPainter() || m_map->relationTool() || m_map->multiSelect()) return false;
    const MapSnapshot::Topic *topic = nullptr;
    const QPointF point = mapPoint(QPointF(x, y));
    if (labelAt(point, &topic).isEmpty()) return false;
    m_map->select(topic->node->id);
    const QList<QRectF> pills = topic->geometry->labelRects();
    for (const QRectF &place : pills) {
        const QRectF pill = place.translated(topic->position);
        if (!pill.contains(point)) continue;
        const QPointF below = itemPoint({pill.center().x(), pill.bottom()});
        emit labelMenuRequested(below.x(), below.y());
        break;
    }
    return true;
}

void MapCanvas::tapAt(qreal x, qreal y) {
    if (!m_map) return;
    setLabelHighlight(QString());
    const QPointF point = mapPoint(QPointF(x, y));
    if (m_map->formatPainter()) {
        m_map->paintAt(point);
        return;
    }
    if (m_map->relationTool()) {
        m_map->relateAt(point, MapSnapshot::kRelationTouchPx / m_zoom);
        return;
    }
    if (m_map->multiSelect()) {  // FEAT-111: a tap only adds or removes a topic
        m_map->multiSelectAt(point);
        return;
    }
    if (relationHandleAt(QPointF(x, y)) != 0) return;  // FEAT-110: as the desktop, a handle keeps the selection
    if (pictureHandleAt(QPointF(x, y)) != 0) return;   // FEAT-106: the same for the picture's
    m_map->selectAt(point, MapSnapshot::kRelationTouchPx / m_zoom);
    // The icon is 22 px at zoom 1: its target grows to the phone's 48 px, but only inside the topic,
    // so a tap beside it still reaches the neighbour or the background.
    const MapSnapshot::Topic *topic = m_map->selectedTopic();
    if (!topic || !topic->contains(point)) return;
    // FEAT-114: a tag pill highlights the topics with its tag (user's choice 2026-11-12; the desktop
    // does it from the pill's menu).
    if (const QString label = labelAt(point); !label.isEmpty() && topic->pills.contains(point)) {
        setLabelHighlight(label);
        return;
    }
    if (!topic->bounds.contains(point)) return;
    // At a small zoom 48 px cover the whole topic: the grown part never reaches over the title or
    // the picture, so a tap on them only selects; on the icon itself it still counts.
    const bool onContent = topic->geometry->textArea().translated(topic->position).contains(point)
                           || topic->geometry->imageRect().translated(topic->position).contains(point);
    const auto target = [&](const QRectF &icon) {
        if (onContent) return icon;
        const qreal side = std::max(icon.width(), kKnobTargetPx / m_zoom);
        return QRectF(icon.center() - QPointF(side, side) / 2, QSizeF(side, side));
    };
    // FEAT-106, FEAT-108: the icon nearest to the finger among those whose target holds it, as the
    // targets of neighbours overlap (the note's and the web link's buttons are 4 px apart).
    enum class Icon { None, Marker, Checkbox, Note, TopicLink, WebLink, Files };
    Icon nearest = Icon::None;
    int nearestMarker = -1;
    QRectF nearestIcon;
    qreal nearestDistance = 0;
    const auto consider = [&](Icon kind, const QRectF &place, int marker = -1) {
        if (place.isEmpty()) return;
        const QRectF icon = place.translated(topic->position);
        if (!target(icon).contains(point)) return;
        const qreal distance = QLineF(icon.center(), point).length();
        if (nearest != Icon::None && distance >= nearestDistance) return;
        nearest = kind;
        nearestMarker = marker;
        nearestIcon = icon;
        nearestDistance = distance;
    };
    const QList<QRectF> markers = topic->geometry->markerRects();
    for (int i = 0; i < markers.size() && i < topic->node->markers.size(); ++i) consider(Icon::Marker, markers[i], i);
    consider(Icon::Checkbox, topic->geometry->taskRect());
    consider(Icon::Note, topic->geometry->noteRect());
    consider(Icon::TopicLink, topic->geometry->linkIconRect(true));  // FEAT-114
    consider(Icon::WebLink, topic->geometry->linkIconRect(false));
    consider(Icon::Files, topic->geometry->filesRect());  // FEAT-114
    switch (nearest) {
    case Icon::None: break;
    case Icon::Marker: {
        const QPointF below = itemPoint({nearestIcon.center().x(), nearestIcon.bottom()});
        emit markerRequested(topic->node->markers[nearestMarker], below.x(), below.y());
        break;
    }
    case Icon::Checkbox: m_map->toggleTaskDone(); break;
    case Icon::Note: emit noteRequested(); break;
    case Icon::TopicLink: emit topicLinkRequested(); break;
    case Icon::WebLink: emit webLinkRequested(); break;
    case Icon::Files: {
        const QPointF below = itemPoint({nearestIcon.center().x(), nearestIcon.bottom()});
        emit filesRequested(below.x(), below.y());
        break;
    }
    }
}

bool MapCanvas::beginPictureDrag(qreal x, qreal y) {
    const MapSnapshot::Topic *topic = m_map && m_map->pictureSelected() ? m_map->selectedTopic() : nullptr;
    if (!topic || !topic->geometry->imageRect().translated(topic->position).contains(mapPoint(QPointF(x, y)))) return false;
    endMove(false);
    m_pictureId = topic->node->id;
    m_moveItemPoint = QPointF(x, y);
    m_indicatorDirty = true;
    emit movingChanged();
    update();
    return true;
}

int MapCanvas::pictureHandleAt(const QPointF &point) const {
    const MapSnapshot::Topic *topic = m_map && m_map->pictureSelected() ? m_map->selectedTopic() : nullptr;
    if (!topic) return 0;
    const QRectF image = topic->geometry->imageRect().translated(topic->position);
    const QPointF corners[] = {image.topLeft(), image.topRight(), image.bottomRight(), image.bottomLeft()};
    // The nearest corner whose 48 px target holds the finger (ui-mobile.md, "Touch targets").
    int nearest = 0;
    qreal nearestDistance = MapSnapshot::kRelationTouchPx;
    for (int i = 0; i < 4; ++i) {
        const qreal distance = QLineF(point, itemPoint(corners[i])).length();
        if (distance <= nearestDistance) { nearest = i + 1; nearestDistance = distance; }
    }
    return nearest;
}

QRectF MapCanvas::shownPictureRect(const MapSnapshot::Topic &topic) const {
    const QRectF image = topic.geometry->imageRect().translated(topic.position);
    if (m_resizedCorner == 0) return image;
    return QRectF(image.center() - QPointF(m_resizeSize.width(), m_resizeSize.height()) / 2, m_resizeSize);
}

bool MapCanvas::beginPictureResize(qreal x, qreal y) {
    if (m_editing) return false;
    const int corner = pictureHandleAt(QPointF(x, y));
    if (corner == 0) return false;
    endMove(false);
    m_resizedCorner = corner;
    m_resizeStartSize = m_map->selectedTopic()->node->imageSize;
    m_resizeSize = m_resizeStartSize;
    m_moveItemPoint = QPointF(x, y);
    m_grabOffset = mapPoint(m_moveItemPoint);  // where the finger started (map px)
    m_selectionDirty = true;
    emit movingChanged();
    update();
    return true;
}

int MapCanvas::relationHandleAt(const QPointF &point) const {
    const MapSnapshot::Relation *relation = m_map ? m_map->selectedRelation() : nullptr;
    if (!relation || relation->relationship.shape == Relationship::Shape::Straight) return 0;
    // The nearer handle whose 48 px target holds the finger (ui-mobile.md, "Touch targets").
    const qreal toFrom = QLineF(point, itemPoint(relation->geometry.fromControl)).length();
    const qreal toTo = QLineF(point, itemPoint(relation->geometry.toControl)).length();
    if (std::min(toFrom, toTo) > MapSnapshot::kRelationTouchPx) return 0;
    return toFrom <= toTo ? 1 : 2;
}

MapSnapshot::Relation MapCanvas::shownRelation(const MapSnapshot::Relation &relation) const {
    if (m_bentHandle == 0) return relation;
    // RelationshipItem::mouseMoveEvent: the dragged handle's offset from its element's centre.
    MapSnapshot::Relation bent = relation;
    if (m_bentHandle == 1) bent.relationship.fromControl = m_bentControl - relation.geometry.fromCentre;
    else bent.relationship.toControl = m_bentControl - relation.geometry.toCentre;
    bent.geometry = Relationships::geometry(bent.relationship, bent.from, bent.to);
    if (!bent.relationship.label.isEmpty())
        bent.labelRect = Relationships::labelRect(bent.geometry.labelPoint, bent.labelRect.size());
    return bent;
}

bool MapCanvas::beginRelationBend(qreal x, qreal y) {
    if (m_editing) return false;
    const int handle = relationHandleAt(QPointF(x, y));
    if (handle == 0) return false;
    endMove(false);
    const Relationships::Geometry &geometry = m_map->selectedRelation()->geometry;
    m_bentHandle = handle;
    m_bentControl = handle == 1 ? geometry.fromControl : geometry.toControl;
    // The handle keeps its distance from the finger, so it does not jump under it.
    m_grabOffset = m_bentControl - mapPoint(QPointF(x, y));
    m_moveItemPoint = QPointF(x, y);
    m_sceneDirty = true;  // the line is drawn by the selection while it bends
    m_selectionDirty = true;
    emit movingChanged();
    update();
    return true;
}

bool MapCanvas::beginMove(qreal x, qreal y) {
    if (beginPictureDrag(x, y)) return true;
    endMove(false);
    const MapSnapshot *snapshot = m_map ? m_map->snapshot() : nullptr;
    const QPointF finger(x, y);
    const MapSnapshot::Topic *topic = snapshot ? snapshot->topicAt(mapPoint(finger)) : nullptr;
    if (!topic) return false;
    m_map->select(topic->node->id);
    if (!topic->node->parent) return false;  // the central topic does not move
    // FEAT-115 step 2, NodeItem's ItemIsMovable: nor a summary topic and its branch, which follow the bracket.
    if (summaryTopicOf(topic->node)) return false;
    m_movingId = topic->node->id;
    m_moveItemPoint = finger;
    m_grabOffset = topic->position - mapPoint(finger);
    m_moveTarget = {};
    m_ghostDirty = true;
    m_indicatorDirty = true;
    m_autoScrollTimer.start();
    emit movingChanged();
    update();
    return true;
}

void MapCanvas::moveTo(qreal x, qreal y) {
    if (!moving()) return;
    m_moveItemPoint = QPointF(x, y);
    if (m_resizedCorner != 0) {
        // NodeItem::mouseMoveEvent: proportions locked; the topic stays centred, so the corner
        // follows the finger when the width grows by twice its horizontal move.
        const qreal direction = m_resizedCorner == 2 || m_resizedCorner == 3 ? 1.0 : -1.0;
        const qreal width = std::clamp(std::round(m_resizeStartSize.width() + 2 * direction * (mapPoint(m_moveItemPoint).x() - m_grabOffset.x())),
                                       NodeGeometry::kMinImageWidthPx, NodeGeometry::kMaxImageWidthPx);
        m_resizeSize = QSizeF(width, std::round(width * m_resizeStartSize.height() / std::max<qreal>(1, m_resizeStartSize.width())));
        m_selectionDirty = true;
        update();
        return;
    }
    if (m_bentHandle != 0) {
        m_bentControl = mapPoint(m_moveItemPoint) + m_grabOffset;
        m_selectionDirty = true;
        update();
        return;
    }
    if (!m_pictureId.isEmpty()) {  // the zones and the picture follow; the map does not scroll
        m_indicatorDirty = true;
        update();
        return;
    }
    m_moveTarget = m_map->moveTarget(m_movingId, mapPoint(m_moveItemPoint));
    m_indicatorDirty = true;
    update();
}

void MapCanvas::endMove(bool drop) {
    if (!moving()) return;
    if (m_resizedCorner != 0) {
        m_resizedCorner = 0;
        m_selectionDirty = true;
        emit movingChanged();
        update();
        // After the state is cleared: the step builds the sheet again (contentChanged).
        if (drop && m_map && m_resizeSize != m_resizeStartSize) m_map->resizePicture(m_resizeSize);
        return;
    }
    if (m_bentHandle != 0) {
        const bool fromHandle = std::exchange(m_bentHandle, 0) == 1;
        m_sceneDirty = true;
        m_selectionDirty = true;
        emit movingChanged();
        update();
        // After the state is cleared: the step builds the sheet again (contentChanged).
        if (drop && m_map) m_map->bendRelation(fromHandle, m_bentControl);
        return;
    }
    if (!m_pictureId.isEmpty()) {
        const QString id = std::exchange(m_pictureId, QString());
        m_indicatorDirty = true;
        emit movingChanged();
        update();
        // After the state is cleared: the drop builds the sheet again (contentChanged).
        if (drop && m_map) m_map->placePicture(id, mapPoint(m_moveItemPoint));
        return;
    }
    m_autoScrollTimer.stop();
    const QString id = std::exchange(m_movingId, QString());
    m_moveTarget = {};
    m_ghostDirty = true;
    m_indicatorDirty = true;
    emit movingChanged();
    update();
    // After the state is cleared: the move builds the sheet again (contentChanged).
    if (drop && m_map) m_map->moveTopic(id, mapPoint(m_moveItemPoint));
}

QPointF MapCanvas::autoScrollStep(const QPointF &itemPoint) const {
    // MindMapView::autoScrollStep: per axis 0 away from the edges; inside the zone it grows from
    // the minimum to the maximum towards the edge; at the edge and beyond it is the maximum.
    const auto axis = [](qreal position, qreal low, qreal high) -> qreal {
        const auto step = [](qreal depth) {
            return kAutoScrollMinStepPx + (kAutoScrollMaxStepPx - kAutoScrollMinStepPx) * std::min(depth, kAutoScrollMarginPx) / kAutoScrollMarginPx;
        };
        if (const qreal intoLow = low + kAutoScrollMarginPx - position; intoLow > 0) return -step(intoLow);
        if (const qreal intoHigh = position - (high - kAutoScrollMarginPx); intoHigh > 0) return step(intoHigh);
        return 0;
    };
    return {axis(itemPoint.x(), 0, width()), axis(itemPoint.y(), m_bandTop, visibleBottom())};
}

void MapCanvas::autoScrollTick() {
    const QPointF step = autoScrollStep(m_moveItemPoint);
    if (!moving() || step.isNull()) return;
    panBy(-step.x(), -step.y());  // the view goes towards the edge: the map moves the other way
    // The finger is still but the map moved under it: the slot follows.
    moveTo(m_moveItemPoint.x(), m_moveItemPoint.y());
}

void MapCanvas::revealSelected() {
    if (const QRectF selected = selectedBounds(); !selected.isEmpty()) reveal(selected);
}

void MapCanvas::reveal(const QRectF &mapRect) {
    if (width() <= 0 || height() <= 0) return;
    const qreal margin = kFitMarginPx / m_zoom;
    const QRectF visible(mapPoint(QPointF(0, m_bandTop)), mapPoint(QPointF(width(), visibleBottom())));
    if (visible.height() <= 0) return;
    const QRectF wanted = mapRect.adjusted(-margin, -margin, margin, margin);
    // Each axis: the smallest move that brings the rect in; a rect larger than the view shows its
    // top-left corner.
    const auto shift = [](qreal low, qreal high, qreal wantedLow, qreal wantedHigh) {
        if (wantedLow < low || wantedHigh - wantedLow > high - low) return wantedLow - low;
        if (wantedHigh > high) return wantedHigh - high;
        return 0.0;
    };
    const QPointF delta(shift(visible.left(), visible.right(), wanted.left(), wanted.right()),
                        shift(visible.top(), visible.bottom(), wanted.top(), wanted.bottom()));
    if (delta.isNull()) return;
    m_origin += delta;
    emit viewChanged();
    update();
}

void MapCanvas::centerSelected() {
    const QRectF bounds = selectedBounds();  // FEAT-110: a relationship's label too
    const qreal top = m_bandTop;
    const qreal band = visibleBottom() - top;
    if (width() <= 2 * kFitMarginPx || band <= 2 * kFitMarginPx || bounds.isEmpty()) return;
    const qreal fits = std::min((width() - 2 * kFitMarginPx) / bounds.width(), (band - 2 * kFitMarginPx) / bounds.height());
    m_zoom = std::clamp(std::min(std::max(m_zoom, kEditZoom), fits), minZoom(), kMaxZoom);
    m_origin = bounds.center() - QPointF(width() / 2, top + band / 2) / m_zoom;
    emit viewChanged();
    update();
}

void MapCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    emit editorChanged();
    if (m_fitPending) fit();
    else update();
}

qreal MapCanvas::fitZoom() const {
    const MapSnapshot *snapshot = m_map ? m_map->snapshot() : nullptr;
    if (!snapshot || snapshot->bounds().isEmpty() || width() <= 2 * kFitMarginPx || height() <= 2 * kFitMarginPx) return 1;
    const QRectF bounds = snapshot->bounds();
    return std::min({(width() - 2 * kFitMarginPx) / bounds.width(), (height() - 2 * kFitMarginPx) / bounds.height(), 1.0});
}

qreal MapCanvas::minZoom() const { return std::min(kMinZoom, fitZoom()); }

void MapCanvas::fit() {
    const MapSnapshot *snapshot = m_map ? m_map->snapshot() : nullptr;
    if (!snapshot || width() <= 0 || height() <= 0) { update(); return; }
    m_fitPending = false;
    m_zoom = fitZoom();
    m_origin = snapshot->bounds().center() - QPointF(width(), height()) / (2 * m_zoom);
    emit viewChanged();
    update();
}

void MapCanvas::zoomStep(int deltaPercent) {
    const qreal target = deltaPercent ? (std::round(m_zoom * 100) + deltaPercent) / 100 : 1.0;
    zoomBy(target / m_zoom, width() / 2, height() / 2);
}

void MapCanvas::panBy(qreal dx, qreal dy) {
    m_origin -= QPointF(dx, dy) / m_zoom;
    emit viewChanged();
    update();
}

void MapCanvas::zoomBy(qreal factor, qreal centreX, qreal centreY) {
    const QPointF centre(centreX, centreY);
    const QPointF anchor = mapPoint(centre);
    const qreal zoom = std::clamp(m_zoom * factor, minZoom(), kMaxZoom);
    if (qFuzzyCompare(zoom, m_zoom)) return;
    m_zoom = zoom;
    m_origin = anchor - centre / m_zoom;
    emit viewChanged();
    update();
}

QSGNode *MapCanvas::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) {
    QElapsedTimer timer;
    timer.start();
    // root: the background (item px), then the transform to map px with three holders: the sheet's
    // nodes, the selection ring above them, the moved topic's copy and slot (B2) on top.
    QSGNode *root = oldNode;
    if (!root) {
        root = new QSGNode;
        root->appendChildNode(window()->createRectangleNode());
        auto *transform = new QSGTransformNode;
        transform->appendChildNode(new QSGNode);
        transform->appendChildNode(new QSGNode);
        auto *move = new QSGNode;
        auto *ghostOpacity = new QSGOpacityNode;
        ghostOpacity->setOpacity(ReorderTarget::kGhostOpacity);
        ghostOpacity->appendChildNode(new QSGTransformNode);
        move->appendChildNode(ghostOpacity);
        move->appendChildNode(new QSGNode);  // the slot and its branch, above the copy as on the desktop
        transform->appendChildNode(move);
        root->appendChildNode(transform);
        m_sceneDirty = true;
        m_selectionDirty = true;
        m_ghostDirty = true;
        m_indicatorDirty = true;
    }
    const auto clear = [](QSGNode *holder) {
        while (QSGNode *child = holder->firstChild()) {
            holder->removeChildNode(child);
            delete child;  // and its children, owned by their parents
        }
    };
    const MapSnapshot *snapshot = m_map ? m_map->snapshot() : nullptr;
    auto *background = static_cast<QSGRectangleNode *>(root->firstChild());
    background->setRect(boundingRect());
    background->setColor(snapshot ? snapshot->background() : QColor(Qt::white));
    auto *transform = static_cast<QSGTransformNode *>(root->lastChild());
    QSGNode *sheet = transform->firstChild();
    QSGNode *selection = sheet->nextSibling();
    QSGNode *move = transform->lastChild();
    auto *ghost = static_cast<QSGTransformNode *>(move->firstChild()->firstChild());
    QSGNode *indicator = move->lastChild();
    if (m_sceneDirty) {
        m_sceneDirty = false;
        clear(sheet);
        m_vertexCount = 0;
        if (snapshot) {
            QElapsedTimer buildClock;
            buildClock.start();
            const MindNode *edited = m_editing && m_map && m_map->selectedTopic() ? m_map->selectedTopic()->node : nullptr;
            // FEAT-115: a boundary's tab while its title is typed (the field draws it).
            QString editedLabel;
            if (m_editing && m_map)
                editedLabel = m_map->selectedRelationId().isEmpty() ? m_map->selectedBoundaryId() : m_map->selectedRelationId();
            const QString bent = m_bentHandle != 0 && m_map ? m_map->selectedRelationId() : QString();
            const MapScene::Nodes nodes = MapScene::build(*snapshot, window(), edited, nullptr, editedLabel, bent, m_labelHighlight);
            sheet->appendChildNode(nodes.content);
            m_vertexCount = nodes.vertexCount;
            // Once per sheet, on the render thread before the sheet's first frame.
            qCInfo(lcMapCanvas, "FEAT-097 scene built: %lld ms, %d vertices, %d topics", buildClock.elapsed(), m_vertexCount,
                   int(snapshot->topics().size()));
        }
    }
    if (m_selectionDirty) {
        m_selectionDirty = false;
        clear(selection);
        if (const MapSnapshot::Topic *topic = m_map ? m_map->selectedTopic() : nullptr) {
            // NodeItem::paint: the ring kSelectionGap px outside the topic's outer border, around
            // the whole topic (the bounds keep a 4 px margin). B1 draws the rounded rectangle for
            // every shape; the desktop follows the outline of the non-rectangular ones. FEAT-111: one
            // ring for each topic of a multiple selection, all in the same nodes.
            QList<const MapSnapshot::Topic *> ringed{topic};
            if (m_map->multiSelect()) {
                ringed.clear();
                const QStringList ids = m_map->selectedIds();
                const QSet<QString> chosen(ids.cbegin(), ids.cend());  // one pass over the drawn topics
                for (const MapSnapshot::Topic &at : snapshot->topics())
                    if (chosen.contains(at.node->id)) ringed.append(&at);
            }
            MapScene::Vertices vertices;
            for (const MapSnapshot::Topic *at : ringed) {
                const qreal inset = kSelectionGap + NodeGeometry::borderOutside(at->node) + kSelectionWidth / 2;
                QPainterPath ring;
                ring.addRoundedRect(at->bounds.adjusted(4 - inset, 4 - inset, inset - 4, inset - 4), kSelectionRadius, kSelectionRadius);
                for (const QPolygonF &polyline : MapScene::strokePolylines(ring))
                    MapScene::appendStroke(vertices, polyline, kSelectionWidth, polyline.isClosed(), false, kSelectionColor);
            }
            // FEAT-106: the selected picture's outline and its corner handles (NodeItem's, drawn as
            // the relationship's round handles); while a handle moves, at the new size.
            if (m_map->pictureSelected()) {
                const QRectF image = shownPictureRect(*topic);
                QPainterPath outline;
                outline.addRect(image);
                for (const QPolygonF &polyline : MapScene::strokePolylines(outline))
                    MapScene::appendStroke(vertices, polyline, kPictureOutlinePx, true, false, kSelectionColor);
                for (const QPointF &corner : {image.topLeft(), image.topRight(), image.bottomRight(), image.bottomLeft()}) {
                    QPainterPath handle;
                    handle.addEllipse(corner, Relationships::kHandleRadiusPx, Relationships::kHandleRadiusPx);
                    for (const QPolygonF &polygon : MapScene::fillPolygons(handle)) MapScene::appendFill(vertices, polygon, Qt::white);
                    for (const QPolygonF &polyline : MapScene::strokePolylines(handle))
                        MapScene::appendStroke(vertices, polyline, kHandleRingPx, true, false, kSelectionColor);
                }
            }
            for (QSGGeometryNode *node : MapScene::geometryNodes(vertices)) selection->appendChildNode(node);
        } else if (const MapSnapshot::Relation *selected = m_map ? m_map->selectedRelation() : nullptr) {
            // FEAT-110, RelationshipItem::paint when selected: the band under the line (the line is
            // drawn again over it here), the placeholder of an empty label, the handles of a curved
            // or elbow line. While a handle moves, the scene leaves the line out
            // and it is drawn here whole, bent, with its label.
            const MapSnapshot::Relation relation = shownRelation(*selected);
            const bool bending = m_bentHandle != 0;
            QColor band = kSelectionColor;
            band.setAlpha(Relationships::kSelectionAlpha);
            MapScene::Vertices vertices;
            for (const QPolygonF &polyline : MapScene::strokePolylines(relation.geometry.path))
                MapScene::appendStroke(vertices, polyline, relation.look.lineWidth + Relationships::kSelectionExtraPx, false, true, band);
            MapScene::appendRelation(vertices, relation, bending);
            const bool placeholder = relation.relationship.label.isEmpty() && !m_editing;
            const QRectF box = relationLabelBox(relation);
            if (placeholder) {
                QPainterPath backdrop;
                backdrop.addRoundedRect(box, Relationships::kLabelRadiusPx, Relationships::kLabelRadiusPx);
                for (const QPolygonF &polygon : MapScene::fillPolygons(backdrop)) MapScene::appendFill(vertices, polygon, relation.look.backdrop);
            }
            const Relationships::Geometry &geometry = relation.geometry;
            if (relation.relationship.shape != Relationship::Shape::Straight) {
                for (const QLineF &guide : {QLineF(geometry.start, geometry.fromControl), QLineF(geometry.end, geometry.toControl)})
                    MapScene::appendStroke(vertices, QPolygonF{guide.p1(), guide.p2()}, kHandleGuidePx, false, false, kSelectionColor);
                for (const QPointF &control : {geometry.fromControl, geometry.toControl}) {
                    QPainterPath handle;
                    handle.addEllipse(control, Relationships::kHandleRadiusPx, Relationships::kHandleRadiusPx);
                    for (const QPolygonF &polygon : MapScene::fillPolygons(handle)) MapScene::appendFill(vertices, polygon, Qt::white);
                    for (const QPolygonF &polyline : MapScene::strokePolylines(handle))
                        MapScene::appendStroke(vertices, polyline, kHandleRingPx, true, false, kSelectionColor);
                }
            }
            for (QSGGeometryNode *node : MapScene::geometryNodes(vertices)) selection->appendChildNode(node);
            if (placeholder) {
                QColor text = relation.look.textColor;
                text.setAlphaF(Relationships::kPlaceholderAlpha);
                if (QSGTextNode *label = MapScene::labelNode(window(), relation.look.font, tr("Relationship"), text, box))
                    selection->appendChildNode(label);
            } else if (bending && !relation.labelRect.isEmpty()) {
                if (QSGTextNode *label = MapScene::labelNode(window(), relation.look.font, relation.relationship.label, relation.look.textColor, relation.labelRect))
                    selection->appendChildNode(label);
            }
        } else if (const MapSnapshot::BoundaryShape *boundary = m_map ? m_map->selectedBoundary() : nullptr) {
            // FEAT-115, BoundaryItem::paint when selected: the blue frame kBoundarySelectionGapPx
            // outside the border and the tab.
            const qreal gap = boundary->look.pen.widthF() / 2 + kBoundarySelectionGapPx;
            QPainterPath frame;
            frame.addRect(boundary->frame().adjusted(-gap, -gap, gap, gap));
            MapScene::Vertices vertices;
            for (const QPolygonF &polyline : MapScene::strokePolylines(frame))
                MapScene::appendStroke(vertices, polyline, kBoundarySelectionWidthPx, true, false, kSelectionColor);
            for (QSGGeometryNode *node : MapScene::geometryNodes(vertices)) selection->appendChildNode(node);
        }
    }
    const MapSnapshot::Topic *moved = nullptr;
    if (moving() && snapshot && m_map->document())
        moved = snapshot->topicOf(m_map->document()->model()->nodeById(m_movingId));
    if (m_ghostDirty) {
        m_ghostDirty = false;
        clear(ghost);
        if (moved) ghost->appendChildNode(MapScene::build(*snapshot, window(), nullptr, moved->node).content);
    }
    if (moved) {
        // The copy where the finger holds it: its origin keeps the distance it had from the finger.
        const QPointF shift = mapPoint(m_moveItemPoint) + m_grabOffset - moved->position;
        QMatrix4x4 follow;
        follow.translate(float(shift.x()), float(shift.y()));
        ghost->setMatrix(follow);
    }
    if (m_indicatorDirty) {
        m_indicatorDirty = false;
        clear(indicator);
        using Kind = ReorderTarget::Kind;
        const MindNode *pictureNode = !m_pictureId.isEmpty() && snapshot && m_map->document()
                                          ? m_map->document()->model()->nodeById(m_pictureId) : nullptr;
        if (const MapSnapshot::Topic *topic = pictureNode ? snapshot->topicOf(pictureNode) : nullptr) {
            // NodeItem::paint while the picture is dragged: the zones inside the topic, the one
            // under the finger stronger, and the picture's place following the finger.
            const QPointF finger = mapPoint(m_moveItemPoint);
            const int active = topic->geometry->imageDropZoneAt(finger - topic->position);
            const QList<QRectF> zones = topic->geometry->imageDropZones();
            MapScene::Vertices vertices;
            for (int i = 0; i < zones.size(); ++i) {
                QColor fill = kSelectionColor;
                fill.setAlpha(i == active ? kActiveZoneAlpha : kZoneAlpha);
                QPainterPath zone;
                zone.addRect(zones[i].translated(topic->position));
                for (const QPolygonF &polygon : MapScene::fillPolygons(zone)) MapScene::appendFill(vertices, polygon, fill);
                for (const QPolygonF &polyline : MapScene::strokePolylines(zone))
                    MapScene::appendStroke(vertices, polyline, kPictureOutlinePx, true, false, kSelectionColor);
            }
            const QSizeF size = topic->geometry->imageRect().size();
            QPainterPath picture;
            picture.addRect(QRectF(finger - QPointF(size.width(), size.height()) / 2, size));
            QColor veil = kSelectionColor;
            veil.setAlpha(kDraggedPictureAlpha);
            for (const QPolygonF &polygon : MapScene::fillPolygons(picture)) MapScene::appendFill(vertices, polygon, veil);
            for (QSGGeometryNode *node : MapScene::geometryNodes(vertices)) indicator->appendChildNode(node);
        }
        if (moved && (m_moveTarget.kind == Kind::Child || m_moveTarget.kind == Kind::Insert)) {
            // MindMapScene::onReorderMoved: the blue branch from the parent to the slot (curved as
            // the branches) and the filled slot.
            const QRectF &slot = m_moveTarget.slot;
            const QPointF from = m_moveTarget.from;
            const bool right = slot.center().x() > from.x();
            const QPointF end(right ? slot.left() : slot.right(), slot.center().y());
            QPainterPath branch(from);
            const qreal middle = (from.x() + end.x()) / 2;
            branch.cubicTo({middle, from.y()}, {middle, end.y()}, end);
            QPainterPath box;
            box.addRoundedRect(slot, ReorderTarget::kSlotRadiusPx, ReorderTarget::kSlotRadiusPx);
            MapScene::Vertices vertices;
            for (const QPolygonF &polyline : MapScene::strokePolylines(branch))
                MapScene::appendStroke(vertices, polyline, ReorderTarget::kBranchWidthPx, false, true, kSelectionColor);
            for (const QPolygonF &polygon : MapScene::fillPolygons(box)) MapScene::appendFill(vertices, polygon, kSelectionColor);
            for (QSGGeometryNode *node : MapScene::geometryNodes(vertices)) indicator->appendChildNode(node);
        }
    }
    QMatrix4x4 matrix;
    matrix.scale(float(m_zoom), float(m_zoom));
    matrix.translate(float(-m_origin.x()), float(-m_origin.y()));
    transform->setMatrix(matrix);
    // NodeItem::paint, P6: below the detail zoom only the shapes, without the text. The titles are
    // the content's last child (MapScene::build); an opacity of 0 skips them in the renderer.
    if (QSGNode *content = sheet->firstChild()) {
        auto *text = static_cast<QSGOpacityNode *>(content->lastChild());
        const qreal opacity = m_zoom >= Performance::detailZoomThreshold() ? 1 : 0;
        if (text->opacity() != opacity) text->setOpacity(opacity);
    }
    if (benchmarkRunning()) m_updateUs.append(timer.nsecsElapsed() / 1000);
    return root;
}

void MapCanvas::runBenchmark() {
    const MapSnapshot *snapshot = m_map ? m_map->snapshot() : nullptr;
    if (benchmarkRunning() || !snapshot || snapshot->topics().isEmpty() || !window()) return;
    m_frameReport.clear();
    {
        QMutexLocker lock(&m_swapMutex);
        m_swapTimesNs.clear();
    }
    m_updateUs.clear();
    m_benchmarkStep = 0;
    // At 100% on the central topic, where every title is drawn.
    m_zoom = 1;
    m_origin = snapshot->topics().constFirst().position - QPointF(width(), height()) / 2;
    // frameSwapped comes from the render thread: the time is taken there, when the frame is shown.
    // The steps run as a Qt Quick animation, one per frame on this thread before the frame is
    // synchronised (https://doc.qt.io/qt-6/qtquick-visualcanvas-scenegraph.html#scene-graph-and-rendering).
    // A step queued after frameSwapped missed a vsync each time and halved the frame rate even on
    // 3 topics; one made on afterAnimating stopped after the first frame on Android.
    m_frameClock.start();
    m_swapConnection = connect(window(), &QQuickWindow::frameSwapped, this, [this] {
        QMutexLocker lock(&m_swapMutex);
        m_swapTimesNs.append(m_frameClock.nsecsElapsed());
    }, Qt::DirectConnection);
    m_stepAnimation.start();
    emit viewChanged();
    emit benchmarkChanged();
    update();
}

void MapCanvas::benchmarkStep() {
    if (!benchmarkRunning()) return;
    // Half the frames pan the map round a circle 400 px wide, half zoom from 50% to 150% and back
    // around the item's centre: the moves of a finger and of a pinch.
    const int half = kBenchmarkFrames / 2;
    const qreal phase = 2 * std::numbers::pi * (m_benchmarkStep % half) / half;
    if (m_benchmarkStep < half) {
        panBy(-200 * std::sin(phase) * 2 * std::numbers::pi / half, 200 * std::cos(phase) * 2 * std::numbers::pi / half);
    } else {
        const qreal target = 1 + 0.5 * std::sin(phase);
        zoomBy(target / m_zoom, width() / 2, height() / 2);
        update();  // a step that leaves the zoom as it is still asks for the next frame
    }
    if (++m_benchmarkStep <= kBenchmarkFrames) return;

    disconnect(m_swapConnection);
    m_stepAnimation.stop();
    m_benchmarkStep = -1;
    QList<qint64> intervalsUs;
    {
        QMutexLocker lock(&m_swapMutex);
        for (qsizetype i = 1; i < m_swapTimesNs.size(); ++i) intervalsUs.append((m_swapTimesNs.at(i) - m_swapTimesNs.at(i - 1)) / 1000);
    }
    qsizetype onTime = 0;
    for (const qint64 interval : std::as_const(intervalsUs)) onTime += interval <= kOnTimeFrameUs;
    const qreal onTimePercent = intervalsUs.isEmpty() ? 0 : 100.0 * onTime / intervalsUs.size();
    const int topics = m_map && m_map->snapshot() ? int(m_map->snapshot()->topics().size()) : 0;
    const QSGRendererInterface *renderer = window() ? window()->rendererInterface() : nullptr;
    m_frameReport = tr("%1 frames, %2% on time (60 fps), frame p95 %3 ms, scene update p95 %4 ms, %5 topics, %6 vertices, %7, MSAA %8")
                        .arg(intervalsUs.size())
                        .arg(onTimePercent, 0, 'f', 1)
                        .arg(percentile(intervalsUs, 0.95) / 1000.0, 0, 'f', 1)
                        .arg(percentile(m_updateUs, 0.95) / 1000.0, 0, 'f', 1)
                        .arg(topics)
                        .arg(m_vertexCount)
                        .arg(renderer ? graphicsApiName(renderer->graphicsApi()) : QStringLiteral("?"))
                        // After the window is made, its format is the one the platform gave (on
                        // OpenGL the EGL configuration), not only the requested one (QWindow::format).
                        .arg(window() ? window()->format().samples() : 0);
    qCInfo(lcMapCanvas, "FEAT-097 frames (scene graph): %s", qPrintable(m_frameReport));
    emit benchmarkChanged();
}
