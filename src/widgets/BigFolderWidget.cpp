#include "widgets/BigFolderWidget.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

#include "core/Grid.h"
#include "core/Log.h"
#include "render/Layer.h"
#include "shell/Launcher.h"
#include "shell/ShellMenu.h"
#include "widgets/Paint.h"

namespace {

constexpr int kMaxGridDim = 8;

constexpr int kMenuIdDefault = 1100;
// 删除（解散）整个收纳文件夹：里面的图标会被挪回桌面平铺，目录本身删掉
constexpr int kMenuIdDeleteFolder = 1101;
// 新建收纳文件夹：和桌面右键那条一个意思（在 data\box 下再加一个格子）
constexpr int kMenuIdNewBox = 1102;
// 宽度/高度子菜单的 id 段位：id = base + 格数（1..kMaxSpan）
constexpr int kMenuIdWidthBase = 1200;
constexpr int kMenuIdHeightBase = 1300;

// 自定义跨度的上限：和桌面网格的 kMaxSpan 一致，超了 ClampSpan 也会夹回来
constexpr int kCustomSpanMax = tf::grid::kMaxSpan;

// 图标和标签之间唯一的垂直缝（DIP）。
// "图标 + 缝 + 标签" 必须整块塞进一格，所以这个值越小、标签带越薄，
// 格子高度就越富余——富余出来的高度全部给图标，让它和桌面上的 1×1 图标一样大。
constexpr float kLabelGap = 1.0f;

// ---- 翻页动画参数（秒）----
// 往下翻：旧图标原地淡出，新图标从右下角“更多”里依次滑出、由小变大落位。
// 往上翻：反过来，当前图标依次被收回“更多”，上一页原地淡入。
constexpr float kFlyDur = 0.36f;       // 单个图标从“更多”滑到格子的时长
constexpr float kFlyLag = 0.03f;       // 先让旧图标淡一点再开始飞，避免叠影
constexpr float kFlyStartScale = 0.38f;
constexpr float kFadeOutDur = 0.16f;   // 旧图标原地淡出
constexpr float kSuckDur = 0.30f;      // 往上翻：图标收回“更多”
constexpr float kFadeInLag = 0.10f;    // 往上翻：上一页原地淡入的延迟
constexpr float kFadeInDur = 0.24f;
constexpr float kMorphDur = 0.36f;     // “更多” <-> 最后一个图标 的变形
constexpr float kMaxStaggerTotal = 0.26f;  // 依次滑出的总错峰上限（格子多时压缩间隔）
constexpr float kMaxStagger = 0.045f;
constexpr float kQueuedSpeedup = 1.8f;  // 动画中又滚了一下：加速播完当前这段再接下一段

float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float Lerp(float a, float b, float t) { return a + (b - a) * t; }
float EaseOutCubic(float t) {
    const float u = 1.0f - Clamp01(t);
    return 1.0f - u * u * u;
}
float EaseOutQuart(float t) {
    const float u = 1.0f - Clamp01(t);
    return 1.0f - u * u * u * u;
}
float EaseInOutCubic(float t) {
    t = Clamp01(t);
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}
// 落位时轻轻冲过一点再收回（约 4% 过冲，不显得蹦）
float EaseOutBack(float t) {
    t = Clamp01(t);
    constexpr float c1 = 1.10f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
// t 在 [a,b] 区间内从 0 平滑到 1
float Smooth(float a, float b, float t) {
    const float x = Clamp01((t - a) / (b - a));
    return x * x * (3.0f - 2.0f * x);
}

// 右键"大小"子菜单：宽度/高度各一个子菜单，1..kCustomSpanMax 任意组合（2×1 这种也能选），
// 不再用写死的预设表——用户要的 2×1 / 5×3 以前根本没有入口。

}  // namespace

namespace tf::widget {

BigFolderWidget::BigFolderWidget(box::Group group, RECT boundsPx, int spanCols, int spanRows)
    : group_(std::move(group)), spanCols_(spanCols), spanRows_(spanRows) {
    window_ = std::make_unique<WidgetWindow>(group_.name, boundsPx);
}

BigFolderWidget::~BigFolderWidget() { Close(); }

bool BigFolderWidget::Show(HWND host, HINSTANCE instance) {
    if (!window_) return false;

    window_->SetPaintHandler(
        [this](ID2D1DeviceContext* ctx, D2D1_SIZE_F size) { OnPaint(ctx, size); });
    window_->SetClickHandler([this](float x, float y) { OnClick(x, y); });
    window_->SetRightClickHandler([this](float x, float y) { OnRightClick(x, y); });
    window_->SetPressHandler([this](float x, float y) { return OnPress(x, y); });
    window_->SetMouseMoveHandler([this](float x, float y) { OnItemMove(x, y); });
    window_->SetReleaseHandler([this](float x, float y) { OnItemRelease(x, y); });
    window_->SetWheelHandler([this](float notches) { return OnWheel(notches); });
    window_->SetHoverHandler([this](float x, float y) { OnHover(x, y); });
    window_->SetHoldHandler([this](bool down, float x, float y) { OnHold(down, x, y); });
    window_->SetAnimHandler([this](float dt) { return OnAnim(dt); });
    // 鼠标移开面板：滚到一半的列表自动回顶
    window_->SetMouseLeaveHandler([this] { ResetScroll(); });
    window_->SetMovedHandler([this](const RECT&) {
        if (onMoved_) onMoved_();
    });

    return window_->Show(host, instance);
}

void BigFolderWidget::Close() {
    if (window_) {
        window_->Close();
        window_.reset();
    }
}

HWND BigFolderWidget::Hwnd() const { return window_ ? window_->Hwnd() : nullptr; }

void BigFolderWidget::Invalidate() {
    if (window_) window_->Invalidate();
}

void BigFolderWidget::SetBoundsPx(const RECT& boundsPx) {
    if (window_) window_->SetBoundsPx(boundsPx);
}

void BigFolderWidget::SetLabelBandDip(float dip) {
    labelBandDip_ = dip > 0.0f ? dip : 0.0f;
    if (window_) window_->SetBottomBandDip(labelBandDip_);
}

// ---- 几何 ----

// 面板内部就是 spanCols × spanRows 个格子，一格一个图标
int BigFolderWidget::Cols() const { return std::clamp(spanCols_, 1, kMaxGridDim); }

int BigFolderWidget::Rows() const { return std::clamp(spanRows_, 1, kMaxGridDim); }

int BigFolderWidget::VisibleCountAt(int first) const {
    const int total = static_cast<int>(group_.items.size());
    const int maxCells = Rows() * Cols();
    if (total <= maxCells) return total;
    const int remaining = total - first;
    // 还有条目没露头：留最后一格当“更多”
    if (remaining > maxCells) return maxCells - 1;
    return remaining;
}

bool BigFolderWidget::HasMoreAt(int first) const {
    const int total = static_cast<int>(group_.items.size());
    const int maxCells = Rows() * Cols();
    if (total <= maxCells) return false;
    return total - first > maxCells;
}

int BigFolderWidget::VisibleCount() const { return VisibleCountAt(static_cast<int>(firstItem_)); }

bool BigFolderWidget::HasMoreCell() const { return HasMoreAt(static_cast<int>(firstItem_)); }

int BigFolderWidget::HiddenCount() const {
    const int total = static_cast<int>(group_.items.size());
    return std::max(0, total - static_cast<int>(firstItem_) - VisibleCount());
}

int BigFolderWidget::MaxFirst() const {
    const int total = static_cast<int>(group_.items.size());
    return std::max(0, total - Rows() * Cols());
}

// 1×1 的退化情况：只有一个格子，装不下时也不能把“更多”画在同一个位置上
bool BigFolderWidget::CanShowMoreCell() const { return Rows() * Cols() > 1; }

// 一页 = “更多”之外的那些格子。最后一页对齐到末尾（first = MaxFirst），
// 正好把格子填满，“更多”那格变成最后一个图标。
int BigFolderWidget::PageSize() const {
    return std::max(1, Rows() * Cols() - (CanShowMoreCell() ? 1 : 0));
}

int BigFolderWidget::NeighborPage(int from, int dir) const {
    const int ps = PageSize();
    if (dir > 0) return std::min(MaxFirst(), (from / ps + 1) * ps);
    if (from <= 0) return 0;
    // 往回翻落在整页边界上：从对齐到末尾的最后一页往回，也能回到正常分页
    return ((from - 1) / ps) * ps;
}

int BigFolderWidget::SettledTarget() const {
    if (pendingFirst_ >= 0) return pendingFirst_;
    return static_cast<int>(firstItem_);
}

void BigFolderWidget::RequestFirst(int target) {
    target = std::clamp(target, 0, MaxFirst());
    if (target == SettledTarget()) return;
    hoverCell_ = -1;
    pressCell_ = -1;
    if (transActive_) {
        // 正在播：不打断（打断会让飞到一半的图标瞬移），排队，当前这段加速播完再接
        pendingFirst_ = target;
    } else {
        StartTransition(static_cast<int>(firstItem_), target);
    }
    if (window_) window_->RequestAnimation();
}

void BigFolderWidget::StartTransition(int from, int to) {
    transFrom_ = from;
    firstItem_ = static_cast<std::size_t>(to);
    transT_ = 0.0f;
    transActive_ = from != to;
    pendingFirst_ = -1;
    RebuildSlotOrder();
    // 总时长 = 最晚那格的延迟 + 它自己的动画；变形段单独算，取最大
    const float maxDelay = stagger_ * static_cast<float>(std::max(0, Rows() * Cols() - 1));
    float dur = maxDelay + std::max({kFlyLag + kFlyDur, kSuckDur, kFadeInLag + kFadeInDur});
    dur = std::max(dur, MorphStart(to > from) + kMorphDur);
    transDur_ = dur;
}

float BigFolderWidget::MorphStart(bool forward) const {
    // 往下翻到最后一页：等一半图标滑出来之后，“更多”再变成最后一个图标
    // 往上翻离开最后一页：先把“更多”变回来，图标才有地方收进去
    if (!forward) return 0.0f;
    const float maxDelay = stagger_ * static_cast<float>(std::max(0, Rows() * Cols() - 1));
    return maxDelay * 0.5f + 0.06f;
}

// 按离“更多”格的远近排先后：近的先滑出来，像是从里面一个个“吐”出来
void BigFolderWidget::RebuildSlotOrder() {
    const int cols = Cols();
    const int n = Rows() * cols;
    slotRank_.assign(static_cast<std::size_t>(n), 0);
    std::vector<int> order(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) order[static_cast<std::size_t>(i)] = i;
    const int mr = (n - 1) / cols;
    const int mc = (n - 1) % cols;
    auto dist2 = [&](int i) {
        const int dr = i / cols - mr;
        const int dc = i % cols - mc;
        return dr * dr + dc * dc;
    };
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const int da = dist2(a);
        const int db = dist2(b);
        if (da != db) return da < db;
        return a > b;  // 同距离：先右后左、先下后上
    });
    for (int r = 0; r < n; ++r) {
        slotRank_[static_cast<std::size_t>(order[static_cast<std::size_t>(r)])] = r;
    }
    stagger_ = n > 1 ? std::min(kMaxStagger, kMaxStaggerTotal / static_cast<float>(n - 1)) : 0.0f;
}

