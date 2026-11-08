#include "shell/DesktopHost.h"

#include "core/Log.h"

namespace {

struct EnumCtx {
    HWND found = nullptr;
};

BOOL CALLBACK EnumWorkerWProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);

    wchar_t cls[128]{};
    if (::GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) == 0) return TRUE;
    if (::lstrcmpW(cls, L"WorkerW") != 0) return TRUE;

    if (::FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr) {
        ctx->found = hwnd;
        return FALSE;  // 找到了，停止枚举
    }
    return TRUE;
}

HWND ProgmanWindow() {
    return ::FindWindowW(L"Progman", nullptr);
}

BOOL CALLBACK EnumShellViewOwnerProc(HWND hwnd, LPARAM lp) {
    // 通用兜底：任何一个直接持有 SHELLDLL_DefView 的窗口都能当宿主
    if (::FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr) {
        *reinterpret_cast<HWND*>(lp) = hwnd;
        return FALSE;
    }
    return TRUE;
}

BOOL CALLBACK SetNativeIconsVisibleProc(HWND hwnd, LPARAM visible) {
    HWND view = ::FindWindowExW(hwnd, nullptr, L"SHELLDLL_DefView", nullptr);
    if (view != nullptr) {
        ::ShowWindow(view, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
    return TRUE;
}

}  // namespace

namespace tf::host {

std::wstring ClassName(HWND hwnd) {
    if (hwnd == nullptr) return L"<null>";
    wchar_t cls[128]{};
    if (::GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) == 0) return L"<?>";
    return std::wstring(cls);
}

HWND FindDefView(HWND host) {
    if (host == nullptr) return nullptr;
    return ::FindWindowExW(host, nullptr, L"SHELLDLL_DefView", nullptr);
}

bool NativeListVisible(HWND defView) {
    if (defView == nullptr || !::IsWindow(defView)) return false;
    const HWND list = ::FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
    if (list == nullptr || ::IsWindow(list) == FALSE) return false;
    return ::IsWindowVisible(list) != FALSE;
}

bool IconAtPoint(HWND defView, POINT screenPx) {
    if (defView == nullptr || !::IsWindow(defView)) return false;
    const HWND list = ::FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
    if (list == nullptr) return false;
    // 列表不可见（设置里关了“显示桌面图标”）或里面根本没图标：桌面就是空的
    if (::IsWindowVisible(list) == FALSE) return false;
    constexpr UINT kLvmFirst = 0x1000;
    constexpr UINT kLvmGetItemCount = kLvmFirst + 4;  // LVM_GETITEMCOUNT
    constexpr UINT kLvmHitTest = kLvmFirst + 18;      // LVM_HITTEST
    if (::SendMessageW(list, kLvmGetItemCount, 0, 0) <= 0) return false;

    POINT client = screenPx;
    if (::ScreenToClient(list, &client) == FALSE) return false;
    const LRESULT hit = ::SendMessageW(list, kLvmHitTest, 0,
                                       reinterpret_cast<LPARAM>(&client));
    return hit >= 0;
}

HostInfo Find() {
    HostInfo info;

    // --- 1. Progman：Win11 默认情况下桌面的顶层容器 ---
    HWND progman = ProgmanWindow();
    if (progman != nullptr) {
        if (::FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr) {
            info.hwnd = progman;
            info.className = L"Progman";
            info.hasDefView = true;
            log::Infof(L"桌面宿主 = Progman (0x{:X})，DefView 直接在其子窗口", reinterpret_cast<uintptr_t>(progman));
            return info;
        }
    }

    // --- 2. 请求 Explorer 生成 WorkerW，然后找持有 DefView 的那个 ---
    if (progman != nullptr) {
        DWORD_PTR result = 0;
        ::SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    }

    EnumCtx ctx;
    ::EnumWindows(&EnumWorkerWProc, reinterpret_cast<LPARAM>(&ctx));
    if (ctx.found != nullptr) {
        info.hwnd = ctx.found;
        info.className = L"WorkerW";
        info.hasDefView = true;
        log::Infof(L"桌面宿主 = WorkerW (0x{:X})，持有 SHELLDLL_DefView",
                   reinterpret_cast<uintptr_t>(ctx.found));
        return info;
    }

    // --- 3. 通用兜底：任何直接持有 DefView 的顶层窗口 ---
    HWND owner = nullptr;
    ::EnumWindows(&EnumShellViewOwnerProc, reinterpret_cast<LPARAM>(&owner));
    if (owner != nullptr) {
        info.hwnd = owner;
        info.className = ClassName(owner);
        info.hasDefView = true;
        log::Infof(L"桌面宿主 = {} (0x{:X}) [通用兜底]", info.className,
                   reinterpret_cast<uintptr_t>(owner));
        return info;
    }

    info.hwnd = progman;
    info.className = progman ? L"Progman" : L"<none>";
    info.hasDefView = false;
    info.fallback = true;
    log::Warn(L"未找到持有桌面图标视图的宿主窗口，进入降级模式（顶层窗口）");
    return info;
}

bool IsHostAlive(HWND host) {
    if (host == nullptr || !::IsWindow(host)) return false;
    // Progman/WorkerW 必须还持有 DefView，否则说明 Explorer 已经重建了桌面
    return ::FindWindowExW(host, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr;
}

void SetNativeIconsVisible(bool visible) {
    ::EnumWindows(&SetNativeIconsVisibleProc, visible ? TRUE : FALSE);
}

}  // namespace tf::host
