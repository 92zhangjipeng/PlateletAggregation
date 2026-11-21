#ifndef FRAMELESSWINDOWSUPPORT_H
#define FRAMELESSWINDOWSUPPORT_H

#include <QByteArray>

class QWidget;

namespace frameless {

/**
 * 无边框窗口边缘缩放（Windows：处理 WM_NCHITTEST）。
 * 在 QMainWindow / QDialog 的 nativeEvent() 里调用即可；
 * 非 Windows 平台返回 false，不做处理。
 */
bool handleNativeResizeEvent(QWidget *window, const QByteArray &eventType,
                             void *message, long *result, int border = 6);

/**
 * 给无边框窗口补上 WS_THICKFRAME，让 WM_NCHITTEST 返回的
 * HTLEFT/HTTOP 等边缘命中能够真正拖动缩放。
 * 在窗口 showEvent 里调用一次即可。
 */
void enableNativeResize(QWidget *window);

} // namespace frameless

#endif // FRAMELESSWINDOWSUPPORT_H
