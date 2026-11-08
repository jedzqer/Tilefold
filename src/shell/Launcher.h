#pragma once

#include <string>

namespace tf::shell {

// 启动一个条目（.lnk / .exe / 目录 / shell: 命名空间）。
//
// 为什么不在 UI 线程直接调：ShellExecuteEx 会加载第三方 shell 扩展
// （右键菜单处理器、UWP 激活器等），任何一个卡住都会把整个桌面卡死。
// 所以这里起一个独立的 STA 线程去做，UI 立刻返回。
void LaunchPath(const std::wstring& path);

}  // namespace tf::shell
