#include "boundaryitem.h"

#include "nodeitem.h"

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

constexpr qreal kSelectionGapPx = 3;    // blue frame just outside the boundary
constexpr qreal kSelectionWidthPx = 1.5;
constexpr qreal kBorderHitPx = 8;       // clickable band around the border

}  // namespace

BoundaryItem::BoundaryItem(const QString &id, QGraphicsItem *parent) : QGraphicsObject(parent) {
    m_boundary.id = id;
    setFlags(ItemIsSelectable | ItemIsFocusable);
    // Under the branch lines (-1, -2) and the topics (0); nested ones are raised by the scene.
    setZValue(-3);
}

void BoundaryItem::setContent(const Boundary &boundary, const QRectF &covered, const Look &look) {
    prepareGeometryChange();
    m_boundary = boundary;
    m_look = look;
    m_stroke = Boundaries::strokeRect(covered);
    updateTitleRect();
    update();
}

void BoundaryItem::updateTitleRect() {
    // The title is measured only when it or its font changes, not at every move of a topic.
    const QString text = m_boundary.title;
    if (text != m_measuredTitle || m_look.font != m_measuredFont) {
        m_measuredTitle = text;
        m_measuredFont = m_look.font;
        m_titleWidth = QFontMetricsF(m_look.font).horizontalAdvance(text);
    }
    m_titleRect = text.isEmpty() && !m_editor ? QRectF() : Boundaries::titleRect(m_stroke, m_titleWidth);
}

QPainterPath BoundaryItem::outline() const {
    QPainterPath path = Boundaries::outline(m_boundary.shape, m_stroke);
    if (!m_titleRect.isEmpty()) path = path.united(Boundaries::titlePath(m_titleRect));
    return path;
}

QRectF BoundaryItem::boundingRect() const {
    const qreal margin = std::max(m_look.pen.widthF(), kBorderHitPx) / 2 + kSelectionGapPx + kSelectionWidthPx;
    QRectF bounds = m_stroke;
    if (!m_titleRect.isEmpty()) bounds = bounds.united(m_titleRect);
    return bounds.adjusted(-margin, -margin, margin, margin);
}

QPainterPath BoundaryItem::shape() const {
    QPainterPath path = Boundaries::outline(m_boundary.shape, m_stroke);
    QPainterPathStroker stroker;
    stroker.setWidth(std::max(kBorderHitPx, m_look.pen.widthF()));
    path = path.united(stroker.createStroke(path));
    if (!m_titleRect.isEmpty()) path.addRect(m_titleRect);
    return path;
}

void BoundaryItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) {
    painter->setRenderHint(QPainter::Antialiasing);
    const QPainterPath body = Boundaries::outline(m_boundary.shape, m_stroke);
    painter->fillPath(body, m_look.fill);
    painter->strokePath(body, m_look.pen);
    if (!m_titleRect.isEmpty()) {
        // The tab has the border's color; with no border (pattern none) it still shows.
        painter->fillPath(Boundaries::titlePath(m_titleRect), m_look.pen.color());
        painter->setFont(m_look.font);
        if (!m_editor) {
            painter->setPen(m_look.titleText);
            painter->drawText(m_titleRect, Qt::AlignCenter, m_boundary.title);
        } else if (m_editor->document()->isEmpty()) {
            QColor placeholder = m_look.titleText;
            placeholder.setAlphaF(0.5);
            painter->setPen(placeholder);
            painter->drawText(m_titleRect, Qt::AlignCenter, m_look.placeholder);
        }
    }
    if (isSelected()) {
        QRectF frame = m_stroke;
        if (!m_titleRect.isEmpty()) frame = frame.united(m_titleRect);
        const qreal gap = m_look.pen.widthF() / 2 + kSelectionGapPx;
        painter->setPen(QPen(NodeItem::selectionStyle().selectedColor, kSelectionWidthPx));
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(frame.adjusted(-gap, -gap, gap, gap));
    }
}

QVariant BoundaryItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == ItemSelectedHasChanged && !value.toBool() && m_editor) finishEditing(true, true);
    return QGraphicsObject::itemChange(change, value);
}

void BoundaryItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) {
    if (event->button() != Qt::LeftButton) return;
    setSelected(true);
    beginEditing();
    event->accept();
}

void BoundaryItem::contextMenuEvent(QGraphicsSceneContextMenuEvent *event) {
    if (!isSelected()) {
        if (scene()) scene()->clearSelection();
        setSelected(true);
    }
    emit menuRequested(id(), event->screenPos());
    event->accept();
}

void BoundaryItem::keyPressEvent(QKeyEvent *event) {
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

void BoundaryItem::beginEditing(const QString &initialText) {
    if (m_editor) return;
    prepareGeometryChange();
    m_editor = new QGraphicsTextItem(this);
    m_editor->setFont(m_look.font);
    m_editor->setDefaultTextColor(m_look.titleText);
    m_editor->document()->setDocumentMargin(0);
    m_editor->setTextInteractionFlags(Qt::TextEditorInteraction);
    m_editor->setPlainText(initialText.isEmpty() ? m_boundary.title : initialText);
    // The tab grows with the text being written; an empty one is as wide as the placeholder.
    const auto place = [this] {
        prepareGeometryChange();
        const QString text = m_editor->toPlainText();
        m_titleWidth = QFontMetricsF(m_look.font).horizontalAdvance(text.isEmpty() ? m_look.placeholder : text);
        m_measuredTitle = text;
        m_titleRect = Boundaries::titleRect(m_stroke, m_titleWidth);
        const QRectF box = m_editor->boundingRect();
        m_editor->setPos(m_titleRect.left() + Boundaries::kTitlePaddingPx, m_titleRect.center().y() - box.height() / 2);
        update();
    };
    connect(m_editor->document(), &QTextDocument::contentsChanged, this, place);
    place();
    m_editor->installSceneEventFilter(this);
    m_editor->setFocus(Qt::OtherFocusReason);
    QTextCursor cursor(m_editor->document());
    cursor.movePosition(QTextCursor::End);
    // Existing text is selected (replaced by typing), typed text keeps the cursor after it.
    if (initialText.isEmpty()) cursor.movePosition(QTextCursor::Start, QTextCursor::KeepAnchor);
    m_editor->setTextCursor(cursor);
}

void BoundaryItem::commitEditing() { if (m_editor) finishEditing(true); }

void BoundaryItem::finishEditing(bool keep, bool fromSceneEvent) {
    QGraphicsTextItem *editor = std::exchange(m_editor, nullptr);
    if (!editor) return;
    const QString text = editor->toPlainText().simplified();
    // BUG-023: the filter is not removed here. This runs inside sceneEventFilter, while
    // QGraphicsScene walks its filter map; removing an entry there crashed. The editor's
    // destruction drops the filter, and m_editor is already null, so the filter ignores it.
    editor->hide();
    editor->disconnect(this);
    editor->document()->disconnect(this);
    editor->deleteLater();  // may be inside one of its own events
    prepareGeometryChange();
    m_measuredTitle.clear();  // measured again for the saved title
    m_measuredFont = QFont();
    updateTitleRect();
    update();
    if (!keep || text == m_boundary.title) return;
    Boundary changed = m_boundary;
    changed.title = text;
    if (!fromSceneEvent) { emit edited(changed); return; }
    // BUG-023: a click elsewhere confirms the title while QGraphicsScene is still handling that
    // press; the new title lays the map out again (the tab needs room), which moved and replaced
    // items under the scene's running loop and crashed. The tab shows the title at once; the
    // document gets it right after the event (dropped if the item is gone by then).
    prepareGeometryChange();
    m_boundary.title = text;
    updateTitleRect();
    update();
    QMetaObject::invokeMethod(this, [this, changed] { emit edited(changed); }, Qt::QueuedConnection);
}

bool BoundaryItem::sceneEventFilter(QGraphicsItem *watched, QEvent *event) {
    if (watched != m_editor) return false;
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) { finishEditing(false); setFocus(); return true; }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            finishEditing(true);
            setFocus();
            return true;
        }
    } else if (event->type() == QEvent::FocusOut) {
        finishEditing(true, true);
    }
    return false;
}