void BigFolderWidget::ScrollPages(int pages) {
    if (pages == 0 || MaxFirst() <= 0) return;
    int target = SettledTarget();
    const int dir = pages > 0 ? 1 : -1;
    for (int i = 0; i < std::abs(pages); ++i) target = NeighborPage(target, dir);
    RequestFirst(target);
}

void BigFolderWidget::ResetScroll() {
    wheelAccum_ = 0.0f;
    // 正在拖图标时不能换内容：拖出去之后拿到的下标会指错条目
    if (pendingCell_ >= 0) return;
    if (SettledTarget() == 0) return;
    tf::log::Infof(L"鼠标离开 [{}]，滚动回顶", group_.name);
    RequestFirst(0);
}

bool BigFolderWidget::OnWheel(float notches) {
    if (MaxFirst() <= 0) return false;
    // 触控板会送来很多零点几格：攒够一格才翻一页；换方向就清零重攒
    if ((wheelAccum_ > 0.0f && notches < 0.0f) || (wheelAccum_ < 0.0f && notches > 0.0f)) {
        wheelAccum_ = 0.0f;
    }
    wheelAccum_ += notches;
    const int whole = static_cast<int>(wheelAccum_);  // 向 0 取整
    if (whole == 0) return true;
    wheelAccum_ -= static_cast<float>(whole);
    // 向上滚 = notches 正 = 回到上一页
    ScrollPages(-whole);
    const int target = SettledTarget();
    // 每次翻页一条：滚动密集时会连着刷，降到 debug
    tf::log::Debugf(L"滚轮 [{}] notches={:.2f} -> 首条目={} 可见={} 上限={}", group_.name, notches,
                    target, VisibleCountAt(target), MaxFirst());
    return true;
}

