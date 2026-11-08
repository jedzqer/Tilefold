#include "widgets/ExpandedFolder.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "core/Log.h"
#include "render/Layer.h"
#include "shell/Launcher.h"
#include "shell/ShellMenu.h"
#include "widgets/Paint.h"

namespace {

// 一次拖动至少挪这么多 DIP 才算“拖”，否则当点击启动
constexpr float kDragThreshold = 6.0f;

bool PtIn(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

}  // namespace

namespace tf::widget {

ExpandedFolder::ExpandedFolder(box::Group group, RECT workAreaPx)
    : group_(std::move(group)), workArea_(workAreaPx) {}

ExpandedFolder::~ExpandedFolder() { Close(); }

bool ExpandedFolder::Show(HINSTANCE instance) {
    window_ = std::make_unique<WidgetWindow>(group_.name, workArea_);
    // 面板模式：能激活（收 ESC）、压在普通窗口之上（盖住用户正在用的程序是用户自己要的）
    window_->SetPanelMode(true);
    window_->SetPaintHandler(
        [this](ID2D1DeviceContext* ctx, D2D1_SIZE_F size) { OnPaint(ctx, size); });
    window_->SetPressHandler([this](float x, float y) { return OnPress(x, y); });
    window_->SetMouseMoveHandler([this](float x, float y) { OnMove(x, y); });
    window_->SetReleaseHandler([this](float x, float y) { OnRelease(x, y); });
    window_->SetWheelHandler([this](float notches) { return OnWheel(notches); });
    window_->SetRightClickHandler([this](float x, float y) { OnRightClick(x, y); });
    window_->SetKeyHandler([this](WPARAM vk) { return OnKey(vk); });

    if (!window_->Show(nullptr, instance)) {
        window_.reset();
        return false;
    }
    log::Infof(L"放大面板 [{}] 打开：{} 项", group_.name, group_.items.size());
    return true;
}

void ExpandedFolder::Close() {
    if (window_) {
        window_->Close();
        window_.reset();
    }
}

HWND ExpandedFolder::Hwnd() const { return window_ ? window_->Hwnd() : nullptr; }

void ExpandedFolder::Invalidate() {
    if (window_) window_->Invalidate();
}

ExpandedFolder::Geo ExpandedFolder::GeoNow() const {
    return ComputeGeo(window_ ? window_->SizeDip() : D2D1::SizeF(1.0f, 1.0f));
}

// ---- 几何 ----

ExpandedFolder::Geo ExpandedFolder::ComputeGeo(D2D1_SIZE_F size) const {
    return ComputeExpandedGeometry(size, static_cast<int>(group_.items.size()));
}

bool ExpandedFolder::InCard(const Geo& geo, float x, float y) const {
    return PtIn(geo.card, x, y);
}

int ExpandedFolder::MaxScrollRows(const Geo& geo) const {
    return std::max(0, geo.totalRows - geo.rowsVisible);
}

D2D1_RECT_F ExpandedFolder::CellRect(const Geo& geo, int index) const {
    const int row = index / geo.cols - scrollRows_;
    const int col = index % geo.cols;
    const float x = geo.grid.left + static_cast<float>(col) * geo.cellW;
    const float y = geo.grid.top + static_cast<float>(row) * geo.cellH;
    return D2D1::RectF(x, y, x + geo.cellW, y + geo.cellH);
}

int ExpandedFolder::ItemAt(float x, float y, const Geo& geo) const {
    if (!InCard(geo, x, y)) return -1;
    const float gx = x - geo.grid.left;
    const float gy = y - geo.grid.top;
    if (gx < 0.0f || gy < 0.0f) return -1;
    const int col = static_cast<int>(gx / geo.cellW);
    const int row = static_cast<int>(gy / geo.cellH);
    if (col < 0 || col >= geo.cols || row < 0 || row >= geo.rowsVisible) return -1;
    const int index = (scrollRows_ + row) * geo.cols + col;
    if (index < 0 || index >= static_cast<int>(group_.items.size())) return -1;
    return index;
}

void ExpandedFolder::ScrollRows(int delta, const Geo& geo) {
    const int next = std::clamp(scrollRows_ + delta, 0, MaxScrollRows(geo));
    if (next == scrollRows_) return;
    scrollRows_ = next;
    if (window_) window_->Invalidate();
}

// ---- 绘制 ----

void ExpandedFolder::OnPaint(ID2D1DeviceContext* ctx, D2D1_SIZE_F size) {
    if (ctx == nullptr) return;
    const Geo geo = ComputeGeo(size);
    scrollRows_ = std::clamp(scrollRows_, 0, MaxScrollRows(geo));

    // 背景压暗：既是“模态”提示，也保证卡片在亮壁纸/亮窗口上依然能读
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> dim;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f),
                                             dim.GetAddressOf()))) {
        ctx->FillRectangle(D2D1::RectF(0.0f, 0.0f, size.width, size.height), dim.Get());
    }

    // 卡片用深色玻璃：它下面可能是任何窗口，浅色玻璃会读不清
    tf::layer::GlassRect(ctx, geo.card, tf::layer::style::kCornerRadius,
                         D2D1::ColorF(0.07f, 0.08f, 0.09f, 0.94f),
                         D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.22f));

    DrawHeader(ctx, geo);

    const int count = static_cast<int>(group_.items.size());
    for (int i = 0; i < count; ++i) {
        if (dragging_ && i == pressIndex_) continue;  // 被拖的那个跟着鼠标画
        const int row = i / geo.cols - scrollRows_;
        if (row < 0 || row >= geo.rowsVisible) continue;
        DrawItem(ctx, geo, i, CellRect(geo, i), 1.0f);
    }

    // 拖动时：目标格子画个落点框，被拖的图标画在鼠标下面
    if (dragging_ && pressIndex_ >= 0) {
        if (dropIndex_ >= 0 && dropIndex_ != pressIndex_) {
            const D2D1_RECT_F cell = CellRect(geo, dropIndex_);
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ring;
            if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(0.55f, 0.78f, 1.0f, 0.85f),
                                                     ring.GetAddressOf()))) {
                ctx->FillRoundedRectangle(
                    D2D1::RoundedRect(
                        D2D1::RectF(cell.left + 2.0f, cell.top + 2.0f, cell.right - 2.0f,
                                    cell.bottom - 2.0f),
                        10.0f, 10.0f),
                    ring.Get());
            }
        }
        if (window_ != nullptr) {
            POINT cur{};
            if (::GetCursorPos(&cur)) {
                ::ScreenToClient(window_->Hwnd(), &cur);
                const float scale = std::max(
                    0.5f, window_->DpiScale());
                const float cx = static_cast<float>(cur.x) / scale;
                const float cy = static_cast<float>(cur.y) / scale;
                const float half = geo.iconSize * 0.5f;
                const box::Item& it = group_.items[static_cast<std::size_t>(pressIndex_)];
                paint::DrawIcon(ctx, D2D1::RectF(cx - half, cy - half, cx + half, cy + half),
                                it.path, it.displayName, std::max(6.0f, geo.iconSize * 0.20f));
            }
        }
    }

    DrawScrollBar(ctx, geo);
}

