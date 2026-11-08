#pragma once

#include <windows.h>

namespace tf::widget {

// 跟屏幕刷新同步的帧时钟。
// 一条后台线程等合成器的每一次 vsync（DCompositionWaitForCompositorClock，旧系统退回 DwmFlush），
// 然后给所有订阅的窗口投递 kFrameMsg。窗口收到后推进动画并立刻绘制。
// 取代 SetTimer(16ms)：后者实际精度约 15.6ms 且会抖，帧率和显示器刷新率对不上。
//
// 合并投递：某个窗口上一帧的消息还没处理完，这一拍就不再投（不会把消息队列塞爆）。
class FrameClock {
public:
    static constexpr UINT kFrameMsg = WM_APP + 0x40;

    static FrameClock& Get();

    // 开始/停止给 hwnd 投递帧消息（UI 线程调用）
    void Subscribe(HWND hwnd);
    void Unsubscribe(HWND hwnd);
    // 窗口处理完一帧后调用，允许投下一帧
    void Ack(HWND hwnd);

    // 进程退出前调用，结束后台线程
    void Shutdown();

private:
    FrameClock() = default;
    ~FrameClock();
    FrameClock(const FrameClock&) = delete;
    FrameClock& operator=(const FrameClock&) = delete;

    struct Impl;
    Impl* impl_ = nullptr;
    Impl& EnsureImpl();
};

}  // namespace tf::widget