BigFolderWidget::Geo BigFolderWidget::ComputeGeo(D2D1_SIZE_F size) const {
    Geo g;
    const float w = size.width;
    const float h = size.height;

    g.pad = std::clamp(std::min(w, h) * 0.042f, 5.0f, 14.0f);
    // 名字已经搬到卡片外面，卡片内部整块都给图标网格
    g.gridTop = g.pad;
    g.gridH = std::max(30.0f, h - 2.0f * g.pad);
    g.rows = Rows();
    g.cellW = (w - 2.0f * g.pad) / static_cast<float>(Cols());
    g.cellH = g.gridH / static_cast<float>(g.rows);

    const float side = std::min(g.cellW, g.cellH);
    // 标签带比 1×1 图标下面那条窄一档（0.19 而不是 0.21，上限 12）：
    // 3×3 时从 12.5 缩到 11.3，省出来的高度正好让图标长回 kNativeIconDip
    g.labelH = std::clamp(g.cellH * 0.19f, 9.0f, 12.0f);
    // 图标大小是固定的（= 外面 1×1 图标的大小），格子多出来的是留白
    g.iconSize = std::clamp(std::min(paint::kNativeIconDip, side - g.labelH - kLabelGap),
                            12.0f, paint::kNativeIconDip);
    return g;
}

D2D1_RECT_F BigFolderWidget::CellRect(const Geo& geo, int index) const {
    const int cols = Cols();
    const int col = index % cols;
    const int row = index / cols;
    const float x = geo.pad + static_cast<float>(col) * geo.cellW;
    const float y = geo.gridTop + static_cast<float>(row) * geo.cellH;
    return D2D1::RectF(x, y, x + geo.cellW, y + geo.cellH);
}

D2D1_RECT_F BigFolderWidget::IconRect(const Geo& geo, const D2D1_RECT_F& cell) const {
    const float cx = (cell.left + cell.right) * 0.5f;
    // 块高 = 图标 + 缝 + 标签，正好等于真正画出来的内容高度
    const float block = geo.iconSize + kLabelGap + geo.labelH;
    const float top = cell.top + std::max(0.0f, ((cell.bottom - cell.top) - block) * 0.5f);
    return D2D1::RectF(cx - geo.iconSize * 0.5f, top, cx + geo.iconSize * 0.5f,
                       top + geo.iconSize);
}

int BigFolderWidget::HitCell(float xDip, float yDip, const Geo& geo) const {
    if (xDip < geo.pad || yDip < geo.gridTop) return -1;
    const int col = static_cast<int>((xDip - geo.pad) / geo.cellW);
    const int row = static_cast<int>((yDip - geo.gridTop) / geo.cellH);
    const int cols = Cols();
    if (col < 0 || col >= cols || row < 0 || row >= geo.rows) return -1;
    return row * cols + col;
}

// ---- 绘制 ----

BigFolderWidget::SlotContent BigFolderWidget::ContentAt(int first, int slot) const {
    const int total = static_cast<int>(group_.items.size());
    const int n = Rows() * Cols();
    if (slot == n - 1 && HasMoreAt(first) && CanShowMoreCell()) return {SlotKind::More, -1};
    const int idx = first + slot;
    if (idx >= 0 && idx < total) return {SlotKind::Item, idx};
    return {SlotKind::None, -1};
}

BigFolderWidget::MoreLook BigFolderWidget::MoreLookAt(int first) const {
    MoreLook look;
    look.firstHidden = first + VisibleCountAt(first);
    look.hiddenCount = std::max(0, static_cast<int>(group_.items.size()) - look.firstHidden);
    return look;
}

void BigFolderWidget::OnPaint(ID2D1DeviceContext* ctx, D2D1_SIZE_F size) {
    // size 包含下面那条名字带；玻璃卡片只占上面 cardH 这么高
    const float cardH = std::max(1.0f, size.height - labelBandDip_);
    const D2D1_SIZE_F card = D2D1::SizeF(size.width, cardH);
    const D2D1_RECT_F body = D2D1::RectF(0.5f, 0.5f, card.width - 0.5f, cardH - 0.5f);
    tf::layer::GlassRect(ctx, body, tf::layer::style::kCornerRadius,
                         D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.14f),
                         D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f));

    const Geo geo = ComputeGeo(card);
    EnsureAnimSlots();

    // 裁到卡片内：飞行中的图标不能溢出玻璃边界
    ctx->PushAxisAlignedClip(D2D1::RectF(0.0f, 0.0f, card.width, cardH),
                             D2D1_ANTIALIAS_MODE_ALIASED);
    if (transActive_) {
        PaintTransition(ctx, geo);
    } else {
        PaintStatic(ctx, geo);
    }
    ctx->PopAxisAlignedClip();

    DrawTitle(ctx, size);

    // 正在拖图标：给被拖的格子画个高亮框，让用户知道拖的是哪个
    if (pendingCell_ >= 0 && itemDragging_) {
        const D2D1_RECT_F cell = CellRect(geo, pendingCell_);
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ring;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.55f),
                                                 ring.GetAddressOf()))) {
            ctx->DrawRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(cell.left + 2, cell.top + 2, cell.right - 2,
                                              cell.bottom - 2),
                                  8.0f, 8.0f),
                ring.Get(), 1.5f);
        }
    }
}

