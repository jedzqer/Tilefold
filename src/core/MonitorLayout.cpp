#include "core/MonitorLayout.h"
#include "core/Dpi.h"
#include "core/LayoutPlacement.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace tf::desktop {
std::vector<Display> Enumerate() {
    std::vector<Display> result;
    ::EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR handle, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!::GetMonitorInfoW(handle, &info)) return TRUE;
        Display d;
        d.handle = handle;
        d.workPx = info.rcWork;
        d.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        d.scale = dpi::ScaleForRect(info.rcMonitor);
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        // Interface paths survive display numbering changes and normal reconnects.
        d.id = ::EnumDisplayDevicesW(info.szDevice, 0, &device, EDD_GET_DEVICE_INTERFACE_NAME) &&
                       device.DeviceID[0] ? device.DeviceID : info.szDevice;
        reinterpret_cast<std::vector<Display>*>(data)->push_back(std::move(d));
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    std::stable_sort(result.begin(), result.end(), [](const Display& a, const Display& b) {
        return a.primary && !b.primary;
    });
    if (result.empty()) {
        Display d;
        d.primary = true;
        d.id = L"primary-fallback";
        ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &d.workPx, 0);
        if (d.workPx.right <= d.workPx.left) d.workPx = {0, 0, 800, 600};
        result.push_back(d);
    }
    return result;
}
void Configure(std::vector<Display>& displays, cfg::Layout& preferred) {
    for (auto& d : displays) {
        const int w = dpi::Px2Dip(d.workPx.right - d.workPx.left, d.scale);
        const int h = dpi::Px2Dip(d.workPx.bottom - d.workPx.top, d.scale);
        auto old = std::find_if(preferred.monitors.begin(), preferred.monitors.end(),
                               [&](const cfg::MonitorGrid& m) { return m.monitor == d.id; });
        const cfg::GridState saved = old != preferred.monitors.end() ? old->grid :
                                     (d.primary ? preferred.grid : cfg::GridState{});
        int cols = preferred.cols;
        if (cols <= 0 && saved.valid && saved.pitch > 0) {
            cols = std::abs(saved.workW - w) <= 4 && std::abs(saved.workH - h) <= 4 ?
                   saved.cols : static_cast<int>(std::lround(w / saved.pitch));
        }
        d.grid = grid::Make({0, 0, w, h}, cols);
        d.grid.monitor = d.id;
        const cfg::GridState state{d.grid.cols, d.grid.pitch, w, h, true};
        if (old == preferred.monitors.end()) preferred.monitors.push_back({d.id, state});
        else old->grid = state;
        if (d.primary) preferred.grid = state;
    }
    for (auto& e : preferred.entries) {
        if ((e.kind == L"folder" || e.kind == L"icon" || e.kind == L"shell" ||
             e.kind == L"desktop") && e.monitor.empty())
            e.monitor = displays.front().id;
    }
}
const Display& ForPoint(const std::vector<Display>& displays, POINT px) {
    const Display* best = &displays.front();
    double distance = std::numeric_limits<double>::max();
    for (const auto& d : displays) {
        const double dx = px.x - std::clamp(px.x, d.workPx.left, d.workPx.right - 1);
        const double dy = px.y - std::clamp(px.y, d.workPx.top, d.workPx.bottom - 1);
        const double candidate = dx * dx + dy * dy;
        if (candidate < distance) { distance = candidate; best = &d; }
    }
    return *best;
}
const Display& ForEntry(const std::vector<Display>& displays, const cfg::Entry& e) {
    const auto it = std::find_if(displays.begin(), displays.end(), [&](const Display& d) {
        return d.id == e.monitor;
    });
    return it == displays.end() ? displays.front() : *it;
}
POINT LocalDip(const Display& d, POINT px) {
    return {dpi::Px2Dip(px.x - d.workPx.left, d.scale), dpi::Px2Dip(px.y - d.workPx.top, d.scale)};
}
RECT BoundsPx(const Display& d, const cfg::Entry& e) {
    RECT r = dpi::DipRectToPx(grid::RectForSpan(d.grid, e.col, e.row, e.spanCols, e.spanRows), d.scale);
    ::OffsetRect(&r, d.workPx.left, d.workPx.top);
    return r;
}
cfg::Layout Resolve(const std::vector<Display>& displays, const cfg::Layout& preferred) {
    cfg::Layout visible = preferred;
    // Place resident widgets first; disconnected-screen widgets fill remaining space.
    const auto connected = [&](const cfg::Entry& e) {
        return std::any_of(displays.begin(), displays.end(), [&](const Display& d) { return d.id == e.monitor; });
    };
    std::stable_sort(visible.entries.begin(), visible.entries.end(), [&](const cfg::Entry& a, const cfg::Entry& b) {
        return connected(a) && !connected(b);
    });
    for (auto& e : visible.entries) if (!connected(e)) e.monitor = displays.front().id;
    for (const auto& d : displays) placement::Normalize(d.grid, visible);
    return visible;
}
}
