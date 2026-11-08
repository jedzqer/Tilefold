#include "core/SettingsWindow.h"

#include <commctrl.h>

#include <utility>
#include <algorithm>
#include <format>
#include <string>
#include <windowsx.h>
#include "core/Autostart.h"
#include "core/Dpi.h"

// 由 CMakeLists.txt 定义；没定义时（比如单元测试目标）退回一个占位版本号。
#ifndef TILEFOLD_DISPLAY_VERSION
#define TILEFOLD_DISPLAY_VERSION L"1.0.0"
#endif

namespace {

constexpr wchar_t kClassName[] = L"Tilefold.Settings";
constexpr int kColorCombo = 1001;
constexpr int kHideDesktopIcons = 1005;
constexpr int kAutostart = 1007;

}  // namespace

namespace tf::core {

bool SettingsWindow::Show(HWND owner, HINSTANCE instance, cfg::Settings settings,
                          ChangedHandler changed, bool firstRun) {
    if (hwnd_ != nullptr && ::IsWindow(hwnd_)) {
        ::ShowWindow(hwnd_, SW_SHOWNORMAL);
        CenterAndFit();
        ::SetForegroundWindow(hwnd_);
        return true;
    }
    instance_ = instance;
    settings_ = settings;
    changed_ = std::move(changed);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::StaticWndProc;
    wc.hInstance = instance_;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;

    // 标题带版本号：beta 包要让人一眼能看出来装的是哪个
    const std::wstring title = firstRun
                                   ? std::format(L"欢迎使用 Tilefold {}", TILEFOLD_DISPLAY_VERSION)
                                   : std::format(L"Tilefold 设置 {}", TILEFOLD_DISPLAY_VERSION);
    hwnd_ = ::CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, title.c_str(),
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, CW_USEDEFAULT, CW_USEDEFAULT,
                              380, 240, owner, nullptr, instance_, this);
    if (hwnd_ == nullptr) return false;

    ::CreateWindowExW(0, L"STATIC", L"桌面标签文字颜色", WS_CHILD | WS_VISIBLE,
                      20, 24, 145, 24, hwnd_, reinterpret_cast<HMENU>(1002), instance_, nullptr);
    HWND combo = ::CreateWindowExW(0, WC_COMBOBOXW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                   CBS_DROPDOWNLIST | WS_VSCROLL, 170, 20, 170, 120,
                                   hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kColorCombo)),
                                   instance_, nullptr);
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动（白字 + 阴影）"));
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"黑色"));
    ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"白色"));
    ::SendMessageW(combo, CB_SETCURSEL, static_cast<WPARAM>(settings_.labelColor), 0);
    ::CreateWindowExW(0, L"STATIC", L"选择适合当前桌面背景的文字颜色。",
                      WS_CHILD | WS_VISIBLE, 20, 58, 320, 44, hwnd_, reinterpret_cast<HMENU>(1003), instance_, nullptr);
    HWND nativeIcons = ::CreateWindowExW(0, L"BUTTON", L"隐藏 Windows 原生桌面图标，由 Tilefold 显示",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE, 20, 146, 330, 30, hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kHideDesktopIcons)), instance_, nullptr);
    ::SendMessageW(nativeIcons, BM_SETCHECK, settings_.hideNativeDesktopIcons ? BST_CHECKED : BST_UNCHECKED, 0);
    ::CreateWindowExW(0, L"STATIC", L"文件和快捷方式保留在桌面原位置；Tilefold 只扫描并显示，不移动内容。",
                      WS_CHILD | WS_VISIBLE, 20, 180, 330, 44, hwnd_,
                      reinterpret_cast<HMENU>(1006), instance_, nullptr);
    HWND autostart = ::CreateWindowExW(0, L"BUTTON", L"登录 Windows 时自动启动 Tilefold",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX | BS_MULTILINE,
        20, 224, 330, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutostart)),
        instance_, nullptr);
    ::SendMessageW(autostart, BM_SETCHECK,
        autostart::IsEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);
    ::CreateWindowExW(0, L"STATIC", L"仅为当前用户启用，不需要管理员权限。",
        WS_CHILD | WS_VISIBLE, 20, 258, 330, 40, hwnd_,
        reinterpret_cast<HMENU>(1008), instance_, nullptr);

    CenterAndFit();
    ::ShowWindow(hwnd_, SW_SHOWNORMAL);
    ::UpdateWindow(hwnd_);
    ::SetForegroundWindow(hwnd_);
    return true;
}

