#pragma once

#include <QPoint>
#include <QSize>
#include <functional>

class QWidget;

// FEAT-071: the app-drawn window takes part in the Windows window layout (Snap, Snap layouts on the
// Maximize button, Win+Arrows, resizing from the edges) while it keeps its own top bar and controls.
// hitTest is the rule without Win32 (tested on every platform); the rest only acts on Windows.
namespace WindowFrame {

// What is under a point of the window, as Windows asks with WM_NCHITTEST.
enum class Hit { Client, Caption, MaximizeButton, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

inline constexpr int kResizeBorder = 6;   // logical px inside the window (user, 2026-11-01)
inline constexpr int kResizeCorner = 12;  // along each edge from a corner

// pos and size in logical px. resizable: false while maximized or full screen (no edge band).
// caption / maximizeButton: what the window has at pos (a drag area, the Maximize button). The edge
// band wins over both, so the top edge stays resizable above the top bar.
Hit hitTest(const QPoint &pos, const QSize &size, bool resizable, bool caption, bool maximizeButton);

struct Hooks {
    // What is at a point (logical px in the window): Client, Caption or MaximizeButton; the edges
    // are added by the frame.
    std::function<Hit(const QPoint &)> contentAt;
    // The Maximize button's look while Windows owns the mouse over it (Snap layouts).
    std::function<void(bool hovered, bool pressed)> maximizeState;
    std::function<void()> maximizeClicked;
};

// Windows: sets the window style that enables Snap and, on Windows 11, the system's rounded
// corners and shadow. Returns true when the system draws the corners (the window needs no mask).
// Call again when the native window changes (QEvent::WinIdChange). Other platforms: false.
bool install(QWidget *window);

// Windows 11: rounded (FEAT-071) or square corners (full screen, FEAT-087). Elsewhere nothing.
void setRoundedCorners(QWidget *window, bool rounded);

// From QWidget::nativeEvent. True = the message is handled and *result is the answer.
bool handleMessage(QWidget *window, void *message, qintptr *result, const Hooks &hooks);

}  // namespace WindowFrame
