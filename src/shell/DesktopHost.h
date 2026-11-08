#pragma once

#include <windows.h>

#include <string>

namespace tf::host {

struct HostInfo {
    HWND hwnd = nullptr;
    std::wstring className;
    bool hasDefView = false;   // 桌面图标视图是否在这个宿主的子树里
    bool fallback = false;     // true 表示没找到桌面层，只能退化成顶层窗口
};

// 探测桌面层父窗口。顺序：
//   1. Progman（当它直接持有 SHELLDLL_DefView 时）
//   2. 持有 SHELLDLL_DefView 的 WorkerW
//   3. 失败 -> fallback
HostInfo Find();

// host 子树里的 SHELLDLL_DefView（原生桌面图标视图的父窗口），找不到返回 nullptr
HWND FindDefView(HWND host);

// 这个屏幕点是不是落在「原生桌面图标」上（SHELLDLL_DefView 里那个 SysListView32）。
// 图标被隐藏 / 列表为空 / 列表本身不可见时一律返回 false —— 那些情况下桌面背景
// 就是真的空的，可以放心接管双击、右键这些手势。
bool IconAtPoint(HWND defView, POINT screenPx);

// 原生桌面图标列表（SysListView32）在不在、可不可见。
// 设置里关掉“显示桌面图标”之后这个列表变成不可见，Explorer 的桌面右键菜单
// （刷新/新建/显示设置…）也跟着没了 —— 这时才轮到我们接管桌面背景右键。
// 注意：SHELLDLL_DefView 自己仍然是 visible 的，所以不能拿它做判据。
bool NativeListVisible(HWND defView);

std::wstring ClassName(HWND hwnd);

// 判断宿主是否仍然有效（Explorer 重启后旧 HWND 会失效）
bool IsHostAlive(HWND host);
void SetNativeIconsVisible(bool visible);

}  // namespace tf::host
