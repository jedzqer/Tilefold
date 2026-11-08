#pragma once

#include <windows.h>

#include <cstddef>
#include <memory>
#include <vector>

#include "core/Grid.h"
#include "core/MonitorLayout.h"
#include "core/Layout.h"
#include "core/Tray.h"
#include "core/Settings.h"
#include "core/SettingsWindow.h"
#include "shell/BoxScan.h"
#include "shell/DesktopHost.h"
#include "widgets/BigFolderWidget.h"
#include "widgets/ExpandedFolder.h"
#include "widgets/SingleIconWidget.h"

namespace tf::core {

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    bool Initialize();
    int Run();

    // 低级鼠标钩子把滚轮事件转过来：返回 true 表示这次滚轮被某个文件夹吃掉了
    bool HandleWheelAt(POINT screenPx, float notches);
    // 低级鼠标钩子把桌面空白处的右键转过来：返回 true 表示这次右键已被吃掉
    // （桌面背景菜单由我们自己弹，原因见 shell/DesktopContextMenu.h 里 ShowBackgroundMenu 的注释）
    bool HandleDesktopRightClickAt(POINT screenPx);
    // 这次桌面右键是不是归我们处理（吞掉事件、自己弹菜单）。
    // 钩子在 WM_RBUTTONDOWN 时先问一次，为 true 就吞掉按键；
    // 否则一律放行，交给 Explorer。
    bool CanTakeDesktopRightClick(POINT screenPx);
    // 低级鼠标钩子把桌面空白处的「中键」转过来：新建收纳文件夹。
    // 中键在空桌面上系统什么都不做，所以只观测、不吞事件。
    bool HandleDesktopNewBoxGestureAt(POINT screenPx);

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    bool CreateControlWindow();
    void BuildDesktopLayer();
    void TeardownDesktopLayer();
    void RestartDesktopLayer(const wchar_t* reason);

    // ---- 网格 ----
    void RefreshGrid();
    bool PlaceEntry(cfg::Entry& entry, RECT boundsPx, int spanCols, int spanRows);
    // ---- 事件 ----
    void OnFolderMoved(widget::BigFolderWidget* folder);
    void OnFolderResize(widget::BigFolderWidget* folder, int spanCols, int spanRows);
    void OnItemDragOut(widget::BigFolderWidget* folder, std::size_t index, POINT dropPx);
    // 右键菜单「删除文件夹」：widget 只能发请求（不能在它自己的窗口过程栈里被销毁）
    void OnFolderDeleteRequested(widget::BigFolderWidget* folder);
    // 真正干活：把图标撕回桌面目录、删掉空目录、清掉布局记录、重扫
    void OnFolderDeleteConfirmed();
    // 点小文件夹的空处 / “更多”格 -> 放大成居中面板
    void OnFolderExpand(widget::BigFolderWidget* folder);
    void CloseExpandedFolder();
    // 面板里拖动排序松手：order 是整组的新顺序（displayName）
    void OnExpandedReordered(const std::wstring& group, std::vector<std::wstring> order);
    // 把 layout.tsv 里记的顺序套到扫描结果上（磁盘顺序不能改，所以顺序只能自己记）
    void ApplySavedOrder();
    void OnIconMoved(widget::SingleIconWidget* icon);
    void OnDesktopIconMoved(widget::SingleIconWidget* icon);
    void OnShellIconMoved(widget::SingleIconWidget* icon);
    void OnIconReturn(widget::SingleIconWidget* icon);
    void RelayoutAll();
    // 桌面右键菜单「新建收纳文件夹」：弹输入框、建目录、重扫桌面层
    void OnNewBoxRequested();
    // 桌面空白处右键：真弹菜单（钩子只投递请求，见 kDesktopMenuMsg）
    void OnDesktopMenuRequested(POINT screenPx);
    // 拖到文件夹上松手要把图标收回去，所以可能需要重新读盘
    void RescanGroups();
    bool MoveIconIntoGroup(const std::wstring& srcPath, const std::wstring& groupName);
    // 新搬进分组的图标默认排到末尾：把“该组当前显示顺序 + 新条目”整份写成 order 记录，
    // 不然它会跟磁盘名字排序、其他未记录条目抢位置，看起来像“随机插在中间”
    void PutItemAtEndOfOrder(const std::wstring& groupName, const std::wstring& itemName);
    void PostRelayout();
    // 右键菜单里选了系统动词：磁盘可能已经变了（删了/改名了/剪切走了/新建了），
    // 投递到消息循环里再重扫。不能直接重建 —— 现在还在弹菜单那个 widget 的
    // 窗口过程栈上，重建等于在它的成员函数里删掉它自己
    void PostShellVerbRescan();
    // 重扫磁盘，并把 layout.tsv 里指向已经不存在的东西的记录清理掉。
    // 不清理的话：右键菜单里“删除”了一个图标，它在网格里那格就永远空着（空洞）。
    void PruneMissingEntries();
    // 托盘菜单
    bool OpenSettings(bool firstRun = false);
    void Quit();

    const box::Group* FindGroup(const std::wstring& name) const;
    // 这个屏幕点（物理像素）落在哪个大文件夹上（没有则 nullptr）
    widget::BigFolderWidget* FolderAt(POINT screenPx) const;
    // 这一下是不是“桌面背景”上的鼠标事件：宿主的子窗口、不在任何部件/图标上，
    // 也不在原生桌面图标上（原生图标归 Explorer）
    bool IsDesktopBackgroundPoint(POINT screenPx);

    HINSTANCE instance_ = nullptr;
    HWND controlWnd_ = nullptr;
    HHOOK wheelHook_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    // RegisterWindowMessageW(tf::ipc::kNewBoxMessage)：--new-box 那个短命进程往这里投递
    UINT newBoxMsg_ = 0;
    bool shuttingDown_ = false;
    bool rebuilding_ = false;
    // 桌面背景菜单已经弹出来了：钩子会继续收到鼠标事件，不能再叠第二层
    bool menuOpen_ = false;
    // 待删除的分组名（kDeleteGroupMsg 投递时携带），在消息循环里才真删
    std::wstring pendingDeleteGroup_;

    tf::host::HostInfo host_;
    tf::grid::Spec grid_{};
    std::vector<desktop::Display> displays_;
    cfg::Layout visibleLayout_;
    // 本次算出的网格跟 layout.tsv 里记的不一样（首次运行 / 换过分辨率），需要回写
    bool gridDirty_ = false;

    // 扫描结果缓存：拖出图标/改尺寸时不用重新读盘
    std::vector<box::Group> groups_;
    std::vector<box::Item> desktopItems_;

    std::vector<std::unique_ptr<widget::BigFolderWidget>> folders_;
    std::vector<std::unique_ptr<widget::SingleIconWidget>> icons_;
    // 同一时刻只开一个放大面板；它不属于 folders_，所以 RelayoutAll 不会把它拆掉
    std::unique_ptr<widget::ExpandedFolder> expanded_;

    tf::cfg::Layout layout_;
    Tray tray_;
    cfg::Settings settings_{};
    SettingsWindow settingsWindow_;
};

}  // namespace tf::core
