#include "mindmapview.h"
#include "uitheme.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include "nodeitem.h"
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QWheelEvent>
#include <QtMath>
#include <QApplication>
#include <QGuiApplication>
#include <algorithm>

MindMapView::MindMapView(QWidget *parent) : QGraphicsView(parent) {
    // FEAT-035 part B: the items paint with the document's font (the viewport too: under a style
    // sheet a child does not inherit its parent's font).
    setFont(UiTheme::documentFont());
    viewport()->setFont(UiTheme::documentFont());
    setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    setDragMode(RubberBandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    // I rami vengono ricreati durante il trascinamento: ridisegnare l'intero
    // viewport evita che restino frammenti delle curve precedenti.
    // Ora i rami non vengono più ricreati ma aggiornati con setPath(), che invalida
    // correttamente l'area vecchia e nuova. Si torna quindi al default di Qt,
    // MinimalViewportUpdate: viene ridisegnata solo la regione cambiata (il nodo
    // trascinato e i suoi rami) invece dell'intera finestra. Su CPU lente e senza
    // accelerazione grafica è il guadagno più visibile durante il trascinamento.
    setViewportUpdateMode(MinimalViewportUpdate);
    m_autoScrollTimer.setInterval(kAutoScrollIntervalMs);
    connect(&m_autoScrollTimer, &QTimer::timeout, this, &MindMapView::autoScrollTick);
}

int MindMapView::zoomPercent() const {
    return qRound(transform().m11() * 100.0);
}

void MindMapView::setZoomPercent(int percent) {
    const int bounded = qBound(25, percent, 400);
    const qreal targetScale = bounded / 100.0;
    QTransform next;
    next.scale(targetScale, targetScale);
    setTransform(next);
    emit zoomChanged(bounded);
}

void MindMapView::reveal(const QRectF &sceneRect, int margin) {
    if (!scene() || sceneRect.isNull()) return;
    const qreal mx = margin / transform().m11();
    const qreal my = margin / transform().m22();
    const QRectF target = sceneRect.adjusted(-mx, -my, mx, my);
    // The visible area moved to contain the target: the scene must contain it for the scroll
    // bars to reach it. MindMapScene::updateSceneRect() recomputes the area at the next layout.
    QRectF next = mapToScene(viewport()->rect()).boundingRect();
    if (target.right() > next.right()) next.moveRight(target.right());
    if (target.left() < next.left()) next.moveLeft(target.left());
    if (target.bottom() > next.bottom()) next.moveBottom(target.bottom());
    if (target.top() < next.top()) next.moveTop(target.top());
    if (!this->sceneRect().contains(next)) scene()->setSceneRect(this->sceneRect().united(next));
    ensureVisible(sceneRect, margin, margin);
}

void MindMapView::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    emit resized();
}

void MindMapView::wheelEvent(QWheelEvent *event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // (B10, 2026-10-13: con delta verticale 0, cioè rotellina orizzontale o touchpad di
        // lato, lo zoom scendeva sempre; sui touchpad ogni piccolo evento valeva un passo
        // intero del 15%.) Ora i delta si sommano e un passo scatta ogni 120 unità, cioè uno
        // scatto della rotellina (QWheelEvent::angleDelta, 1/8 di grado).
        const int delta = event->angleDelta().y();
        event->accept();
        if (delta == 0) return;
        if ((delta > 0) != (m_wheelZoomRemainder > 0)) m_wheelZoomRemainder = 0;  // cambio di verso
        m_wheelZoomRemainder += delta;
        int steps = m_wheelZoomRemainder / 120;
        if (steps == 0) return;
        m_wheelZoomRemainder -= steps * 120;
        qreal factor = 1.0;
        for (; steps > 0; --steps) factor *= 1.15;
        for (; steps < 0; ++steps) factor /= 1.15;
        setZoomPercent(qRound(zoomPercent() * factor));
        return;
    }
    // Richiesta dell'utente 2026-10-21: Maiusc + rotellina scorre in orizzontale (la rotellina da sola
    // in verticale, come prima). La rotazione si passa alla barra orizzontale senza Maiusc, cosi'
    // il passo e' lo stesso della verticale.
    if ((event->modifiers() & Qt::ShiftModifier) && event->angleDelta().y() != 0) {
        const QPoint delta(event->angleDelta().y(), 0);
        QWheelEvent horizontal(event->position(), event->globalPosition(), event->pixelDelta().transposed(), delta,
                               event->buttons(), event->modifiers() & ~Qt::ShiftModifier, event->phase(), event->inverted());
        QApplication::sendEvent(horizontalScrollBar(), &horizontal);
        event->accept();
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void MindMapView::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::MiddleButton) {
        m_panning = true; m_lastPan = event->pos(); setCursor(Qt::ClosedHandCursor); event->accept(); return;
    }
    QGraphicsView::mousePressEvent(event);
    // FEAT-033: lo scorrimento automatico vale solo se il clic ha preso un nodo (spostamento o
    // bordo); non per la selezione a rettangolo ne' mentre si scrive nel nodo.
    const auto *grabbed = scene() ? qgraphicsitem_cast<NodeItem *>(scene()->mouseGrabberItem()) : nullptr;
    m_autoScrollArmed = event->button() == Qt::LeftButton && grabbed && !grabbed->isEditing();
}

