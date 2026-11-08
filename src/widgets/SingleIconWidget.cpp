#include "widgets/SingleIconWidget.h"

#include <algorithm>
#include <cmath>

#include "core/Log.h"
#include "shell/Launcher.h"
#include "shell/ShellMenu.h"
#include "widgets/Motion.h"
#include "widgets/Paint.h"

namespace {

constexpr int kMenuIdOpen = 2001;
constexpr int kMenuIdReturn = 2002;

}  // namespace

namespace tf::widget {

SingleIconWidget::SingleIconWidget(std::wstring groupName, box::Item item, RECT boundsPx)
    : groupName_(std::move(groupName)), item_(std::move(item)) {
    window_ = std::make_unique<WidgetWindow>(item_.displayName, boundsPx);
}

SingleIconWidget::~SingleIconWidget() { Close(); }

bool SingleIconWidget::Show(HWND host, HINSTANCE instance) {
    if (!window_) return false;
    window_->SetPaintHandler(
        [this](ID2D1DeviceContext* ctx, D2D1_SIZE_F size) { OnPaint(ctx, size); });
    window_->SetClickHandler([this](float, float) { OnClick(); });
    window_->SetRightClickHandler([this](float, float) { OnRightClick(); });
    window_->SetMovedHandler([this](const RECT&) {
        if (onMoved_) onMoved_();
    });
    // 悬停放大 + 文字淡出，按下缩小、松手弹簧回弹
    window_->SetHoverHandler([this](float x, float y) { OnHover(x, y); });
    window_->SetHoldHandler([this](bool down, float, float) { OnHold(down); });
    window_->SetAnimHandler([this](float dt) { return OnAnim(dt); });

    return window_->Show(host, instance);
}

void SingleIconWidget::Close() {
    if (window_) {
        window_->Close();
        window_.reset();
    }
}

HWND SingleIconWidget::Hwnd() const { return window_ ? window_->Hwnd() : nullptr; }

void SingleIconWidget::Invalidate() {
    if (window_) window_->Invalidate();
}

void SingleIconWidget::SetBoundsPx(const RECT& boundsPx) {
    if (window_) window_->SetBoundsPx(boundsPx);
}

void SingleIconWidget::SetLabelBandDip(float dip) {
    labelBandDip_ = dip > 0.0f ? dip : 0.0f;
    if (window_) window_->SetBottomBandDip(labelBandDip_);
}

void SingleIconWidget::OnPaint(ID2D1DeviceContext* ctx, D2D1_SIZE_F size) {
    const float w = size.width;
    // size 包含下面那条名字带；格子本身只有 cardH 这么高
    const float cardH = std::max(1.0f, size.height - labelBandDip_);

    // 1×1 格子要和原生桌面图标看上去一样大：图标恒为 kNativeIconDip（和文件夹里一致），
    // 在格子内居中；名字完全搬到格子上方那条缝里，不再从格子里扣高度
    const float iconSize = std::min(paint::kNativeIconDip, std::max(16.0f, cardH - 2.0f));
    const float cx = w * 0.5f;
    const float cy = cardH * 0.5f;

    // 悬停放大 / 按下缩小：绕图标中心缩放
    const float k = motion::IconScale(hover_, press_);
    const float side = iconSize * k;
    const D2D1_RECT_F icon = D2D1::RectF(cx - side * 0.5f, cy - side * 0.5f, cx + side * 0.5f,
                                         cy + side * 0.5f);
    paint::DrawIcon(ctx, icon, item_.path, item_.displayName, std::max(4.0f, iconSize * 0.18f));

    const float band = labelBandDip_;
    if (band < 5.0f) return;

    static Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    static float fontSize = 0.0f;
    // 行高 ≈ 字号 × 1.33，字号取带高的 0.72 倍恰好不截字形
    const float want = std::clamp(band * 0.72f, 7.0f, 12.0f);
    if (!fmt || std::abs(want - fontSize) > 0.5f) {
        fontSize = want;
        fmt = paint::MakeFormat(fontSize, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
                                true);
    }
    if (!fmt) return;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(ctx->CreateSolidColorBrush(paint::LabelColor(0.92f),
                                          brush.GetAddressOf()))) {
        return;
    }
    // 图标下方加一条很淡的阴影层，浅色壁纸上也读得清（不画底色，保持"桌面图标"的感觉）
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadow;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f),
                                             shadow.GetAddressOf()))) {
        const D2D1_RECT_F label = D2D1::RectF(2.0f, cardH, w - 2.0f, cardH + band);
        D2D1_RECT_F shifted = label;
        shifted.left += 1.0f;
        shifted.right += 1.0f;
        shifted.top += 1.0f;
        shifted.bottom += 1.0f;
        ctx->DrawTextW(item_.displayName.c_str(), static_cast<UINT32>(item_.displayName.size()),
                       fmt.Get(), shifted, shadow.Get());
    }
    const D2D1_RECT_F label = D2D1::RectF(2.0f, cardH, w - 2.0f, cardH + band);
    ctx->DrawTextW(item_.displayName.c_str(), static_cast<UINT32>(item_.displayName.size()),
                   fmt.Get(), label, brush.Get());
}

