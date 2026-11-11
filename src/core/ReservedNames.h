#pragma once

#include <cstddef>
#include <string_view>

namespace tf::cfg {

// 布局文件里有两个"不是分组"的系统槽位：
//   kShellGroup   -> 此电脑 / 回收站的图标记录（kind="shell" 全都挂在这个 group 上）
//   kDesktopGroup -> 没有分组的桌面条目（kind="desktop"）
// 它们和用户分组共用同一个 group 字段，所以用户分组一旦叫这两个名字，
// RemoveGroup() 就会把系统槽位的记录一起清掉（回收站/此电脑永久消失）。
// 创建和解析时都挡一道，RemoveGroup 自己也再挡一道。
inline constexpr wchar_t kShellGroup[] = L"system";
inline constexpr wchar_t kDesktopGroup[] = L"__desktop_items__";

// 保留名全是 ASCII，忽略大小写的比较手写就够了（不走 CompareStringOrdinal，
// 免得这个头文件把 windows.h 拖进纯字符串的测试目标）。
inline bool SameNameNoCase(std::wstring_view a, std::wstring_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        wchar_t x = a[i];
        wchar_t y = b[i];
        if (x >= L'A' && x <= L'Z') x = static_cast<wchar_t>(x - L'A' + L'a');
        if (y >= L'A' && y <= L'Z') y = static_cast<wchar_t>(y - L'A' + L'a');
        if (x != y) return false;
    }
    return true;
}

inline bool IsReservedGroupName(std::wstring_view name) {
    return SameNameNoCase(name, kShellGroup) || SameNameNoCase(name, kDesktopGroup);
}

}  // namespace tf::cfg
