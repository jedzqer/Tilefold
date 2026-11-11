#pragma once

#include <windows.h>

#include <functional>

namespace tf::core {

// 右下角通知区域图标：右键弹菜单（打开设置 / 退出），双击 = 打开设置。
// 依附在 App 的 Control 窗口上：通知消息由 App::WndProc 转给 HandleMessage。
class Tray {
public:
    static constexpr UINT kCallbackMsg = WM_APP + 0x20;

    ~Tray();

    bool Add(HWND owner);
    void Remove();
    // Explorer 重启后（TaskbarCreated）托盘图标会丢，要重新加
    void Readd();

    // 返回 true = 已处理
    bool HandleMessage(UINT msg, WPARAM wp, LPARAM lp);

    std::function<void()> onSettings;
    std::function<void()> onExit;
    // 托盘菜单里的「新建收纳文件夹」
    // （桌面右键菜单被系统（XAML 新菜单）挡着透传不出来，这里是更稳的入口）
    std::function<void()> onNewBox;
    // 菜单里的「开机自启动」被点了之后回调，参数 = 点完之后的新状态
    std::function<void(bool)> onAutostartChanged;

private:
    void ShowMenu();

    HWND owner_ = nullptr;
    HICON icon_ = nullptr;
    bool added_ = false;
};

}  // namespace tf::core
