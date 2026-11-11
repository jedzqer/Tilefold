#pragma once

#include <windows.h>

#include <d2d1_1.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "shell/BoxScan.h"
#include "widgets/WidgetWindow.h"
#include "widgets/ExpandedGeometry.h"

namespace tf::widget {

// 点小文件夹的空处（或“更多”格）-> 放大成屏幕居中的操作面板。
//   · 卡片居中，图标比小面板大一圈，一屏能看全；装不下就滚轮翻
//   · 在面板里按住图标拖动可以改顺序，松手就交给 App 写进 layout.tsv
//   · 点卡片外 / 按 ESC / 点右上角 × 都能关掉
// 它是“用户主动打开的模态层”：唯一一个能抢焦点的部件（要收键盘），
// 也是唯一一个不参与“紧贴桌面宿主”Z 序约束的部件。
class ExpandedFolder {
public:
    ExpandedFolder(box::Group group, RECT workAreaPx);
    ~ExpandedFolder();

    ExpandedFolder(const ExpandedFolder&) = delete;
    ExpandedFolder& operator=(const ExpandedFolder&) = delete;

    bool Show(HINSTANCE instance);
    void Close();
    HWND Hwnd() const;
    void Invalidate();

    // 拖动排序松手后回调：按新的显示顺序给出全部条目完整路径
    void SetReorderedHandler(std::function<void(std::vector<std::wstring>)> fn) {
        onReordered_ = std::move(fn);
    }
    void SetClosedHandler(std::function<void()> fn) { onClosed_ = std::move(fn); }
    // 右键菜单里执行了系统动词（删除/重命名/剪切…会真改磁盘），App 要稍后重扫
    void SetShellVerbHandler(std::function<void()> fn) { onShellVerb_ = std::move(fn); }

private:
    using Geo = ExpandedGeometry;

    Geo ComputeGeo(D2D1_SIZE_F size) const;
    Geo GeoNow() const;                      // 按当前窗口尺寸算一遍
    int ItemAt(float x, float y, const Geo& geo) const;  // 绝对下标，-1 = 没命中
    D2D1_RECT_F CellRect(const Geo& geo, int index) const;
    bool InCard(const Geo& geo, float x, float y) const;
    int MaxScrollRows(const Geo& geo) const;
    void ScrollRows(int delta, const Geo& geo);

    void OnPaint(ID2D1DeviceContext* ctx, D2D1_SIZE_F size);
    void DrawHeader(ID2D1DeviceContext* ctx, const Geo& geo);
    void DrawItem(ID2D1DeviceContext* ctx, const Geo& geo, int index,
                  const D2D1_RECT_F& cell, float alpha);
    void DrawScrollBar(ID2D1DeviceContext* ctx, const Geo& geo);

    bool OnPress(float x, float y);
    void OnMove(float x, float y);
    void OnRelease(float x, float y);
    // 右键落在某个图标上：弹那个条目的原版菜单；落在卡片空白处什么也不弹
    // （卡片外的那圈右键是「关掉面板」，由 OnPress/OnRelease 那条路管）
    void OnRightClick(float x, float y);
    bool OnWheel(float notches);
    bool OnKey(WPARAM vk);
    void RequestClose();

    box::Group group_;
    std::unique_ptr<WidgetWindow> window_;
    RECT workArea_{};
    int scrollRows_ = 0;
    bool closed_ = false;

    // 拖动排序：按下哪个、是否已经算拖动、现在悬在哪个上
    int pressIndex_ = -1;
    bool dragging_ = false;
    POINT pressDip_{};
    int dropIndex_ = -1;

    std::function<void(std::vector<std::wstring>)> onReordered_;
    std::function<void()> onClosed_;
    std::function<void()> onShellVerb_;
};

}  // namespace tf::widget
