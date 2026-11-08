#pragma once

#include <string>
#include <vector>

namespace tf::cfg {

// 桌面快捷方式 -> 分组名 的映射表。
//
// 这张表从 data\categories.tsv 读，**不进源码也不进仓库**（data/ 整个在 .gitignore 里）：
// 谁的分组习惯只跟谁自己的 data\ 目录走，公开仓库里不该出现别人的软件清单。
//
// 文件格式（UTF-8，制表符分隔，`#` 开头是注释，空行忽略）：
//
//   # 分组名<TAB>桌面文件名
//   分组甲	示例应用.lnk
//   分组乙	另一个应用.lnk
//
// 键是桌面上的**文件名**（带扩展名，不区分大小写），所以重命名快捷方式后要同步改这里。
// 目标分组不存在时（大文件夹被删了）不会归进去，那个快捷方式退回成 1×1 独立图标。
struct Category {
    std::wstring filename;  // 桌面文件名，含扩展名
    std::wstring group;     // data\box 下的分组目录名
};

// <数据根>\categories.tsv
std::wstring CategoriesPath();

// 读文件并解析；文件不存在时返回空表（= 不自动分类）
std::vector<Category> LoadCategories();

// 纯解析，方便单元测试（一行一条，容忍 BOM / CRLF / 前后空白）
std::vector<Category> ParseCategories(const std::wstring& text);

// 查这条路径对应哪个分组；没配就返回 nullptr
const std::wstring* GroupForPath(const std::vector<Category>& categories, const std::wstring& path);

}  // namespace tf::cfg
