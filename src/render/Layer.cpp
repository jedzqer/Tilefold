#include "render/Layer.h"

#include <wrl/client.h>

#include <algorithm>
#include <cmath>

namespace {

using Microsoft::WRL::ComPtr;

void EnsureContextReady(ID2D1DeviceContext* ctx) {
    ctx->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

}  // namespace

namespace tf::layer {

void GlassRect(ID2D1DeviceContext* ctx, const D2D1_ROUNDED_RECT& rrect,
               const D2D1_COLOR_F& fill, const D2D1_COLOR_F& innerStroke) {
    if (ctx == nullptr) return;
    EnsureContextReady(ctx);

    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(fill, brush.GetAddressOf()))) {
        ctx->FillRoundedRectangle(rrect, brush.Get());
    }

    if (innerStroke.a > 0.001f && rrect.rect.right - rrect.rect.left > 2.0f &&
        rrect.rect.bottom - rrect.rect.top > 2.0f) {
        ComPtr<ID2D1SolidColorBrush> stroke;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(innerStroke, stroke.GetAddressOf()))) {
            // 内缩半个描边宽度，让 1px 线落在内部而不是压在外沿
            const float inset = style::kBorderWidth * 0.5f;
            D2D1_ROUNDED_RECT inner{};
            inner.rect.left = rrect.rect.left + inset;
            inner.rect.top = rrect.rect.top + inset;
            inner.rect.right = rrect.rect.right - inset;
            inner.rect.bottom = rrect.rect.bottom - inset;
            inner.radiusX = std::max(0.0f, rrect.radiusX - inset);
            inner.radiusY = std::max(0.0f, rrect.radiusY - inset);
            ctx->DrawRoundedRectangle(inner, stroke.Get(), style::kBorderWidth);
        }
    }
}

void GlassRect(ID2D1DeviceContext* ctx, const D2D1_RECT_F& rect, float radius,
               const D2D1_COLOR_F& fill, const D2D1_COLOR_F& innerStroke) {
    D2D1_ROUNDED_RECT rr{};
    rr.rect = rect;
    rr.radiusX = radius;
    rr.radiusY = radius;
    GlassRect(ctx, rr, fill, innerStroke);
}

void TopSheen(ID2D1DeviceContext* ctx, const D2D1_RECT_F& rect, float radius, float alpha) {
    if (ctx == nullptr || alpha <= 0.001f) return;
    EnsureContextReady(ctx);

    const float h = std::min((rect.bottom - rect.top) * 0.45f, 48.0f);
    if (h <= 0.5f) return;

    D2D1_RECT_F band{rect.left, rect.top, rect.right, rect.top + h};

    ComPtr<ID2D1LinearGradientBrush> grad;
    D2D1_GRADIENT_STOP stops[2]{};
    stops[0].position = 0.0f;
    stops[0].color = D2D1::ColorF(D2D1::ColorF::White, alpha);
    stops[1].position = 1.0f;
    stops[1].color = D2D1::ColorF(D2D1::ColorF::White, 0.0f);

    ComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(ctx->CreateGradientStopCollection(stops, 2, collection.GetAddressOf()))) return;

    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props{};
    props.startPoint = D2D1::Point2F(band.left, band.top);
    props.endPoint = D2D1::Point2F(band.left, band.bottom);
    if (FAILED(ctx->CreateLinearGradientBrush(props, collection.Get(), grad.GetAddressOf()))) return;

    D2D1_ROUNDED_RECT rr{};
    rr.rect = band;
    rr.radiusX = radius;
    rr.radiusY = radius;
    ctx->FillRoundedRectangle(rr, grad.Get());
}

}  // namespace tf::layer