void MindMapView::mouseReleaseEvent(QMouseEvent *event) {
    if (m_panning) { m_panning = false; unsetCursor(); event->accept(); return; }
    stopAutoScroll();
    QGraphicsView::mouseReleaseEvent(event);
}

void MindMapView::mouseMoveEvent(QMouseEvent *event) {
    if (m_panning) {
        const QPoint delta = event->pos() - m_lastPan; m_lastPan = event->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept(); return;
    }
    QGraphicsView::mouseMoveEvent(event);
    if (!m_autoScrollArmed) return;
    if (!(event->buttons() & Qt::LeftButton)) { stopAutoScroll(); return; }
    m_autoScrollPos = event->position().toPoint();
    if (autoScrollStep(m_autoScrollPos).isNull()) m_autoScrollTimer.stop();
    else if (!m_autoScrollTimer.isActive()) m_autoScrollTimer.start();
}

void MindMapView::keyPressEvent(QKeyEvent *event) {
    // FEAT-033: Esc mentre si trascina il bordo di un nodo annulla l'allargamento; FEAT-015: annulla
    // anche il trascinamento strutturale.
    if (event->key() == Qt::Key_Escape && scene()) {
        if (auto *item = qgraphicsitem_cast<NodeItem *>(scene()->mouseGrabberItem()); item && (item->cancelResize() || item->cancelReorder())) {
            stopAutoScroll();
            event->accept();
            return;
        }
    }
    QGraphicsView::keyPressEvent(event);
}

QPoint MindMapView::autoScrollStep(const QPoint &viewportPos) const {
    // Passo per asse: 0 lontano dai bordi; nella zona di kAutoScrollMarginPx cresce da
    // kAutoScrollMinStepPx a kAutoScrollMaxStepPx avvicinandosi al bordo; oltre il bordo e'
    // massimo. Negativo verso sinistra/alto.
    const QRect area = viewport()->rect();
    auto axis = [](int position, int low, int high) {
        const int intoLow = low + kAutoScrollMarginPx - position;   // > 0 nella zona bassa
        const int intoHigh = position - (high - kAutoScrollMarginPx);  // > 0 nella zona alta
        auto step = [](int depth) {
            const int clamped = std::min(depth, kAutoScrollMarginPx);
            return kAutoScrollMinStepPx + (kAutoScrollMaxStepPx - kAutoScrollMinStepPx) * clamped / kAutoScrollMarginPx;
        };
        if (intoLow > 0) return -step(intoLow);
        if (intoHigh > 0) return step(intoHigh);
        return 0;
    };
    return {axis(viewportPos.x(), area.left(), area.right()), axis(viewportPos.y(), area.top(), area.bottom())};
}

void MindMapView::stopAutoScroll() {
    m_autoScrollArmed = false;
    m_autoScrollTimer.stop();
}

void MindMapView::autoScrollTick() {
    // Il trascinamento puo' essere finito senza rilascio visto dalla vista (Esc, perdita del mouse).
    if (!m_autoScrollArmed || !scene() || !scene()->mouseGrabberItem()) { stopAutoScroll(); return; }
    const QPoint step = autoScrollStep(m_autoScrollPos);
    if (step.isNull()) { m_autoScrollTimer.stop(); return; }
    // Le barre scorrono solo dentro l'area della scena: la si allarga quanto serve perche' il
    // passo sia possibile. MindMapScene::updateSceneRect() la ricalcola al rilascio.
    const QRectF visible = mapToScene(viewport()->rect()).boundingRect();
    const QRectF next = visible.translated(step.x() / transform().m11(), step.y() / transform().m22());
    if (!sceneRect().contains(next)) scene()->setSceneRect(sceneRect().united(next));
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + step.x());
    verticalScrollBar()->setValue(verticalScrollBar()->value() + step.y());
    // Il mouse e' fermo ma la mappa si e' spostata sotto: si ripete il movimento perche' il nodo
    // (o il suo bordo) segua il puntatore.
    QMouseEvent move(QEvent::MouseMove, QPointF(m_autoScrollPos), mapToGlobal(QPointF(m_autoScrollPos)),
                     Qt::NoButton, Qt::LeftButton, QGuiApplication::keyboardModifiers());
    QGraphicsView::mouseMoveEvent(&move);
}

// FEAT-049: file trascinati da Esplora risorse. Solo file locali; chi riceve filesDropped decide
// (sopra un nodo si allegano, le immagini diventano l'immagine del nodo).
namespace {
QStringList localFiles(const QMimeData *data) {
    QStringList paths;
    if (!data || !data->hasUrls()) return paths;
    for (const QUrl &url : data->urls())
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) paths.append(url.toLocalFile());
    return paths;
}
}  // namespace

void MindMapView::dragEnterEvent(QDragEnterEvent *event) {
    if (!localFiles(event->mimeData()).isEmpty()) { event->acceptProposedAction(); return; }
    QGraphicsView::dragEnterEvent(event);
}

void MindMapView::dragMoveEvent(QDragMoveEvent *event) {
    if (!localFiles(event->mimeData()).isEmpty()) { event->acceptProposedAction(); return; }
    QGraphicsView::dragMoveEvent(event);
}

void MindMapView::dropEvent(QDropEvent *event) {
    const QStringList paths = localFiles(event->mimeData());
    if (paths.isEmpty()) { QGraphicsView::dropEvent(event); return; }
    event->acceptProposedAction();
    emit filesDropped(paths, mapToScene(event->position().toPoint()));
}
