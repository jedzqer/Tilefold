#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace tf::shell {

// 一次「原版右键菜单」会话。
//
// 菜单内容不是我们拼的：交给 explorer 自己的 IContextMenu 生成，所以「复制」
// 「剪切」「重命名」「删除」「发送到」「固定到"开始"」「属性」这些系统项全都在，
// 以后 Windows 加新动词也能自动跟上（和右键此电脑/回收站是同一套东西）。
//
//   BuildItem       普通条目：文件、快捷方式、目录、::{GUID} 命名空间条目
//   BuildBackground 文件夹背景 / 桌面背景：查看、排序、刷新、粘贴、新建、属性…
//
// 系统动词占 id [1, 0x6FFF]，我们自己的项走 0xE000 起的高段，永远不会撞。
// 构建失败（路径已失效之类）时菜单依然可用，只是只剩我们自己追加的项，
// 调用方可以用 ShellMenuAvailable() 判断要不要退回自拼菜单。
//
// 典型用法：
//     SystemMenu menu;
//     menu.BuildItem(hwnd, path);
//     menu.AppendExtra(L"放回 xx 文件夹", kMyCommand);
//     const UINT cmd = menu.Track(cursorPos);
//     if (menu.IsShellCommand(cmd)) menu.Invoke(cmd);   // shell 动词，会真改磁盘
//
// 必须在本线程已经 OleInitialize/CoInitializeEx(STA) 之后调用。
class SystemMenu {
public:
    SystemMenu();
    ~SystemMenu();

    SystemMenu(const SystemMenu&) = delete;
    SystemMenu& operator=(const SystemMenu&) = delete;

    bool BuildItem(HWND owner, const std::wstring& parsingName);
    // folderPath 为空表示桌面根（「此电脑」上层那个虚拟桌面）
    bool BuildBackground(HWND owner, const std::wstring& folderPath);

    // 系统菜单是否真的生成出来了（false = 只有我们自己追加的项）
    bool ShellMenuAvailable() const;

    // 底层 HMENU：想在中间插子菜单（比如「宽度」子菜单）时用它自己 AppendMenuW
    HMENU Menu() const;

    void AppendSeparator();
    // 在已有项之后追加一条自有项；第一次调用会自动先补一条分隔线
    void AppendExtra(const std::wstring& label, UINT id);
    // 在系统项「之前」放自有项：必须在 Build*() 之后调用（QueryContextMenu 是往
    // 尾部追加，所以倒着插回去代价太大，这里用 InsertMenuItem）。
    // 用法：Build 完 -> 从最后一条自有项开始往前 PrependExtra，最后 PrependSeparator
    void PrependExtra(const std::wstring& label, UINT id, UINT position);
    void PrependSeparator(UINT position);

    // 弹出菜单，返回选中的命令 id（0 = 用户取消 / 弹不出来）
    UINT Track(POINT screenPt);

    bool IsShellCommand(UINT commandId) const;
    // 执行系统动词；返回 true 表示已交给 shell（可能已经删了/改了磁盘上的东西）
    bool Invoke(UINT commandId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// 一次性便捷函数：弹菜单，选中的自有项通过 *chosenId 返回；
// 选中的系统动词当场执行。extraLabel 为空表示不追加自有项。
// 返回 true 表示菜单真的弹出来了（用户选没选东西都一样）。
bool ShowSystemContextMenu(HWND owner, const std::wstring& parsingName,
                           const std::wstring& extraLabel, UINT* chosenId);

}  // namespace tf::shell
