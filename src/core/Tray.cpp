#include "core/Tray.h"

#include <shellapi.h>

#include <cmath>
#include <cwchar>

#include "core/Autostart.h"
#include "core/Log.h"

namespace tf::core {

namespace {

constexpr UINT kTrayId = 1;
constexpr UINT kCmdSettings = 1001;
constexpr UINT kCmdExit = 1002;
constexpr UINT kCmdAutostart = 1003;
constexpr UINT kCmdNewBox = 1004;


// 程序还没有 .ico 资源：运行时画一个 2×2 圆角方块的小图标（跟大文件夹的样子呼应）
HICON MakeTrayIcon() {
    const int size = ::GetSystemMetrics(SM_CXSMICON) > 0 ? ::GetSystemMetrics(SM_CXSMICON) : 16;

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = size;
    bi.bV5Height = -size;  // 自上而下
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* bits = nullptr;
    HDC screen = ::GetDC(nullptr);
    HBITMAP color = ::CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                       &bits, nullptr, 0);
    ::ReleaseDC(nullptr, screen);
    if (color == nullptr || bits == nullptr) return ::LoadIconW(nullptr, IDI_APPLICATION);

    auto* px = static_cast<unsigned*>(bits);
    const float s = static_cast<float>(size);
    const float gap = s * 0.10f;
    const float tile = (s - gap * 3.0f) * 0.5f;
    const float r = tile * 0.28f;
    // 四块颜色：左上蓝、右上青、左下紫、右下白（“更多”）
    const unsigned colors[4] = {0x3D7EF0, 0x2EC4B6, 0x8E6CEF, 0xF2F2F2};
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            unsigned out = 0;
            for (int t = 0; t < 4; ++t) {
                const float x0 = gap + (t % 2) * (tile + gap);
                const float y0 = gap + (t / 2) * (tile + gap);
                const float fx = static_cast<float>(x) + 0.5f;
                const float fy = static_cast<float>(y) + 0.5f;
                // 圆角矩形的有符号距离，做 1px 抗锯齿
                const float qx = std::fabs(fx - (x0 + tile * 0.5f)) - (tile * 0.5f - r);
                const float qy = std::fabs(fy - (y0 + tile * 0.5f)) - (tile * 0.5f - r);
                const float ox = qx > 0.0f ? qx : 0.0f;
                const float oy = qy > 0.0f ? qy : 0.0f;
                const float inside = (qx > qy ? qx : qy) < 0.0f ? (qx > qy ? qx : qy) : 0.0f;
                const float d = std::sqrt(ox * ox + oy * oy) + inside - r;
                float a = 0.5f - d;
                if (a <= 0.0f) continue;
                if (a > 1.0f) a = 1.0f;
                const unsigned c = colors[t];
                const unsigned A = static_cast<unsigned>(a * 255.0f + 0.5f);
                // 预乘 alpha
                const unsigned R = ((c >> 16) & 0xFF) * A / 255;
                const unsigned G = ((c >> 8) & 0xFF) * A / 255;
                const unsigned B = (c & 0xFF) * A / 255;
                out = (A << 24) | (R << 16) | (G << 8) | B;
                break;
            }
            px[y * size + x] = out;
        }
    }

    HBITMAP mask = ::CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = color;
    ii.hbmMask = mask;
    HICON icon = ::CreateIconIndirect(&ii);
    ::DeleteObject(color);
    if (mask != nullptr) ::DeleteObject(mask);
    return icon != nullptr ? icon : ::LoadIconW(nullptr, IDI_APPLICATION);
}

}  // namespace

Tray::~Tray() {
    Remove();
    if (icon_ != nullptr) {
        ::DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

bool Tray::Add(HWND owner) {
    owner_ = owner;
    if (icon_ == nullptr) icon_ = MakeTrayIcon();

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = kCallbackMsg;
    nid.hIcon = icon_;
    wcscpy_s(nid.szTip, L"Tilefold");
    if (!::Shell_NotifyIconW(NIM_ADD, &nid)) {
        log::Warnf(L"托盘图标添加失败 err={}", ::GetLastError());
        added_ = false;
        return false;
    }
    nid.uVersion = NOTIFYICON_VERSION_4;
    ::Shell_NotifyIconW(NIM_SETVERSION, &nid);
    added_ = true;
    log::Info(L"托盘图标已添加");
    return true;
}

void Tray::Remove() {
    if (!added_ || owner_ == nullptr) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = owner_;
    nid.uID = kTrayId;
    ::Shell_NotifyIconW(NIM_DELETE, &nid);
    added_ = false;
}

void Tray::Readd() {
    if (owner_ == nullptr) return;
    added_ = false;  // Explorer 重启后旧图标已经没了，不需要先删
    Add(owner_);
}

bool Tray::HandleMessage(UINT msg, WPARAM, LPARAM lp) {
    if (msg != kCallbackMsg) return false;
    // NOTIFYICON_VERSION_4：事件在 LOWORD(lParam)
    switch (LOWORD(lp)) {
        case WM_CONTEXTMENU:
        case WM_RBUTTONUP:
            ShowMenu();
            break;
        case WM_LBUTTONDBLCLK:
            if (onSettings) onSettings();
            break;
        default:
            break;
    }
    return true;
}

void Tray::ShowMenu() {
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) return;
    ::AppendMenuW(menu, MF_STRING, kCmdNewBox, L"新建收纳文件夹(&N)");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kCmdSettings, L"打开设置");
    // 每次弹菜单现查注册表：用户可能在任务管理器里手改过启动项，缓存一份只会显示旧状态
    const bool autostartOn = autostart::IsEnabled();
    ::AppendMenuW(menu, MF_STRING | (autostartOn ? MF_CHECKED : MF_UNCHECKED), kCmdAutostart,
                   L"开机自启动");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    ::AppendMenuW(menu, MF_STRING, kCmdExit, L"退出");
    ::SetMenuDefaultItem(menu, kCmdNewBox, FALSE);

    POINT pt{};
    ::GetCursorPos(&pt);
    // 不先切前台的话，点菜单外面菜单不会消失（托盘菜单的老问题）
    ::SetForegroundWindow(owner_);
    const UINT cmd = static_cast<UINT>(::TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, owner_,
        nullptr));
    ::PostMessageW(owner_, WM_NULL, 0, 0);
    ::DestroyMenu(menu);

    if (cmd == kCmdNewBox && onNewBox) onNewBox();
    if (cmd == kCmdSettings && onSettings) onSettings();
    if (cmd == kCmdAutostart) {
        if (autostartOn) {
            autostart::Disable();
        } else {
            autostart::Enable();
        }
        if (onAutostartChanged) onAutostartChanged(!autostartOn);
    }
    if (cmd == kCmdExit && onExit) onExit();
}

}  // namespace tf::core
