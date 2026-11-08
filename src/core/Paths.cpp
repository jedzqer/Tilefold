#include "Paths.h"

#include <windows.h>
#include <shlobj.h>

#include <vector>

namespace {

// 往上找几层：build\Debug\Tilefold.exe -> 项目根要跳 3 层，留一点余量
constexpr int kMaxUp = 4;

std::wstring FullExePath() {
    std::vector<wchar_t> buf(MAX_PATH * 2, L'\0');
    for (int attempt = 0; attempt < 2; ++attempt) {
        const DWORD n = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) return std::wstring(buf.data(), n);
        buf.assign(n + 1, L'\0');  // 路径比缓冲区长，按返回长度重来一次
    }
    return {};
}

std::wstring ExeDir() {
    const std::wstring exe = FullExePath();
    if (exe.empty()) return L".";
    const size_t cut = exe.find_last_of(L'\\');
    return cut == std::wstring::npos ? L"." : exe.substr(0, cut);
}

bool FileExists(const std::wstring& path) {
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

// 大小写不敏感的前缀判断：Windows 路径不区分大小写，%LOCALAPPDATA% 可能拼成 Local / local
bool StartsWithNoCase(const std::wstring& s, const std::wstring& prefix) {
    if (prefix.empty() || s.size() < prefix.size()) return false;
    return ::CompareStringOrdinal(s.c_str(), static_cast<int>(prefix.size()), prefix.c_str(),
                                  static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

// 取已知文件夹路径；
// create 为 true 时由系统建目录。取不到返回空串。
std::wstring KnownFolder(REFKNOWNFOLDERID id, bool create) {
    PWSTR raw = nullptr;
    std::wstring result;
    const DWORD flags = create ? KF_FLAG_CREATE : KF_FLAG_DEFAULT;
    if (SUCCEEDED(::SHGetKnownFolderPath(id, flags, nullptr, &raw)) && raw) result = raw;
    if (raw) ::CoTaskMemFree(raw);
    return result;
}

// 安装版的数据宿主：%LOCALAPPDATA%\Tilefold。
// 不能留在安装目录里 —— MSI 卸载/升级会把安装目录连同 data\ 一起删掉，
// 布局和 data\box 里的快捷方式就没了。取不到就返回空串，由调用方兜底。
std::wstring LocalAppDataRoot() {
    const std::wstring base = KnownFolder(FOLDERID_LocalAppData, true);
    return base.empty() ? std::wstring() : base + L"\\Tilefold";
}

// dir 等于 base，或者 base 是 dir 的上级目录
bool SameOrUnder(const std::wstring& dir, const std::wstring& base) {
    if (base.empty()) return false;
    if (dir.size() == base.size()) return StartsWithNoCase(dir, base);
    return StartsWithNoCase(dir, base + L"\\");
}

// MSI 写的安装标记：HKCU\Software\Tilefold\Installer\InstallLocation
// （见 cmake/tilefold-wix-patch.xml.in）。便携 ZIP 解压出来的目录里没有这条值。
std::wstring InstalledLocation() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Tilefold\\Installer", 0, KEY_QUERY_VALUE,
                        &key) != ERROR_SUCCESS) {
        return {};
    }
    wchar_t buf[MAX_PATH * 2]{};
    DWORD type = 0;
    DWORD bytes = static_cast<DWORD>(sizeof(buf) - sizeof(wchar_t));
    const LONG r = ::RegQueryValueExW(key, L"InstallLocation", nullptr, &type,
                                      reinterpret_cast<LPBYTE>(buf), &bytes);
    ::RegCloseKey(key);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        return {};
    }
    std::wstring value(buf, bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    // MSI 写进来的值带尾反斜杠（"...\Tilefold 1.0.0\"），去掉以后再拼 "\\" 才不会变成双反斜杠
    while (!value.empty() && (value.back() == L'\\' || value.back() == L'/')) value.pop_back();
    return value;
}

// 这个目录是不是“装出来的”（而不是用户自己解压的一个文件夹）。
// 两条判据：MSI 写的 InstallLocation 盖住了 exe 目录；或者 exe 就在 Program Files /
// %LOCALAPPDATA%\Programs 这类安装位置里。卸载和升级会把安装目录整个删掉，
// 所以这些位置的 data\ 不能留在里面。
bool IsInstallDir(const std::wstring& dir) {
    if (SameOrUnder(dir, InstalledLocation())) return true;
    if (SameOrUnder(dir, KnownFolder(FOLDERID_LocalAppData, false) + L"\\Programs")) return true;
    if (SameOrUnder(dir, KnownFolder(FOLDERID_ProgramFiles, false))) return true;
    if (SameOrUnder(dir, KnownFolder(FOLDERID_ProgramFilesX86, false))) return true;
    return false;
}

// 从 exe 目录往上找 CMakeLists.txt。用“有没有 CMakeLists.txt”当项目根的判据，
// 是为了让 build\Debug 里的 exe 把数据写回项目根，而不是写进 build（清理 build 就丢）。
std::wstring ResolveRoot() {
    wchar_t env[MAX_PATH * 2]{};
    const DWORD n = ::GetEnvironmentVariableW(L"TF_HOME", env, static_cast<DWORD>(std::size(env)));
    if (n > 0 && n < std::size(env)) return std::wstring(env, n);

    const std::wstring exeDir = ExeDir();
    std::wstring probe = exeDir;
    for (int up = 0; up < kMaxUp; ++up) {
        if (FileExists(probe + L"\\CMakeLists.txt")) return probe;
        const size_t cut = probe.find_last_of(L'\\');
        if (cut == std::wstring::npos || cut < 2) break;  // 到 "C:\" 就别再往上
        probe.resize(cut);
    }
    // 不在开发树里，分两种情况：
    //   安装版（MSI 装到 %LOCALAPPDATA%\Tilefold <版本>\...）：数据放 %LOCALAPPDATA%\Tilefold，
    //     否则卸载/升级删安装目录时会把布局和 box 里的快捷方式一起带走；
    //   便携 ZIP：数据就放在 exe 旁边，跟着解压出来的目录走。
    if (IsInstallDir(exeDir)) {
        const std::wstring local = LocalAppDataRoot();
        if (!local.empty()) return local;
    }
    return exeDir;
}

const std::wstring& Root() {
    static const std::wstring root = ResolveRoot();
    return root;
}

}  // namespace

namespace tf::paths {

std::wstring AppRoot() { return Root(); }

std::wstring ExePath() { return FullExePath(); }

std::wstring DataDir() { return Root() + L"\\data"; }

std::wstring BoxDir() { return DataDir() + L"\\box"; }

std::wstring LayoutPath() { return DataDir() + L"\\layout.tsv"; }

std::wstring LogDir() { return DataDir() + L"\\logs"; }

bool EnsureDir(const std::wstring& dir) {
    const int r = ::SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_FILE_EXISTS || r == ERROR_ALREADY_EXISTS;
}

}  // namespace tf::paths
