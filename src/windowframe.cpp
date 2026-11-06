#include "windowframe.h"

namespace WindowFrame {

Hit hitTest(const QPoint &pos, const QSize &size, bool resizable, bool caption, bool maximizeButton) {
    if (resizable) {
        const int x = pos.x(), y = pos.y(), w = size.width(), h = size.height();
        const bool left = x < kResizeBorder, right = x >= w - kResizeBorder;
        const bool top = y < kResizeBorder, bottom = y >= h - kResizeBorder;
        // The corners take kResizeCorner along each edge, so they are easy to grab.
        const bool nearLeft = x < kResizeCorner, nearRight = x >= w - kResizeCorner;
        const bool nearTop = y < kResizeCorner, nearBottom = y >= h - kResizeCorner;
        if ((top && nearLeft) || (left && nearTop)) return Hit::TopLeft;
        if ((top && nearRight) || (right && nearTop)) return Hit::TopRight;
        if ((bottom && nearLeft) || (left && nearBottom)) return Hit::BottomLeft;
        if ((bottom && nearRight) || (right && nearBottom)) return Hit::BottomRight;
        if (left) return Hit::Left;
        if (right) return Hit::Right;
        if (top) return Hit::Top;
        if (bottom) return Hit::Bottom;
    }
    if (maximizeButton) return Hit::MaximizeButton;
    return caption ? Hit::Caption : Hit::Client;
}

#ifndef Q_OS_WIN
bool install(QWidget *) { return false; }
void setRoundedCorners(QWidget *, bool) {}
bool handleMessage(QWidget *, void *, qintptr *, const Hooks &) { return false; }
#endif

}  // namespace WindowFrame