void BigFolderWidget::PaintStatic(ID2D1DeviceContext* ctx, const Geo& geo) {
    if (group_.items.empty()) {
        // 空文件夹（比如刚用右键新建出来的问）：只画一张空白玻璃卡会让人以为坏了，给一句提示
        static Microsoft::WRL::ComPtr<IDWriteTextFormat> emptyFmt;
        if (!emptyFmt) {
            emptyFmt = paint::MakeFormat(11.0f, DWRITE_FONT_WEIGHT_NORMAL,
                                         DWRITE_TEXT_ALIGNMENT_CENTER, true);
            if (emptyFmt) emptyFmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (emptyFmt && SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.62f),
                                                             brush.GetAddressOf()))) {
            const float right = geo.pad + geo.cellW * static_cast<float>(Cols());
            const D2D1_RECT_F rect =
                D2D1::RectF(geo.pad, geo.gridTop, right, geo.gridTop + geo.gridH);
            constexpr std::wstring_view hint = L"空 · 把图标拖进来";
            ctx->DrawTextW(hint.data(), static_cast<UINT32>(hint.size()), emptyFmt.Get(), rect,
                           brush.Get());
        }
        return;
    }

    const int n = geo.rows * Cols();
    const int first = static_cast<int>(firstItem_);
    // 悬停/按下的那格要最后画：放大后会探到旁边的格子上，不能被邻居盖住
    int highlight = pressCell_ >= 0 ? pressCell_ : hoverCell_;
    if (highlight >= n) highlight = -1;

    auto drawSlot = [&](int slot) {
        const CellAnim& a = cellAnim_[static_cast<std::size_t>(slot)];
        const SlotContent c = ContentAt(first, slot);
        if (c.kind == SlotKind::More) {
            DrawMoreCell(ctx, geo, slot, MoreLookAt(first), a.hover, a.press);
        } else if (c.kind == SlotKind::Item) {
            DrawCell(ctx, geo, slot, group_.items[static_cast<std::size_t>(c.index)], Pose{},
                     a.hover, a.press);
        }
    };
    for (int slot = 0; slot < n; ++slot) {
        if (slot != highlight) drawSlot(slot);
    }
    if (highlight >= 0) drawSlot(highlight);
}

D2D1_POINT_2F BigFolderWidget::IconCenter(const Geo& geo, int cellIndex) const {
    const D2D1_RECT_F r = IconRect(geo, CellRect(geo, cellIndex));
    return D2D1::Point2F((r.left + r.right) * 0.5f, (r.top + r.bottom) * 0.5f);
}

void BigFolderWidget::PaintTransition(ID2D1DeviceContext* ctx, const Geo& geo) {
    const int n = geo.rows * Cols();
    const int moreSlot = n - 1;
    const int from = transFrom_;
    const int to = static_cast<int>(firstItem_);
    const bool forward = to > from;
    const float t = transT_;
    const bool canFly = CanShowMoreCell();
    const D2D1_POINT_2F moreC = IconCenter(geo, moreSlot);
    const int maxRank = std::max(0, n - 1);

    // 三层：原地淡入/淡出 -> 飞行（压在“更多”下面，看起来是从里面滑出来的）-> “更多”格
    struct Draw {
        int slot;
        int item;
        Pose pose;
    };
    std::vector<Draw> inPlace;
    std::vector<Draw> flying;
    inPlace.reserve(static_cast<std::size_t>(n) * 2);
    flying.reserve(static_cast<std::size_t>(n));

    bool moreHandled = false;

    for (int s = 0; s < n; ++s) {
        const SlotContent oldC = ContentAt(from, s);
        const SlotContent newC = ContentAt(to, s);

        // “更多”格自己（保持 / 变成最后一个图标 / 变回来）最后单独画
        if (s == moreSlot && (oldC.kind == SlotKind::More || newC.kind == SlotKind::More)) {
            moreHandled = true;
            continue;
        }

        const int rank = slotRank_.empty() ? 0 : slotRank_[static_cast<std::size_t>(s)];
        if (forward) {
            const float d = stagger_ * static_cast<float>(rank);
            if (oldC.kind == SlotKind::Item) {
                const float p = EaseOutCubic((t - d) / kFadeOutDur);
                if (p < 1.0f) {
                    Pose pose;
                    pose.alpha = 1.0f - p;
                    pose.scale = 1.0f - 0.12f * p;
                    inPlace.push_back({s, oldC.index, pose});
                }
            }
            if (newC.kind == SlotKind::Item) {
                const float p = Clamp01((t - d - kFlyLag) / kFlyDur);
                if (p > 0.0f) {
                    Pose pose;
                    if (canFly && s != moreSlot) {
                        const D2D1_POINT_2F c = IconCenter(geo, s);
                        const float e = EaseOutQuart(p);
                        pose.dx = (moreC.x - c.x) * (1.0f - e);
                        pose.dy = (moreC.y - c.y) * (1.0f - e);
                        pose.scale = Lerp(kFlyStartScale, 1.0f, EaseOutBack(p));
                        pose.alpha = Smooth(0.0f, 0.22f, p);
                        pose.labelAlpha = Smooth(0.55f, 1.0f, p);
                        flying.push_back({s, newC.index, pose});
                    } else {
                        const float e = EaseOutCubic(p);
                        pose.alpha = e;
                        pose.scale = Lerp(0.9f, 1.0f, e);
                        inPlace.push_back({s, newC.index, pose});
                    }
                }
            }
        } else {
            // 往上翻：远的先收，近的后收，正好是滑出顺序倒过来
            const float d = stagger_ * static_cast<float>(maxRank - rank);
            if (oldC.kind == SlotKind::Item) {
                const float p = Clamp01((t - d) / kSuckDur);
                if (p < 1.0f) {
                    Pose pose;
                    if (canFly && s != moreSlot) {
                        const D2D1_POINT_2F c = IconCenter(geo, s);
                        const float e = EaseInOutCubic(p);
                        pose.dx = (moreC.x - c.x) * e;
                        pose.dy = (moreC.y - c.y) * e;
                        pose.scale = Lerp(1.0f, kFlyStartScale, e);
                        pose.alpha = 1.0f - Smooth(0.55f, 1.0f, p);
                        pose.labelAlpha = 1.0f - Smooth(0.0f, 0.25f, p);
                        flying.push_back({s, oldC.index, pose});
                    } else {
                        const float e = EaseOutCubic(p);
                        pose.alpha = 1.0f - e;
                        pose.scale = 1.0f - 0.12f * e;
                        inPlace.push_back({s, oldC.index, pose});
                    }
                }
            }
            if (newC.kind == SlotKind::Item) {
                const float p = EaseOutCubic((t - d - kFadeInLag) / kFadeInDur);
                if (p > 0.0f) {
                    Pose pose;
                    pose.alpha = p;
                    pose.scale = Lerp(0.9f, 1.0f, p);
                    inPlace.push_back({s, newC.index, pose});
                }
            }
        }
    }

    auto drawList = [&](const std::vector<Draw>& list) {
        for (const Draw& dr : list) {
            DrawCell(ctx, geo, dr.slot, group_.items[static_cast<std::size_t>(dr.item)], dr.pose,
                     0.0f, 0.0f);
        }
    };
    drawList(inPlace);
    drawList(flying);

    if (!moreHandled) return;

    const SlotContent oldC = ContentAt(from, moreSlot);
    const SlotContent newC = ContentAt(to, moreSlot);
    const float mp = Clamp01((t - MorphStart(forward)) / kMorphDur);
    MoreLook look;
    if (oldC.kind == SlotKind::More && newC.kind == SlotKind::More) {
        // 一直是“更多”：吐出一批时轻轻收一下，像被掏空了一点
        look = MoreLookAt(to);
        look.pulse = 1.0f - 0.06f * std::sin(3.14159265f * Clamp01(t / transDur_));
    } else if (oldC.kind == SlotKind::More) {
        // 翻到底：“更多”变成最后一个图标。缩略图沿用旧状态的（“里面还剩的东西”）
        look = MoreLookAt(from);
        look.morph = mp;
        look.target = newC.kind == SlotKind::Item
                          ? &group_.items[static_cast<std::size_t>(newC.index)]
                          : nullptr;
    } else {
        // 离开最后一页：最后一个图标变回“更多”
        look = MoreLookAt(to);
        look.morph = 1.0f - mp;
        look.target = oldC.kind == SlotKind::Item
                          ? &group_.items[static_cast<std::size_t>(oldC.index)]
                          : nullptr;
    }
    DrawMoreCell(ctx, geo, moreSlot, look, 0.0f, 0.0f);
}

