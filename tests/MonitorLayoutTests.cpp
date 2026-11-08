#include "core/MonitorLayout.h"
#include "core/LayoutPlacement.h"
#include "core/Paths.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
using namespace tf;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
const cfg::Entry& Find(const cfg::Layout& l, const wchar_t* name) {
    const auto* e = cfg::FindEntry(l, L"folder", name);
    if (!e) throw std::runtime_error("missing entry");
    return *e;
}
desktop::Display Screen(const wchar_t* id, RECT work, float scale, bool primary) {
    desktop::Display d; d.id = id; d.workPx = work; d.scale = scale; d.primary = primary; return d;
}
int main() {
    try {
        auto preferred = cfg::ParseLayoutBytes(
            "# Tilefold layout v4\ngrid\t20\t7600\t1520\t760\t0\t0\nfolder\tA\t\t0\t0\t3\t3\n", L"test");
        std::vector<desktop::Display> screens{
            Screen(L"primary", {0,0,1520,760}, 1, true),
            Screen(L"left", {-1920,-120,0,1080}, 1.5f, false)};
        desktop::Configure(screens, preferred);
        Check(Find(preferred,L"A").monitor == L"primary", "legacy monitor migration");
        cfg::Entry b{L"folder", L"B", L"", 0,0,3,3,L"left"};
        cfg::SetEntry(preferred,b);
        auto both = desktop::Resolve(screens,preferred);
        Check(Find(both,L"B").col == 0, "same cells on separate monitors must not collide");
        const RECT bounds = desktop::BoundsPx(screens[1], Find(both,L"B"));
        const POINT local = desktop::LocalDip(screens[1], {bounds.left,bounds.top});
        Check(bounds.left < 0 && local.x >= 0 && local.x < 30, "negative-origin DPI transform");
        Check(desktop::ForPoint(screens, {-1000,0}).id == L"left", "left display selection");
        Check(desktop::ForPoint(screens, {-1,200}).id == L"left" &&
              desktop::ForPoint(screens, {0,200}).id == L"primary", "mixed DPI display boundary");
        for (float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
            auto d = Screen(L"scaled", {-2400,-600,0,1200}, scale, true);
            auto copy = preferred;
            std::vector<desktop::Display> one{d};
            desktop::Configure(one, copy);
            cfg::Entry probe{L"folder",L"probe",L"",2,1,2,2,L"scaled"};
            const RECT px = desktop::BoundsPx(one.front(),probe);
            const POINT dip = desktop::LocalDip(one.front(),{px.left,px.top});
            const POINT cell = grid::CellAt(one.front().grid,dip);
            Check(cell.x == 2 && cell.y == 1, "DPI cell roundtrip");
        }
        auto onlyPrimary = std::vector<desktop::Display>{screens.front()};
        auto disconnected = desktop::Resolve(onlyPrimary,preferred);
        Check(Find(disconnected,L"B").monitor == L"primary", "disconnected fallback");
        Check(Find(disconnected,L"B").col != 0 || Find(disconnected,L"B").row != 0, "fallback overlap");
        Check(Find(preferred,L"B").monitor == L"left" && Find(preferred,L"B").col == 0, "preferred layout mutated");
        auto reconnected = desktop::Resolve(screens,preferred);
        Check(Find(reconnected,L"B").monitor == L"left" && Find(reconnected,L"B").col == 0, "reconnect restore");
        auto changedPrimary = screens;
        std::swap(changedPrimary[0],changedPrimary[1]);
        changedPrimary[0].primary = true;
        changedPrimary[1].primary = false;
        auto swapped = desktop::Resolve(changedPrimary,preferred);
        Check(Find(swapped,L"A").monitor == L"primary" && Find(swapped,L"B").monitor == L"left",
              "changing primary monitor must preserve assignment");
        preferred.entries[0].col = 15;
        auto smaller = std::vector<desktop::Display>{Screen(L"primary",{0,0,640,480},1,true)};
        desktop::Configure(smaller,preferred);
        auto small = desktop::Resolve(smaller,preferred);
        Check(Find(small,L"A").col < 15 && Find(preferred,L"A").col == 15, "shrink must not overwrite preferred coordinates");
        wchar_t temp[MAX_PATH]{}; ::GetTempPathW(MAX_PATH,temp);
        const auto root = std::filesystem::path(temp) / (L"Tilefold-tests-" + std::to_wstring(::GetCurrentProcessId()));
        ::SetEnvironmentVariableW(L"TF_HOME",root.c_str());
        cfg::SaveLayout(preferred);
        auto roundtrip = cfg::LoadLayout();
        Check(Find(roundtrip,L"B").monitor == L"left", "monitor persistence roundtrip");
        Check(roundtrip.monitors.size() == 2, "per-monitor grid persistence");
        std::filesystem::remove_all(root);
        std::cout << "Monitor layout tests passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
