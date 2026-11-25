#include "stickercanvasview.h"

#include "mobilestickereditor.h"

#include <QQuickWindow>

StickerCanvasView::StickerCanvasView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAcceptedMouseButtons(Qt::LeftButton);
    setAntialiasing(true);
    setFlag(ItemIsFocusScope, false);
    setActiveFocusOnTab(true);  // a hardware keyboard's arrows, Delete and Esc
    connect(this, &StickerCanvasView::colorsChanged, this, [this] { update(); });
}

StickerCanvasView::~StickerCanvasView() {
    if (m_editor) m_editor->canvas().updateRequested = nullptr;
}

void StickerCanvasView::setEditor(MobileStickerEditor *editor) {
    if (editor == m_editor) return;
    if (m_editor) m_editor->canvas().updateRequested = nullptr;
    m_editor = editor;
    if (m_editor) {
        StickerCanvas &canvas = m_editor->canvas();
        canvas.setMetrics({kHandlePx, kReachPx, kPickPx, 16});
        canvas.setViewSize(size());
        if (window()) canvas.setDevicePixelRatio(window()->effectiveDevicePixelRatio());
        canvas.updateRequested = [this] { update(); };
    }
    emit editorChanged();
    update();
}

void StickerCanvasView::paint(QPainter *painter) {
    if (!m_editor) return;
    m_editor->canvas().paint(painter, {m_raised, m_sunken, m_surface, m_accent});
}

QPointF StickerCanvasView::toView(const QPointF &point) const { return m_editor ? m_editor->canvas().toView(point) : QPointF(); }

QPointF StickerCanvasView::toDocument(const QPointF &point) const { return m_editor ? m_editor->canvas().toDocument(point) : QPointF(); }

void StickerCanvasView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (m_editor && newGeometry.size() != oldGeometry.size()) m_editor->canvas().setViewSize(newGeometry.size());
}

void StickerCanvasView::itemChange(ItemChange change, const ItemChangeData &value) {
    QQuickPaintedItem::itemChange(change, value);
    if ((change == ItemSceneChange || change == ItemDevicePixelRatioHasChanged) && m_editor && window())
        m_editor->canvas().setDevicePixelRatio(window()->effectiveDevicePixelRatio());
}

void StickerCanvasView::setPressed(bool pressed) {
    if (pressed == m_pressed) return;
    m_pressed = pressed;
    setKeepMouseGrab(pressed);  // a drag on the preview edits; the modal's list must not take it
    setKeepTouchGrab(pressed);
    emit pressedChanged();
}

bool StickerCanvasView::shift(const QInputEvent *event) const {
    return (m_editor && m_editor->shift()) || (event->modifiers() & Qt::ShiftModifier);
}

void StickerCanvasView::mousePressEvent(QMouseEvent *event) {
    if (!m_editor || event->button() != Qt::LeftButton) {
        event->ignore();
        return;
    }
    forceActiveFocus(Qt::MouseFocusReason);
    setPressed(true);
    m_last = event->position();
    m_editor->canvas().press(m_last, shift(event));
}

void StickerCanvasView::mouseDoubleClickEvent(QMouseEvent *event) {
    if (!m_editor || event->button() != Qt::LeftButton) return;
    m_editor->canvas().doubleClick(event->position());
}

void StickerCanvasView::mouseMoveEvent(QMouseEvent *event) {
    if (!m_editor) return;
    m_last = event->position();
    m_editor->canvas().move(m_last, shift(event));
}

void StickerCanvasView::mouseReleaseEvent(QMouseEvent *event) {
    setPressed(false);
    if (!m_editor || event->button() != Qt::LeftButton) return;
    m_last = event->position();
    m_editor->canvas().release(m_last, shift(event));
}

void StickerCanvasView::mouseUngrabEvent() {
    // The grab taken away (the modal closed, a gesture of the system): the drag ends where it was.
    setPressed(false);
    if (m_editor && m_editor->canvas().dragging()) m_editor->canvas().release(m_last, m_editor->shift());
}

void StickerCanvasView::keyPressEvent(QKeyEvent *event) {
    if (!m_editor || !m_editor->canvas().key(event->key(), event->modifiers() & Qt::ShiftModifier)) event->ignore();
}
