// 同目录相对包含：这样即使编辑器侧拿不到编译数据库（没有 -Isrc），本文件
// 也一定能找到自己的头文件，不会因为一条 include 失败连带报出一堆 std:: 假错。
#include "Log.h"
#include "Paths.h"

#include <windows.h>

#include <mutex>
#include <string>

namespace {

std::mutex g_mutex;
std::wstring g_logPath;
bool g_ready = false;

constexpr long long kMaxBytes = 2LL * 1024 * 1024;

std::string ToUtf8(std::wstring_view w) {
    if (w.empty()) return {};
    const int need = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), need, nullptr, nullptr);
    return out;
}

void AppendUtf8(const std::string& utf8) {
    HANDLE h = ::CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    ::WriteFile(h, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    ::CloseHandle(h);
}

}  // namespace

namespace tf::log {

void Initialize() {
    std::lock_guard lock(g_mutex);
    if (g_ready) return;

    const std::wstring dir = tf::paths::LogDir();
    tf::paths::EnsureDir(dir);
    g_logPath = dir + L"\\tilefold.log";

    // 简单滚动：超过 2MB 就把旧文件改名
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (::GetFileAttributesExW(g_logPath.c_str(), GetFileExInfoStandard, &fad) &&
        fad.nFileSizeHigh == 0 && fad.nFileSizeLow > static_cast<DWORD>(kMaxBytes)) {
        const std::wstring old = dir + L"\\tilefold.1.log";
        ::DeleteFileW(old.c_str());
        ::MoveFileW(g_logPath.c_str(), old.c_str());
    }

    g_ready = true;
    AppendUtf8("--- Tilefold session start ---\n");
}

void Shutdown() {
    std::lock_guard lock(g_mutex);
    g_ready = false;
}

void Write(std::wstring_view level, std::wstring_view message) {
    SYSTEMTIME st{};
    ::GetLocalTime(&st);

    wchar_t line[4096]{};
    const int n = ::swprintf_s(line, L"[%02u:%02u:%02u.%03u] %.*s %.*s\n",
                               st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                               static_cast<int>(level.size()), level.data(),
                               static_cast<int>(message.size()), message.data());

    ::OutputDebugStringW(n > 0 ? line : L"[log] (format failed)\n");

    std::lock_guard lock(g_mutex);
    if (!g_ready) return;
    if (n > 0) AppendUtf8(ToUtf8(std::wstring_view(line, static_cast<size_t>(n))));
}

}  // namespace tf::log