void BigFolderWidget::DrawTitle(ID2D1DeviceContext* ctx, D2D1_SIZE_F size) {
    // 名字画在玻璃卡片**下方**那一条带里（带高 = 相邻格子的缝 = 2×margin）。
    // 所以字既不占卡片面积、也不压到下一行的文件夹，只在缝里。
    const float band = labelBandDip_;
    if (band < 5.0f) return;
    const float cardH = std::max(1.0f, size.height - band);

    static Microsoft::WRL::ComPtr<IDWriteTextFormat> nameFmt;
    static float nameSize = 0.0f;
    // 雅黑 UI 的行高 ≈ 字号 × 1.33，字号取带高的 0.72 倍恰好不截字形
    const float want = std::clamp(band * 0.72f, 7.0f, 12.0f);
    if (!nameFmt || std::abs(want - nameSize) > 0.5f) {
        nameSize = want;
        nameFmt = paint::MakeFormat(nameSize, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                    DWRITE_TEXT_ALIGNMENT_CENTER, true);
    }
    if (!nameFmt) return;

    // 名字下面不再附带 "+N"：卡片里已经有一格是"更多"，名字只写名字
    const std::wstring& text = group_.name;

    const D2D1_RECT_F rect = D2D1::RectF(1.0f, cardH, size.width - 1.0f, cardH + band);

    // 字现在落在壁纸上（不再是玻璃上）：浅色壁纸需要一层淡阴影才读得清
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> shadow;
    if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.45f),
                                             shadow.GetAddressOf()))) {
        const D2D1_RECT_F shifted = D2D1::RectF(rect.left + 1.0f, rect.top + 1.0f,
                                                rect.right + 1.0f, rect.bottom + 1.0f);
        ctx->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), nameFmt.Get(), shifted,
                       shadow.Get());
    }

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(ctx->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f),
                                          brush.GetAddressOf()))) {
        return;
    }
    ctx->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), nameFmt.Get(), rect,
                   brush.Get());
}

void BigFolderWidget::DrawCell(ID2D1DeviceContext* ctx, const Geo& geo, int cellIndex,
                               const box::Item& item, const Pose& pose, float hover, float press) {
    if (pose.alpha <= 0.002f) return;
    const D2D1_RECT_F cell = CellRect(geo, cellIndex);
    const D2D1_RECT_F plain = IconRect(geo, cell);

    // 悬停放大 / 按下缩小：绕图标中心缩放，格子本身（和点击热区）完全不动，
    // 所以不会出现“鼠标追着图标跑”的枢轴问题。翻页时再叠加飞行位移和缩放。
    const float k = motion::IconScale(hover, press) * pose.scale;
    const float cx = (plain.left + plain.right) * 0.5f + pose.dx;
    const float cy = (plain.top + plain.bottom) * 0.5f + pose.dy;
    const float hw = (plain.right - plain.left) * 0.5f * k;
    const float hh = (plain.bottom - plain.top) * 0.5f * k;
    const D2D1_RECT_F icon = D2D1::RectF(cx - hw, cy - hh, cx + hw, cy + hh);
    paint::DrawIcon(ctx, icon, item.path, item.displayName,
                    std::max(2.0f, geo.iconSize * 0.18f * pose.scale), pose.alpha);

    // 悬停时文字保持可见和原位；只有翻页过渡影响文字透明度。
    const float alpha = 0.90f * pose.alpha * pose.labelAlpha;
    if (alpha <= 0.02f) return;
    DrawLabel(ctx, geo, cell, plain.bottom + kLabelGap, item.displayName, alpha);
}

void BigFolderWidget::DrawLabel(ID2D1DeviceContext* ctx, const Geo& geo, const D2D1_RECT_F& cell,
                                float top, const std::wstring& text, float alpha) {
    static Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    static float fontSize = 0.0f;
    const float want = std::clamp(geo.labelH * 0.80f, 8.0f, 13.0f);
    if (!fmt || std::abs(want - fontSize) > 0.5f) {
        fontSize = want;
        fmt = paint::MakeFormat(fontSize, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER,
                                true);
    }
    if (!fmt) return;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(ctx->CreateSolidColorBrush(paint::LabelColor(alpha),
                                          brush.GetAddressOf()))) {
        return;
    }
    const D2D1_RECT_F label = D2D1::RectF(cell.left, top, cell.right, top + geo.labelH);
    ctx->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), fmt.Get(), label, brush.Get());
}

