#include "core/Dpi.h"

#include <shellscalingapi.h>

namespace tf::dpi {

float ScaleForWindow(HWND hwnd) {
    if (hwnd) {
        const UINT d = ::GetDpiForWindow(hwnd);
        if (d != 0) return static_cast<float>(d) / 96.0f;
    }
    return ScaleForPoint(POINT{0, 0});
}

float ScaleForPoint(POINT screenPoint) {
    // Query an unshown per-monitor-aware top-level window: desktop children can
    // inherit the host DPI, and GetDpiForMonitor is not valid for PM-aware threads.
    thread_local HWND probe = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"STATIC", L"", WS_POPUP, screenPoint.x, screenPoint.y, 1, 1,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (probe != nullptr) {
        ::SetWindowPos(probe, nullptr, screenPoint.x, screenPoint.y, 1, 1,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        const UINT dpi = ::GetDpiForWindow(probe);
        if (dpi != 0) return static_cast<float>(dpi) / 96.0f;
    }
    return static_cast<float>(::GetDpiForSystem()) / 96.0f;
}

float ScaleForRect(const RECT& screenRect) {
    const POINT center{(screenRect.left + screenRect.right) / 2, (screenRect.top + screenRect.bottom) / 2};
    return ScaleForPoint(center);
}

RECT DipRectToPx(const RECT& dip, float scale) {
    return RECT{Dip2Px(dip.left, scale), Dip2Px(dip.top, scale),
                Dip2Px(dip.right, scale), Dip2Px(dip.bottom, scale)};
}

RECT PxRectToDip(const RECT& px, float scale) {
    return RECT{Px2Dip(px.left, scale), Px2Dip(px.top, scale),
                Px2Dip(px.right, scale), Px2Dip(px.bottom, scale)};
}

}  // namespace tf::dpi
