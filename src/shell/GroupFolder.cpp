#include "shell/GroupFolder.h"

#include <windows.h>

#include <cwchar>
#include <cwctype>

#include "core/ReservedNames.h"

namespace {

constexpr wchar_t kForbidden[] = L"\\/:*?\"<>|";
// 桌面文件夹的标题就那么宽，再长的名字只能截，没别的办法
constexpr std::size_t kMaxNameLength = 48;

bool IsSpaceLike(wchar_t ch) { return ch == L' ' || ch == L'\t' || ch == 0x3000; }

// "CON"、"nul.txt"、"COM7" 这些是 DOS 设备名，加了扩展名也一样建不出来
bool IsReservedDeviceName(const std::wstring& name) {
    static const wchar_t* const kDevices[] = {
        L"CON",  L"PRN",  L"AUX",  L"NUL",  L"COM1", L"COM2", L"COM3", L"COM4", L"COM5",
        L"COM6", L"COM7", L"COM8", L"COM9", L"LPT1", L"LPT2", L"LPT3", L"LPT4", L"LPT5",
        L"LPT6", L"LPT7", L"LPT8", L"LPT9"};
    std::wstring head = name.substr(0, name.find(L'.'));
    if (head.empty()) return false;
    for (wchar_t& ch : head) ch = static_cast<wchar_t>(::towupper(ch));
    for (const wchar_t* device : kDevices) {
        if (head == device) return true;
    }
    return false;
}

}  // namespace

namespace tf::box {

std::wstring SanitizeGroupName(const std::wstring& desired) {
    std::wstring out;
    out.reserve(desired.size());
    bool lastWasSpace = false;
    for (const wchar_t ch : desired) {
        if (ch < 0x20 || ch == 0x7F) continue;                 // 控制字符
        if (std::wcschr(kForbidden, ch) != nullptr) continue;  // 文件名里不允许
        if (IsSpaceLike(ch)) {
            if (out.empty() || lastWasSpace) continue;  // 连着的空白压成一个
            out.push_back(L' ');
            lastWasSpace = true;
            continue;
        }
        lastWasSpace = false;
        out.push_back(ch);
    }
    auto trimTail = [&out] {
        while (!out.empty() && (out.back() == L' ' || out.back() == L'.')) out.pop_back();
    };
    trimTail();
    if (out.size() > kMaxNameLength) {
        out.resize(kMaxNameLength);
        trimTail();
    }
    if (out.empty()) out = kDefaultGroupName;
    if (IsReservedDeviceName(out)) out.insert(out.begin(), L'_');
    // "system" / "__desktop_items__" 在 layout.tsv 里是系统槽位（此电脑/回收站、未分组
    // 桌面条目），分组叫这个名字会让删除分组连累它们，所以同设备名一样补个下划线。
    if (tf::cfg::IsReservedGroupName(out)) out.insert(out.begin(), L'_');
    return out;
}

}  // namespace tf::box