void SettingsWindow::Close() {
    if (hwnd_ != nullptr && ::IsWindow(hwnd_)) ::DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    changed_ = {};
}

bool SettingsWindow::IsOpen() const { return hwnd_ != nullptr && ::IsWindow(hwnd_); }

LRESULT CALLBACK SettingsWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsWindow* self = reinterpret_cast<SettingsWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        self = static_cast<SettingsWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    }
    return self ? self->WndProc(msg, wp, lp) : ::DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_COMMAND && LOWORD(wp) == kColorCombo && HIWORD(wp) == CBN_SELCHANGE) {
        ApplySelection();
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == kHideDesktopIcons &&
        HIWORD(wp) == BN_CLICKED) {
        ApplyToggle(LOWORD(wp));
        return 0;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == kAutostart && HIWORD(wp) == BN_CLICKED) {
        ApplyToggle(LOWORD(wp));
        return 0;
    }
    if (msg == WM_SIZE) { LayoutControls(); return 0; }
    if (msg == WM_DPICHANGED) {
        const RECT r = *reinterpret_cast<const RECT*>(lp);
        ::SetWindowPos(hwnd_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                       SWP_NOZORDER | SWP_NOACTIVATE);
        FitToWorkArea();
        UpdateFont();
        LayoutControls();
        return 0;
    }
    if (msg == WM_DISPLAYCHANGE || msg == WM_SETTINGCHANGE) {
        FitToWorkArea();
        LayoutControls();
    }
    if (msg == WM_VSCROLL) {
        SCROLLINFO si{sizeof(si), SIF_ALL};
        ::GetScrollInfo(hwnd_, SB_VERT, &si);
        int pos = scroll_;
        switch (LOWORD(wp)) {
            case SB_LINEUP: pos -= dpi::Dip2Px(24, dpi::ScaleForWindow(hwnd_)); break;
            case SB_LINEDOWN: pos += dpi::Dip2Px(24, dpi::ScaleForWindow(hwnd_)); break;
            case SB_PAGEUP: pos -= si.nPage; break;
            case SB_PAGEDOWN: pos += si.nPage; break;
            case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
            case SB_TOP: pos = 0; break;
            case SB_BOTTOM: pos = contentHeight_; break;
        }
        ScrollTo(pos);
        return 0;
    }
    if (msg == WM_MOUSEWHEEL) {
        ScrollTo(scroll_ - MulDiv(GET_WHEEL_DELTA_WPARAM(wp),
                 dpi::Dip2Px(48, dpi::ScaleForWindow(hwnd_)), WHEEL_DELTA));
        return 0;
    }
    if (msg == WM_CLOSE) {
        ::DestroyWindow(hwnd_);
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        const LRESULT result = ::DefWindowProcW(hwnd_, msg, wp, lp);
        hwnd_ = nullptr;
        if (font_) { ::DeleteObject(font_); font_ = nullptr; }
        scroll_ = 0;
        changed_ = {};
        return result;
    }
    return ::DefWindowProcW(hwnd_, msg, wp, lp);
}

