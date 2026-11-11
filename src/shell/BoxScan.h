#pragma once

#include <string>
#include <vector>

namespace tf::box {

// 一个可点击条目：快捷方式、exe、目录都行
struct Item {
    std::wstring path;         // 完整路径，用于启动
    std::wstring displayName;  // 显示名（已去掉扩展名）
    bool isDir = false;
    bool desktopSource = false;
};

// 一个虚拟分组 = 桌面上的一个大文件夹 widget
struct Group {
    std::wstring name;  // 分组名，同时作为布局的键
    std::vector<Item> items;
};

// 扫描用户桌面 + 公共桌面，产出所有未隐藏的条目。
// 排序规则：按显示名不区分大小写。
std::vector<Item> ScanDesktopItems();

}  // namespace tf::box
