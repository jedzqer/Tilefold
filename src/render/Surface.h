#pragma once

#include <windows.h>
#include <wrl/client.h>

#include <d2d1_1.h>
#include <dcomp.h>
#include <dxgi1_3.h>

using Microsoft::WRL::ComPtr;

namespace tf::gpu {

// 一个 HWND 对应一个 Surface：
//   DXGI flip-model swap chain -> DComp visual -> 窗口
// 用法：Ensure() 之后 Begin()/End() 包住一帧绘制的全部 D2D 调用。
class Surface {
public:
    Surface() = default;
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    // 幂等：尺寸或窗口不变时什么都不做。返回是否可绘制。
    bool Ensure(HWND hwnd, UINT widthPx, UINT heightPx, float dpiScale = 0.0f);

    // 返回已 SetTarget + BeginDraw 的上下文；失败返回 nullptr
    ID2D1DeviceContext* Begin();

    // EndDraw + Present + Commit。返回是否成功。
    bool End();

    void Destroy();

    bool Valid() const { return swap_ != nullptr; }
    UINT WidthPx() const { return width_; }
    UINT HeightPx() const { return height_; }
    float DpiScale() const { return dpiScale_; }

private:
    bool CreateResources();
    void ReleaseResources();

    HWND hwnd_ = nullptr;
    UINT width_ = 0;
    UINT height_ = 0;
    float dpiScale_ = 1.0f;

    ComPtr<IDXGISwapChain1> swap_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<IDCompositionTarget> compTarget_;
    ComPtr<IDCompositionVisual> visual_;
    ComPtr<ID2D1DeviceContext> context_;
};

}  // namespace tf::gpu
