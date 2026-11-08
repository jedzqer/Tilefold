#include "render/Surface.h"

#include "core/Log.h"
#include "render/GpuDevice.h"

namespace {

using namespace tf;

// 从 ID3D11Device 拿 IDXGIFactory2
HRESULT GetDxgiFactory(IDXGIDevice* dxgiDevice, ComPtr<IDXGIFactory2>& out) {
    ComPtr<IDXGIAdapter> adapter;
    HRESULT hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) return hr;
    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) return hr;
    out = std::move(factory);
    return S_OK;
}

}  // namespace

namespace tf::gpu {

Surface::~Surface() {
    Destroy();
}

bool Surface::Ensure(HWND hwnd, UINT widthPx, UINT heightPx, float dpiScale) {
    if (hwnd == nullptr || widthPx == 0 || heightPx == 0) return false;
    if (!GpuDevice::Get().IsValid()) return false;

    const float scale = dpiScale > 0.0f ? dpiScale : static_cast<float>(::GetDpiForWindow(hwnd)) / 96.0f;
    const bool sameWindow = (hwnd_ == hwnd);
    if (sameWindow && Valid() && width_ == widthPx && height_ == heightPx && dpiScale_ == scale) return true;

    if (!sameWindow) {
        Destroy();
        hwnd_ = hwnd;
    }

    width_ = widthPx;
    height_ = heightPx;
    dpiScale_ = scale;
    if (dpiScale_ <= 0.0f) dpiScale_ = 1.0f;

    if (!Valid()) return CreateResources();

    // 只改尺寸：重建 swap chain buffer
    ReleaseResources();
    return CreateResources();
}

bool Surface::CreateResources() {
    GpuDevice& gpu = GpuDevice::Get();

    ComPtr<IDXGIFactory2> factory;
    if (!log::Check(GetDxgiFactory(gpu.Dxgi(), factory), L"获取 IDXGIFactory2")) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = width_;
    desc.Height = height_;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;  // 逐像素透明的前提
    desc.Flags = 0;

    if (!log::Check(factory->CreateSwapChainForComposition(gpu.D3D(), &desc, nullptr,
                                                           swap_.ReleaseAndGetAddressOf()),
                    L"CreateSwapChainForComposition")) {
        return false;
    }

    if (!compTarget_) {
        // 实测：topmost 传 TRUE 或 FALSE，屏幕像素完全一致，说明对
        // “子窗口 + WS_EX_NOREDIRECTIONBITMAP”这个组合两者没区别。
        // 保留 TRUE（visual 作为窗口内容之上合成），语义更直观。
        if (!log::Check(gpu.Comp()->CreateTargetForHwnd(hwnd_, TRUE, compTarget_.ReleaseAndGetAddressOf()),
                        L"CreateTargetForHwnd")) {
            return false;
        }
    }

    if (!visual_) {
        if (!log::Check(gpu.Comp()->CreateVisual(visual_.ReleaseAndGetAddressOf()), L"CreateVisual")) {
            return false;
        }
    }

    if (!log::Check(visual_->SetContent(swap_.Get()), L"IDCompositionVisual::SetContent")) return false;
    if (!log::Check(compTarget_->SetRoot(visual_.Get()), L"IDCompositionTarget::SetRoot")) return false;
    if (!log::Check(gpu.Comp()->Commit(), L"IDCompositionDevice::Commit")) return false;

    if (!context_) {
        if (!log::Check(gpu.D2DDevice()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                                             context_.ReleaseAndGetAddressOf()),
                        L"CreateDeviceContext")) {
            return false;
        }
    }

    // 把上下文 DPI 设成窗口 DPI：布局代码一律用 DIP，像素换算交给 D2D
    context_->SetDpi(96.0f * dpiScale_, 96.0f * dpiScale_);

    ComPtr<IDXGISurface> surface;
    if (!log::Check(swap_->GetBuffer(0, IID_PPV_ARGS(surface.GetAddressOf())), L"swapchain GetBuffer")) {
        return false;
    }

    D2D1_BITMAP_PROPERTIES1 props{};
    props.pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    props.dpiX = 96.0f * dpiScale_;
    props.dpiY = 96.0f * dpiScale_;

    if (!log::Check(context_->CreateBitmapFromDxgiSurface(surface.Get(), &props, target_.ReleaseAndGetAddressOf()),
                    L"CreateBitmapFromDxgiSurface")) {
        return false;
    }

    return true;
}

void Surface::ReleaseResources() {
    target_.Reset();
    swap_.Reset();
    context_.Reset();  // 上下文可以复用，但 target 必须重置，重建更简单
}

void Surface::Destroy() {
    if (context_) context_->SetTarget(nullptr);
    target_.Reset();
    swap_.Reset();
    context_.Reset();
    visual_.Reset();
    compTarget_.Reset();
    hwnd_ = nullptr;
    width_ = 0;
    height_ = 0;
}

ID2D1DeviceContext* Surface::Begin() {
    if (!Valid() || !context_ || !target_) return nullptr;
    context_->SetTarget(nullptr);
    context_->SetTarget(target_.Get());
    context_->SetTransform(D2D1::Matrix3x2F::Identity());
    context_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    context_->BeginDraw();
    // 清成全透明：底下是真实壁纸/桌面图标，我们要"透"出去
    context_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    return context_.Get();
}

bool Surface::End() {
    if (!context_ || !swap_) return false;

    const HRESULT drawHr = context_->EndDraw();
    context_->SetTarget(nullptr);

    if (FAILED(drawHr)) {
        // D2DERR_RECREATE_TARGET：设备丢失，丢资源等下一帧重建
        log::Errorf(L"EndDraw 失败 hr=0x{:08X}，释放 Surface 等待重建", static_cast<unsigned>(drawHr));
        Destroy();
        return false;
    }

    const HRESULT presentHr = swap_->Present(0, 0);
    if (FAILED(presentHr)) {
        log::Errorf(L"Present 失败 hr=0x{:08X}", static_cast<unsigned>(presentHr));
        return false;
    }

    GpuDevice::Get().Comp()->Commit();
    return true;
}

}  // namespace tf::gpu
