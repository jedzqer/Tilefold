#include "widgets/WidgetWindow.h"

#include <d2d1_1.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <utility>

#include "core/Dpi.h"
#include "core/Log.h"
#include "render/GpuDevice.h"
#include "render/Layer.h"
#include "shell/DesktopHost.h"
#include "widgets/FrameClock.h"

namespace {

constexpr wchar_t kClassName[] = L"Tilefold.Widget";
constexpr UINT kDpiChangedAfterParent = 0x02E3;


long long NowTick() {
    LARGE_INTEGER li{};
    if (!::QueryPerformanceCounter(&li)) return 0;
    return li.QuadPart;
}

float TickSeconds() {
    LARGE_INTEGER f{};
    if (!::QueryPerformanceFrequency(&f) || f.QuadPart == 0) return 0.016f;
    return 1.0f / static_cast<float>(f.QuadPart);
}

}  // namespace

namespace tf::widget {

WidgetWindow::WidgetWindow(std::wstring title, RECT boundsPx)
    : title_(std::move(title)), boundsPx_(boundsPx) {}

WidgetWindow::~WidgetWindow() {
    Close();
}

bool WidgetWindow::RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return true;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;  // 不要 CS_HREDRAW/CS_VREDRAW：重绘完全由我们自己控制
    wc.lpfnWndProc = &WidgetWindow::StaticWndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;  // 绝不擦背景，避免闪白
    wc.lpszClassName = kClassName;

    if (::RegisterClassExW(&wc) == 0) {
        const DWORD err = ::GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            log::Errorf(L"RegisterClassExW 失败 err={}", err);
            return false;
        }
    }
    registered = true;
    return true;
}

