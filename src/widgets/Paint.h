#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>

#include "core/Settings.h"

namespace tf::widget::paint {

// 桌面图标的“标准尺寸”（DIP）。1×1 独立图标和文件夹里的图标都用这个值，
// 这样文件夹里的图标不会因为格子变大而变大——文件夹大小只决定能摆下多少个。
inline constexpr float kNativeIconDip = 46.0f;

// 按名称取稳定色相：同一个 app 每次都是同一个颜色（占位块用）
D2D1_COLOR_F AccentFor(const std::wstring& name);
void SetLabelColor(tf::cfg::LabelColor color);
D2D1_COLOR_F LabelColor(float alpha);

// 首字：中文取一个字，英文取首字母大写
std::wstring FirstGlyph(const std::wstring& name);

// 统一的文本格式工厂（调用方负责缓存 ComPtr，别每帧创建）
Microsoft::WRL::ComPtr<IDWriteTextFormat> MakeFormat(float sizePt, DWRITE_FONT_WEIGHT weight,
                                                     DWRITE_TEXT_ALIGNMENT align, bool ellipsis);

// 画一个图标方块。
//
// 有真实图标就只画图标；没有才画“哈希色圆角块 + 首字”占位。
// 两者绝不能叠画——多数 .lnk 图标四周是透明的，色块会从透明区透出来，
// 看起来像“图标后面还有一个图标”。
// opacity：整体不透明度（文件夹翻页时图标淡入淡出用），默认不透明。
void DrawIcon(ID2D1DeviceContext* ctx, const D2D1_RECT_F& tile, const std::wstring& path,
              const std::wstring& name, float cornerRadius, float opacity = 1.0f);

}  // namespace tf::widget::paint
