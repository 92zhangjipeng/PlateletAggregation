#include "FramelessWindowSupport.h"

#include <QCursor>
#include <QPoint>
#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

bool frameless::handleNativeResizeEvent(QWidget *window,
                                        const QByteArray &eventType,
                                        void *message,
                                        long *result,
                                        int border)
{
#ifdef Q_OS_WIN
    if (!window || !message || !result
            || eventType != QByteArrayLiteral("windows_generic_MSG")) {
        return false;
    }

    MSG *msg = static_cast<MSG *>(message);
    if (msg->message != WM_NCHITTEST) {
        return false;
    }
    if (window->isMaximized() || window->isFullScreen()) {
        return false;
    }

    // 用 QCursor 取逻辑坐标，避免高 DPI 下 lParam 的物理坐标不匹配
    const QPoint pos = window->mapFromGlobal(QCursor::pos());
    const int w = window->width();
    const int h = window->height();
    const int b = qMax(1, border);

    const bool left = pos.x() >= 0 && pos.x() < b;
    const bool right = pos.x() <= w && pos.x() > w - b;
    const bool top = pos.y() >= 0 && pos.y() < b;
    const bool bottom = pos.y() <= h && pos.y() > h - b;

    if (top && left) {
        *result = HTTOPLEFT;
    } else if (top && right) {
        *result = HTTOPRIGHT;
    } else if (bottom && left) {
        *result = HTBOTTOMLEFT;
    } else if (bottom && right) {
        *result = HTBOTTOMRIGHT;
    } else if (left) {
        *result = HTLEFT;
    } else if (right) {
        *result = HTRIGHT;
    } else if (top) {
        *result = HTTOP;
    } else if (bottom) {
        *result = HTBOTTOM;
    } else {
        // 窗口内部明确返回 HTCLIENT，保证鼠标事件正常下发到子控件
        *result = HTCLIENT;
    }
    return true;
#else
    Q_UNUSED(window);
    Q_UNUSED(eventType);
    Q_UNUSED(message);
    Q_UNUSED(result);
    Q_UNUSED(border);
    return false;
#endif
}

void frameless::enableNativeResize(QWidget *window)
{
#ifdef Q_OS_WIN
    if (!window) {
        return;
    }
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if ((style & WS_THICKFRAME) == 0) {
        SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_THICKFRAME);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER
                     | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#else
    Q_UNUSED(window);
#endif
}
