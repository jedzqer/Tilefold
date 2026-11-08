#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <dwrite.h>

#include <functional>
#include <string>

#include "render/Surface.h"

namespace tf::widget {

// M0 阶段的最小可用 widget：一个挂在桌面层、可拖动、半透明的圆角面板。
// 后续 BigFolderWidget 会继承或替换它。
class WidgetWindow {
public:
    WidgetWindow(std::wstring title, RECT boundsPx);
    ~WidgetWindow();

    WidgetWindow(const WidgetWindow&) = delete;
    WidgetWindow& operator=(const WidgetWindow&) = delete;

    // host 为 nullptr 时退化为顶层窗口（降级模式）
    bool Show(HWND host, HINSTANCE instance);
    void Close();

    HWND Hwnd() const { return hwnd_; }

    // 外部请求重绘（动画/内容变化时用）
    void Invalidate();

    // 外部绘制/输入回调：大文件夹网格复用本类已验证的 popup + DPI + 拖动逻辑，
    // 避免再写一遍容易出错的窗口模式代码（子窗口模式 alpha 是不透墙纸的）。
    using PaintFn = std::function<void(ID2D1DeviceContext*, D2D1_SIZE_F)>;
    using ClickFn = std::function<void(float xDip, float yDip)>;
    using MovedFn = std::function<void(const RECT& boundsPx)>;
    using RightClickFn = std::function<void(float xDip, float yDip)>;

    // 按下接管：返回 true 表示这次按下由外部处理（例如从文件夹里拖图标出来），
    // 窗口本身不跟着动。之后的移动/松开都转发给外部。
    using PressFn = std::function<bool(float xDip, float yDip)>;
    using MoveFn = std::function<void(float xDip, float yDip)>;
    using ReleaseFn = std::function<void(float xDip, float yDip)>;

    // 滚轮：delta 是滚轮格数（正=向上）。返回 true 表示这次滚轮由外部吃掉了。
    // 鼠标悬停滚动在 Win10+ 上会路由给“光标下面的窗口”，即使它没有焦点，
    // 所以 WS_EX_NOACTIVATE 的面板也能收到 WM_MOUSEWHEEL。
    using WheelFn = std::function<bool(float delta)>;
    // 键：面板模式才有（能激活才收得到键盘）。返回 true 表示已处理。
    using KeyFn = std::function<bool(WPARAM vk)>;

    void SetPaintHandler(PaintFn fn) { paintFn_ = std::move(fn); }
    void SetClickHandler(ClickFn fn) { clickFn_ = std::move(fn); }
    void SetMovedHandler(MovedFn fn) { movedFn_ = std::move(fn); }
    void SetRightClickHandler(RightClickFn fn) { rightClickFn_ = std::move(fn); }
    void SetPressHandler(PressFn fn) { pressFn_ = std::move(fn); }
    void SetMouseMoveHandler(MoveFn fn) { moveFn_ = std::move(fn); }
    void SetReleaseHandler(ReleaseFn fn) { releaseFn_ = std::move(fn); }
    void SetWheelHandler(WheelFn fn) { wheelFn_ = std::move(fn); }
    void SetKeyHandler(KeyFn fn) { keyFn_ = std::move(fn); }
    void SetMouseLeaveHandler(std::function<void()> fn) { leaveFn_ = std::move(fn); }

    // 悬停跟踪：鼠标每次移动都回调（坐标是客户区 DIP），移出窗口时回调 (-1,-1)。
    // 和 MoveFn 分开是因为 MoveFn 只在“按下接管之后”才转发。
    using HoverFn = std::function<void(float xDip, float yDip)>;
    void SetHoverHandler(HoverFn fn) { hoverFn_ = std::move(fn); }

    // 按住状态：左键按下/松开都回调，不管这次按下是窗口拖动还是外部接管。
    // 纯做视觉（按下缩小）用，不参与任何交互决策。down=false 时坐标是 (-1,-1)。
    using HoldFn = std::function<void(bool down, float xDip, float yDip)>;
    void SetHoldHandler(HoldFn fn) { holdFn_ = std::move(fn); }