bool WidgetWindow::Show(HWND host, HINSTANCE instance) {
    if (hwnd_ != nullptr) return true;
    if (!RegisterWindowClass(instance)) return false;

    instance_ = instance;
    host_ = host;

    dpiScale_ = tf::dpi::ScaleForRect(boundsPx_);

    // 窗口模式：桌面 widget 走 child（挂到桌面宿主下），放大面板走 popup。
    //
    // child 的理由（实测）：Win+D「显示桌面」只搬顶层窗口——Shell 把每个顶层窗口逐个
    // SetWindowPos(HWND_BOTTOM)（带 SWP_NOSENDCHANGING，WM_WINDOWPOSCHANGING 拦不到），
    // 8 块 popup 面板的 z 序被整体反转、全部落到 Progman 之下；Progman 不透明，于是
    // 桌面只剩一块侥幸留在宿主上面的文件夹。子窗口完全不参与 Show Desktop，实测
    // Win+D 之后 8 块全都还在原处，且圆角外的透明区照旧透出墙纸（不是黑底）。
    //
    // popup 的理由（放大面板）：面板是用户主动打开的，必须压在用户正在用的程序之上、
    // 还要能收 ESC，所以它必须是顶层窗口。panelMode_ 永远走 popup。
    //
    // TF_WIDGET_MODE=popup|child 可强制覆盖（排查/对照用）。
    bool wantChild = !panelMode_ && host != nullptr;
    wchar_t modeBuf[16]{};
    if (::GetEnvironmentVariableW(L"TF_WIDGET_MODE", modeBuf, 16) > 0) {
        if (::lstrcmpiW(modeBuf, L"child") == 0) {
            wantChild = (host != nullptr);
        } else if (::lstrcmpiW(modeBuf, L"popup") == 0) {
            wantChild = false;
        }
        log::Infof(L"TF_WIDGET_MODE={} -> {}", modeBuf, wantChild ? L"child" : L"popup");
    }
    isChild_ = wantChild;

    const DWORD exStyle = WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP |
                          (panelMode_ ? 0u : WS_EX_NOACTIVATE);
    const DWORD style = isChild_ ? WS_CHILD : WS_POPUP;
    // Physical desktop pixels are mapped to parent client coordinates for children.
    const RECT px = boundsPx_;
    POINT tl{px.left, px.top};
    POINT br{px.right, px.bottom};
    const int bandPx = static_cast<int>(std::lround(bottomBandDip_ * dpiScale_));
    br.y += bandPx;
    if (isChild_) {
        ::MapWindowPoints(HWND_DESKTOP, host, &tl, 1);
        ::MapWindowPoints(HWND_DESKTOP, host, &br, 1);
    }

    hwnd_ = ::CreateWindowExW(exStyle, kClassName, title_.c_str(), style, tl.x, tl.y,
                              br.x - tl.x, br.y - tl.y, host, nullptr, instance, this);
    if (hwnd_ == nullptr) {
        log::Errorf(L"CreateWindowExW(Widget) 失败 err={}", ::GetLastError());
        return false;
    }

    // Desktop children inherit their host DPI; use the target monitor for their
    // local rendering/input scale. Top-level panels use GetDpiForWindow.
    UpdateDpiScale();

    ::ShowWindow(hwnd_, panelMode_ ? SW_SHOW : SW_SHOWNOACTIVATE);

    if (isChild_) {
        // 必顶到兄弟窗口最上层。实测：Progman 会让 SHELLDLL_DefView 保持 topmost，
        // 新建的子窗口反而落在 DefView 下方，被桌面图标视图盖住。
        ::SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else if (host_ != nullptr && ::IsWindow(host_)) {
        // 实测（枚举 z 序）：owner=Progman 并不能把 popup 压在桌面层——8 个面板落在
        // z#4..z#11，反而盖在 PowerShell 终端(z#13)之上。必须显式插到 Progman 之后，
        // 才能得到“墙纸之上、所有普通窗口之下”。
        ::SetWindowPos(hwnd_, host_, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else if (panelMode_) {
        // 放大面板：抢前台才能收键盘（ESC）。是用户自己点开的，抢焦点不突兀。
        ::SetForegroundWindow(hwnd_);
    }
    log::Infof(L"Widget 模式={} 父/属主={}", isChild_ ? L"child" : L"popup",
               tf::host::ClassName(::GetWindow(hwnd_, GW_OWNER)));
    ::InvalidateRect(hwnd_, nullptr, FALSE);

    log::Infof(L"Widget 已创建 hwnd=0x{:X} host={} 尺寸={}x{}px",
               reinterpret_cast<uintptr_t>(hwnd_),
               host != nullptr ? tf::host::ClassName(host) : L"<顶层>",
               boundsPx_.right - boundsPx_.left, boundsPx_.bottom - boundsPx_.top);
    return true;
}

void WidgetWindow::Close() {
    if (hwnd_ != nullptr) {
        HWND h = hwnd_;
        hwnd_ = nullptr;
        if (::IsWindow(h)) ::DestroyWindow(h);
    }
    surface_.Destroy();
    titleFormat_.Reset();
}

void WidgetWindow::ApplyBounds() {
    if (hwnd_ == nullptr || !::IsWindow(hwnd_)) return;

    // Bounds are physical desktop pixels; only local drawing uses DIP.
    // While dragging, follow the pointer's monitor, matching the scaled anchor.
    POINT cursor{};
    dpiScale_ = dragging_ && ::GetCursorPos(&cursor)
                    ? tf::dpi::ScaleForPoint(cursor) : tf::dpi::ScaleForRect(boundsPx_);
    MONITORINFO monitor{sizeof(monitor)};
    if (::GetMonitorInfoW(::MonitorFromRect(&boundsPx_, MONITOR_DEFAULTTONEAREST), &monitor)) {
        const int w = boundsPx_.right - boundsPx_.left;
        const int h = boundsPx_.bottom - boundsPx_.top;
        const int keepX = std::min({w, tf::dpi::Dip2Px(80, dpiScale_), static_cast<int>(monitor.rcWork.right - monitor.rcWork.left)});
        const int keepY = std::min({h, tf::dpi::Dip2Px(80, dpiScale_), static_cast<int>(monitor.rcWork.bottom - monitor.rcWork.top)});
        boundsPx_.left = std::clamp(boundsPx_.left, monitor.rcWork.left + keepX - w,
                                  monitor.rcWork.right - keepX);
        boundsPx_.top = std::clamp(boundsPx_.top, monitor.rcWork.top + keepY - h,
                                 monitor.rcWork.bottom - keepY);
        boundsPx_.right = boundsPx_.left + w;
        boundsPx_.bottom = boundsPx_.top + h;
    }

    const RECT px = boundsPx_;
    POINT tl{px.left, px.top};
    POINT br{px.right, px.bottom};
    // 标签带只是多画一条，不算进 boundsPx_——吸附与重叠判定只看卡片
    const int bandPx = static_cast<int>(std::lround(bottomBandDip_ * dpiScale_));
    br.y += bandPx;
    if (isChild_ && host_ != nullptr && ::IsWindow(host_)) {
        ::MapWindowPoints(HWND_DESKTOP, host_, &tl, 1);
        ::MapWindowPoints(HWND_DESKTOP, host_, &br, 1);
    }

    ::SetWindowPos(hwnd_, nullptr, tl.x, tl.y, br.x - tl.x, br.y - tl.y,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    Invalidate();
}

void WidgetWindow::SetBoundsPx(const RECT& boundsPx) {
    boundsPx_ = boundsPx;
    ApplyBounds();
}

void WidgetWindow::SetBottomBandDip(float dip) {
    const float clamped = dip > 0.0f ? dip : 0.0f;
    if (std::fabs(clamped - bottomBandDip_) < 0.01f) return;
    bottomBandDip_ = clamped;
    ApplyBounds();
}

void WidgetWindow::UpdateDpiScale() {
    if (hwnd_ == nullptr || dragging_) return;
    const float s = isChild_ ? tf::dpi::ScaleForRect(boundsPx_) : tf::dpi::ScaleForWindow(hwnd_);
    if (s > 0.0f && std::fabs(s - dpiScale_) > 0.001f) {
        dpiScale_ = s;
        titleFormat_.Reset();
    }
}

void WidgetWindow::RequestAnimation() {
    if (hwnd_ == nullptr || !::IsWindow(hwnd_)) return;
    if (animRunning_) return;
    animRunning_ = true;
    animLastTick_ = NowTick();
    // 跟屏幕刷新同步：每次 vsync 收到一条 FrameClock::kFrameMsg
    FrameClock::Get().Subscribe(hwnd_);
}

void WidgetWindow::Invalidate() {
    if (hwnd_ != nullptr && ::IsWindow(hwnd_)) {
        ::InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool WidgetWindow::EnsureTextFormat() {
    if (titleFormat_) return true;
    IDWriteFactory* dwrite = tf::gpu::GpuDevice::Get().DWrite();
    if (dwrite == nullptr) return false;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    const HRESULT hr = dwrite->CreateTextFormat(
        L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"zh-CN", fmt.GetAddressOf());
    if (FAILED(hr)) {
        log::Errorf(L"CreateTextFormat 失败 hr=0x{:08X}", static_cast<unsigned>(hr));
        return false;
    }
    fmt->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    titleFormat_ = std::move(fmt);
    return true;
}

void WidgetWindow::Paint() {
    if (hwnd_ == nullptr || !::IsWindow(hwnd_)) return;

    RECT client{};
    ::GetClientRect(hwnd_, &client);
    const UINT wPx = static_cast<UINT>(client.right - client.left);
    const UINT hPx = static_cast<UINT>(client.bottom - client.top);
    if (wPx == 0 || hPx == 0) return;

    if (!surface_.Ensure(hwnd_, wPx, hPx, dpiScale_)) return;
    ID2D1DeviceContext* ctx = surface_.Begin();
    if (ctx == nullptr) return;

    const D2D1_SIZE_F size = ctx->GetSize();  // DIP

    // 外部绘制钩子：大文件夹网格走这条路径
    if (paintFn_) {
        paintFn_(ctx, size);
        surface_.End();
        if (!firstFrameLogged_) {
            firstFrameLogged_ = true;
            tf::log::Infof(L"首帧已提交(外部绘制) widget=0x{:X} 像素尺寸={}x{} dpiScale={:.2f}",
                           reinterpret_cast<uintptr_t>(hwnd_), wPx, hPx, dpiScale_);
        }
        return;
    }

    // 玻璃底
    const D2D1_RECT_F body = D2D1::RectF(0.5f, 0.5f, size.width - 0.5f, size.height - 0.5f);
    tf::layer::GlassRect(ctx, body, tf::layer::style::kCornerRadius,
                         D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f),
                         D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.34f));
    tf::layer::TopSheen(ctx, body, tf::layer::style::kCornerRadius, 0.10f);

    // 标题 + 提示
    if (EnsureTextFormat()) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.92f),
                                                 textBrush.GetAddressOf()))) {
            const D2D1_RECT_F titleRect = D2D1::RectF(16.0f, 10.0f, size.width - 16.0f, 36.0f);
            ctx->DrawTextW(title_.c_str(), static_cast<UINT32>(title_.size()), titleFormat_.Get(),
                           titleRect, textBrush.Get());

            Microsoft::WRL::ComPtr<IDWriteTextFormat> hintFmt;
            if (SUCCEEDED(tf::gpu::GpuDevice::Get().DWrite()->CreateTextFormat(
                    L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"zh-CN",
                    hintFmt.GetAddressOf()))) {
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> hintBrush;
                if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.55f),
                                                         hintBrush.GetAddressOf()))) {
                    const D2D1_RECT_F hintRect =
                        D2D1::RectF(16.0f, size.height - 32.0f, size.width - 16.0f, size.height - 12.0f);
                    const wchar_t* hint = L"M0 · 可拖动 · 挂在桌面层";
                    ctx->DrawTextW(hint, static_cast<UINT32>(::lstrlenW(hint)), hintFmt.Get(), hintRect,
                                   hintBrush.Get());
                }
            }
        }
    }

    surface_.End();

    if (!firstFrameLogged_) {
        firstFrameLogged_ = true;
        tf::log::Infof(L"首帧已提交 widget=0x{:X} 像素尺寸={}x{} dpiScale={:.2f}",
                       reinterpret_cast<uintptr_t>(hwnd_), wPx, hPx, dpiScale_);
    }
}

