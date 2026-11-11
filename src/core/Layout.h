#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tf::cfg {

// 一个桌面部件在网格中的位置与跨度。
//   kind = "folder" -> group 是虚拟分组名
//   kind = "icon"   -> group + item(路径) 指向被拖出分组的独立图标
//   kind = "order"  -> 旧版排序记录，读取后由清理逻辑丢弃
//   kind = "desktop"-> 未分组的桌面条目，item 是完整路径
//   kind = "shell"  -> 系统图标（此电脑/回收站）
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

struct GridState {
    int cols = 0;
    float pitch = 0.0f;
    int workW = 0;
    int workH = 0;
    bool valid = false;
};

struct MonitorGrid {
    std::wstring monitor;
    GridState grid;
};

// 分组中的一个条目。path 是唯一标识，也是启动时使用的完整路径；
// Tilefold 不再复制或移动这个文件。
struct GroupItem {
    std::wstring path;
    int order = 0;
};

// 虚拟收纳分组。分组本身和条目都保存在 layout.tsv，不再有对应的真实目录。
struct GroupState {
    std::wstring name;
    std::vector<GroupItem> items;
};

struct Layout {
    int cols = 0;  // 桌面网格列数；0 = 自动
    std::vector<Entry> entries;
    std::vector<MonitorGrid> monitors;
    GridState grid;
    std::vector<GroupState> groups;
};

// <数据根>\\layout.tsv
std::wstring LayoutPath();

bool AcquireLayoutFile();
void ReleaseLayoutFile();

Layout ParseLayoutBytes(const std::string& bytes, const std::wstring& label);
Layout LoadLayout();

// 保存时要不要顺手轮换历史快照 / .bak。
// 短命进程（--new-box）只想往 layout.tsv 里加一行，不该付出“写快照 + 轮换备份”的代价：
// 那既是多余的磁盘写入，又会让后面真正的那次保存拿不到上一份已知 good 的备份。
enum class Backup { Rotate, Skip };
bool SaveLayout(const Layout& layout, Backup backup = Backup::Rotate);

const Entry* FindEntry(const Layout& layout, std::wstring_view kind, std::wstring_view group,
                       std::wstring_view item = {});
void SetEntry(Layout& layout, Entry entry);
bool RemoveEntry(Layout& layout, std::wstring_view kind, std::wstring_view group,
                 std::wstring_view item = {});

// 虚拟分组的增删查。
const GroupState* FindGroup(const Layout& layout, std::wstring_view name);
GroupState* FindGroup(Layout& layout, std::wstring_view name);
GroupState& EnsureGroup(Layout& layout, std::wstring_view name);
// 保留名（system / __desktop_items__）不会被删：那些记录属于此电脑/回收站/未分组桌面条目，
// 分组只是借用了同一个 group 字段。返回 false 表示拒绝。
bool RemoveGroup(Layout& layout, std::wstring_view name);
const GroupState* GroupForPath(const Layout& layout, std::wstring_view path);
const GroupItem* FindGroupItem(const Layout& layout, std::wstring_view group,
                               std::wstring_view path);

// 向分组添加条目；已存在返回 true。order < 0 表示追加到末尾。
bool AddGroupItem(Layout& layout, std::wstring_view group, std::wstring_view path,
                  int order = -1);
bool RemoveGroupItem(Layout& layout, std::wstring_view group, std::wstring_view path);
bool MoveGroupItem(Layout& layout, std::wstring_view path, std::wstring_view toGroup);
void SetGroupItemOrder(Layout& layout, std::wstring_view group,
                       const std::vector<std::wstring>& paths);

// 该条目是否已被拖出到桌面（是则分组里不再显示它）
bool IsExtracted(const Layout& layout, std::wstring_view group, std::wstring_view item);

}  // namespace tf::cfg
