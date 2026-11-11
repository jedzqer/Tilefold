#pragma once

#include <string>

namespace tf::box {

// 「新建收纳文件夹」用的默认名字
inline constexpr wchar_t kDefaultGroupName[] = L"新文件夹";

// 用户输入的名字 -> 干净的分组名（同时作为 layout.tsv 的键）：
// 去掉 \ / : * ? " < > | 和控制字符，压缩连续空白，去掉结尾的空格和点，
// 超长截断；洗完之后什么都不剩就退回 kDefaultGroupName。
// 设备名（CON / NUL / COM1 …）前面补一个下划线，避免以后导出成文件名时踩雷。
std::wstring SanitizeGroupName(const std::wstring& desired);

}  // namespace tf::box
