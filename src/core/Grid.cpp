#include "core/Grid.h"

#include <algorithm>
#include <cmath>

namespace tf::grid {

namespace {

int ClampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

Spec Make(const RECT& workDip, int cols) {
    Spec s;
    s.work = workDip;

    const float w = static_cast<float>(workDip.right - workDip.left);
    const float h = static_cast<float>(workDip.bottom - workDip.top);

    // 自动模式：让格子边长（而不是列数）成为不变量。
    // 这样 1×1 的部件不管在 1080p 还是 4K、100% 还是 150% 缩放下，
    // 都跟原生桌面图标差不多大。
    if (cols <= kAutoCols) {
        const int base = static_cast<int>(std::lround(w / kNativeCellDip));
        // 再在 base±2 里挑“底部剩得最少”的列数：格距由宽度除出来，
        // 行数取整之后底部总会留下 h - rows*pitch。选一个能整除得最干净的列数，
        // 屏幕最下面就不会白留一条放不了东西的缝。
        int bestCols = base;
        float bestSlack = 3.4e38f;
        float bestDelta = 3.4e38f;
        for (int c = base - 2; c <= base + 2; ++c) {
            if (c < kMinCols || c > kMaxCols) continue;
            const float p = w / static_cast<float>(c);
            if (p < 48.0f) continue;
            const int r = std::max(2, static_cast<int>(std::floor(h / p)));
            const float slack = h - static_cast<float>(r) * p;
            const float delta = std::fabs(p - kNativeCellDip);
            if (slack < bestSlack - 0.5f ||
                (std::fabs(slack - bestSlack) <= 0.5f && delta < bestDelta)) {
                bestSlack = slack;
                bestDelta = delta;
                bestCols = c;
            }
        }
        cols = bestCols;
    }
    const int fittingCols = std::max(1, std::min(kMaxCols, static_cast<int>(w / 48.0f)));
    s.cols = ClampInt(cols, std::min(kMinCols, fittingCols), fittingCols);

    // 正方形格子：先按宽度定 pitch，再由高度决定有几行
    float pitch = w / static_cast<float>(s.cols);
    pitch = std::max(1.0f, std::min(pitch, std::max(1.0f, h)));
    s.pitch = pitch;

    int rows = static_cast<int>(std::floor(h / pitch));
    if (rows < 1) rows = 1;
    s.rows = rows;

    // 留白跟随格子大小：格子 ~76 DIP 时留白 ~8.4，~190 DIP 时留白 ~20
    // 0.11 是「两条缝 = 名字带」和「卡片别缩太多」之间的折中：
    // 相邻 widget 之间的缝 = 2×margin ≈ 0.22×pitch，3×3 卡片 = 3×pitch - 2×margin ≈ 92%
    s.margin = std::max(3.0f, std::min(20.0f, pitch * 0.15f));
    return s;
}

RECT RectForSpan(const Spec& s, int col, int row, int spanCols, int spanRows) {
    const float x = static_cast<float>(s.work.left) + static_cast<float>(col) * s.pitch + s.margin;
    const float y = static_cast<float>(s.work.top) + static_cast<float>(row) * s.pitch + s.margin;
    const float w = static_cast<float>(spanCols) * s.pitch - 2.0f * s.margin;
    const float h = static_cast<float>(spanRows) * s.pitch - 2.0f * s.margin;
    RECT r{};
    r.left = static_cast<LONG>(std::lround(x));
    r.top = static_cast<LONG>(std::lround(y));
    r.right = static_cast<LONG>(std::lround(x + w));
    r.bottom = static_cast<LONG>(std::lround(y + h));
    return r;
}

POINT CellAt(const Spec& s, POINT ptDip) {
    const float dx = static_cast<float>(ptDip.x - s.work.left) / s.pitch;
    const float dy = static_cast<float>(ptDip.y - s.work.top) / s.pitch;
    // floor 而不是截断：负坐标在左边/上边时要落到 -1 而不是 0
    POINT p{};
    p.x = static_cast<LONG>(std::floor(dx));
    p.y = static_cast<LONG>(std::floor(dy));
    return p;
}

POINT ClampCell(const Spec& s, POINT cell, int spanCols, int spanRows) {
    const int maxCol = std::max(0, s.cols - std::max(1, spanCols));
    const int maxRow = std::max(0, s.rows - std::max(1, spanRows));
    POINT p{};
    p.x = ClampInt(static_cast<int>(cell.x), 0, maxCol);
    p.y = ClampInt(static_cast<int>(cell.y), 0, maxRow);
    return p;
}

RECT SnapRect(const Spec& s, const RECT& rect, int spanCols, int spanRows) {
    const POINT cell = CellAt(s, POINT{rect.left, rect.top});
    const POINT snapped = ClampCell(s, cell, spanCols, spanRows);
    return RectForSpan(s, snapped.x, snapped.y, spanCols, spanRows);
}

void ClampSpan(const Spec& s, int& spanCols, int& spanRows) {
    spanCols = ClampInt(spanCols, kMinSpan, std::min(kMaxSpan, s.cols));
    spanRows = ClampInt(spanRows, kMinSpan, std::min(kMaxSpan, s.rows));
}

}  // namespace tf::grid