void BigFolderWidget::DrawMoreCell(ID2D1DeviceContext* ctx, const Geo& geo, int cellIndex,
                                   const MoreLook& look, float hover, float press) {
    const D2D1_RECT_F cell = CellRect(geo, cellIndex);
    const D2D1_RECT_F plain = IconRect(geo, cell);
    const float k = motion::IconScale(hover, press) * look.pulse;
    const float cx = (plain.left + plain.right) * 0.5f;
    const float cy = (plain.top + plain.bottom) * 0.5f;

    // morph：0 = “更多”（三张错位缩略图），1 = 最后一个图标。
    // 缩略图向中心收拢、转正、放大到图标尺寸，同时和真图标交叉淡化——
    // 终点和普通格子一模一样，动画结束切回静态绘制时不会跳。
    const float m = Clamp01(look.morph);
    const float em = EaseInOutCubic(m);
    const float spread = 1.0f - em;
    const float thumbSize = geo.iconSize * Lerp(0.62f, 1.0f, em) * k;
    const float thumbAlpha = 1.0f - Smooth(0.35f, 0.95f, m);
    const float iconAlpha = look.target != nullptr ? Smooth(0.20f, 0.80f, m) : 0.0f;

    D2D1_MATRIX_3X2_F baseRaw{};
    ctx->GetTransform(&baseRaw);
    const D2D1::Matrix3x2F base = *D2D1::Matrix3x2F::ReinterpretBaseType(&baseRaw);

    // 3 个缩略错位叠放：不是静态 "...", 而是"里面还有东西"的视觉暗示
    constexpr float kDx[3] = {-1.0f, 0.0f, 1.0f};
    constexpr float kDy[3] = {0.42f, -0.28f, 0.52f};
    constexpr float kAngle[3] = {-9.0f, 0.0f, 9.0f};
    if (thumbAlpha > 0.002f) {
        const int available = std::min(look.hiddenCount, 3);
        for (int i = 0; i < available; ++i) {
            const std::size_t idx = static_cast<std::size_t>(look.firstHidden + i);
            if (idx >= group_.items.size()) continue;
            const D2D1_POINT_2F center =
                D2D1::Point2F(cx + kDx[i] * thumbSize * 0.26f * spread,
                              cy + kDy[i] * thumbSize * 0.26f * spread);
            const D2D1_RECT_F tile =
                D2D1::RectF(center.x - thumbSize * 0.5f, center.y - thumbSize * 0.5f,
                            center.x + thumbSize * 0.5f, center.y + thumbSize * 0.5f);
            ctx->SetTransform(D2D1::Matrix3x2F::Rotation(kAngle[i] * spread, center) * base);
            const box::Item& it = group_.items[idx];
            paint::DrawIcon(ctx, tile, it.path, it.displayName,
                            Lerp(6.0f, geo.iconSize * 0.18f, em), thumbAlpha);
            ctx->SetTransform(baseRaw);
        }
    }
    if (iconAlpha > 0.002f) {
        const D2D1_RECT_F tile = D2D1::RectF(cx - thumbSize * 0.5f, cy - thumbSize * 0.5f,
                                             cx + thumbSize * 0.5f, cy + thumbSize * 0.5f);
        paint::DrawIcon(ctx, tile, look.target->path, look.target->displayName,
                        std::max(2.0f, geo.iconSize * 0.18f), iconAlpha);
    }

    // "+N" 角标：变形前半段就缩没，不和图标抢视线
    const float badgeScale = 1.0f - EaseInOutCubic(m / 0.45f);
    if (badgeScale > 0.01f && look.hiddenCount > 0) {
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeFill;
        if (SUCCEEDED(ctx->CreateSolidColorBrush(D2D1::ColorF(0.10f, 0.11f, 0.13f, 0.92f),
                                                 badgeFill.GetAddressOf()))) {
            badgeFill->SetOpacity(badgeScale);
            const std::wstring text = L"+" + std::to_wstring(look.hiddenCount);
            const float bh = std::clamp(geo.iconSize * 0.30f, 13.0f, 20.0f);
            const float bw = bh * 0.6f + 5.5f * static_cast<float>(text.size());
            const D2D1_RECT_F badge = D2D1::RectF(cell.right - bw - 4.0f, cell.top + 4.0f,
                                                  cell.right - 4.0f, cell.top + 4.0f + bh);
            const D2D1_POINT_2F bc = D2D1::Point2F((badge.left + badge.right) * 0.5f,
                                                   (badge.top + badge.bottom) * 0.5f);
            ctx->SetTransform(D2D1::Matrix3x2F::Scale(badgeScale, badgeScale, bc) * base);
            ctx->FillRoundedRectangle(D2D1::RoundedRect(badge, bh * 0.5f, bh * 0.5f),
                                      badgeFill.Get());

            static Microsoft::WRL::ComPtr<IDWriteTextFormat> badgeFmt;
            static float badgeSize = 0.0f;
            const float want = bh * 0.66f;
            if (!badgeFmt || std::abs(want - badgeSize) > 0.5f) {
                badgeSize = want;
                badgeFmt = paint::MakeFormat(badgeSize, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                             DWRITE_TEXT_ALIGNMENT_CENTER, false);
            }
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> badgeText;
            if (badgeFmt && SUCCEEDED(ctx->CreateSolidColorBrush(
                                D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f * badgeScale),
                                badgeText.GetAddressOf()))) {
                ctx->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), badgeFmt.Get(),
                               badge, badgeText.Get());
            }
            ctx->SetTransform(baseRaw);
        }
    }

    // 名字：“更多”往下沉着淡出，最后一个图标的名字从下面浮上来，交叉淡化
    const float baseTop = plain.bottom + kLabelGap;
    const float moreAlpha = 0.80f * (1.0f - Smooth(0.0f, 0.55f, m));
    if (moreAlpha > 0.02f) {
        DrawLabel(ctx, geo, cell, baseTop + 3.0f * em, L"更多", moreAlpha);
    }
    if (look.target != nullptr) {
        const float nameAlpha = 0.90f * Smooth(0.45f, 1.0f, m);
        if (nameAlpha > 0.02f) {
            DrawLabel(ctx, geo, cell, baseTop + 3.0f * (1.0f - em), look.target->displayName,
                      nameAlpha);
        }
    }
}

// ---- 交互 ----

void BigFolderWidget::OnClick(float xDip, float yDip) {
    // 几何必须和 OnPaint 一样只看卡片（不含下面那条名字带），否则下半行的
    // 点击会落到上下一个格子里。
    const D2D1_SIZE_F size = window_->SizeDip();
    const float cardH = std::max(
        1.0f, size.height - labelBandDip_);
    const Geo geo = ComputeGeo(
        D2D1::SizeF(size.width, cardH));
    const int cell = HitCell(xDip, yDip, geo);

    // 空处（卡片留白 / 没落满的格子 / 名字带）：放大成居中面板
    if (cell < 0) {
        tf::log::Infof(L"点击 [{}] 的空处 -> 放大面板", group_.name);
        if (onExpand_) onExpand_();
        return;
    }

    const int visible = VisibleCount();
    if (cell < visible) {
        const int vi = static_cast<int>(firstItem_) + cell;
        if (vi < 0 || vi >= static_cast<int>(group_.items.size())) return;
        const box::Item& item = group_.items[static_cast<std::size_t>(vi)];
        tf::log::Infof(L"点击格子 {} -> {}", cell, item.displayName);
        tf::shell::LaunchPath(item.path);
        return;
    }

    // “更多”格（以及没落满时空着的格子）：放大成面板，不再临时去打开目录
    if (HasMoreCell()) {
        tf::log::Infof(L"点击 [{}] 的更多格（还藏着 {} 个）-> 放大面板", group_.name,
                       HiddenCount());
    } else {
        tf::log::Infof(L"点击 [{}] 的空格子 -> 放大面板", group_.name);
    }
    if (onExpand_) onExpand_();
}