void SettingsWindow::CenterAndFit() {
    POINT cursor{};
    ::GetCursorPos(&cursor);
    MONITORINFO mi{sizeof(mi)};
    if (!::GetMonitorInfoW(::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const float scale = dpi::ScaleForPoint(cursor);
    const UINT dpiValue = static_cast<UINT>(96.0f * scale + 0.5f);
    const DWORD style = static_cast<DWORD>(::GetWindowLongPtrW(hwnd_, GWL_STYLE)) &
                        ~(WS_VSCROLL | WS_HSCROLL);
    const DWORD exStyle = static_cast<DWORD>(::GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
    RECT frame{};
    if (!::AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle, dpiValue)) return;
    const int frameWidth = frame.right - frame.left;
    const int frameHeight = frame.bottom - frame.top;
    const int workWidth = mi.rcWork.right - mi.rcWork.left;
    const int workHeight = mi.rcWork.bottom - mi.rcWork.top;
    const int width = std::min(dpi::Dip2Px(420, scale) + frameWidth, workWidth);
    int height = std::min(dpi::Dip2Px(300, scale) + frameHeight, workHeight);
    scroll_ = 0;
    // Move to the target monitor first so font and content measurements use its DPI.
    ::SetWindowPos(hwnd_, nullptr, mi.rcWork.left + (workWidth - width) / 2,
                   mi.rcWork.top + (workHeight - height) / 2, width, height,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    UpdateFont();
    // Re-measure after sizing: a scrollbar on a small display can change text wrapping.
    for (int pass = 0; pass < 3; ++pass) {
        LayoutControls();
        height = std::min(contentHeight_ + frameHeight, workHeight);
        ::SetWindowPos(hwnd_, nullptr, mi.rcWork.left + (workWidth - width) / 2,
                       mi.rcWork.top + (workHeight - height) / 2, width, height,
                       SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void SettingsWindow::FitToWorkArea() {
    RECT rect{};
    ::GetWindowRect(hwnd_, &rect);
    MONITORINFO mi{sizeof(mi)};
    if (!::GetMonitorInfoW(::MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &mi)) return;
    const int width = std::min(rect.right - rect.left, mi.rcWork.right - mi.rcWork.left);
    const int height = std::min(rect.bottom - rect.top, mi.rcWork.bottom - mi.rcWork.top);
    const int x = std::clamp(rect.left, mi.rcWork.left, mi.rcWork.right - width);
    const int y = std::clamp(rect.top, mi.rcWork.top, mi.rcWork.bottom - height);
    ::SetWindowPos(hwnd_, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::UpdateFont() {
    HFONT next = ::CreateFontW(-dpi::Dip2Px(14, dpi::ScaleForWindow(hwnd_)), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (!next) return;
    for (int id : {1001, 1002, 1003, kHideDesktopIcons, 1006, kAutostart, 1008})
        ::SendMessageW(::GetDlgItem(hwnd_, id), WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
    if (font_) ::DeleteObject(font_);
    font_ = next;
}

void SettingsWindow::ScrollTo(int position) {
    RECT client{};
    ::GetClientRect(hwnd_, &client);
    scroll_ = std::clamp(position, 0, std::max(0, contentHeight_ - static_cast<int>(client.bottom)));
    LayoutControls();
}

void SettingsWindow::LayoutControls() {
    if (layingOut_ || !hwnd_ || !font_) return;
    layingOut_ = true;
    const float scale = dpi::ScaleForWindow(hwnd_);
    auto px = [scale](int value) { return dpi::Dip2Px(value, scale); };
    HDC dc = ::GetDC(hwnd_);
    HGDIOBJ previousFont = dc ? ::SelectObject(dc, font_) : nullptr;
    auto textHeight = [&](int id, int availableWidth, int minimum) {
        wchar_t text[256]{};
        ::GetWindowTextW(::GetDlgItem(hwnd_, id), text, static_cast<int>(std::size(text)));
        RECT bounds{0, 0, std::max(1, availableWidth), 0};
        if (dc) ::DrawTextW(dc, text, -1, &bounds, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
        return std::max(px(minimum), static_cast<int>(bounds.bottom) + px(4));
    };
    // Showing/hiding the scrollbar changes client width, so measure again when needed.
    for (int pass = 0; pass < 3; ++pass) {
        RECT client{};
        ::GetClientRect(hwnd_, &client);
        const int margin = std::min(px(20), static_cast<int>(std::max(0L, client.right / 8)));
        const int width = std::max(1L, client.right - 2 * margin);
        // Stack controls instead of forcing a fixed horizontal row on narrow screens.
        const int labelHeight = textHeight(1002, width, 26);
        const int comboTop = px(24) + labelHeight;
        const int hintTop = comboTop + px(40);
        const int hintHeight = textHeight(1003, width, 24);
        const int iconsTop = hintTop + hintHeight + px(12);
        const int iconsHeight = textHeight(kHideDesktopIcons, width - px(24), 30);
        const int footerTop = iconsTop + iconsHeight + px(8);
        const int footerHeight = textHeight(1006, width, 24);
        const int autostartTop = footerTop + footerHeight + px(8);
        const int autostartHeight = textHeight(kAutostart, width, 30);
        const int autostartHintTop = autostartTop + autostartHeight + px(4);
        const int autostartHintHeight = textHeight(1008, width, 24);
        contentHeight_ = autostartHintTop + autostartHintHeight + margin;
        scroll_ = std::clamp(scroll_, 0, std::max(0, contentHeight_ - static_cast<int>(client.bottom)));
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_POS};
        si.nMin = 0; si.nMax = contentHeight_ - 1;
        si.nPage = client.bottom; si.nPos = scroll_;
        ::SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
        RECT updatedClient{};
        ::GetClientRect(hwnd_, &updatedClient);
        if (updatedClient.right != client.right && pass < 2) continue;
        ::SetWindowPos(::GetDlgItem(hwnd_, 1002), nullptr, margin, px(20) - scroll_, width, labelHeight, SWP_NOZORDER);
        ::SetWindowPos(::GetDlgItem(hwnd_, 1001), nullptr, margin, comboTop - scroll_, width, px(140), SWP_NOZORDER);
        ::SetWindowPos(::GetDlgItem(hwnd_, 1003), nullptr, margin, hintTop - scroll_, width, hintHeight, SWP_NOZORDER);
        ::SetWindowPos(::GetDlgItem(hwnd_, kHideDesktopIcons), nullptr, margin, iconsTop - scroll_, width, iconsHeight, SWP_NOZORDER);
        ::SetWindowPos(::GetDlgItem(hwnd_, 1006), nullptr, margin, footerTop - scroll_, width, footerHeight, SWP_NOZORDER);
        ::SetWindowPos(::GetDlgItem(hwnd_, kAutostart), nullptr, margin, autostartTop - scroll_, width, autostartHeight, SWP_NOZORDER);
        ::SetWindowPos(::GetDlgItem(hwnd_, 1008), nullptr, margin, autostartHintTop - scroll_, width, autostartHintHeight, SWP_NOZORDER);
        break;
    }
    if (dc) {
        if (previousFont) ::SelectObject(dc, previousFont);
        ::ReleaseDC(hwnd_, dc);
    }
    ::InvalidateRect(hwnd_, nullptr, TRUE);
    layingOut_ = false;
}

void SettingsWindow::ApplySelection() {
    HWND combo = ::GetDlgItem(hwnd_, kColorCombo);
    const LRESULT selected = ::SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selected < 0 || selected > 2) return;
    settings_.labelColor = static_cast<cfg::LabelColor>(selected);
    if (applying_) return;
    if (cfg::SaveSettings(settings_) && changed_) changed_(settings_);
}

void SettingsWindow::ApplyToggle(int id) {
    if (applying_) return;
    applying_ = true;
    if (id == kAutostart) {
        const bool enabled = ::SendMessageW(::GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (!(enabled ? autostart::Enable() : autostart::Disable())) {
            ::SendMessageW(::GetDlgItem(hwnd_, id), BM_SETCHECK,
                           enabled ? BST_UNCHECKED : BST_CHECKED, 0);
            ::MessageBoxW(hwnd_, L"无法更新当前用户的开机启动设置。", L"Tilefold 设置",
                          MB_OK | MB_ICONERROR);
        }
        applying_ = false;
        return;
    }
    if (id == kHideDesktopIcons)
        settings_.hideNativeDesktopIcons = ::SendMessageW(::GetDlgItem(hwnd_, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
    settings_.onboardingComplete = true;
    if (cfg::SaveSettings(settings_) && changed_) changed_(settings_);
    applying_ = false;
}

}  // namespace tf::core

