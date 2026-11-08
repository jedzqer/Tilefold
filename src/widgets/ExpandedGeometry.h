#pragma once

#include <d2d1_1.h>
#include <algorithm>
#include <cmath>

namespace tf::widget {

struct ExpandedGeometry {
    D2D1_RECT_F card{};
    D2D1_RECT_F grid{};
    D2D1_RECT_F close{};
    float cellW = 100.0f;
    float cellH = 104.0f;
    float iconSize = 56.0f;
    float labelH = 14.0f;
    int cols = 3;
    int rowsVisible = 1;
    int totalRows = 1;
    bool scrollable = false;
};

inline ExpandedGeometry ComputeExpandedGeometry(D2D1_SIZE_F size, int count) {
    ExpandedGeometry g;
    count = std::max(1, count);
    const float pad = std::min(24.0f, size.width * 0.04f);
    const float headerH = std::min(46.0f, size.height * 0.25f);
    const float maxCardW = size.width * 0.86f;
    const float maxCardH = size.height * 0.86f;

    // 列数挑一个“接近正方形”的：条目越多列越多，但不铺满整屏
    g.cols = std::clamp(
        static_cast<int>(std::ceil(std::sqrt(static_cast<float>(count) * 1.15f))), 2, 5);
    g.cols = std::min(g.cols, std::max(1, static_cast<int>((maxCardW - 2.0f * pad - 16.0f) / 56.0f)));
    g.totalRows = (count + g.cols - 1) / g.cols;

    // 宽度兜不住就整体缩格（图标一起缩），不要挤成一条缝
    const float spareW = maxCardW - 2.0f * pad - 16.0f;
    const float cellW = std::max(1.0f, std::min(104.0f, spareW / static_cast<float>(g.cols)));
    const float cellH = std::max(1.0f, std::min(cellW * 1.04f, maxCardH - headerH - pad));

    const int rowsFit = std::max(1, static_cast<int>((maxCardH - headerH - pad) / cellH));
    g.rowsVisible = std::min(g.totalRows, rowsFit);
    g.scrollable = g.totalRows > g.rowsVisible;

    g.cellW = cellW;
    g.cellH = cellH;
    g.iconSize = std::clamp(std::min(cellW, cellH) * 0.62f, 26.0f, 64.0f);
    g.labelH = std::clamp(cellH * 0.16f, 11.0f, 16.0f);

    const float cardW = g.cols * cellW + 2.0f * pad + (g.scrollable ? 14.0f : 0.0f);
    const float cardH = headerH + g.rowsVisible * cellH + pad;
    const float left = std::round((size.width - cardW) * 0.5f);
    const float top = std::round((size.height - cardH) * 0.5f);

    g.card = D2D1::RectF(left, top, left + cardW, top + cardH);
    g.grid = D2D1::RectF(g.card.left + pad, g.card.top + headerH,
                         g.card.left + pad + g.cols * cellW,
                         g.card.top + headerH + g.rowsVisible * cellH);
    const float closeSize = 24.0f;
    const float closeCy = g.card.top + headerH * 0.5f;
    g.close = D2D1::RectF(g.card.right - pad - closeSize, closeCy - closeSize * 0.5f,
                          g.card.right - pad, closeCy + closeSize * 0.5f);
    return g;
}

}  // namespace tf::widget
