#pragma once

#include <string>

namespace tf::core::autostart {

// 登记位置：HKCU\Software\Microsoft\Windows\CurrentVersion\Run
// 用 HKCU 而不是 HKLM：不需要管理员权限，也不需要计划任务。
inline constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
inline constexpr wchar_t kValueName[] = L"Tilefold";

// 本进程 exe 的完整路径，已带引号（路径里有空格也不会被拆开）
std::wstring CurrentCommand();

// Run 值里现在登记的命令行；没登记时返回空串
std::wstring RegisteredCommand();

// 是否已登记开机自启
bool IsEnabled();

bool Enable();
bool Disable();

// 启动时调用：已启用但登记的是旧路径（exe 被挪过 / 重编译改了输出目录）就重写成当前路径。
// 返回 true 表示这次真的改了注册表。
bool SyncIfEnabled();

}  // namespace tf::core::autostart
