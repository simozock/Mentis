#include "panelresizehandle.h"

#include <QEvent>
#include <QMouseEvent>

PanelResizeHandle::PanelResizeHandle(QWidget *panel, Qt::Edge edge, int inside, int outside)
    : QWidget(panel->parentWidget()), m_panel(panel), m_edge(edge), m_inside(inside), m_outside(outside) {
    setObjectName(panel->objectName() + QStringLiteral("ResizeHandle"));
    setCursor(Qt::SizeHorCursor);
    setAttribute(Qt::WA_NoSystemBackground);
    panel->installEventFilter(this);
    place();
}

void PanelResizeHandle::place() {
    if (!m_panel || m_panel->parentWidget() != parentWidget()) {
        hide();
        return;
    }
    const QRect panel = m_panel->geometry();
    const int left = m_edge == Qt::LeftEdge ? panel.left() - m_outside : panel.right() + 1 - m_inside;
    setGeometry(left, panel.top(), m_inside + m_outside, panel.height());
    setVisible(m_panel->isVisible());
    raise();
}

bool PanelResizeHandle::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_panel) {
        switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
        case QEvent::ParentChange: place(); break;
        default: break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void PanelResizeHandle::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !m_panel) return QWidget::mousePressEvent(event);
    m_dragging = true;
    m_startX = event->globalPosition().toPoint().x();
    m_startWidth = m_lastWidth = m_panel->width();
    event->accept();
}

void PanelResizeHandle::mouseMoveEvent(QMouseEvent *event) {
    if (!m_dragging || !m_panel || !resizeTo) return QWidget::mouseMoveEvent(event);
    const int dx = event->globalPosition().toPoint().x() - m_startX;
    m_lastWidth = resizeTo(m_startWidth + (m_edge == Qt::LeftEdge ? -dx : dx));
    event->accept();
}

void PanelResizeHandle::mouseReleaseEvent(QMouseEvent *event) {
    if (!m_dragging || event->button() != Qt::LeftButton) return QWidget::mouseReleaseEvent(event);
    m_dragging = false;
    if (m_lastWidth != m_startWidth && finished) finished(m_lastWidth);
    event->accept();
}
