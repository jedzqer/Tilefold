#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tf::cfg {

// 一个桌面部件在网格中的位置与跨度。
//   kind = "folder" -> group 是 data\box 的一级子目录名
//   kind = "icon"   -> group + item 指向一个被拖出文件夹的独立图标
//   kind = "order"  -> group + item 记录“文件夹内图标的显示顺序”，
//                      col = 顺序号（0 开始）；row/span 无意义，固定写 0/1/1
//                      不参与占位计算（CellFreeInLayout / NormalizeLayout 都要跳过它）
struct Entry {
    std::wstring kind;
    std::wstring group;
    std::wstring item;
    int col = 0;
    int row = 0;
    int spanCols = 2;
    int spanRows = 2;
    std::wstring monitor;  // Stable display interface ID; empty in legacy layouts.
};

// 上一次真正用过的网格。每次启动都按工作区重新挑列数的话，
// 工作区尺寸略变（任务栏自动隐藏、缩放微调）就会让列数 20↔19 来回翻、
// 格距从 76.8 跳到 80.8；条目存的格子坐标跟着落到别处，
// NormalizeLayout() 再把撞车的位置永久改写 —— 用户看到的就是“重启后排布被重置”。
// 所以把算出来的列数和格距一起存下来，下次启动按它还原。
struct GridState {
    int cols = 0;        // 0 = 没有记录
    float pitch = 0.0f;  // 单格边长（DIP）
    int workW = 0;       // 记录时的逻辑工作区尺寸（DIP，四舍五入）
    int workH = 0;
    bool valid = false;
};

struct MonitorGrid {
    std::wstring monitor;
    GridState grid;
};

struct Layout {
    int cols = 0;  // 桌面网格列数；0 = 自动（按原生图标间距反推）
    std::vector<Entry> entries;
    std::vector<MonitorGrid> monitors;
    GridState grid;  // 上次实际生效的网格（启动时还原用）
};

// <数据根>\layout.tsv（开发树是 data\，安装版是 %LOCALAPPDATA%\Tilefold\data\）
std::wstring LayoutPath();

// 启动预检并持有目录锁，防止外部进程同时操作布局数据。
bool AcquireLayoutFile();
void ReleaseLayoutFile();

// 位置以“格子坐标”保存，不存像素：换分辨率、换显示器都不会跑出屏幕
Layout ParseLayoutBytes(const std::string& bytes, const std::wstring& label);
Layout LoadLayout();
void SaveLayout(const Layout& layout);

const Entry* FindEntry(const Layout& layout, std::wstring_view kind, std::wstring_view group,
                       std::wstring_view item = {});
void SetEntry(Layout& layout, Entry entry);
bool RemoveEntry(Layout& layout, std::wstring_view kind, std::wstring_view group,
                 std::wstring_view item = {});

// 该条目是否已被拖出到桌面（是则文件夹里不再显示它）
bool IsExtracted(const Layout& layout, std::wstring_view group, std::wstring_view item);

// 该文件夹里条目的显示顺序（displayName -> 序号）。没记录过的名字不在里面。
// 用户能在放大面板里拖动排序（磁盘上的顺序不能改，所以顺序必须自己记）。
std::vector<std::pair<std::wstring, int>> LoadOrder(const Layout& layout,
                                                   std::wstring_view group);
// 整组重写顺序：先删掉这组的旧 order 条目，再按 names 的顺序写新的
void SetOrder(Layout& layout, std::wstring_view group, const std::vector<std::wstring>& names);

}  // namespace tf::cfg