    // 动画帧：约 16ms 一次，dt 是秒。返回 true = 还要下一帧，返回 false 自动停表。
    // 没有动画时完全不起定时器，闲置零开销。
    using AnimFn = std::function<bool(float dt)>;
    void SetAnimHandler(AnimFn fn) { animFn_ = std::move(fn); }
    // 请求开启动画帧（幂等：已经在跑就什么都不做）
    void RequestAnimation();

    // 放大面板模式：窗口可被激活（要收键盘）、不再强行贴住桌面宿主（要压在普通窗口之上）。
    // 必须在 Show() 之前设置。
    void SetPanelMode(bool on) { panelMode_ = on; }

    float DpiScale() const { return dpiScale_; }
    // Use the real client size so hit-testing and D2D painting share rounding.
    D2D1_SIZE_F SizeDip() const {
        RECT client{};
        if (hwnd_ && ::GetClientRect(hwnd_, &client))
            return D2D1::SizeF(client.right / dpiScale_, client.bottom / dpiScale_);
        return D2D1::SizeF((boundsPx_.right - boundsPx_.left) / dpiScale_,
                           (boundsPx_.bottom - boundsPx_.top) / dpiScale_ + bottomBandDip_);
    }

    const RECT& BoundsPx() const { return boundsPx_; }

    // 外部改几何（网格吸附 / 放大缩小）。不动 Z 序、不触发 MovedFn。
    void SetBoundsPx(const RECT& boundsPx);

    // 卡片下方额外留出的“标签带”（DIP）。
    //
    // 窗口实际高度 = boundsPx_ 高 + band，多出来的一条透明带正好落在
    // 相邻格子的缝里（= 2×margin），用来写文字——文字因此完全不占卡片面积，
    // 又和格子的节奏对齐。boundsPx_ 仍然只表示卡片本身，所以网格吸附、
    // 重叠判定、拖动位移都不用改。
    void SetBottomBandDip(float dip);
    float BottomBandDip() const { return bottomBandDip_; }

private:
    static bool RegisterWindowClass(HINSTANCE instance);
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    void ApplyBounds();
    void Paint();
    void UpdateDpiScale();
    bool EnsureTextFormat();

    std::wstring title_;
    RECT boundsPx_{};        // 屏幕物理像素坐标
    HWND hwnd_ = nullptr;
    HWND host_ = nullptr;
    HINSTANCE instance_ = nullptr;
    float dpiScale_ = 1.0f;
    float bottomBandDip_ = 0.0f;  // 卡片下方的标签带（DIP），不属于 boundsPx_
    bool isChild_ = false;  // true = 宿主的子窗口；false = 顶层 popup（owner 为宿主）

    tf::gpu::Surface surface_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> titleFormat_;

    bool dragging_ = false;
    bool dragExceeded_ = false;  // 超过拖动阈值：区分“点击格子”与“拖动面板”
    bool childCapture_ = false;  // 这次按下已被外部接管（拖图标而不是拖面板）
    bool firstFrameLogged_ = false;
    POINT dragCursorStart_{};
    RECT dragBoundsStart_{};
    float dragScaleStart_ = 1.0f;

    PaintFn paintFn_;
    ClickFn clickFn_;
    MovedFn movedFn_;
    RightClickFn rightClickFn_;
    PressFn pressFn_;
    MoveFn moveFn_;
    ReleaseFn releaseFn_;
    WheelFn wheelFn_;
    KeyFn keyFn_;
    std::function<void()> leaveFn_;
    HoverFn hoverFn_;
    HoldFn holdFn_;
    AnimFn animFn_;
    bool animRunning_ = false;     // 已订阅 FrameClock（有动画在跑）
    long long animLastTick_ = 0;   // 上一帧的 QPC 计数，用来算 dt
    bool panelMode_ = false;
    bool trackingLeave_ = false;  // 已向系统登记 WM_MOUSELEAVE
};

}  // namespace tf::widget
