#include "core/LayoutPlacement.h"

#include <algorithm>
#include <climits>
#include <cstdlib>
#include <vector>

namespace tf::placement {

namespace {

bool RectsOverlap(const RECT& a, const RECT& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

bool OccupiesCells(const cfg::Entry& entry) {
    return entry.kind == L"folder" || entry.kind == L"icon" || entry.kind == L"shell" ||
           entry.kind == L"desktop";
}

}  // namespace

void ClampFolderSpan(const grid::Spec& grid, int& spanCols, int& spanRows) {
    grid::ClampSpan(grid, spanCols, spanRows);
    spanRows = std::max(1, spanRows);
}

bool CellFree(const grid::Spec& grid, const cfg::Layout& layout, POINT cell, int spanCols,
              int spanRows, const cfg::Entry* keep) {
    const RECT want = grid::RectForSpan(grid, cell.x, cell.y, spanCols, spanRows);
    for (const cfg::Entry& entry : layout.entries) {
        if (&entry == keep || !OccupiesCells(entry) || entry.monitor != grid.monitor) continue;
        const RECT occupied = grid::RectForSpan(grid, entry.col, entry.row, entry.spanCols,
                                                entry.spanRows);
        if (RectsOverlap(want, occupied)) return false;
    }
    return true;
}

bool FindFreeCell(const grid::Spec& grid, const cfg::Layout& layout, int spanCols, int spanRows,
                  POINT& out) {
    const int maxCol = std::max(0, grid.cols - spanCols);
    const int maxRow = std::max(0, grid.rows - spanRows);
    for (int row = 0; row <= maxRow; ++row) {
        for (int col = 0; col <= maxCol; ++col) {
            const POINT cell{col, row};
            if (CellFree(grid, layout, cell, spanCols, spanRows)) {
                out = cell;
                return true;
            }
        }
    }
    // A full grid still gives the caller a visible fallback position.
    out = POINT{0, 0};
    return false;
}

bool NearestFreeCell(const grid::Spec& grid, const cfg::Layout& layout, POINT want, int spanCols,
                     int spanRows, const cfg::Entry* keep, POINT& out, const POINT* avoid) {
    if (CellFree(grid, layout, want, spanCols, spanRows, keep)) {
        out = want;
        return true;
    }

    const int maxCol = std::max(0, grid.cols - spanCols);
    const int maxRow = std::max(0, grid.rows - spanRows);
    int best = INT_MAX;
    bool found = false;
    for (int row = 0; row <= maxRow; ++row) {
        for (int col = 0; col <= maxCol; ++col) {
            const POINT candidate{col, row};
            if (!CellFree(grid, layout, candidate, spanCols, spanRows, keep)) continue;
            const int distance = std::abs(col - want.x) + std::abs(row - want.y);
            const bool isAvoid = avoid != nullptr && col == avoid->x && row == avoid->y;
            const int score = distance * 2 + (isAvoid ? 1 : 0);
            if (score < best) {
                best = score;
                out = candidate;
                found = true;
            }
        }
    }
    return found;
}

int Normalize(const grid::Spec& grid, cfg::Layout& layout) {
    std::vector<RECT> placed;
    int moved = 0;

    for (cfg::Entry& entry : layout.entries) {
        if (!OccupiesCells(entry) || entry.monitor != grid.monitor) continue;
        int spanCols = entry.spanCols;
        int spanRows = entry.spanRows;
        if (entry.kind == L"folder") {
            ClampFolderSpan(grid, spanCols, spanRows);
        } else {
            grid::ClampSpan(grid, spanCols, spanRows);
        }

        POINT cell = grid::ClampCell(grid, POINT{entry.col, entry.row}, spanCols, spanRows);
        RECT rect = grid::RectForSpan(grid, cell.x, cell.y, spanCols, spanRows);
        const bool overlapped = std::any_of(placed.begin(), placed.end(), [&](const RECT& existing) {
            return RectsOverlap(rect, existing);
        });

        if (overlapped) {
            int bestScore = INT_MAX;
            const POINT origin = cell;
            const int maxCol = std::max(0, grid.cols - spanCols);
            const int maxRow = std::max(0, grid.rows - spanRows);
            for (int row = 0; row <= maxRow; ++row) {
                for (int col = 0; col <= maxCol; ++col) {
                    const RECT candidateRect = grid::RectForSpan(grid, col, row, spanCols, spanRows);
                    const bool clash = std::any_of(placed.begin(), placed.end(), [&](const RECT& p) {
                        return RectsOverlap(candidateRect, p);
                    });
                    if (clash) continue;
                    const int distance = std::abs(col - origin.x) + std::abs(row - origin.y);
                    const bool isOriginal = col == entry.col && row == entry.row;
                    const int score = 2 * distance + (isOriginal ? 1 : 0);
                    if (score < bestScore) {
                        bestScore = score;
                        cell = POINT{col, row};
                    }
                }
            }
            if (bestScore != INT_MAX) {
                rect = grid::RectForSpan(grid, cell.x, cell.y, spanCols, spanRows);
            }
        }

        if (entry.col != cell.x || entry.row != cell.y || entry.spanCols != spanCols ||
            entry.spanRows != spanRows) {
            entry.col = cell.x;
            entry.row = cell.y;
            entry.spanCols = spanCols;
            entry.spanRows = spanRows;
            ++moved;
        }
        placed.push_back(rect);
    }

    return moved;
}

}  // namespace tf::placement
