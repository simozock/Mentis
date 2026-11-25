#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class MobileStickerEditor;

// FEAT-070 on the phone: the sticker editor's preview (StickerCanvas) as a Qt Quick item. A finger
// (Qt Quick gives touches to the item as a mouse) or a pen drives it as the desktop's mouse; the
// editor's shift stands for the Shift key. Handles and points are drawn larger and reached from
// half the touch target (ui-mobile.md, "Touch targets"). The pointer is kept while dragging, so the
// modal's list does not scroll away with it.
class StickerCanvasView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MobileStickerEditor *editor READ editor WRITE setEditor NOTIFY editorChanged)
    Q_PROPERTY(QColor raisedColor MEMBER m_raised NOTIFY colorsChanged)
    Q_PROPERTY(QColor sunkenColor MEMBER m_sunken NOTIFY colorsChanged)
    Q_PROPERTY(QColor surfaceColor MEMBER m_surface NOTIFY colorsChanged)
    Q_PROPERTY(QColor accentColor MEMBER m_accent NOTIFY colorsChanged)
    // A finger or the mouse down on the preview (the modal's list stops scrolling meanwhile).
    Q_PROPERTY(bool pressed READ pressed NOTIFY pressedChanged)

public:
    explicit StickerCanvasView(QQuickItem *parent = nullptr);
    ~StickerCanvasView() override;  // the canvas stops calling the item

    MobileStickerEditor *editor() const { return m_editor; }
    bool pressed() const { return m_pressed; }
    void setEditor(MobileStickerEditor *editor);
    void paint(QPainter *painter) override;

    // View <-> viewBox units (for tests).
    Q_INVOKABLE QPointF toView(const QPointF &point) const;
    Q_INVOKABLE QPointF toDocument(const QPointF &point) const;

    static constexpr qreal kHandlePx = 12;
    static constexpr qreal kReachPx = 24;  // half the 48 px touch target
    static constexpr qreal kPickPx = 16;

signals:
    void editorChanged();
    void colorsChanged();
    void pressedChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseUngrabEvent() override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    bool shift(const QInputEvent *event) const;
    void setPressed(bool pressed);

    QPointer<MobileStickerEditor> m_editor;
    QColor m_raised, m_sunken, m_surface, m_accent;
    bool m_pressed = false;
    QPointF m_last;  // the pointer's last position, for a release the item did not get
};
