#pragma once

#include <string>

namespace tf::box {

// 「新建收纳文件夹」用的默认名字
inline constexpr wchar_t kDefaultGroupName[] = L"新文件夹";

// 用户输入的名字 -> 能当目录名用的名字：
// 去掉 \ / : * ? " < > | 和控制字符，压缩连续空白，去掉结尾的空格和点，
// 超长截断；洗完之后什么都不剩就退回 kDefaultGroupName。
// 设备名（CON / NUL / COM1 …）前面补一个下划线，否则根本建不出来。
std::wstring SanitizeGroupName(const std::wstring& desired);

// 在 root 下挑一个不冲突的名字：新文件夹 / 新文件夹 (2) / 新文件夹 (3)…
// 冲突判断交给文件系统（Windows 上不区分大小写）。
std::wstring UniqueGroupName(const std::wstring& root, const std::wstring& desired);

// 真正建目录：返回建好的完整路径，失败返回空串（原因写日志）。
std::wstring CreateGroupFolder(const std::wstring& root, const std::wstring& desired);

}  // namespace tf::box
