#include "shell/IconStore.h"

#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <utility>

#include "core/Log.h"

namespace {

using Microsoft::WRL::ComPtr;

// HBITMAP(32bpp) -> 预乘 BGRA
bool HBitmapToBgra(HBITMAP hbmp, std::vector<unsigned char>& out, int& width, int& height) {
    BITMAP bm{};
    if (::GetObjectW(hbmp, sizeof(bm), &bm) == 0) return false;
    const int w = bm.bmWidth;
    const int h = bm.bmHeight;
    if (w <= 0 || h <= 0) return false;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;  // 负高度 = 自顶向下，正好是 D2D 要的顺序
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    std::vector<unsigned char> buf(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    HDC dc = ::GetDC(nullptr);
    const int lines = ::GetDIBits(dc, hbmp, 0, static_cast<UINT>(h), buf.data(), &bi, DIB_RGB_COLORS);
    ::ReleaseDC(nullptr, dc);
    if (lines != h) return false;

    // 不同 shell 来源给的是"直通 alpha"或"预乘 alpha"，判别方式：出现任一颜色通道
    // 大于 alpha 的像素，就说明它是直通 alpha（预乘时通道恒 <= alpha）。
    bool straight = false;
    for (size_t i = 0; i + 3 < buf.size(); i += 4) {
        const unsigned char a = buf[i + 3];
        if (buf[i] > a || buf[i + 1] > a || buf[i + 2] > a) {
            straight = true;
            break;
        }
    }
    if (straight) {
        for (size_t i = 0; i + 3 < buf.size(); i += 4) {
            const unsigned int a = buf[i + 3];
            if (a == 0 || a == 255) continue;
            buf[i] = static_cast<unsigned char>((buf[i] * a) / 255u);
            buf[i + 1] = static_cast<unsigned char>((buf[i + 1] * a) / 255u);
            buf[i + 2] = static_cast<unsigned char>((buf[i + 2] * a) / 255u);
        }
    }

    width = w;
    height = h;
    out = std::move(buf);
    return true;
}

bool ExtractIcon(const std::wstring& path, int pixels, std::vector<unsigned char>& out, int& width,
                 int& height) {
    ComPtr<IShellItemImageFactory> factory;
    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF attrs = 0;
    HRESULT hr = ::SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, &attrs);
    if (SUCCEEDED(hr) && pidl != nullptr) {
        hr = ::SHCreateItemFromIDList(pidl, IID_PPV_ARGS(factory.GetAddressOf()));
        ::CoTaskMemFree(pidl);
    } else {
        hr = ::SHCreateItemFromParsingName(path.c_str(), nullptr,
                                           IID_PPV_ARGS(factory.GetAddressOf()));
    }

    SIZE size{pixels, pixels};
    HBITMAP hbmp = nullptr;
    if (SUCCEEDED(hr) && factory) {
        // Prefer the high-resolution shell image, which handles virtual items and shortcuts.
        hr = factory->GetImage(size, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbmp);
    }

    // Some shell extensions do not expose IShellItemImageFactory. Ask the shell's
    // association/icon handler as a fallback, including for extension-only paths.
    if (hbmp == nullptr) {
        SHFILEINFOW info{};
        const DWORD_PTR result = ::SHGetFileInfoW(path.c_str(), 0, &info, sizeof(info),
                                                  SHGFI_ICON | SHGFI_LARGEICON);
        if (result != 0 && info.hIcon != nullptr) {
            ICONINFO iconInfo{};
            if (::GetIconInfo(info.hIcon, &iconInfo) && iconInfo.hbmColor != nullptr) {
                hbmp = iconInfo.hbmColor;
                if (iconInfo.hbmMask != nullptr) ::DeleteObject(iconInfo.hbmMask);
            }
            ::DestroyIcon(info.hIcon);
        }
    }
    if (hbmp == nullptr) {
        tf::log::Warnf(L"图标: Shell 图标提取失败 hr=0x{:08X} {}", static_cast<unsigned>(hr), path);
        return false;
    }

    const bool ok = HBitmapToBgra(hbmp, out, width, height);
    ::DeleteObject(hbmp);
    return ok;
}

}  // namespace

namespace tf::shell {

IconStore& IconStore::Get() {
    static IconStore store;
    return store;
}

IconStore::~IconStore() { Stop(); }

void IconStore::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_.joinable()) return;
    stop_ = false;
    worker_ = std::thread([this] { WorkerLoop(); });
}

void IconStore::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!worker_.joinable()) return;
        stop_ = true;
        queue_.clear();
    }
    cv_.notify_all();
    worker_.join();
}

void IconStore::SetReadyCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    readyCallback_ = std::move(cb);
}

void IconStore::Request(const std::wstring& path) {
    if (path.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = entries_.find(path);
    if (it != entries_.end() && (it->second.ready || !it->second.pixels.empty())) return;
    if (it != entries_.end()) return;  // 已排队
    entries_.emplace(path, Entry{});
    queue_.push_back(path);
    // 队列里已有条目时也要唤醒
    cv_.notify_one();
}

void IconStore::WorkerLoop() {
    const HRESULT hrCo = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    for (;;) {
        std::wstring path;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (stop_) break;
            path = queue_.front();
            queue_.pop_front();
        }

        std::vector<unsigned char> pixels;
        int w = 0;
        int h = 0;
        const bool ok = ExtractIcon(path, kPixelSize, pixels, w, h);

        std::function<void()> notify;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = entries_.find(path);
            if (it != entries_.end()) {
                if (ok) {
                    it->second.ready = true;
                    it->second.width = w;
                    it->second.height = h;
                    it->second.pixels = std::move(pixels);
                } else {
                    // 失败也要留个"已处理"的标记，避免每次重绘都重新排队重试
                    it->second.ready = false;
                    it->second.width = 0;
                    it->second.height = 0;
                }
            }
            notify = readyCallback_;
        }
        if (ok && notify) notify();
    }

    if (SUCCEEDED(hrCo)) ::CoUninitialize();
}

ID2D1Bitmap* IconStore::Bitmap(ID2D1DeviceContext* ctx, const std::wstring& path) {
    if (ctx == nullptr) return nullptr;

    // 只擦掉成功提取但位图已被设备丢失清掉的情况
    ID2D1Bitmap1* cached = nullptr;
    int w = 0;
    int h = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(path);
        if (it == entries_.end()) return nullptr;
        Entry& e = it->second;
        if (e.bitmap) return e.bitmap.Get();
        if (!e.ready || e.pixels.empty()) return nullptr;
        w = e.width;
        h = e.height;
        cached = nullptr;

        // 在锁内上传：避免两个 widget 同时为同一路径创建位图
        ComPtr<ID2D1Bitmap1> bmp;
        const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
            96.0f);
        const HRESULT hr = ctx->CreateBitmap(D2D1::SizeU(static_cast<UINT32>(w),
                                                         static_cast<UINT32>(h)),
                                             e.pixels.data(),
                                             static_cast<UINT32>(w) * 4u, props, bmp.GetAddressOf());
        if (FAILED(hr)) {
            tf::log::Warnf(L"图标: CreateBitmap 失败 hr=0x{:08X} {}", static_cast<unsigned>(hr),
                           path);
            return nullptr;
        }
        e.bitmap = bmp;
        e.pixels.clear();
        e.pixels.shrink_to_fit();
        cached = e.bitmap.Get();
    }
    return cached;
}

void IconStore::DropDeviceResources() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [path, entry] : entries_) {
        (void)path;
        entry.bitmap.Reset();
    }
}

}  // namespace tf::shell
