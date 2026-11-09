#pragma once

#include <QGraphicsView>
#include <QMouseEvent>
#include <QTimer>
#include <memory>

class MindMapView final : public QGraphicsView {
    Q_OBJECT
public:
    explicit MindMapView(QWidget *parent = nullptr);
    int zoomPercent() const;
    void setZoomPercent(int percent);
    // Scrolls just enough for the scene rectangle and a margin (view pixels) to be visible, growing
    // the scene area when the map edge would stop the scroll (as autoScrollTick does).
    void reveal(const QRectF &sceneRect, int margin);
    // FEAT-033: zona vicino al bordo della vista in cui parte lo scorrimento automatico (px della
    // vista, griglia di AGENTS.md) e passo dello scorrimento ogni kAutoScrollIntervalMs: minimo
    // appena entrati nella zona, massimo sul bordo e oltre.
    static constexpr int kAutoScrollMarginPx = 24;
    static constexpr int kAutoScrollMinStepPx = 4;
    static constexpr int kAutoScrollMaxStepPx = 40;
    static constexpr int kAutoScrollIntervalMs = 16;
    // Node drag on Linux (Wayland and X11): at most one move handled per interval (about 120 per second); the
    // latest position waits and is handled when the interval ends or at the release.
    static constexpr int kDragMoveIntervalMs = 8;
    // On by default only on Linux (see the constructor); tests turn it on to check it.
    void setDragMoveCoalescing(bool enabled) { m_coalesceDragMoves = enabled; }
signals:
    void zoomChanged(int percent);
    // FEAT-049: file del computer lasciati su un punto della mappa (coordinate della scena).
    void filesDropped(const QStringList &paths, const QPointF &scenePos);
    // After the view took its new size (the side panel opening narrows it).
    void resized();
protected:
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
private:
    QPoint autoScrollStep(const QPoint &viewportPos) const;
    void stopAutoScroll();
    void autoScrollTick();
    void handleMouseMove(QMouseEvent *event);
    void flushPendingDragMove();

    bool m_panning = false;
    QPoint m_lastPan;
    // Delta della rotellina non ancora diventato un passo di zoom (touchpad: eventi piccoli).
    int m_wheelZoomRemainder = 0;
    // FEAT-033: vero mentre si trascina un nodo o il suo bordo con il tasto sinistro.
    bool m_autoScrollArmed = false;
    QPoint m_autoScrollPos;  // ultima posizione del mouse, coordinate del viewport
    QTimer m_autoScrollTimer;
    bool m_coalesceDragMoves = false;
    QTimer m_dragMoveTimer;
    std::unique_ptr<QMouseEvent> m_pendingDragMove;
};
