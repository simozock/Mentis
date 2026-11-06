#include "relationshipitem.h"

#include "branchpath.h"
#include "nodeitem.h"
#include "nodestyle.h"

#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsTextItem>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPathStroker>
#include <QTextCursor>
#include <QTextDocument>

namespace {

constexpr qreal kHandleRadiusPx = 4;
constexpr qreal kHandleHitPx = 7;
constexpr qreal kLineHitWidthPx = 10;  // clickable band around a thin line
constexpr qreal kLabelPaddingPx = 3;
constexpr qreal kHighlightExtraPx = 4;  // selection band wider than the line

qreal distance(const QPointF &a, const QPointF &b) { return QLineF(a, b).length(); }

}  // namespace

RelationshipItem::RelationshipItem(const QString &id, QGraphicsItem *parent) : QGraphicsObject(parent) {
    m_relationship.id = id;
    setFlags(ItemIsSelectable | ItemIsFocusable);
    // Above the topics: the dashed line stays visible where it crosses one.
    setZValue(5);
}

void RelationshipItem::setContent(const Relationship &relationship, const Relationships::Geometry &geometry, const Look &look) {
    prepareGeometryChange();
    m_relationship = relationship;
    m_geometry = geometry;
    m_look = look;
    updateCachedShapes();
    update();
}

bool RelationshipItem::showsHandles() const {
    return isSelected() && m_relationship.shape != Relationship::Shape::Straight;
}

void RelationshipItem::updateCachedShapes() {
    // The text is measured only when it or its font changes, not at every move of a topic.
    const QString text = m_relationship.label.isEmpty() ? m_look.placeholder : m_relationship.label;
    if (text != m_measuredText || m_look.font != m_measuredFont) {
        m_measuredText = text;
        m_measuredFont = m_look.font;
        m_labelSize = QFontMetricsF(m_look.font).size(0, text) + QSizeF(2 * kLabelPaddingPx, 2 * kLabelPaddingPx);
    }
    m_labelRect = QRectF(m_geometry.labelPoint - QPointF(m_labelSize.width() / 2, m_labelSize.height() / 2), m_labelSize);

    QPainterPathStroker stroker;
    stroker.setWidth(std::max(kLineHitWidthPx, m_look.pen.widthF() + 6));
    m_hitPath = stroker.createStroke(m_geometry.path);
    m_hitPath.addRect(m_labelRect);

    const qreal margin = std::max<qreal>(kHandleHitPx, m_look.pen.widthF() * 3 + kHighlightExtraPx) + 2;
    QRectF bounds = m_geometry.path.boundingRect().united(m_labelRect);
    for (const QPointF &point : {m_geometry.fromControl, m_geometry.toControl, m_geometry.start, m_geometry.end})
        bounds = bounds.united(QRectF(point, QSizeF(1, 1)));
    m_bounds = bounds.adjusted(-margin, -margin, margin, margin);
}

QRectF RelationshipItem::boundingRect() const {
    // While an end is dragged the preview line reaches the mouse.
    if (m_dragPart == Part::FromEnd || m_dragPart == Part::ToEnd)
        return m_bounds.united(QRectF(m_dragPoint, QSizeF(1, 1)).adjusted(-4, -4, 4, 4));
    return m_bounds;
}

QPainterPath RelationshipItem::shape() const {
    QPainterPath path = m_hitPath;
    if (isSelected()) {
        for (const QPointF &point : {m_geometry.start, m_geometry.end})
            path.addEllipse(point, kHandleHitPx, kHandleHitPx);
        if (showsHandles())
            for (const QPointF &point : {m_geometry.fromControl, m_geometry.toControl})
                path.addEllipse(point, kHandleHitPx, kHandleHitPx);
    }
    return path;
}

void RelationshipItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    painter->setRenderHint(QPainter::Antialiasing);
    const QColor selection = NodeItem::selectionStyle().selectedColor;
    const qreal width = m_look.pen.widthF();
    if (isSelected()) {
        QColor band = selection;
        band.setAlpha(110);
        painter->setPen(QPen(band, width + kHighlightExtraPx, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->drawPath(m_geometry.path);
    }
    if (m_look.pen.style() != Qt::NoPen) {
        painter->setPen(m_look.pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(m_geometry.path);
        // End marks (default: a filled arrow at the target), drawn solid as on the branches.
        const auto drawMark = [&](BranchEnd kind, const QPointF &tip, const QPointF &outward) {
            if (kind == BranchEnd::None) return;
            const bool stroke = BranchPath::endIsStroke(kind);
            painter->setPen(stroke ? QPen(m_look.pen.color(), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin) : QPen(Qt::NoPen));
            painter->setBrush(stroke ? QBrush() : QBrush(m_look.pen.color()));
            painter->drawPath(BranchPath::endMark(kind, tip, outward, width));
        };
        drawMark(m_relationship.startMark.value_or(Relationships::kDefaultStartMark), m_geometry.start, m_geometry.startDirection);
        drawMark(m_relationship.endMark.value_or(Relationships::kDefaultEndMark), m_geometry.end, m_geometry.endDirection);
    }
    const bool placeholder = m_relationship.label.isEmpty();
    if (!m_editor && (!placeholder || isSelected())) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_look.backdrop);
        painter->drawRoundedRect(m_labelRect, 2, 2);
        QColor text = m_look.textColor;
        if (placeholder) text.setAlphaF(0.45);
        painter->setPen(text);
        painter->setFont(m_look.font);
        painter->drawText(m_labelRect, Qt::AlignCenter, placeholder ? m_look.placeholder : m_relationship.label);
    }
    if (isSelected()) {
        painter->setBrush(Qt::white);
        if (showsHandles()) {
            painter->setPen(QPen(selection, 1));
            painter->drawLine(m_geometry.start, m_geometry.fromControl);
            painter->drawLine(m_geometry.end, m_geometry.toControl);
            painter->setPen(QPen(selection, 1.5));
            for (const QPointF &point : {m_geometry.fromControl, m_geometry.toControl})
                painter->drawEllipse(point, kHandleRadiusPx, kHandleRadiusPx);
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(selection);
        for (const QPointF &point : {m_geometry.start, m_geometry.end})
            painter->drawEllipse(point, kHandleRadiusPx - 0.5, kHandleRadiusPx - 0.5);
    }
    if (m_dragPart == Part::FromEnd || m_dragPart == Part::ToEnd) {
        painter->setPen(QPen(selection, 1.5, Qt::DashLine));
        painter->drawLine(m_dragPart == Part::FromEnd ? m_geometry.end : m_geometry.start, m_dragPoint);
    }
}

QVariant RelationshipItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemSelectedHasChanged && !value.toBool() && m_editor) finishEditing(true);
    return QGraphicsObject::itemChange(change, value);
}

RelationshipItem::Part RelationshipItem::partAt(const QPointF &pos) const {
    if (!isSelected()) return Part::None;
    if (showsHandles()) {
        if (distance(pos, m_geometry.fromControl) <= kHandleHitPx) return Part::FromControl;
        if (distance(pos, m_geometry.toControl) <= kHandleHitPx) return Part::ToControl;
    }
    if (distance(pos, m_geometry.start) <= kHandleHitPx) return Part::FromEnd;
    if (distance(pos, m_geometry.end) <= kHandleHitPx) return Part::ToEnd;
    return Part::None;
}

void RelationshipItem::mousePressEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() != Qt::LeftButton) { QGraphicsObject::mousePressEvent(event); return; }
    m_dragPart = partAt(event->scenePos());
    if (m_dragPart == Part::None) { QGraphicsObject::mousePressEvent(event); return; }
    m_dragged = m_relationship;
    m_dragPoint = event->scenePos();
    event->accept();
}

void RelationshipItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event) {
    if (m_dragPart == Part::None) { QGraphicsObject::mouseMoveEvent(event); return; }
    prepareGeometryChange();
    m_dragPoint = event->scenePos();
    if (m_dragPart == Part::FromControl) m_dragged.fromControl = m_dragPoint - m_geometry.fromCentre;
    else if (m_dragPart == Part::ToControl) m_dragged.toControl = m_dragPoint - m_geometry.toCentre;
    if ((m_dragPart == Part::FromControl || m_dragPart == Part::ToControl) && geometryFor) {
        m_geometry = geometryFor(m_dragged);
        updateCachedShapes();
    }
    update();
}

void RelationshipItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event) {
    const Part part = std::exchange(m_dragPart, Part::None);
    if (part == Part::None) { QGraphicsObject::mouseReleaseEvent(event); return; }
    prepareGeometryChange();
    update();
    if (part == Part::FromEnd || part == Part::ToEnd) {
        // Dropped on another element: that end moves there with an automatic handle;
        // anywhere else nothing changes.
        const QString target = elementAt ? elementAt(event->scenePos()) : QString();
        const QString &other = part == Part::FromEnd ? m_relationship.to : m_relationship.from;
        const QString &current = part == Part::FromEnd ? m_relationship.from : m_relationship.to;
        if (target.isEmpty() || target == other || target == current) return;
        if (part == Part::FromEnd) { m_dragged.from = target; m_dragged.fromControl.reset(); }
        else { m_dragged.to = target; m_dragged.toControl.reset(); }
    }
    if (m_dragged != m_relationship) emit edited(m_dragged);
}

void RelationshipItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    setSelected(true);
    beginEditing();
    event->accept();
}

void RelationshipItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    if (!isSelected()) {
        if (scene()) scene()->clearSelection();
        setSelected(true);
    }
    emit menuRequested(id(), event->screenPos());
    event->accept();
}

void RelationshipItem::keyPressEvent(QKeyEvent *event) {
    const QString text = event->text();
    const bool printable = !text.isEmpty() && text.at(0).isPrint() &&
                           !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (isSelected() && printable) { beginEditing(text); event->accept(); return; }
    if (isSelected() && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_F2)) {
        beginEditing();
        event->accept();
        return;
    }
    QGraphicsObject::keyPressEvent(event);
}

void RelationshipItem::beginEditing(const QString &initialText) {
    if (m_editor) return;
    m_editor = new QGraphicsTextItem(this);
    m_editor->setFont(m_look.font);
    m_editor->setDefaultTextColor(m_look.textColor);
    m_editor->setPlainText(initialText.isEmpty() ? m_relationship.label : initialText);
    m_editor->document()->setDocumentMargin(kLabelPaddingPx);
    m_editor->setTextInteractionFlags(Qt::TextEditorInteraction);
    const QRectF box = m_editor->boundingRect();
    m_editor->setPos(m_geometry.labelPoint - QPointF(box.width() / 2, box.height() / 2));
    m_editor->installSceneEventFilter(this);
    m_editor->setFocus(Qt::OtherFocusReason);
    QTextCursor cursor(m_editor->document());
    cursor.movePosition(QTextCursor::End);
    // Existing text is selected (replaced by typing), typed text keeps the cursor after it.
    if (initialText.isEmpty()) cursor.movePosition(QTextCursor::Start, QTextCursor::KeepAnchor);
    m_editor->setTextCursor(cursor);
    update();
}

void RelationshipItem::commitEditing() { if (m_editor) finishEditing(true); }

void RelationshipItem::finishEditing(bool keep) {
    QGraphicsTextItem *editor = std::exchange(m_editor, nullptr);
    if (!editor) return;
    const QString text = editor->toPlainText().simplified();
    // BUG-023: not removeSceneEventFilter, which crashed when called from sceneEventFilter (the
    // scene walks its filter map); the editor's destruction drops the filter.
    editor->hide();
    editor->deleteLater();  // may be inside one of its own events
    update();
    if (keep && text != m_relationship.label) {
        Relationship changed = m_relationship;
        changed.label = text;
        emit edited(changed);
    }
}

bool RelationshipItem::sceneEventFilter(QGraphicsItem *watched, QEvent *event) {
    if (watched != m_editor) return false;
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { finishEditing(false); setFocus(); return true; }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && !(key->modifiers() & Qt::ShiftModifier)) {
            finishEditing(true);
            setFocus();
            return true;
        }
    } else if (event->type() == QEvent::FocusOut) {
        finishEditing(true);
    }
    return false;
}