void BigFolderWidget::OnRightClick(float xDip, float yDip) {
    HWND h = Hwnd();
    if (h == nullptr) return;

    // 几何必须和 OnPaint / OnClick 一样只看卡片（不含下面那条名字带）
    const D2D1_SIZE_F size = window_->SizeDip();
    const float cardH = std::max(1.0f, size.height - labelBandDip_);
    const Geo geo = ComputeGeo(D2D1::SizeF(size.width, cardH));
    const int cell = HitCell(xDip, yDip, geo);

    // 落在某个图标上 -> 那个条目自己的原版菜单（打开/剪切/复制/删除/属性…）
    // 其余（空处、名字带、“更多”格）-> 文件夹自己的菜单
    if (cell >= 0 && cell < VisibleCount()) {
        const int vi = static_cast<int>(firstItem_) + cell;
        if (vi >= 0 && vi < static_cast<int>(group_.items.size())) {
            ShowItemMenu(group_.items[static_cast<std::size_t>(vi)]);
            return;
        }
    }
    ShowFolderMenu();
}

void BigFolderWidget::ShowItemMenu(const box::Item& item) {
    HWND h = Hwnd();
    if (h == nullptr) return;

    tf::shell::SystemMenu menu;
    if (!menu.BuildItem(h, item.path)) {
        log::Warnf(L"右键文件夹条目 [{}]：取不到系统菜单 path={}", item.displayName, item.path);
        return;
    }

    POINT pt{};
    if (!::GetCursorPos(&pt)) return;
    const UINT cmd = menu.Track(pt);
    if (cmd == 0) return;

    if (menu.IsShellCommand(cmd)) {
        log::Infof(L"文件夹 [{}] 里右键 [{}] -> 系统动词 id={}", group_.name, item.displayName, cmd);
        // 系统动词可能真的把文件删了/改名了/移走了，App 得稍后重扫
        if (menu.Invoke(cmd)) {
            if (window_) window_->Invalidate();
            if (onShellVerb_) onShellVerb_();
        }
    }
}

void BigFolderWidget::ShowFolderMenu() {
    HWND h = Hwnd();
    if (h == nullptr) return;

    // 收纳文件夹自己只弹我们的项（尺寸 / 恢复默认 / 删除）。
    // 不拼「原版」菜单：那是资源管理器对一个真实目录的动词（打开、剪切、重命名、
    // 删除、属性…），和我们这个格子视图对不上，用户要的是格子设置。
    HMENU menu = ::CreatePopupMenu();
    if (menu == nullptr) return;
    AppendOwnMenuItems(menu);

    POINT pt{};
    if (!::GetCursorPos(&pt)) {
        ::DestroyMenu(menu);
        return;
    }

    // WS_EX_NOACTIVATE 的窗口不是前台窗口；不先 SetForegroundWindow，
    // TrackPopupMenu 的菜单点其它地方不会消失
    ::SetForegroundWindow(h);
    const UINT cmd = ::TrackPopupMenuEx(menu,
                                        TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
                                        pt.x, pt.y, h, nullptr);
    ::DestroyMenu(menu);
    ::PostMessageW(h, WM_NULL, 0, 0);

    auto spanFromId = [](UINT id, int base) {
        const UINT n = id - static_cast<UINT>(base);
        return n >= 1 && n <= static_cast<UINT>(kCustomSpanMax) ? static_cast<int>(n) : 0;
    };

    const int newCols = spanFromId(cmd, kMenuIdWidthBase);
    const int newRows = spanFromId(cmd, kMenuIdHeightBase);
    if (newCols > 0) {
        if (onResize_) onResize_(newCols, spanRows_);
    } else if (newRows > 0) {
        if (onResize_) onResize_(spanCols_, newRows);
    } else if (cmd == kMenuIdDefault) {
        if (onResize_) onResize_(3, 3);
    } else if (cmd == kMenuIdDeleteFolder) {
        if (onDelete_) onDelete_();
    } else if (cmd == kMenuIdNewBox) {
        if (onNewBox_) onNewBox_();
    }
}

void BigFolderWidget::AppendOwnMenuItems(HMENU menu) {
    if (menu == nullptr) return;

    HMENU widthSub = ::CreatePopupMenu();
    HMENU heightSub = ::CreatePopupMenu();
    if (widthSub == nullptr || heightSub == nullptr) {
        if (widthSub != nullptr) ::DestroyMenu(widthSub);
        if (heightSub != nullptr) ::DestroyMenu(heightSub);
        return;
    }

    // 宽度 / 高度各一列 1..6：拼起来就是任意 W×H，不只预设那 8 种
    for (int c = 1; c <= kCustomSpanMax; ++c) {
        const UINT flags = MF_STRING | (c == spanCols_ ? MF_CHECKED : 0u);
        const std::wstring label = std::format(L"{} 列", c);
        ::AppendMenuW(widthSub, flags, kMenuIdWidthBase + static_cast<UINT>(c), label.c_str());
    }
    for (int r = 1; r <= kCustomSpanMax; ++r) {
        const UINT flags = MF_STRING | (r == spanRows_ ? MF_CHECKED : 0u);
        const std::wstring label = std::format(L"{} 行", r);
        ::AppendMenuW(heightSub, flags, kMenuIdHeightBase + static_cast<UINT>(r), label.c_str());
    }

    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // 当前尺寸做成一条灰字，用户不用数勾选也能知道现在几乘几
    const std::wstring current = std::format(L"当前 {}×{}", spanCols_, spanRows_);
    ::AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, current.c_str());
    const std::wstring widthTitle = std::format(L"宽度（现在 {}）", spanCols_);
    const std::wstring heightTitle = std::format(L"高度（现在 {}）", spanRows_);
    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(widthSub), widthTitle.c_str());
    ::AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(heightSub), heightTitle.c_str());
    ::AppendMenuW(menu, MF_STRING, kMenuIdDefault, L"恢复默认 3×3");
    ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    // 新建收纳文件夹：不用先去桌面空白处右键，在任意一个文件夹上就能加一个
    ::AppendMenuW(menu, MF_STRING, kMenuIdNewBox, L"新建收纳文件夹(&N)…");
    // 文案写清楚“图标会平铺到桌面”，否则“删除文件夹”听起来像把里面的东西一起删了
    ::AppendMenuW(menu, MF_STRING, kMenuIdDeleteFolder, L"删除文件夹（图标平铺到桌面）…");
}

