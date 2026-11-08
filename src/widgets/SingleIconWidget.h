#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <wrl/client.h>

#include <functional>
#include <memory>
#include <string>

#include "shell/BoxScan.h"
#include "widgets/WidgetWindow.h"

namespace tf::widget {

// 从大文件夹里拖出来的独立图标，占 1×1 个网格。
// 不加玻璃底：看起来就像桌面图标本身，只是由我们来排布。
// 图标在格子里居中，名字画在格子下方那一缝里（和大文件夹的名字同一个规则）。
class SingleIconWidget {
public:
    SingleIconWidget(std::wstring groupName, box::Item item, RECT boundsPx);
    ~SingleIconWidget();

    SingleIconWidget(const SingleIconWidget&) = delete;
    SingleIconWidget& operator=(const SingleIconWidget&) = delete;

    bool Show(HWND host, HINSTANCE instance);
    void Close();

    HWND Hwnd() const;
    RECT BoundsPx() const { return window_ ? window_->BoundsPx() : RECT{}; }
    void Invalidate();
    void SetBoundsPx(const RECT& boundsPx);
    // 格子下方那一条名字带（DIP）——和文件夹用的是同一个值（= 2×margin）
    void SetLabelBandDip(float dip);

    const std::wstring& GroupName() const { return groupName_; }
    const box::Item& Item() const { return item_; }

    void SetMovedHandler(std::function<void()> fn) { onMoved_ = std::move(fn); }
    // 右键菜单选了"放回文件夹"
    void SetReturnHandler(std::function<void()> fn) { onReturn_ = std::move(fn); }
    // 右键菜单额外补一条「打开」。只对命名空间条目（此电脑/回收站）有意义：
    // 它们的原版菜单第一项是「打开(资源管理器)」/「清空回收站」，没有双击那种
    // 直接打开；普通文件/快捷方式系统菜单自带「打开」，不用重复加。
    void SetOpenExtra(bool on) { openExtra_ = on; }
    // 右键菜单里执行了系统动词（删除/重命名/剪切…会真改磁盘），App 要重扫
    void SetShellVerbHandler(std::function<void()> fn) { onShellVerb_ = std::move(fn); }

private:
    void OnPaint(ID2D1DeviceContext* ctx, D2D1_SIZE_F size);
    void OnClick();
    void OnRightClick();
    void ShowMenu();

    // 悬停/按下动画：只一个图标，所以只要三个数
    void OnHover(float xDip, float yDip);
    void OnHold(bool down);
    bool OnAnim(float dt);

    std::wstring groupName_;
    box::Item item_;
    std::unique_ptr<WidgetWindow> window_;
    float labelBandDip_ = 0.0f;
    bool openExtra_ = false;
    std::function<void()> onMoved_;
    std::function<void()> onReturn_;
    std::function<void()> onShellVerb_;

    bool hovered_ = false;
    bool pressTarget_ = false;  // 左键现在是否按着（弹簧目标）
    float hover_ = 0.0f;      // 0..1 悬停进度
    float press_ = 0.0f;      // 0..1 按下程度
    float pressVel_ = 0.0f;   // 弹簧速度
};

}  // namespace tf::widget
