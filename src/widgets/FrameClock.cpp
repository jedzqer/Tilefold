#include "widgets/FrameClock.h"

#include <dwmapi.h>

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "core/Log.h"

namespace tf::widget {

namespace {

// Win11 起才有；按名字动态取，旧系统没有就退回 DwmFlush
using WaitClockFn = DWORD(WINAPI*)(UINT, const HANDLE*, DWORD);

WaitClockFn LoadWaitClock() {
    HMODULE dcomp = ::GetModuleHandleW(L"dcomp.dll");
    if (dcomp == nullptr) dcomp = ::LoadLibraryW(L"dcomp.dll");
    if (dcomp == nullptr) return nullptr;
    return reinterpret_cast<WaitClockFn>(
        reinterpret_cast<void*>(::GetProcAddress(dcomp, "DCompositionWaitForCompositorClock")));
}

}  // namespace

struct FrameClock::Impl {
    struct Sub {
        HWND hwnd;
        bool pending;  // 已投递、窗口还没处理
    };

    std::mutex mu;
    std::condition_variable cv;
    std::vector<Sub> subs;
    bool quit = false;
    std::thread worker;
    WaitClockFn waitClock = nullptr;

    void Run() {
        waitClock = LoadWaitClock();
        tf::log::Infof(L"帧时钟线程启动：{}",
                       waitClock ? L"DCompositionWaitForCompositorClock" : L"DwmFlush");
        while (true) {
            {
                std::unique_lock lock(mu);
                cv.wait(lock, [&] { return quit || !subs.empty(); });
                if (quit) return;
            }
            WaitVsync();
            std::lock_guard lock(mu);
            if (quit) return;
            for (Sub& s : subs) {
                if (s.pending) continue;
                if (::PostMessageW(s.hwnd, kFrameMsg, 0, 0)) s.pending = true;
            }
        }
    }

    void WaitVsync() {
        if (waitClock != nullptr) {
            // 100ms 超时：合成器睡了（锁屏、所有窗口都没变化）也不会卡死线程
            const DWORD r = waitClock(0, nullptr, 100);
            if (r == WAIT_OBJECT_0 || r == WAIT_TIMEOUT) return;
        }
        if (FAILED(::DwmFlush())) ::Sleep(16);  // DWM 不可用时兜底
    }
};

FrameClock& FrameClock::Get() {
    static FrameClock inst;
    return inst;
}

FrameClock::~FrameClock() { Shutdown(); }

FrameClock::Impl& FrameClock::EnsureImpl() {
    if (impl_ == nullptr) {
        impl_ = new Impl();
        impl_->worker = std::thread([p = impl_] { p->Run(); });
    }
    return *impl_;
}

void FrameClock::Subscribe(HWND hwnd) {
    Impl& im = EnsureImpl();
    {
        std::lock_guard lock(im.mu);
        const auto it = std::find_if(im.subs.begin(), im.subs.end(),
                                     [&](const Impl::Sub& s) { return s.hwnd == hwnd; });
        if (it != im.subs.end()) return;
        im.subs.push_back({hwnd, false});
    }
    im.cv.notify_one();
}

void FrameClock::Unsubscribe(HWND hwnd) {
    if (impl_ == nullptr) return;
    std::lock_guard lock(impl_->mu);
    std::erase_if(impl_->subs, [&](const Impl::Sub& s) { return s.hwnd == hwnd; });
}

void FrameClock::Ack(HWND hwnd) {
    if (impl_ == nullptr) return;
    std::lock_guard lock(impl_->mu);
    for (Impl::Sub& s : impl_->subs) {
        if (s.hwnd == hwnd) s.pending = false;
    }
}

void FrameClock::Shutdown() {
    if (impl_ == nullptr) return;
    {
        std::lock_guard lock(impl_->mu);
        impl_->quit = true;
        impl_->subs.clear();
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    delete impl_;
    impl_ = nullptr;
}

}  // namespace tf::widget
