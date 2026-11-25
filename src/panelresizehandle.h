#pragma once

#include <QPointer>
#include <QWidget>

#include <functional>

// A side panel's draggable edge (user's request 2026-11-22): an invisible strip with the
// horizontal resize cursor, on the panel's parent so it can lie over the gap beside the panel.
// Dragging it asks for a new width; the owner clamps and applies it, and stores it on release.
class PanelResizeHandle final : public QWidget {
    Q_OBJECT

public:
    // edge: the panel's side being dragged (Qt::LeftEdge or Qt::RightEdge). The strip covers
    // [edge - inside, edge + outside] measured from the panel outwards.
    PanelResizeHandle(QWidget *panel, Qt::Edge edge, int inside, int outside);

    // Called while dragging with the wanted width (not yet clamped); returns the width applied.
    std::function<int(int wanted)> resizeTo;
    // Called once on release when the width changed.
    std::function<void(int width)> finished;

    void place();  // follows the panel's geometry and visibility

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QPointer<QWidget> m_panel;
    Qt::Edge m_edge;
    int m_inside;
    int m_outside;
    bool m_dragging = false;
    int m_startX = 0;
    int m_startWidth = 0;
    int m_lastWidth = 0;
};