bool BigFolderWidget::OnPress(float xDip, float yDip) {
    const D2D1_SIZE_F size = window_->SizeDip();
    const float cardH = std::max(1.0f, size.height - labelBandDip_);
    const Geo geo = ComputeGeo(D2D1::SizeF(size.width, cardH));
    const int cell = HitCell(xDip, yDip, geo);
    // 只有真图标能拖：空处 / “更多”格 交给窗口 -> 拖动整个文件夹，点击时再放大
    if (cell < 0 || cell >= VisibleCount()) return false;

    pendingCell_ = cell;
    pendingItem_ = firstItem_ + static_cast<std::size_t>(cell);
    itemDragging_ = false;
    pressDip_ = POINT{static_cast<LONG>(xDip), static_cast<LONG>(yDip)};
    return true;  // 这次按下由我接管
}

void BigFolderWidget::OnItemMove(float xDip, float yDip) {
    if (pendingCell_ < 0) return;
    if (!itemDragging_) {
        const float dx = xDip - static_cast<float>(pressDip_.x);
        const float dy = yDip - static_cast<float>(pressDip_.y);
        if (dx * dx + dy * dy < 36.0f) return;  // 6dip 以内当抖动
        itemDragging_ = true;
        ::SetCursor(::LoadCursorW(nullptr, IDC_SIZEALL));
        Invalidate();
    }
}

void BigFolderWidget::OnItemRelease(float xDip, float yDip) {
    const int cell = pendingCell_;
    const std::size_t itemIndex = pendingItem_;
    const bool dragged = itemDragging_;
    pendingCell_ = -1;
    itemDragging_ = false;
    ::SetCursor(::LoadCursorW(nullptr, IDC_ARROW));
    if (cell < 0) return;

    if (!dragged) {
        // 没超过阈值 = 点击启动
        OnClick(xDip, yDip);
        return;
    }

    POINT drop{static_cast<LONG>(std::lround(xDip * window_->DpiScale())),
               static_cast<LONG>(std::lround(yDip * window_->DpiScale()))};
    ::ClientToScreen(Hwnd(), &drop);
    tf::log::Infof(L"图标 [{}] 被拖到 ({},{})",
                   itemIndex < group_.items.size()
                       ? group_.items[itemIndex].displayName
                       : L"?",
                   drop.x, drop.y);
    if (onItemDragOut_) onItemDragOut_(itemIndex, drop);
    Invalidate();
}

// ---- 动画 ----

// 每格的动画槽位（悬停 / 按压）
void BigFolderWidget::EnsureAnimSlots() {
    const std::size_t want = static_cast<std::size_t>(Rows() * Cols());
    if (cellAnim_.size() != want) cellAnim_.resize(want);
}

void BigFolderWidget::OnHover(float xDip, float yDip) {
    EnsureAnimSlots();
    int cell = -1;
    if (xDip >= 0.0f && yDip >= 0.0f) {
        const D2D1_SIZE_F size = window_->SizeDip();
        const float cardH =
            std::max(1.0f, size.height - labelBandDip_);
        const Geo geo =
            ComputeGeo(D2D1::SizeF(size.width, cardH));
        cell = HitCell(xDip, yDip, geo);
    }
    if (cell != hoverCell_) {
        hoverCell_ = cell;
        if (window_) window_->RequestAnimation();
    }
}

void BigFolderWidget::OnHold(bool down, float xDip, float yDip) {
    EnsureAnimSlots();
    int cell = -1;
    if (down) {
        const D2D1_SIZE_F size = window_->SizeDip();
        const float cardH =
            std::max(1.0f, size.height - labelBandDip_);
        const Geo geo =
            ComputeGeo(D2D1::SizeF(size.width, cardH));
        const int hit = HitCell(xDip, yDip, geo);
        // 只给真图标做“按下去”的反馈：空处 / “更多”格不缩
        if (hit >= 0 && hit < VisibleCount() && static_cast<int>(firstItem_) + hit < static_cast<int>(group_.items.size())) {
            cell = hit;
        }
    }
    if (pressCell_ != cell) {
        pressCell_ = cell;
        if (window_) window_->RequestAnimation();
    }
}

bool BigFolderWidget::OnAnim(float dt) {
    EnsureAnimSlots();

    bool busy = false;

    for (int i = 0; i < static_cast<int>(cellAnim_.size()); ++i) {
        CellAnim& a = cellAnim_[static_cast<std::size_t>(i)];

        const float hTarget = (i == hoverCell_) ? 1.0f : 0.0f;
        if (a.hover != hTarget || hTarget > 0.0f) {
            const float tau = hTarget > a.hover ? motion::kHoverTauIn : motion::kHoverTauOut;
            a.hover = motion::Approach(a.hover, hTarget, dt, tau);
            if (std::abs(a.hover - hTarget) < 0.003f) {
                a.hover = hTarget;
            } else {
                busy = true;
            }
        }

        const float pTarget = (i == pressCell_) ? 1.0f : 0.0f;
        if (a.press != pTarget || a.pressVel != 0.0f) {
            if (motion::SpringStep(a.press, a.pressVel, pTarget, dt, motion::kPressStiffness,
                                   motion::kPressDamping)) {
                busy = true;
            }
        }
    }

    // 翻页动画
    if (transActive_) {
        // 后面还排着一页：这段加速播完，连滚时不拖沓
        transT_ += dt * (pendingFirst_ >= 0 ? kQueuedSpeedup : 1.0f);
        if (transT_ >= transDur_) {
            transActive_ = false;
            transT_ = 0.0f;
            const int next = pendingFirst_;
            pendingFirst_ = -1;
            if (next >= 0 && next != static_cast<int>(firstItem_)) {
                StartTransition(static_cast<int>(firstItem_), next);
            }
        }
        busy = true;
    }

    return busy;
}

}  // namespace tf::widget
