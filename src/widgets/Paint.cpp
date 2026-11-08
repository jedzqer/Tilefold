#include "widgets/Paint.h"

#include <algorithm>

#include "render/GpuDevice.h"
#include "shell/IconStore.h"

namespace tf::widget::paint {

namespace {
tf::cfg::LabelColor g_labelColor = tf::cfg::LabelColor::Automatic;
}

void SetLabelColor(tf::cfg::LabelColor color) { g_labelColor = color; }

D2D1_COLOR_F LabelColor(float alpha) {
    return g_labelColor == tf::cfg::LabelColor::Black
               ? D2D1::ColorF(0.0f, 0.0f, 0.0f, alpha)
               : D2D1::ColorF(1.0f, 1.0f, 1.0f, alpha);
}

namespace {

// 占位块用白/浅色文字，深色背景保证任何壁纸下都看得清
constexpr float kGlyphRatio = 0.42f;  // 首字字号 = 方块边长 * 该比例

}  // namespace

D2D1_COLOR_F AccentFor(const std::wstring& name) {
    static const D2D1_COLOR_F kPalette[] = {
        {0.36f, 0.55f, 0.98f, 0.94f},  // blue
        {0.42f, 0.78f, 0.55f, 0.94f},  // green
        {0.95f, 0.66f, 0.32f, 0.94f},  // orange
        {0.86f, 0.44f, 0.55f, 0.94f},  // pink
        {0.62f, 0.50f, 0.92f, 0.94f},  // purple
        {0.35f, 0.76f, 0.82f, 0.94f},  // teal
        {0.93f, 0.52f, 0.42f, 0.94f},  // coral
        {0.55f, 0.62f, 0.70f, 0.94f},  // slate
    };
    uint32_t h = 2166136261u;
    for (const wchar_t c : name) {
        h ^= static_cast<uint32_t>(c | 0x20);  // 大小写不敏感
        h *= 16777619u;
    }
    return kPalette[h % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

std::wstring FirstGlyph(const std::wstring& name) {
    for (const wchar_t c : name) {
        if (c == L' ' || c == L'\t') continue;
        std::wstring g(1, c);
        if (c >= L'a' && c <= L'z') g[0] = static_cast<wchar_t>(c - L'a' + L'A');
        return g;
    }
    return L"?";
}

Microsoft::WRL::ComPtr<IDWriteTextFormat> MakeFormat(float sizePt, DWRITE_FONT_WEIGHT weight,
                                                     DWRITE_TEXT_ALIGNMENT align, bool ellipsis) {
    IDWriteFactory* dwrite = tf::gpu::GpuDevice::Get().DWrite();
    if (dwrite == nullptr) return nullptr;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    const HRESULT hr = dwrite->CreateTextFormat(L"Microsoft YaHei UI", nullptr, weight,
                                                DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                sizePt, L"zh-CN", fmt.GetAddressOf());
    if (FAILED(hr)) return nullptr;

    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    fmt->SetTextAlignment(align);

    if (ellipsis) {
        Microsoft::WRL::ComPtr<IDWriteInlineObject> trimming;
        if (SUCCEEDED(dwrite->CreateEllipsisTrimmingSign(fmt.Get(), trimming.GetAddressOf()))) {
            DWRITE_TRIMMING trim{};
            trim.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
            fmt->SetTrimming(&trim, trimming.Get());
        }
    }
    return fmt;
}

void DrawIcon(ID2D1DeviceContext* ctx, const D2D1_RECT_F& tile, const std::wstring& path,
              const std::wstring& name, float cornerRadius, float opacity) {
    if (ctx == nullptr) return;
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    if (opacity <= 0.0f) return;

    if (ID2D1Bitmap* icon = tf::shell::IconStore::Get().Bitmap(ctx, path)) {
        ctx->DrawBitmap(icon, tile, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
        return;
    }

    const float side = std::min(tile.right - tile.left, tile.bottom - tile.top);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fill;
    if (FAILED(ctx->CreateSolidColorBrush(AccentFor(name), fill.GetAddressOf()))) return;
    fill->SetOpacity(opacity);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(tile, cornerRadius, cornerRadius), fill.Get());

    // 顶部高光，让占位块不显得死板
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> sheen;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.16f),
                                             sheen.GetAddressOf()))) {
        sheen->SetOpacity(opacity);
        const D2D1_RECT_F top = D2D1::RectF(tile.left, tile.top, tile.right,
                                            tile.top + (tile.bottom - tile.top) * 0.5f);
        ctx->FillRoundedRectangle(D2D1::RoundedRect(top, cornerRadius, cornerRadius), sheen.Get());
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> glyphBrush;
    if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.96f),
                                          glyphBrush.GetAddressOf()))) {
        return;
    }
    glyphBrush->SetOpacity(opacity);
    const Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt =
        MakeFormat(std::max(8.0f, side * kGlyphRatio), DWRITE_FONT_WEIGHT_BOLD,
                   DWRITE_TEXT_ALIGNMENT_CENTER, false);
    if (!fmt) return;
    const std::wstring g = FirstGlyph(name);
    ctx->DrawTextW(g.c_str(), static_cast<UINT32>(g.size()), fmt.Get(), tile, glyphBrush.Get());
}

}  // namespace tf::widget::paint
