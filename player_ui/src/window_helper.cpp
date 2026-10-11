#include "window_helper.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwmapi.h>
#endif

void WindowHelper::applyNativeFrame(QWindow* window) {
#ifdef Q_OS_WIN
    if (!window) return;
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    // Windows 11:系统圆角 (DWMWA_WINDOW_CORNER_PREFERENCE = 33, DWMWCP_ROUND = 2);旧系统上调用无害地失败
    const DWORD kCornerPref = 33;
    int round = 2;
    DwmSetWindowAttribute(hwnd, kCornerPref, &round, sizeof(round));
    // 将 DWM 边框扩展 1px 进客户区,无边框窗口即可获得系统原生阴影
    MARGINS m = {0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd, &m);
    // 深色模式标题栏/系统菜单 (DWMWA_USE_IMMERSIVE_DARK_MODE = 20)
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
#else
    Q_UNUSED(window);
#endif
}

bool WindowHelper::isWindows() const {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}
