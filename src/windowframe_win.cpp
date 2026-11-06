// FEAT-071: Windows part of WindowFrame. Technique documented by Microsoft for windows that draw
// their own title bar:
// - a resizable style with Minimize and Maximize boxes, which is what enables Snap and Win+Arrows;
// - WM_NCCALCSIZE without a non-client area, so the app still draws the whole window
//   (https://learn.microsoft.com/windows/win32/dwm/customframe);
// - WM_NCHITTEST answering the edges, the drag areas (HTCAPTION) and the Maximize button
//   (HTMAXBUTTON, which opens the Snap layouts of Windows 11; its hover and pressed look come from
//   the non-client messages: https://learn.microsoft.com/windows/apps/desktop/modernize/ui/apply-snap-layout-menu).
// Qt keeps Qt::FramelessWindowHint, so it counts no frame margins and the geometry stays the window's.
#include "windowframe.h"

#include <QGuiApplication>
#include <QVariant>
#include <QWidget>
#include <QtMath>

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <windowsx.h>

namespace WindowFrame {
namespace {

// Values of the Windows 11 SDK (dwmapi.h), not in every MinGW header.
constexpr DWORD kCornerPreferenceAttribute = 33;  // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr int kCornerRound = 2;                   // DWMWCP_ROUND
constexpr int kCornerSquare = 1;                  // DWMWCP_DONOTROUND

// Per-window state of the Maximize button while Windows owns the mouse over it.
constexpr char kHoverProperty[] = "_mentisMaximizeHover";
constexpr char kPressProperty[] = "_mentisMaximizePress";

HWND handleOf(const QWidget *window) { return reinterpret_cast<HWND>(window->internalWinId()); }

LRESULT codeOf(Hit hit) {
    switch (hit) {
    case Hit::Client: return HTCLIENT;
    case Hit::Caption: return HTCAPTION;
    case Hit::MaximizeButton: return HTMAXBUTTON;
    case Hit::Left: return HTLEFT;
    case Hit::Right: return HTRIGHT;
    case Hit::Top: return HTTOP;
    case Hit::Bottom: return HTBOTTOM;
    case Hit::TopLeft: return HTTOPLEFT;
    case Hit::TopRight: return HTTOPRIGHT;
    case Hit::BottomLeft: return HTBOTTOMLEFT;
    case Hit::BottomRight: return HTBOTTOMRIGHT;
    }
    return HTCLIENT;
}

// A maximized window with a resizable frame is placed beyond the monitor by the frame's thickness;
// without a non-client area that part would be cut off, so the client area is moved back inside.
// An auto-hide taskbar keeps one pixel, or Windows takes the window for full screen and the taskbar
// does not appear (https://learn.microsoft.com/windows/win32/shell/abm-getautohidebarex).
void fitMaximized(HWND hwnd, RECT &rect) {
    const UINT dpi = GetDpiForWindow(hwnd);
    const int frameX = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    const int frameY = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    rect.left += frameX;
    rect.right -= frameX;
    rect.top += frameY;
    rect.bottom -= frameY;
    APPBARDATA state{};
    state.cbSize = sizeof state;
    if (!(SHAppBarMessage(ABM_GETSTATE, &state) & ABS_AUTOHIDE)) return;
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return;
    for (const UINT edge : {ABE_BOTTOM, ABE_TOP, ABE_LEFT, ABE_RIGHT}) {
        APPBARDATA bar{};
        bar.cbSize = sizeof bar;
        bar.uEdge = edge;
        bar.rc = monitor.rcMonitor;
        if (!SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &bar)) continue;
        if (edge == ABE_BOTTOM) --rect.bottom;
        else if (edge == ABE_TOP) ++rect.top;
        else if (edge == ABE_LEFT) ++rect.left;
        else --rect.right;
        return;
    }
}

void setMaximizeState(QWidget *window, const Hooks &hooks, bool hovered, bool pressed) {
    if (window->property(kHoverProperty).toBool() == hovered && window->property(kPressProperty).toBool() == pressed) return;
    window->setProperty(kHoverProperty, hovered);
    window->setProperty(kPressProperty, pressed);
    if (hooks.maximizeState) hooks.maximizeState(hovered, pressed);
}

}  // namespace

