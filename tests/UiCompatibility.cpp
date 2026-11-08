#include <windows.h>
#include <commctrl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <cmath>
#include "core/SettingsWindow.h"
#include "core/Settings.h"
#include "core/Dpi.h"
#include "widgets/WidgetWindow.h"

namespace {
int failures = 0;
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
BOOL CALLBACK FindSettings(HWND window, LPARAM result) {
    wchar_t name[80]{};
    GetClassNameW(window, name, 80);
    if (std::wstring(name) == L"Tilefold.Settings")
        *reinterpret_cast<HWND*>(result) = window;
    return TRUE;
}
void CheckWidgetCoordinates() {
    // Exercise the real HWND boundary without touching Explorer or the active app.
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT bounds{monitor.rcWork.left + 24, monitor.rcWork.top + 24,
                      monitor.rcWork.left + 224, monitor.rcWork.top + 174};
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"",
        WS_POPUP, monitor.rcWork.left, monitor.rcWork.top,
        monitor.rcWork.right - monitor.rcWork.left, monitor.rcWork.bottom - monitor.rcWork.top,
        nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    Check(host != nullptr, "isolated widget host created");
    {
        tf::widget::WidgetWindow widget(L"Coordinate test", bounds);
        widget.SetBottomBandDip(20);
        Check(widget.Show(host, GetModuleHandleW(nullptr)), "desktop child widget creates");
        if (widget.Hwnd()) {
            RECT actual{};
            GetWindowRect(widget.Hwnd(), &actual);
            const float scale = tf::dpi::ScaleForRect(bounds);
            Check(actual.left == bounds.left && actual.top == bounds.top &&
                  actual.right == bounds.right &&
                  actual.bottom == bounds.bottom + tf::dpi::Dip2Px(20, scale),
                  "screen bounds stay physical pixels including separate DIP label band");
            Check(std::abs(widget.SizeDip().width - 200.0f / scale) < 0.01f,
                  "local geometry uses monitor DIP");
            float hitX = -1, hitY = -1;
            widget.SetRightClickHandler([&](float x, float y) { hitX = x; hitY = y; });
            SendMessageW(widget.Hwnd(), WM_RBUTTONUP, 0, MAKELPARAM(50, 60));
            Check(std::abs(hitX - 50 / scale) < 0.01f && std::abs(hitY - 60 / scale) < 0.01f,
                  "physical input maps to drawing coordinates");
            RECT moved = bounds;
            OffsetRect(&moved, 20, 30);
            widget.SetBoundsPx(moved);
            GetWindowRect(widget.Hwnd(), &actual);
            Check(actual.left == moved.left && actual.top == moved.top,
                  "position updates map screen pixels into parent client coordinates");
        }
    }
    if (host) DestroyWindow(host);
}
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto root = std::filesystem::temp_directory_path() /
        (L"Tilefold-ui-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    SetEnvironmentVariableW(L"TF_HOME", root.c_str());
    tf::core::SettingsWindow settings;
    int changes = 0;
    Check(settings.Show(nullptr, GetModuleHandleW(nullptr), {},
        [&](const tf::cfg::Settings&) { ++changes; }, true), "first-run settings opens");
    HWND window = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), FindSettings, reinterpret_cast<LPARAM>(&window));
    Check(window != nullptr, "settings HWND belongs to test thread");
    if (window) {
        wchar_t title[80]{};
        GetWindowTextW(window, title, static_cast<int>(std::size(title)));
        // 标题后面跟着版本号（例如 "欢迎使用 Tilefold 1.0.0-beta"），所以只比前缀。
        Check(std::wstring(title).rfind(L"欢迎使用 Tilefold", 0) == 0, "first run shows onboarding title");
        Check(GetDlgItem(window, 1007) != nullptr, "autostart option exists in first-run settings");
        ShowWindow(window, SW_HIDE);
        RECT rect{};
        GetWindowRect(window, &rect);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor);
        Check(rect.left >= monitor.rcWork.left && rect.top >= monitor.rcWork.top &&
              rect.right <= monitor.rcWork.right && rect.bottom <= monitor.rcWork.bottom,
              "initial settings fits work area");
        Check(std::abs((rect.left + rect.right) - (monitor.rcWork.left + monitor.rcWork.right)) <= 1 &&
              std::abs((rect.top + rect.bottom) - (monitor.rcWork.top + monitor.rcWork.bottom)) <= 1,
              "settings opens centered in the monitor work area");
        const RECT initialRect = rect;
        // On an ordinary display all settings should fit without a vertical scrollbar.
        if (rect.bottom - rect.top < monitor.rcWork.bottom - monitor.rcWork.top) {
            Check((GetWindowLongPtrW(window, GWL_STYLE) & WS_VSCROLL) == 0,
                  "content-fitted settings has no unnecessary scrollbar");
            RECT client{}, footer{};
            GetClientRect(window, &client);
            GetWindowRect(GetDlgItem(window, 1006), &footer);
            MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&footer), 2);
            Check(footer.top >= 0 && footer.bottom < client.bottom,
                  "last setting description and bottom padding are visible on open");
        }
        // Force a viewport smaller than its content without changing the user's display.
        SetWindowPos(window, nullptr, 0, 0, 160, 100, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        SCROLLINFO scroll{sizeof(scroll), SIF_ALL};
        GetScrollInfo(window, SB_VERT, &scroll);
        Check(scroll.nMax >= static_cast<int>(scroll.nPage), "small viewport exposes vertical scrolling");
        SendMessageW(window, WM_VSCROLL, SB_BOTTOM, 0);
        scroll.fMask = SIF_POS;
        GetScrollInfo(window, SB_VERT, &scroll);
        Check(scroll.nPos > 0, "scroll reaches hidden content");
        SendMessageW(window, WM_VSCROLL, SB_TOP, 0);
        HWND combo = GetDlgItem(window, 1001);
        Check(combo != nullptr, "color selector exists");
        if (combo) {
            SendMessageW(combo, CB_SETCURSEL, 1, 0);
            SendMessageW(window, WM_COMMAND, MAKEWPARAM(1001, CBN_SELCHANGE), reinterpret_cast<LPARAM>(combo));
            Check(changes == 1 && tf::cfg::LoadSettings().labelColor == tf::cfg::LabelColor::Black,
                  "resized settings remains functional and saves in isolated directory");
        }
        Check(settings.Show(nullptr, GetModuleHandleW(nullptr), {}, {}, true),
              "opening existing settings restores its content size");
        GetWindowRect(window, &rect);
        Check(EqualRect(&rect, &initialRect), "existing settings recenters and restores automatic size");
        scroll.fMask = SIF_POS;
        GetScrollInfo(window, SB_VERT, &scroll);
        Check(scroll.nPos == 0, "reopening settings resets scroll position");
    }
    settings.Close();
    Check(!settings.IsOpen(), "settings closes cleanly");
    CheckWidgetCoordinates();
    // Only remove the exact test directory created above.
    std::filesystem::remove_all(root);
    std::cout << (failures ? "UI compatibility checks failed\n" : "UI compatibility checks passed\n");
    return failures ? 1 : 0;
}