LRESULT CALLBACK WidgetWindow::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = static_cast<WidgetWindow*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->hwnd_ = hwnd;
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

    auto* self = reinterpret_cast<WidgetWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return ::DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(msg, wp, lp);
}

LRESULT WidgetWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND:
            return 1;  // 自己画，不要 GDI 擦

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            ::BeginPaint(hwnd_, &ps);
            Paint();
            ::EndPaint(hwnd_, &ps);
            return 0;
        }

        case WM_SIZE:
            Invalidate();
            return 0;

        case kDpiChangedAfterParent:
            UpdateDpiScale();
            Invalidate();
            return 0;
        case WM_DPICHANGED:
            if (!isChild_ && !dragging_ && lp != 0) {
                boundsPx_ = *reinterpret_cast<const RECT*>(lp);
                if (panelMode_) {
                    MONITORINFO mi{sizeof(mi)};
                    if (::GetMonitorInfoW(::MonitorFromRect(&boundsPx_, MONITOR_DEFAULTTONEAREST), &mi))
                        boundsPx_ = mi.rcWork;
                }
                ApplyBounds();
            }
            UpdateDpiScale();
            Invalidate();
            return 0;

        case WM_WINDOWPOSCHANGING: {
            // 任何 Z 序变更都拉回“紧贴宿主之上”，防止被系统或别的窗口操作顶到普通应用
            // 窗口上面——桌面美化层挡住用户正在用的程序是不可接受的。
            // 放大面板是用户主动打开的模态层，反而要压在普通窗口之上，所以不参与这个约束。
            // （child 模式下窗口是宿主的子窗口，天然就在宿主之上，不需要这个拦截。）
            if (!panelMode_ && !isChild_ && host_ != nullptr && ::IsWindow(host_)) {
                auto* pos = reinterpret_cast<WINDOWPOS*>(lp);
                if (pos != nullptr && (pos->flags & SWP_NOZORDER) == 0) {
                    pos->hwndInsertAfter = host_;
                }
            }
            break;
        }

        case WM_NCHITTEST: {
            // M0 整块都是可交互区域；M2 起会改成"仅格子可点"
            LRESULT hit = ::DefWindowProcW(hwnd_, msg, wp, lp);
            return hit == HTTRANSPARENT || hit == HTNOWHERE ? HTCLIENT : hit;
        }

        case WM_LBUTTONDOWN: {
            const float scale = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
            const float xDip = static_cast<float>(GET_X_LPARAM(lp)) / scale;
            const float yDip = static_cast<float>(GET_Y_LPARAM(lp)) / scale;

            // 视觉用的“按住”状态：不管这次按下是拖图标、拖窗口还是点启动，都要通知
            if (holdFn_) holdFn_(true, xDip, yDip);

            // 先问外部：这次按下是不是落在某个“可拖出”的格子上
            if (pressFn_ && pressFn_(xDip, yDip)) {
                childCapture_ = true;
                ::SetCapture(hwnd_);
                return 0;
            }

            if (!::GetCursorPos(&dragCursorStart_)) break;
            dragBoundsStart_ = boundsPx_;
            dragScaleStart_ = dpiScale_;
            dragExceeded_ = false;
            dragging_ = true;
            ::SetCapture(hwnd_);
            return 0;
        }

        case WM_MOUSEMOVE: {
            const float scale = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
            // 悬停跟踪先走：图标放大/文字淡出是“鼠标在上面就算”，和按下无关
            if (hoverFn_) {
                hoverFn_(static_cast<float>(GET_X_LPARAM(lp)) / scale,
                         static_cast<float>(GET_Y_LPARAM(lp)) / scale);
            }
            // 只登记一次，移出窗口时由系统发 WM_MOUSELEAVE（滚轮翻页要“移开就回顶”）
            if (!trackingLeave_) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd_;
                if (::TrackMouseEvent(&tme)) trackingLeave_ = true;
            }
            if (childCapture_) {
                if (moveFn_) {
                    moveFn_(static_cast<float>(GET_X_LPARAM(lp)) / scale,
                            static_cast<float>(GET_Y_LPARAM(lp)) / scale);
                }
                return 0;
            }
            if (!dragging_) return 0;
            POINT now{};
            if (!::GetCursorPos(&now)) return 0;
            const int dxPx = now.x - dragCursorStart_.x;
            const int dyPx = now.y - dragCursorStart_.y;

            // 拖动阈值：手指会有 1-2px 抖动，不设阈值的话“点击格子”会变成
            // “把整个面板挪了两像素”，同时也不会误触发窗口移动。
            const int threshold = static_cast<int>(std::lround(4.0f * scale));
            if (!dragExceeded_ && (std::abs(dxPx) > threshold || std::abs(dyPx) > threshold)) {
                dragExceeded_ = true;
                ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEALL));
            }
            if (!dragExceeded_) return 0;

            const float targetScale = tf::dpi::ScaleForPoint(now);
            const float ratio = targetScale / dragScaleStart_;
            const int width = static_cast<int>(std::lround((dragBoundsStart_.right - dragBoundsStart_.left) * ratio));
            const int height = static_cast<int>(std::lround((dragBoundsStart_.bottom - dragBoundsStart_.top) * ratio));
            const int x = now.x - static_cast<int>(std::lround((dragCursorStart_.x - dragBoundsStart_.left) * ratio));
            const int y = now.y - static_cast<int>(std::lround((dragCursorStart_.y - dragBoundsStart_.top) * ratio));
            boundsPx_ = RECT{x, y, x + width, y + height};
            ApplyBounds();
            return 0;
        }

        case WM_LBUTTONUP: {
            if (holdFn_) {
                const float s = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
                holdFn_(false, static_cast<float>(GET_X_LPARAM(lp)) / s,
                        static_cast<float>(GET_Y_LPARAM(lp)) / s);
            }
            if (childCapture_) {
                childCapture_ = false;
                ::ReleaseCapture();
                const float scale = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
                if (releaseFn_) {
                    releaseFn_(static_cast<float>(GET_X_LPARAM(lp)) / scale,
                               static_cast<float>(GET_Y_LPARAM(lp)) / scale);
                }
                return 0;
            }
            if (!dragging_) return 0;
            dragging_ = false;
            const bool wasDrag = dragExceeded_;
            dragExceeded_ = false;
            ::ReleaseCapture();
            ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));

            if (wasDrag) {
                if (movedFn_) movedFn_(boundsPx_);
            } else if (clickFn_) {
                const float scale = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
                clickFn_(static_cast<float>(GET_X_LPARAM(lp)) / scale,
                         static_cast<float>(GET_Y_LPARAM(lp)) / scale);
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            if (rightClickFn_) {
                const float scale = dpiScale_ > 0.0f ? dpiScale_ : 1.0f;
                rightClickFn_(static_cast<float>(GET_X_LPARAM(lp)) / scale,
                              static_cast<float>(GET_Y_LPARAM(lp)) / scale);
                return 0;
            }
            break;
        }

        case WM_CAPTURECHANGED:
            dragging_ = false;
            childCapture_ = false;
            // 捕获被别人抢走就收不到 WM_LBUTTONUP 了，按压状态必须在这里收尾，
            // 否则图标会永久停在“缩小”那一帧。
            if (holdFn_) holdFn_(false, -1.0f, -1.0f);
            return 0;

        case WM_MOUSELEAVE:
            trackingLeave_ = false;
            if (hoverFn_) hoverFn_(-1.0f, -1.0f);
            if (leaveFn_) leaveFn_();
            return 0;

        case FrameClock::kFrameMsg: {
            FrameClock::Get().Ack(hwnd_);
            if (!animRunning_) return 0;
            const long long now = NowTick();
            float dt = static_cast<float>(now - animLastTick_) * TickSeconds();
            animLastTick_ = now;
            // 卡顿/被挂起之后不要给一个巨大的 dt（弹簧会直接炸掉）
            dt = std::clamp(dt, 0.0f, 0.05f);
            const bool again = animFn_ && animFn_(dt);
            if (!again) {
                animRunning_ = false;
                FrameClock::Get().Unsubscribe(hwnd_);
            }
            // 直接画，不走 InvalidateRect：WM_PAINT 优先级最低，会被输入消息往后挤，帧就不准了
            Paint();
            ::ValidateRect(hwnd_, nullptr);
            return 0;
        }

        case WM_MOUSEWHEEL: {
            if (wheelFn_) {
                const float notches =
                    static_cast<float>(GET_WHEEL_DELTA_WPARAM(wp)) / static_cast<float>(WHEEL_DELTA);
                if (wheelFn_(notches)) return 0;
            }
            break;
        }

        case WM_KEYDOWN: {
            if (keyFn_ && keyFn_(wp)) return 0;
            break;
        }

        case WM_DESTROY:
            if (animRunning_) {
                FrameClock::Get().Unsubscribe(hwnd_);
                animRunning_ = false;
            }
            hwnd_ = nullptr;
            return 0;

        default:
            break;
    }
    return ::DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace tf::widget