void ExpandedFolder::DrawHeader(ID2D1DeviceContext* ctx, const Geo& geo) {
    const int count = static_cast<int>(group_.items.size());
    const std::wstring title = std::format(L"{}（{} 项）", group_.name, count);

    static Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    static float sizePt = 0.0f;
    if (!fmt || std::abs(sizePt - 15.0f) > 0.5f) {
        sizePt = 15.0f;
        fmt = paint::MakeFormat(sizePt, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING,
                                true);
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (fmt && SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.96f),
                                                    brush.GetAddressOf()))) {
        const D2D1_RECT_F rect = D2D1::RectF(geo.card.left + 24.0f, geo.card.top + 12.0f,
                                             geo.close.left - 12.0f, geo.card.top + 40.0f);
        ctx->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), fmt.Get(), rect,
                       brush.Get());
    }

    // 右上角 ×
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> xBrush;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.70f),
                                             xBrush.GetAddressOf()))) {
        const float cx = (geo.close.left + geo.close.right) * 0.5f;
        const float cy = (geo.close.top + geo.close.bottom) * 0.5f;
        const float r = 5.5f;
        ctx->DrawLine(D2D1::Point2F(cx - r, cy - r), D2D1::Point2F(cx + r, cy + r), xBrush.Get(),
                      1.6f);
        ctx->DrawLine(D2D1::Point2F(cx - r, cy + r), D2D1::Point2F(cx + r, cy - r), xBrush.Get(),
                      1.6f);
    }
}

