#include "core/Categories.h"

#include <windows.h>

#include <filesystem>
#include <string_view>

#include "core/Paths.h"

namespace {

std::wstring FromUtf8(std::string_view s) {
    if (s.empty()) return {};
    const int need = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), need);
    return out;
}

// 文件很小（几十行），一次读进来足够
bool ReadWholeFile(const std::wstring& path, std::string& out) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(h, &size) || size.QuadPart <= 0 || size.QuadPart > (1 << 20)) {
        ::CloseHandle(h);
        return false;
    }
    out.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ::ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &read, nullptr);
    ::CloseHandle(h);
    if (!ok) return false;
    out.resize(read);
    return true;
}

// 首次运行时留一份带注释的空模板：这个功能不写文档也得能自己被发现
void WriteTemplateIfMissing(const std::wstring& path) {
    if (::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    const std::string text =
        "# Tilefold 桌面分组映射（本文件只存在你自己的 data\\ 目录里，不会进仓库）\n"
        "#\n"
        "# 格式：分组名<TAB>桌面文件名（含扩展名，不区分大小写）\n"
        "# 分组名 = data\\box 下的子目录名；目录不存在时那一项不会被归进去。\n"
        "#\n"
        "# 例子：\n"
        "#   某个分组\t某个快捷方式.lnk\n";
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    ::WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    ::CloseHandle(h);
}

std::wstring_view Trim(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t' || s.front() == L'\r')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t' || s.back() == L'\r')) {
        s.remove_suffix(1);
    }
    return s;
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    return ::CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
                                  static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

}  // namespace

namespace tf::cfg {

std::wstring CategoriesPath() {
    return tf::paths::DataDir() + L"\\categories.tsv";
}

std::vector<Category> ParseCategories(const std::wstring& text) {
    std::vector<Category> out;
    std::wstring_view rest = text;
    // 去掉 UTF-8 BOM 解出来的 U+FEFF
    if (!rest.empty() && rest.front() == 0xFEFF) rest.remove_prefix(1);

    while (!rest.empty()) {
        const std::size_t newline = rest.find(L'\n');
        std::wstring_view line = newline == std::wstring_view::npos ? rest : rest.substr(0, newline);
        rest = newline == std::wstring_view::npos ? std::wstring_view{} : rest.substr(newline + 1);
        line = Trim(line);
        if (line.empty() || line.front() == L'#') continue;

        const std::size_t tab = line.find(L'\t');
        if (tab == std::wstring_view::npos) continue;  // 没有分隔符的行直接跳过，不猜
        const std::wstring_view group = Trim(line.substr(0, tab));
        const std::wstring_view filename = Trim(line.substr(tab + 1));
        if (group.empty() || filename.empty()) continue;

        out.push_back(Category{std::wstring(filename), std::wstring(group)});
    }
    return out;
}

std::vector<Category> LoadCategories() {
    const std::wstring path = CategoriesPath();
    WriteTemplateIfMissing(path);

    std::string raw;
    if (!ReadWholeFile(path, raw)) return {};
    return ParseCategories(FromUtf8(raw));
}

const std::wstring* GroupForPath(const std::vector<Category>& categories, const std::wstring& path) {
    if (categories.empty() || path.empty()) return nullptr;
    const std::wstring filename = std::filesystem::path(path).filename().wstring();
    for (const Category& category : categories) {
        if (EqualsNoCase(category.filename, filename)) return &category.group;
    }
    return nullptr;
}

}  // namespace tf::cfg
