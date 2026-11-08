#include "widgets/ExpandedGeometry.h"
#include <cstdio>

int main() {
    int failures = 0;
    for (const auto size : {D2D1::SizeF(160, 120), D2D1::SizeF(320, 200),
                            D2D1::SizeF(640, 360), D2D1::SizeF(720, 1280),
                            D2D1::SizeF(1920, 1080), D2D1::SizeF(3840, 2160)}) {
        for (const int count : {0, 1, 2, 10, 100, 1000}) {
            const auto g = tf::widget::ComputeExpandedGeometry(size, count);
            const bool fits = g.card.left >= 0 && g.card.top >= 0 &&
                g.card.right <= size.width && g.card.bottom <= size.height &&
                g.close.left >= g.card.left && g.close.right <= g.card.right &&
                g.close.top >= g.card.top && g.close.bottom <= g.card.bottom &&
                g.grid.left >= g.card.left && g.grid.right <= g.card.right &&
                g.grid.top >= g.card.top && g.grid.bottom <= g.card.bottom &&
                g.cols >= 1 && g.rowsVisible >= 1 && g.cellW > 0 && g.cellH > 0 &&
                g.scrollable == (g.totalRows > g.rowsVisible) &&
                g.totalRows * g.cols >= count;
            if (!fits) {
                std::printf("Panel does not fit %.0fx%.0f with %d items\n", size.width, size.height, count);
                ++failures;
            }
        }
    }
    return failures ? 1 : 0;
}
