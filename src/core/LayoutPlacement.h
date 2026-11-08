#pragma once

#include <windows.h>

#include "core/Grid.h"
#include "core/Layout.h"

namespace tf::placement {

void ClampFolderSpan(const grid::Spec& grid, int& spanCols, int& spanRows);

bool CellFree(const grid::Spec& grid, const cfg::Layout& layout, POINT cell, int spanCols,
              int spanRows, const cfg::Entry* keep = nullptr);

bool FindFreeCell(const grid::Spec& grid, const cfg::Layout& layout, int spanCols, int spanRows,
                  POINT& out);

bool NearestFreeCell(const grid::Spec& grid, const cfg::Layout& layout, POINT want, int spanCols,
                     int spanRows, const cfg::Entry* keep, POINT& out,
                     const POINT* avoid = nullptr);

// Clamps spans and moves overlapping entries. Returns the number of changed entries.
int Normalize(const grid::Spec& grid, cfg::Layout& layout);

}  // namespace tf::placement