void SingleIconWidget::OnClick() {
    tf::log::Infof(L"独立图标点击 -> {}", item_.displayName);
    tf::shell::LaunchPath(item_.path);
}

// ---- 悬停 / 按压动画 ----

void SingleIconWidget::OnHover(float xDip, float yDip) {
    const bool in = xDip >= 0.0f && yDip >= 0.0f;
    if (in == hovered_) return;
    hovered_ = in;
    if (window_) window_->RequestAnimation();
}

void SingleIconWidget::OnHold(bool down) {
    if (pressTarget_ == down) return;
    pressTarget_ = down;
    if (window_) window_->RequestAnimation();
}

bool SingleIconWidget::OnAnim(float dt) {
    bool busy = false;

    const float hTarget = hovered_ ? 1.0f : 0.0f;
    if (hover_ != hTarget) {
        const float tau = hTarget > hover_ ? motion::kHoverTauIn : motion::kHoverTauOut;
        hover_ = motion::Approach(hover_, hTarget, dt, tau);
        if (std::abs(hover_ - hTarget) < 0.003f) {
            hover_ = hTarget;
        } else {
            busy = true;
        }
    }

    const float pTarget = pressTarget_ ? 1.0f : 0.0f;
    if (press_ != pTarget || pressVel_ != 0.0f) {
        if (motion::SpringStep(press_, pressVel_, pTarget, dt, motion::kPressStiffness,
                               motion::kPressDamping)) {
            busy = true;
        }
    }

    return busy;
}
void SingleIconWidget::OnRightClick() { ShowMenu(); }

void SingleIconWidget::ShowMenu() {
    HWND h = Hwnd();
    if (h == nullptr) return;

    // 「放回 xx 文件夹」只有从大文件夹里拖出来的图标才有；shell 图标（此电脑/
    // 回收站）groupName_ 是空的，不能拼出一个「放回  文件夹」的死项。
    std::wstring returnLabel;
    if (onReturn_ && !groupName_.empty()) {
        returnLabel = L"放回 " + groupName_ + L" 文件夹";
    }

    // 菜单主体交给 explorer 自己的 IContextMenu 生成：桌面上的文件拿到的是
    // 「打开/剪切/复制/发送到/删除/重命名/属性」这一整套原版动词，快捷方式还能
    // 拿到「以管理员身份运行」「固定到“开始”」；此电脑/回收站拿到「管理」
    // 「属性」「清空回收站」。我们自己只在后面补一两条。
    tf::shell::SystemMenu menu;
    if (!menu.BuildItem(h, item_.path)) {
        log::Warnf(L"右键：取不到系统菜单，只剩自有项 path={}", item_.path);
    }
    if (openExtra_) menu.AppendExtra(L"打开", kMenuIdOpen);
    if (!returnLabel.empty()) menu.AppendExtra(returnLabel, kMenuIdReturn);

    POINT pt{};
    if (!::GetCursorPos(&pt)) return;
    const UINT cmd = menu.Track(pt);
    if (cmd == 0) return;

    if (menu.IsShellCommand(cmd)) {
        // 系统动词可能已经把磁盘上的东西删了/改名了，通知 App 稍后重扫
        if (menu.Invoke(cmd) && onShellVerb_) onShellVerb_();
        return;
    }
    if (cmd == kMenuIdOpen) {
        OnClick();
    } else if (cmd == kMenuIdReturn) {
        if (onReturn_) onReturn_();
    }
}

}  // namespace tf::widget
