#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "shell/BoxScan.h"
#include "widgets/Motion.h"
#include "widgets/WidgetWindow.h"

namespace tf::widget {

// 大文件夹面板（手机式）：
//   · 内部 3 列网格，前 8 格点一下直接启动，第 9 格是"更多"（>8 个条目时才出现）
//   · 文件夹名字画在玻璃面板**下方**、格子与格子的缝里，不占图标区域也不占卡片
//   · 图标占满格子的大部分，格子之间留空隙
//   · 尺寸按桌面网格跨度（N×M 格）决定，右键可改
//   · 按住某个图标拖到面板外 = 把它提成 1×1 的独立图标
class BigFolderWidget {
public:
    BigFolderWidget(box::Group group, RECT boundsPx, int spanCols, int spanRows);
    ~BigFolderWidget();

    BigFolderWidget(const BigFolderWidget&) = delete;
    BigFolderWidget& operator=(const BigFolderWidget&) = delete;

    bool Show(HWND host, HINSTANCE instance);
    void Close();

    HWND Hwnd() const;
    const wchar_t* Name() const { return group_.name.c_str(); }
    const box::Group& Group() const { return group_; }
    RECT BoundsPx() const { return window_ ? window_->BoundsPx() : RECT{}; }
    void Invalidate();

    int SpanCols() const { return spanCols_; }
    int SpanRows() const { return spanRows_; }

    void SetMovedHandler(std::function<void()> fn) { onMoved_ = std::move(fn); }
    void SetResizeHandler(std::function<void(int spanCols, int spanRows)> fn) {
        onResize_ = std::move(fn);
    }
    // 图标被拖出面板：index 是 group_.items 的下标，dropPx 是屏幕物理像素坐标
    void SetItemDragOutHandler(std::function<void(std::size_t index, POINT dropPx)> fn) {
        onItemDragOut_ = std::move(fn);
    }
    // 点空处 / 点“更多”格：要求放大成屏幕居中的操作面板
    void SetExpandHandler(std::function<void()> fn) { onExpand_ = std::move(fn); }

    void SetBoundsPx(const RECT& boundsPx);

    // 右键菜单里的「删除文件夹…」：真正干活的是 App（要动磁盘、要重排整层），
    // 而且不能在 widget 自己的窗口过程栈里销毁它，所以只在这里发个请求
    void SetDeleteHandler(std::function<void()> fn) { onDelete_ = std::move(fn); }

    // 右键菜单里的「新建收纳文件夹」：申请一个新的收纳文件夹（建在 data\box 下，
    // 和桌面右键菜单里那条完全一回事，所以也是交给 App 去做）
    void SetNewBoxHandler(std::function<void()> fn) { onNewBox_ = std::move(fn); }

    // 右键菜单里执行了系统动词（删除/重命名/剪切/新建… 会真改磁盘），App 要稍后重扫
    void SetShellVerbHandler(std::function<void()> fn) { onShellVerb_ = std::move(fn); }

    // 滚轮翻页（App 的低级鼠标钩子转过来的：面板永远拿不到焦点，
    // 收不到 WM_MOUSEWHEEL）。返回 true 表示这次滚轮由我叮下。
    bool OnWheel(float notches);

    // 卡片下方那一条“名字带”（DIP）：窗口比卡片高这么多，字就写在
    // 相邻格子的缝里，既不占卡片面积，也和网格节奏对齐。
    void SetLabelBandDip(float dip);

private:
    // 由面板实际尺寸推出来的内部几何（单位 DIP）。size 传的是**卡片**尺寸，
    // 不含下面的名字带——名字带在面板外，和图标区域完全分开。
    struct Geo {
        float pad = 10.0f;
        float gridTop = 10.0f;
        float gridH = 100.0f;
        float cellW = 60.0f;
        float cellH = 60.0f;
        float iconSize = 44.0f;
        float labelH = 12.0f;
        int rows = 1;
    };

    Geo ComputeGeo(D2D1_SIZE_F size) const;
    D2D1_RECT_F CellRect(const Geo& geo, int index) const;
    D2D1_RECT_F IconRect(const Geo& geo, const D2D1_RECT_F& cell) const;
    int HitCell(float xDip, float yDip, const Geo& geo) const;
    int Cols() const;
    int Rows() const;

    // 装不下时滚轮翻页：firstItem_ 是当前左上角那个条目的下标，一次翻一页。
    //   · 还有条目没露头 -> 右下角最后一格画成“更多”，标注还藏着几个
    //   · 往下翻：新图标从“更多”里依次滑出、由小变大落位，旧图标原地淡出
    //   · 翻到底 -> “更多”变形成最后一个图标，+N 收掉，名字换成图标名
    //   · 鼠标移开面板 -> 自动回顶（用户不用手动拨回去）
    int VisibleCount() const;
    int VisibleCountAt(int first) const;
    bool HasMoreCell() const;
    bool HasMoreAt(int first) const;
    int HiddenCount() const;
    int MaxFirst() const;
    int PageSize() const;
    int NeighborPage(int from, int dir) const;
    int SettledTarget() const;  // 动画（含排队）全部播完后会停在哪
    void ScrollPages(int pages);
    void RequestFirst(int target);
    void ResetScroll();
    // 1×1 的退化情况：只有一个格子，装不下时也不能把“更多”画在同一个位置上
    bool CanShowMoreCell() const;

