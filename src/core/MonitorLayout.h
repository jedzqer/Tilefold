#pragma once
#include "core/Grid.h"
#include "core/Layout.h"
#include <vector>
namespace tf::desktop {
struct Display {
    HMONITOR handle{};
    std::wstring id;
    RECT workPx{};
    float scale = 1.0f;
    bool primary = false;
    grid::Spec grid;
};
std::vector<Display> Enumerate();
void Configure(std::vector<Display>& displays, cfg::Layout& preferred);
const Display& ForPoint(const std::vector<Display>& displays, POINT px);
const Display& ForEntry(const std::vector<Display>& displays, const cfg::Entry& entry);
RECT BoundsPx(const Display& display, const cfg::Entry& entry);
POINT LocalDip(const Display& display, POINT px);
// Only this disposable copy is clamped/repacked when a display disappears or shrinks.
cfg::Layout Resolve(const std::vector<Display>& displays, const cfg::Layout& preferred);
}
