#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <wrl/client.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tf::shell {

// 图标缓存：按路径异步提取真实 shell 图标（.lnk 的图标、UWP 的图标、exe 的图标都走
// IShellItemImageFactory），UI 线程只做"有没有就画"的判断。
//
// 为什么必须异步：GetImage 会加载目标程序的图标资源、解压 WindowsApps 里的
// assets，个别情况下会卡几百毫秒。放在 WM_PAINT 里等于把桌面画面上冻。
//
// 为什么不用 SHGetFileInfo：它最大只给 32px（SHGFI_LARGEICON），40dip @125% 需要
// 50px 以上，放大必然糊；而且它拿不到 UWP 应用的磁贴图标。
class IconStore {
public:
    static IconStore& Get();

    void Start();
    void Stop();

    // 从工作线程调用；实现里自己 PostMessage 回 UI 线程
    void SetReadyCallback(std::function<void()> cb);

    // 幂等：同一个路径只提取一次
    void Request(const std::wstring& path);

    // 只在 UI 线程调用。数据已到手就上传成 D2D 位图（首次调用时做）；
    // 还没好就返回 nullptr，调用方画占位色块。
    ID2D1Bitmap* Bitmap(ID2D1DeviceContext* ctx, const std::wstring& path);

    // D3D 设备重建时调用：位图是设备相关的，必须丢掉重传
    void DropDeviceResources();

    static constexpr int kPixelSize = 96;  // 请求边长，够 40dip@150% 用

private:
    IconStore() = default;
    ~IconStore();
    IconStore(const IconStore&) = delete;
    IconStore& operator=(const IconStore&) = delete;

    struct Entry {
        bool ready = false;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;  // 预乘 BGRA
        Microsoft::WRL::ComPtr<ID2D1Bitmap1> bitmap;
    };

    void WorkerLoop();

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::map<std::wstring, Entry, std::less<>> entries_;
    std::deque<std::wstring> queue_;
    std::thread worker_;
    bool stop_ = false;
    std::function<void()> readyCallback_;
};

}  // namespace tf::shell