bool install(QWidget *window) {
    // Other platform plugins (the tests' offscreen) have no HWND behind winId().
    if (QGuiApplication::platformName() != QLatin1String("windows")) return false;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return false;
    // Qt gives a frameless window WS_POPUP; a normal top-level style is what Snap works with.
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR wanted = (style & ~LONG_PTR(WS_POPUP)) | WS_OVERLAPPED | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU
                            | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if (wanted != style) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, wanted);
        // The frame changed: Windows asks WM_NCCALCSIZE again, answered without a non-client area.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
    // Windows 11 rounds the corners and draws the shadow (user, 2026-11-01). The attribute does not
    // exist on Windows 10, where the call fails and the window keeps its mask.
    const int corners = kCornerRound;
    if (FAILED(DwmSetWindowAttribute(hwnd, kCornerPreferenceAttribute, &corners, sizeof corners))) return false;
    // One pixel of frame inside the window gives DWM a frame to cast the shadow from; the app paints
    // it opaque, so nothing of the frame shows.
    const MARGINS margins{0, 0, 1, 0};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    return true;
}

void setRoundedCorners(QWidget *window, bool rounded) {
    if (QGuiApplication::platformName() != QLatin1String("windows") || !window->internalWinId()) return;
    const int corners = rounded ? kCornerRound : kCornerSquare;
    DwmSetWindowAttribute(handleOf(window), kCornerPreferenceAttribute, &corners, sizeof corners);  // fails on Windows 10: nothing to do
}

bool handleMessage(QWidget *window, void *message, qintptr *result, const Hooks &hooks) {
    const MSG *msg = static_cast<const MSG *>(message);
    const HWND hwnd = handleOf(window);
    if (!hwnd || msg->hwnd != hwnd) return false;
    switch (msg->message) {
    case WM_NCCALCSIZE:
        if (!msg->wParam) return false;
        if (IsZoomed(hwnd)) fitMaximized(hwnd, reinterpret_cast<NCCALCSIZE_PARAMS *>(msg->lParam)->rgrc[0]);
        *result = 0;  // the whole window is client area
        return true;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
        ScreenToClient(hwnd, &point);
        const qreal ratio = window->devicePixelRatioF();
        const QPoint local(qFloor(point.x / ratio), qFloor(point.y / ratio));
        const Hit content = hooks.contentAt ? hooks.contentAt(local) : Hit::Client;
        const bool resizable = !IsZoomed(hwnd) && !window->isFullScreen();
        *result = codeOf(hitTest(local, window->size(), resizable, content == Hit::Caption, content == Hit::MaximizeButton));
        return true;
    }
    case WM_NCMOUSEMOVE:
        if (msg->wParam == HTMAXBUTTON) {
            if (!window->property(kHoverProperty).toBool()) {
                TRACKMOUSEEVENT track{};
                track.cbSize = sizeof track;
                track.dwFlags = TME_LEAVE | TME_NONCLIENT;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);  // WM_NCMOUSELEAVE when the mouse leaves the button
            }
            setMaximizeState(window, hooks, true, window->property(kPressProperty).toBool());
        } else {
            setMaximizeState(window, hooks, false, false);
        }
        return false;  // Windows still shows the Snap layouts
    case WM_NCMOUSELEAVE:
        setMaximizeState(window, hooks, false, false);
        return false;
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK:
        if (msg->wParam != HTMAXBUTTON) return false;
        // Without this Windows would press its own (invisible) Maximize button.
        setMaximizeState(window, hooks, true, true);
        *result = 0;
        return true;
    case WM_NCLBUTTONUP:
        if (msg->wParam != HTMAXBUTTON) return false;
        if (window->property(kPressProperty).toBool()) {
            setMaximizeState(window, hooks, true, false);
            if (hooks.maximizeClicked) hooks.maximizeClicked();
        }
        *result = 0;
        return true;
    default:
        return false;
    }
}

}  // namespace WindowFrame
