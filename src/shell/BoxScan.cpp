#include "BoxScan.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <system_error>

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
            // 显示名去掉最后一级扩展名：editor.lnk -> Visual Studio Code
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
    std::vector<std::wstring> roots;
    const KNOWNFOLDERID* ids[] = {&FOLDERID_Desktop, &FOLDERID_PublicDesktop};
    for (const KNOWNFOLDERID* id : ids) {
        PWSTR raw = nullptr;
        const HRESULT hr = ::SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &raw);
        if (SUCCEEDED(hr) && raw) {
            roots.emplace_back(raw);
            log::Infof(L"桌面扫描：已知文件夹 -> {}", roots.back());
        } else {
            log::Warnf(L"桌面扫描：已知文件夹解析失败 hr=0x{:08X}", static_cast<unsigned>(hr));
        }
        if (raw) ::CoTaskMemFree(raw);
    }
    if (roots.empty()) log::Warn(L"桌面扫描：一个根目录都没解到");

    std::error_code ec;
    for (const auto& root : roots) {
        size_t before = items.size();
        for (fs::directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const fs::path path = it->path();
            const std::wstring filename = path.filename().wstring();
            if (filename.empty() || filename[0] == L'.' || IsHiddenOrSystem(path)) continue;
            Item item;
            item.path = path.wstring();
            item.displayName = path.stem().wstring();
            if (item.displayName.empty()) item.displayName = filename;
            item.isDir = it->is_directory(ec);
            item.desktopSource = true;
            items.push_back(std::move(item));
        }
        log::Infof(L"桌面扫描：{} -> {} 项{}", root, items.size() - before,
                   ec ? std::format(L"（枚举出错 0x{:08X}）", static_cast<unsigned>(ec.value())) : L"");
        ec.clear();
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return NameLess(a.displayName, b.displayName);
    });
    return items;
}

}  // namespace tf::box
