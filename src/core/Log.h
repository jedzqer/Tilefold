#pragma once

#include <windows.h>

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace tf::log {

// 初始化日志：创建 <数据根>\logs\tilefold.log
// （开发树是 data\logs，安装版是 %LOCALAPPDATA%\Tilefold\data\logs）
void Initialize();
void Shutdown();

void Write(std::wstring_view level, std::wstring_view message);

inline void Info(std::wstring_view m) { Write(L"INFO ", m); }
inline void Warn(std::wstring_view m) { Write(L"WARN ", m); }
inline void Error(std::wstring_view m) { Write(L"ERROR", m); }

template <class... Args>
void Infof(std::wformat_string<Args...> fmt, Args&&... args) {
    Info(std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Warnf(std::wformat_string<Args...> fmt, Args&&... args) {
    Warn(std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void Errorf(std::wformat_string<Args...> fmt, Args&&... args) {
    Error(std::format(fmt, std::forward<Args>(args)...));
}

// HRESULT 失败时记录并返回 false，用于串起一串 COM 调用
inline bool Check(HRESULT hr, std::wstring_view what) {
    if (FAILED(hr)) {
        Errorf(L"{} 失败: hr=0x{:08X}", what, static_cast<unsigned>(hr));
        return false;
    }
    return true;
}

}  // namespace tf::log
