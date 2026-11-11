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

std::wstring ClassName(HWND hwnd);

// 判断宿主是否仍然有效（Explorer 重启后旧 HWND 会失效）
bool IsHostAlive(HWND host);

// 把原生桌面图标层（SHELLDLL_DefView）隐藏掉。Tilefold 全面接管桌面：
// 这一层在运行期间从不显示，Explorer 的原生图标、桌面右键菜单都不会再干扰我们。
void HideNativeIcons();

// 退出时把 DefView 恢复显示。注册表里的 HideIcons=1 不动（下次启动照样接管），
// 只是不要让我们进程里那次 ShowWindow(SW_HIDE) 在退出后还残留着：万一
// HideIcons 写失败（权限/组策略），桌面不至于在 Tilefold 关掉后变成空白的。
void RestoreNativeIconsOnExit();

}  // namespace tf::host
