#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_3.h>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;

namespace tf::gpu {

// 全局 GPU 与工厂对象。所有 UI 窗口共享同一套 D3D11/D2D/DComp 设备。
class GpuDevice {
public:
    static GpuDevice& Get();

    bool Initialize();
    void Shutdown();
    bool IsValid() const { return valid_; }

    ID3D11Device* D3D() const { return d3d_.Get(); }
    IDXGIDevice* Dxgi() const { return dxgi_.Get(); }
    ID2D1Factory1* D2DFactory() const { return d2dFactory_.Get(); }
    ID2D1Device* D2DDevice() const { return d2dDevice_.Get(); }
    IDCompositionDevice* Comp() const { return comp_.Get(); }
    IDWriteFactory* DWrite() const { return dwrite_.Get(); }
    IWICImagingFactory2* Wic() const { return wic_.Get(); }

private:
    GpuDevice() = default;
    ~GpuDevice();
    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;

    ComPtr<ID3D11Device> d3d_;
    ComPtr<ID3D11DeviceContext> d3dContext_;
    ComPtr<IDXGIDevice> dxgi_;
    ComPtr<ID2D1Factory1> d2dFactory_;
    ComPtr<ID2D1Device> d2dDevice_;
    ComPtr<IDCompositionDevice> comp_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IWICImagingFactory2> wic_;
    bool valid_ = false;
};

}  // namespace tf::gpu
