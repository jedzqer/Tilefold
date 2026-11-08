#pragma once

#include <windows.h>

#include <cmath>

namespace tf::dpi {

// DPI / 96.0 —— 1.0 表示 100% 缩放
float ScaleForWindow(HWND hwnd);
float ScaleForPoint(POINT screenPoint);
float ScaleForRect(const RECT& screenRect);

inline int Dip2Px(int dip, float scale) { return static_cast<int>(lroundf(static_cast<float>(dip) * scale)); }
inline int Px2Dip(int px, float scale) { return scale > 0.0f ? static_cast<int>(lroundf(static_cast<float>(px) / scale)) : px; }

RECT DipRectToPx(const RECT& dip, float scale);
RECT PxRectToDip(const RECT& px, float scale);

}  // namespace tf::dpi
