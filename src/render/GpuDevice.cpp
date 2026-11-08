#include "render/GpuDevice.h"

#include "core/Log.h"

namespace {

using namespace tf;

// 优先硬件，失败退 WARP，最后退 SDK 层（仅开发机装 SDK 时可用）
bool CreateD3D11(ComPtr<ID3D11Device>& device, ComPtr<ID3D11DeviceContext>& ctx,
                 D3D_FEATURE_LEVEL& outLevel) {
    static const D3D_DRIVER_TYPE kBases[] = {
        D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP, D3D_DRIVER_TYPE_REFERENCE};
    static const D3D_FEATURE_LEVEL kLevels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    static const UINT kFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED;

    for (D3D_DRIVER_TYPE type : kBases) {
        const HRESULT hr = ::D3D11CreateDevice(nullptr, type, nullptr, kFlags, kLevels,
                                               static_cast<UINT>(std::size(kLevels)), D3D11_SDK_VERSION,
                                               device.ReleaseAndGetAddressOf(), &outLevel,
                                               ctx.ReleaseAndGetAddressOf());
        if (SUCCEEDED(hr)) {
            log::Infof(L"D3D11 设备创建成功 driver={} featureLevel=0x{:04X}",
                       static_cast<int>(type), static_cast<unsigned>(outLevel));
            return true;
        }
        log::Warnf(L"D3D11CreateDevice 失败 driver={} hr=0x{:08X}", static_cast<int>(type),
                   static_cast<unsigned>(hr));
    }
    return false;
}

}  // namespace

namespace tf::gpu {

GpuDevice& GpuDevice::Get() {
    static GpuDevice inst;
    return inst;
}

GpuDevice::~GpuDevice() {
    Shutdown();
}

bool GpuDevice::Initialize() {
    if (valid_) return true;

    D3D_FEATURE_LEVEL featureLevel{};
    if (!CreateD3D11(d3d_, d3dContext_, featureLevel)) {
        log::Error(L"无法创建 D3D11 设备，程序无法继续");
        return false;
    }

    if (!log::Check(d3d_.As(&dxgi_), L"ID3D11Device -> IDXGIDevice")) return false;

    // D2D 工厂（单线程：所有绘制都在主线程）
    D2D1_FACTORY_OPTIONS opts{};
#ifdef _DEBUG
    opts.debugLevel = D2D1_DEBUG_LEVEL_NONE;
#endif
    if (!log::Check(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                       reinterpret_cast<void**>(d2dFactory_.GetAddressOf())),
                    L"D2D1CreateFactory")) {
        return false;
    }

    if (!log::Check(d2dFactory_->CreateDevice(dxgi_.Get(), d2dDevice_.GetAddressOf()),
                    L"ID2D1Factory1::CreateDevice")) {
        return false;
    }

    if (!log::Check(::DCompositionCreateDevice(dxgi_.Get(), __uuidof(IDCompositionDevice),
                                              reinterpret_cast<void**>(comp_.GetAddressOf())),
                    L"DCompositionCreateDevice")) {
        return false;
    }

    if (!log::Check(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf())),
                    L"DWriteCreateFactory")) {
        return false;
    }

    if (!log::Check(::CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(wic_.GetAddressOf())),
                    L"CoCreateInstance(WICImagingFactory2)")) {
        return false;
    }

    valid_ = true;
    log::Info(L"GPU 管线初始化完成");
    return true;
}

void GpuDevice::Shutdown() {
    valid_ = false;
    wic_.Reset();
    dwrite_.Reset();
    comp_.Reset();
    d2dDevice_.Reset();
    d2dFactory_.Reset();
    dxgi_.Reset();
    d3dContext_.Reset();
    d3d_.Reset();
}

}  // namespace tf::gpu
