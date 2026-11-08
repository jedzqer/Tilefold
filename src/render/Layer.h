#pragma once

#include <d2d1_1.h>

namespace tf::layer {

// 当前渲染管线的统一视觉常量。全部以 DIP 计（96 DPI 基准）。
namespace style {
inline constexpr float kCornerRadius = 16.0f;
inline constexpr float kTileCornerRadius = 12.0f;
inline constexpr float kBorderWidth = 1.0f;
}  // namespace style

// 半透明玻璃底：填充 + 1px 内侧高光描边
void GlassRect(ID2D1DeviceContext* ctx, const D2D1_ROUNDED_RECT& rrect,
               const D2D1_COLOR_F& fill, const D2D1_COLOR_F& innerStroke);

// 便捷版：给定矩形和半径
void GlassRect(ID2D1DeviceContext* ctx, const D2D1_RECT_F& rect, float radius,
               const D2D1_COLOR_F& fill, const D2D1_COLOR_F& innerStroke);

// 顶部高光带（模拟亚克力的边缘反光），alpha 从 a 衰减到 0
void TopSheen(ID2D1DeviceContext* ctx, const D2D1_RECT_F& rect, float radius, float alpha);

}  // namespace tf::layer