    void OnPaint(ID2D1DeviceContext* ctx, D2D1_SIZE_F size);
    void OnClick(float xDip, float yDip);
    void OnRightClick(float xDip, float yDip);
    // 右键落在某个图标格上：弹这个条目自己的原版菜单（打开/剪切/复制/删除/属性…）
    void ShowItemMenu(const box::Item& item);
    // 右键落在空处 / 名字带 / “更多”格：目录本身的原版菜单 + 我们自己的尺寸/删除项
    void ShowFolderMenu();
    // 宽度/高度子菜单 + 恢复默认 + 删除文件夹（追加到 menu 尾部）
    void AppendOwnMenuItems(HMENU menu);

    // ---- 动画 ----
    // 每格两个量：悬停进度（指数趋近，不回弹）+ 按下程度（弹簧，松手过冲）。
    // 只在真的动的时候才起 16ms 定时器，静止时零开销。
    struct CellAnim {
        float hover = 0.0f;
        float press = 0.0f;
        float pressVel = 0.0f;
    };
    void EnsureAnimSlots();
    void OnHover(float xDip, float yDip);
    void OnHold(bool down, float xDip, float yDip);
    bool OnAnim(float dt);

    // 翻页过渡：transFrom_ -> firstItem_，transT_ 走到 transDur_ 结束
    void StartTransition(int from, int to);
    void RebuildSlotOrder();
    float MorphStart(bool forward) const;

    // 某一页某一格放的是什么
    enum class SlotKind { None, Item, More };
    struct SlotContent {
        SlotKind kind = SlotKind::None;
        int index = -1;  // group_.items 下标（Item 时有效）
    };
    SlotContent ContentAt(int first, int slot) const;

    // 图标的一次性姿态：飞行位移 / 缩放 / 透明度（静止时全是默认值）
    struct Pose {
        float dx = 0.0f;
        float dy = 0.0f;
        float scale = 1.0f;
        float alpha = 1.0f;
        float labelAlpha = 1.0f;
    };
    // “更多”格的样子：morph 0 = 更多，1 = 变成 target 那个图标
    struct MoreLook {
        int firstHidden = 0;
        int hiddenCount = 0;
        float morph = 0.0f;
        float pulse = 1.0f;
        const box::Item* target = nullptr;
    };
    MoreLook MoreLookAt(int first) const;
    D2D1_POINT_2F IconCenter(const Geo& geo, int cellIndex) const;
    void PaintStatic(ID2D1DeviceContext* ctx, const Geo& geo);
    void PaintTransition(ID2D1DeviceContext* ctx, const Geo& geo);

    bool OnPress(float xDip, float yDip);
    void OnItemMove(float xDip, float yDip);
    void OnItemRelease(float xDip, float yDip);

    // pose 是翻页动画的姿态；hover/press 是这格的悬停/按压进度。
    void DrawCell(ID2D1DeviceContext* ctx, const Geo& geo, int cellIndex, const box::Item& item,
                  const Pose& pose, float hover, float press);
    void DrawMoreCell(ID2D1DeviceContext* ctx, const Geo& geo, int cellIndex, const MoreLook& look,
                      float hover, float press);
    void DrawLabel(ID2D1DeviceContext* ctx, const Geo& geo, const D2D1_RECT_F& cell, float top,
                   const std::wstring& text, float alpha);
    void DrawTitle(ID2D1DeviceContext* ctx, D2D1_SIZE_F size);

    box::Group group_;
    std::unique_ptr<WidgetWindow> window_;
    int spanCols_ = 2;
    int spanRows_ = 2;
    float labelBandDip_ = 0.0f;  // 卡片下方的名字带高度（DIP）

    std::function<void()> onMoved_;
    std::function<void(int, int)> onResize_;
    std::function<void(std::size_t, POINT)> onItemDragOut_;
    std::function<void()> onExpand_;
    std::function<void()> onDelete_;
    std::function<void()> onNewBox_;
    std::function<void()> onShellVerb_;

    // 滚动的起点（group_.items 下标）。移出面板 / 内容变了都回顶。
    std::size_t firstItem_ = 0;

    // 翻页过渡（firstItem_ 已经是目标页，过渡期间画 transFrom_ -> firstItem_ 的混合）
    int transFrom_ = 0;
    float transT_ = 0.0f;
    float transDur_ = 0.5f;
    bool transActive_ = false;
    int pendingFirst_ = -1;        // 动画中又滚了：排队的下一页，-1 = 没有
    float wheelAccum_ = 0.0f;      // 触控板小数格累计
    std::vector<int> slotRank_;    // 每格的出场顺序（离“更多”越近越先）
    float stagger_ = 0.03f;        // 相邻出场顺序之间的延迟（秒）

    // 悬停/按压（按“格”算，和 HitCell 的返回值同一个坐标系）
    std::vector<CellAnim> cellAnim_;
    int hoverCell_ = -1;
    int pressCell_ = -1;

    // 图标拖出：按下时记住格子，超过阈值才算拖动，否则当点击启动
    int pendingCell_ = -1;
    // 按下那个条目的绝对下标。滚动之后局部格号会变，拖出时必须用绝对下标。
    std::size_t pendingItem_ = 0;
    bool itemDragging_ = false;
    POINT pressDip_{};
};

}  // namespace tf::widget
