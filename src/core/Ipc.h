#pragma once

#include <windows.h>

// 外部进程（资源管理器右键菜单）和已经在跑的 Tilefold 之间的最小握手：
// 桌面右键「新建收纳文件夹」实际是 tilefold.exe --new-box 拉起的短命进程，
// 它自己不做建目录的活儿，只把请求转发给主实例（主实例知道布局、知道要刷新
// 桌面层）。用普通窗口消息而不是命名管道：控制窗口本来就在、注册一条自定义
// 消息就能到，没必要为此多一套 IPC。

namespace tf::ipc {

// 主实例的控制窗口类名（App::CreateControlWindow 建的就是它）
inline constexpr wchar_t kControlClass[] = L"Tilefold.Control";

// 请主实例弹输入框并新建收纳文件夹（RegisterWindowMessageW 注册得到消息号）
inline constexpr wchar_t kNewBoxMessage[] = L"Tilefold.NewBoxFolder";

inline HWND FindRunningInstance() {
    return ::FindWindowW(kControlClass, nullptr);
}

}  // namespace tf::ipc
