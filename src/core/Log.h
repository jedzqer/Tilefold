#pragma once

#include <windows.h>

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace tf::log {

// 日志级别：低于当前阈值的日志在调用点就被丢掉（不格式化、不开文件）。
// 默认 Info；环境变量 TF_LOG_LEVEL=debug|info|warn|error 可覆盖。
enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3 };

void SetLevel(Level level);
bool Enabled(Level level);

// 初始化日志：创建 <数据根>\logs\tilefold.log
// （开发树是 data\logs，安装版是 %LOCALAPPDATA%\Tilefold\data\logs）
void Initialize();
void Shutdown();

void Write(std::wstring_view level, std::wstring_view message);

inline void Debug(std::wstring_view m) {
    if (Enabled(Level::Debug)) Write(L"DEBUG", m);
}
inline void Info(std::wstring_view m) {
    if (Enabled(Level::Info)) Write(L"INFO ", m);
}
inline void Warn(std::wstring_view m) {
    if (Enabled(Level::Warn)) Write(L"WARN ", m);
}
inline void Error(std::wstring_view m) {
    if (Enabled(Level::Error)) Write(L"ERROR", m);
}

template <class... Args>
void Debugf(std::wformat_string<Args...> fmt, Args&&... args) {
    if (Enabled(Level::Debug)) Debug(std::format(fmt, std::forward<Args>(args)...));
}

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
