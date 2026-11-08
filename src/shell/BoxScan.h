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

// 一个分组 = 分组根目录下的一个一级子目录 = 桌面上的一个大文件夹 widget
struct Group {
    std::wstring name;  // 子目录名，同时作为布局的键
    std::wstring dir;   // 完整路径
    std::vector<Item> items;
};

// 扫描 root 下的一级子目录，每个子目录产出一个 Group。
// 排序规则：快捷方式(.lnk)优先，其次按显示名不区分大小写。
// 跳过隐藏/系统文件与以 '.' 开头的条目。
std::vector<Group> ScanRoot(const std::wstring& root);
std::vector<Item> ScanDesktopItems();

// 分组根目录：默认 <项目>\data\box，可用环境变量 TF_BOX_ROOT 覆盖
std::wstring DefaultRoot();

}  // namespace tf::box
