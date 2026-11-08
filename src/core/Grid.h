#pragma once

#include <windows.h>
#include <string>

namespace tf::grid {

// 桌面网格（手机式）。
//
// 格子是正方形，边长 pitch = 工作区宽度 / cols。列数由用户配置（6/8/10），
// 行数按工作区高度自动算出。widget 占 N×M 个格子，像素尺寸 = span*pitch - 2*margin，
// 所以"格子中间是有空位的"，相邻 widget 之间也有间隔。
struct Spec {
    int cols = 8;         // 列数（0 = 按原生图标间距自动算）
    int rows = 4;         // 行数（按高度算出来，只读）
    float pitch = 96.0f;  // 格子边长（DIP）
    float margin = 5.0f;  // 格子内留白
    std::wstring monitor; // Occupancy is isolated per display.
    RECT work{};          // 工作区（DIP）
};

constexpr int kMinCols = 4;
constexpr int kMaxCols = 64;
// 0 = 自动：格子边长对齐 Windows 桌面图标默认间距，
// 这样 1×1 的部件看起来和原生图标一样大，不随分辨率变形
constexpr int kAutoCols = 0;
constexpr int kDefaultCols = kAutoCols;
constexpr int kMinSpan = 1;
constexpr int kMaxSpan = 6;

// Windows 桌面图标默认间距（100% DPI 下约 75px，图标本身 48px）
constexpr float kNativeCellDip = 76.0f;

// cols <= 0 表示自动（按 kNativeCellDip 反推列数）；pitch 有下限，避免极端配置把格子算成负数
Spec Make(const RECT& workDip, int cols);

// 某个跨度对应的屏幕矩形（DIP）
RECT RectForSpan(const Spec& s, int col, int row, int spanCols, int spanRows);

// 某个屏幕点落在哪个格子（不钳制，可能越界）
POINT CellAt(const Spec& s, POINT ptDip);

// 把 cell 钳制到"跨度为 span 时还放得下"的范围
POINT ClampCell(const Spec& s, POINT cell, int spanCols, int spanRows);

// 吸附：把任意矩形左上角对齐到最近的格子，并钳制在网格内
RECT SnapRect(const Spec& s, const RECT& rect, int spanCols, int spanRows);

// 跨度合法性（1..kMaxSpan，且不超过网格本身）
void ClampSpan(const Spec& s, int& spanCols, int& spanRows);

}  // namespace tf::grid
