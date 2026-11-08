#include "core/Autostart.h"

#include <windows.h>

#include <cwchar>
#include <vector>

#include "core/Log.h"

namespace tf::core::autostart {

namespace {

// 读 Run 下的 Tilefold 值，不存在或读失败返回空串
std::wstring ReadValue() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) {
        return {};
    }
    DWORD type = 0;
    DWORD bytes = 0;
    LONG r = ::RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &bytes);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        ::RegCloseKey(key);
        return {};
    }
    std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 1, L'\0');
    r = ::RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &bytes);
    ::RegCloseKey(key);
    if (r != ERROR_SUCCESS) return {};
    return std::wstring(buf.data());
}

// 去掉首尾引号，方便比较
std::wstring Unquote(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'"')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'"')) s.remove_suffix(1);
    return std::wstring(s);
}

}  // namespace

std::wstring CurrentCommand() {
    std::vector<wchar_t> buf(MAX_PATH * 2, L'\0');
    // exe 路径可能超过 MAX_PATH：拿不到就按需要的长度重试一次
    for (int attempt = 0; attempt < 2; ++attempt) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) return L"\"" + std::wstring(buf.data(), n) + L"\"";
        buf.assign(n + 1, L'\0');
    }
    log::Warn(L"GetModuleFileName 拿不到本进程路径，无法登记开机自启");
    return {};
}

std::wstring RegisteredCommand() { return ReadValue(); }

bool IsEnabled() { return !ReadValue().empty(); }

bool Enable() {
    const std::wstring cmd = CurrentCommand();
    if (cmd.empty()) return false;

    HKEY key = nullptr;
    const LONG open = ::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr,
                                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (open != ERROR_SUCCESS) {
        log::Errorf(L"开机自启：打开 Run 键失败 err={}", open);
        return false;
    }
    const LONG r = ::RegSetValueExW(key, kValueName, 0, REG_SZ,
                                    reinterpret_cast<const BYTE*>(cmd.c_str()),
                                    static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    ::RegCloseKey(key);
    if (r != ERROR_SUCCESS) {
        log::Errorf(L"开机自启：写 Run 值失败 err={}", r);
        return false;
    }
    log::Infof(L"开机自启：已启用 -> {}", cmd);
    return true;
}

bool Disable() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        // 键都不存在 = 本来就没登记，算成功
        log::Info(L"开机自启：已关闭（Run 键不存在，无需删除）");
        return true;
    }
    const LONG r = ::RegDeleteValueW(key, kValueName);
    ::RegCloseKey(key);
    if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) {
        log::Errorf(L"开机自启：删除 Run 值失败 err={}", r);
        return false;
    }
    log::Info(L"开机自启：已关闭");
    return true;
}

bool SyncIfEnabled() {
    const std::wstring registered = ReadValue();
    if (registered.empty()) return false;  // 用户没开过，别偷偷加

    const std::wstring cmd = CurrentCommand();
    if (cmd.empty()) return false;
    if (::CompareStringOrdinal(Unquote(registered).c_str(), -1, Unquote(cmd).c_str(), -1, TRUE) ==
        CSTR_EQUAL) {
        return false;  // 路径一致，什么都不用做
    }
    log::Infof(L"开机自启：登记的路径过旧 {} -> {}", registered, cmd);
    return Enable();
}

}  // namespace tf::core::autostart
