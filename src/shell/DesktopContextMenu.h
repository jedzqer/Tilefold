#pragma once

#include <windows.h>

#include <string>

namespace tf::desktop_menu {

// 把「新建收纳文件夹」挂进桌面空白处右键菜单：
//
//   HKCU\Software\Classes\DesktopBackground\Shell\Tilefold.NewBox
//     (默认)    = 新建收纳文件夹
//     Icon      = "<exe>",0
//     Position  = Top
//     command\(默认) = "<exe>" --new-box
//
// 写在 HKCU 的 DesktopBackground 下，不需要管理员权限、不碰系统文件，
// 每次启动写一遍（幂等）：exe 换了目录之后菜单指向仍然是对的。
bool Install();

// 退出时把上面那两个键摘掉（尽力而为）。留着的话用户把 Tilefold 目录删了，
// 桌面上会永远挂着一个点了报错的死项。
void Remove();

// 命令行里有没有 --new-box（右键菜单的 command 就是 "<exe>" --new-box）
bool IsNewBoxInvocation();

// --new-box 的入口：主实例在跑就把请求转给它（它知道布局、能刷新桌面层），
// 没在跑就自己弹输入框把目录建出来（下次启动就能看到）。
// 返回进程退出码。
int RunNewBoxCommand();

// 桌面背景右键菜单里被点中的那一项
enum class DesktopCommand {
    None,
    NewBox,     // 新建收纳文件夹
    Refresh,    // 重新扫盘 + 重排
    // 选了一个系统动词（新建文件夹、粘贴、刷新、显示设置…）：磁盘或设置可能变了，
    // App 收到后重扫桌面层
    DesktopChanged,
};

// 由 Tilefold 自己弹的桌面背景右键菜单。
//
// 为什么必须自己弹：Tilefold 全面接管桌面，原生图标层（SHELLDLL_DefView）一律隐藏，
// 而桌面的右键菜单正是 DefView 提供的 —— 隐藏之后桌面空白处右键什么都不会出现
// （连系统的“刷新/显示设置”都没有），我们注册的 DesktopBackground\Shell\Tilefold.NewBox
// 也永远没机会被画出来。所以菜单主体由 SystemMenu 取系统背景菜单拼出来，
// 再在最上面插一条自有的「刷新」。
DesktopCommand ShowBackgroundMenu(HWND owner, POINT screenPt);

}  // namespace tf::desktop_menu