void ExpandedFolder::DrawItem(ID2D1DeviceContext* ctx, const Geo& geo, int index,
                              const D2D1_RECT_F& cell, float alpha) {
    if (index < 0 || index >= static_cast<int>(group_.items.size())) return;
    const box::Item& item = group_.items[static_cast<std::size_t>(index)];

    const float cx = (cell.left + cell.right) * 0.5f;
    const float blockH = geo.iconSize + 4.0f + geo.labelH;
    const float top = cell.top + std::max(0.0f, ((cell.bottom - cell.top) - blockH) * 0.5f);
    const D2D1_RECT_F icon =
        D2D1::RectF(cx - geo.iconSize * 0.5f, top, cx + geo.iconSize * 0.5f, top + geo.iconSize);
    paint::DrawIcon(ctx, icon, item.path, item.displayName, std::max(6.0f, geo.iconSize * 0.20f));

    static Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    static float sizePt = 0.0f;
    const float want = std::clamp(geo.labelH * 0.86f, 9.0f, 13.0f);
    if (!fmt || std::abs(want - sizePt) > 0.5f) {
        sizePt = want;
        fmt = paint::MakeFormat(sizePt, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
                                true);
    }
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (fmt && SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.92f * alpha),
                                                    brush.GetAddressOf()))) {
        const D2D1_RECT_F label =
            D2D1::RectF(cell.left, icon.bottom + 4.0f, cell.right,
                        icon.bottom + 4.0f + geo.labelH);
        ctx->DrawTextW(item.displayName.c_str(), static_cast<UINT32>(item.displayName.size()),
                       fmt.Get(), label, brush.Get());
    }
}

void ExpandedFolder::DrawScrollBar(ID2D1DeviceContext* ctx, const Geo& geo) {
    if (!geo.scrollable) return;
    const float trackX = geo.card.right - 12.0f;
    const float top = geo.grid.top;
    const float bottom = geo.grid.bottom;
    const float h = bottom - top;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> track;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f),
                                             track.GetAddressOf()))) {
        ctx->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(trackX, top, trackX + 4.0f, bottom), 2.0f, 2.0f),
            track.Get());
    }

    const float ratio = static_cast<float>(geo.rowsVisible) / static_cast<float>(geo.totalRows);
    const float thumbH = std::max(20.0f, h * ratio);
    const int maxScroll = std::max(1, MaxScrollRows(geo));
    const float t = static_cast<float>(scrollRows_) / static_cast<float>(maxScroll);
    const float thumbTop = top + (h - thumbH) * std::clamp(t, 0.0f, 1.0f);

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> thumb;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.45f),
                                             thumb.GetAddressOf()))) {
        ctx->FillRoundedRectangle(
            D2D1::RoundedRect(
                D2D1::RectF(trackX, thumbTop, trackX + 4.0f, thumbTop + thumbH), 2.0f, 2.0f),
            thumb.Get());
    }
}

// ---- 交互 ----

bool ExpandedFolder::OnPress(float x, float y) {
    const Geo geo = GeoNow();
    pressIndex_ = -1;
    dragging_ = false;
    dropIndex_ = -1;
    pressDip_ = POINT{static_cast<LONG>(std::lround(x)), static_cast<LONG>(std::lround(y))};

    if (PtIn(geo.close, x, y)) {
        RequestClose();
        return true;
    }
    if (!InCard(geo, x, y)) {
        // 点卡片外的暗背景：关掉（和“点了别处”一个意思）
        RequestClose();
        return true;
    }
    pressIndex_ = ItemAt(x, y, geo);
    return true;  // 面板不拖动：所有按下都由面板自己处理
}

