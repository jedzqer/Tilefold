#include "BoxScan.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <system_error>
#include <utility>

#include "core/Log.h"
#include "core/Paths.h"

namespace fs = std::filesystem;

namespace {

bool IsHiddenOrSystem(const fs::path& p) {
    const DWORD attrs = ::GetFileAttributesW(p.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return true;
    return (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
}

bool EndsWithNoCase(const std::wstring& s, const wchar_t* suffix) {
    const size_t n = ::lstrlenW(suffix);
    if (s.size() < n) return false;
    return ::CompareStringOrdinal(s.c_str() + (s.size() - n), static_cast<int>(n), suffix,
                                  static_cast<int>(n), TRUE) == CSTR_EQUAL;
}

bool NameLess(const std::wstring& a, const std::wstring& b) {
    return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                  static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
}

}  // namespace

namespace tf::box {

std::wstring DefaultRoot() {
    // 环境变量优先：可以把分组目录放到任意位置（比如放到网盘同步目录）
    wchar_t buf[MAX_PATH * 2]{};
    const DWORD n = ::GetEnvironmentVariableW(L"TF_BOX_ROOT", buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf)) return std::wstring(buf, n);
    // 默认：<项目>\data\box，跟着程序走，不污染用户目录
    return tf::paths::BoxDir();
}

std::vector<Group> ScanRoot(const std::wstring& root) {
    std::vector<Group> groups;

    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        log::Warnf(L"分组根目录不存在: {}", root);
        return groups;
    }

    for (const auto& entry : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        const fs::path& dirPath = entry.path();
        if (!entry.is_directory(ec)) continue;
        if (IsHiddenOrSystem(dirPath)) continue;

        Group g;
        g.name = dirPath.filename().wstring();
        g.dir = dirPath.wstring();
        if (g.name.empty() || g.name[0] == L'.') continue;

        for (const auto& child : fs::directory_iterator(dirPath, fs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            const fs::path& p = child.path();
            const std::wstring filename = p.filename().wstring();
            if (filename.empty() || filename[0] == L'.') continue;
            if (IsHiddenOrSystem(p)) continue;

            Item it;
            it.path = p.wstring();
            it.isDir = child.is_directory(ec);
            // 显示名去掉最后一级扩展名：example-app.lnk -> example-app
            it.displayName = p.stem().wstring();
            if (it.displayName.empty()) it.displayName = filename;
            g.items.push_back(std::move(it));
        }

        std::stable_sort(g.items.begin(), g.items.end(), [](const Item& a, const Item& b) {
            const bool la = EndsWithNoCase(a.path, L".lnk");
            const bool lb = EndsWithNoCase(b.path, L".lnk");
            if (la != lb) return la;
            return NameLess(a.displayName, b.displayName);
        });

        log::Infof(L"分组 [{}] 共 {} 项", g.name, g.items.size());
        groups.push_back(std::move(g));
    }

    std::stable_sort(groups.begin(), groups.end(),
                     [](const Group& a, const Group& b) { return NameLess(a.name, b.name); });

    log::Infof(L"分组扫描完成：{} 个分组，共 {} 项", groups.size(),
               [&] {
                   size_t n = 0;
                   for (const auto& g : groups) n += g.items.size();
                   return n;
               }());
    return groups;
}

std::vector<Item> ScanDesktopItems() {
    std::vector<Item> items;
    // 已知文件夹在 Paths 里缓存过：看门狗每 2.5s 扫一遍，
    // 每轮重新 SHGetKnownFolderPath 既慢又白读注册表
    const std::vector<std::wstring>& roots = tf::paths::DesktopDirs();
    if (roots.empty()) log::Warn(L"桌面扫描：一个根目录都没解到");

    for (const std::wstring& root : roots) {
        const std::size_t before = items.size();
        // 直接用 FindFirstFileW：属性就在 WIN32_FIND_DATAW 里，
        // 不用像 std::filesystem 那样每项再补一次 GetFileAttributesW
        WIN32_FIND_DATAW fd{};
        HANDLE h = ::FindFirstFileW((root + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) {
            log::Warnf(L"桌面扫描：无法枚举 {} err={}", root, ::GetLastError());
            continue;
        }
        do {
            const std::wstring filename = fd.cFileName;
            if (filename.empty() || filename[0] == L'.') continue;
            if ((fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0) continue;
            Item item;
            item.path = root + L"\\" + filename;
            item.displayName = fs::path(filename).stem().wstring();
            if (item.displayName.empty()) item.displayName = filename;
            item.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            item.desktopSource = true;
            items.push_back(std::move(item));
        } while (::FindNextFileW(h, &fd));
        ::FindClose(h);
        // 逐条明细降到 debug：这行以前每 2.5s 就往磁盘写一次
        log::Debugf(L"桌面扫描：{} -> {} 项", root, items.size() - before);
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return NameLess(a.displayName, b.displayName);
    });
    return items;
}

}  // namespace tf::box
