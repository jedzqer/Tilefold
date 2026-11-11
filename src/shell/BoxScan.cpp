#include "BoxScan.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <utility>

#include "core/Log.h"
#include "core/Paths.h"

namespace fs = std::filesystem;

namespace {

bool NameLess(const std::wstring& a, const std::wstring& b) {
    return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                  static_cast<int>(b.size()), TRUE) == CSTR_LESS_THAN;
}

}  // namespace

namespace tf::box {

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