void ExpandedFolder::OnRightClick(float x, float y) {
    const Geo geo = GeoNow();
    // 卡片外的暗背景 / 关闭键：右键不做任何事（左键才是"关掉面板"）
    if (!InCard(geo, x, y) || PtIn(geo.close, x, y)) return;

    const int index = ItemAt(x, y, geo);
    const bool onItem = index >= 0 && index < static_cast<int>(group_.items.size());

    // 和资源管理器里右键一模一样：点在图标上就是那个条目的一套动词（打开/剪切/
    // 复制/删除/重命名/属性…），点在卡片空白处就是目录本身的一套（打开/新建/
    // 粘贴/属性…）。我们自己一条都不拼。
    tf::shell::SystemMenu menu;
    const std::wstring target = onItem ? group_.items[static_cast<std::size_t>(index)].path
                                       : group_.dir;
    if (!menu.BuildItem(Hwnd(), target)) {
        log::Warnf(L"放大面板右键：取不到系统菜单 path={}", target);
        return;
    }

    POINT pt{};
    if (!::GetCursorPos(&pt)) return;
    const UINT cmd = menu.Track(pt);
    if (cmd == 0) return;
    if (menu.IsShellCommand(cmd)) {
        log::Infof(L"放大面板 [{}] 右键 -> 系统动词 id={}", group_.name, cmd);
        if (menu.Invoke(cmd) && onShellVerb_) onShellVerb_();
    }
}

void ExpandedFolder::OnMove(float x, float y) {
    if (pressIndex_ < 0) return;
    const float dx = x - static_cast<float>(pressDip_.x);
    const float dy = y - static_cast<float>(pressDip_.y);
    if (!dragging_) {
        if (dx * dx + dy * dy < kDragThreshold * kDragThreshold) return;
        dragging_ = true;
        ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEALL));
    }
    const Geo geo = GeoNow();
    const int hit = ItemAt(x, y, geo);
    if (hit != dropIndex_) {
        dropIndex_ = hit;
        if (window_) window_->Invalidate();
    }
}

void ExpandedFolder::OnRelease(float /*x*/, float /*y*/) {
    const int from = pressIndex_;
    const bool wasDragging = dragging_;
    const int to = dropIndex_;
    pressIndex_ = -1;
    dragging_ = false;
    dropIndex_ = -1;
    ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));

    if (from < 0) return;

    if (!wasDragging) {
        const box::Item& item = group_.items[static_cast<std::size_t>(from)];
        log::Infof(L"面板点击 -> {}", item.displayName);
        tf::shell::LaunchPath(item.path);
        if (window_) window_->Invalidate();
        return;
    }

    if (to >= 0 && to != from && to < static_cast<int>(group_.items.size())) {
        box::Item moving = group_.items[static_cast<std::size_t>(from)];
        group_.items.erase(group_.items.begin() + from);
        const int insertAt = to > from ? to - 1 : to;
        group_.items.insert(group_.items.begin() + insertAt, std::move(moving));

        std::vector<std::wstring> order;
        order.reserve(group_.items.size());
        for (const box::Item& it : group_.items) order.push_back(it.displayName);
        log::Infof(L"面板里拖动排序：{} -> {}（共 {} 项）", from, insertAt, order.size());
        if (onReordered_) onReordered_(std::move(order));
    }
    if (window_) window_->Invalidate();
}

bool ExpandedFolder::OnWheel(float notches) {
    const Geo geo = GeoNow();
    if (!geo.scrollable) return false;
    const int delta = -static_cast<int>(std::lround(notches));
    ScrollRows(delta, geo);
    return true;
}

bool ExpandedFolder::OnKey(WPARAM vk) {
    const Geo geo = GeoNow();
    switch (vk) {
        case VK_ESCAPE:
            RequestClose();
            return true;
        case VK_UP:
            ScrollRows(-1, geo);
            return true;
        case VK_DOWN:
            ScrollRows(1, geo);
            return true;
        case VK_PRIOR:
            ScrollRows(-geo.rowsVisible, geo);
            return true;
        case VK_NEXT:
            ScrollRows(geo.rowsVisible, geo);
            return true;
        case VK_HOME:
            ScrollRows(-9999, geo);
            return true;
        case VK_END:
            ScrollRows(9999, geo);
            return true;
        default:
            return false;
    }
}

void ExpandedFolder::RequestClose() {
    if (closed_) return;
    closed_ = true;
    log::Infof(L"放大面板 [{}] 关闭", group_.name);
    // 先把窗口藏掉，等 App 在消息循环里把这个对象销毁（不能在自己的 wndproc 里自毁）
    if (window_ && window_->Hwnd() != nullptr) {
        ::ShowWindow(window_->Hwnd(), SW_HIDE);
    }
    if (onClosed_) onClosed_();
}

}  // namespace tf::widget
