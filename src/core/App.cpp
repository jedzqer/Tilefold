#include "core/App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#include "core/Autostart.h"
#include "core/Grid.h"
#include "core/Ipc.h"
#include "core/Layout.h"
#include "core/LayoutPlacement.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/PromptDialog.h"
#include "core/ReservedNames.h"
#include "core/Settings.h"
#include "render/GpuDevice.h"
#include "shell/BoxScan.h"
#include "shell/DesktopContextMenu.h"
#include "shell/GroupFolder.h"
#include "shell/IconStore.h"
#include "widgets/FrameClock.h"
#include "widgets/Paint.h"

namespace {

constexpr wchar_t kControlClass[] = L"Tilefold.Control";
constexpr UINT kIconReadyMsg = WM_APP + 1;
constexpr UINT kRelayoutMsg = WM_APP + 2;
// 放大面板自己关掉了（点卡片外/ESC/×）：回到消息循环里再销毁对象
constexpr UINT kPanelClosedMsg = WM_APP + 3;
// 桌面空白处右键：低级鼠标钩子不能在钩子里弹菜单（菜单是模态的，会把 hook 链堵住，
// 超过 LowLevelHooksTimeout 后系统会悄悄拆掉我们的钩子），所以钩子只做判断并吃掉这一次右键，
// 真正的弹菜单投递回消息循环里做
constexpr UINT kDesktopMenuMsg = WM_APP + 4;
// 大文件夹右键「删除文件夹」：拆解涉及销毁这个 widget 自己，必须在消息循环里做
constexpr UINT kDeleteGroupMsg = WM_APP + 5;
// 桌面空白处单击中键：新建收纳文件夹（钩子只投递请求，弹输入框在主线程）
constexpr UINT kNewBoxGestureMsg = WM_APP + 6;
// 右键菜单里选了系统动词（删除/重命名/剪切/新建…）：磁盘已经变了，要重扫桌面层。
// 同样不能在弹菜单那条调用栈上重建（正在销毁正在弹菜单的那个 widget）。
// 注意：之前这里也写成 WM_APP + 6，和 kNewBoxGestureMsg 撞了同一个值，
// 结果“删掉一个图标”会被当成“新建收纳文件夹”——弹个输入框，磁盘却根本没重扫。
constexpr UINT kShellVerbMsg = WM_APP + 7;
constexpr UINT_PTR kWatchdogTimerId = 1;
constexpr UINT_PTR kDisplayTimerId = 2;
constexpr UINT kWatchdogIntervalMs = 2500;
// 分组条目的 stat 比扫桌面贵（每条一次 GetFileAttributesW，路径可能在慢盘 / 网络盘上），
// 而“软件被卸载”这种变化晚十秒发现根本无所谓：每 4 次看门狗（~10 秒）查一轮。
constexpr int kVanishedCheckEveryTicks = 4;

constexpr int kDefaultSpanCols = 3;  // 大文件夹默认占 3×3 格（9 格：8 个直接点 + 1 个“更多”）
constexpr int kDefaultSpanRows = 3;
constexpr wchar_t kThisPcPath[] = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";
constexpr wchar_t kRecycleBinPath[] = L"::{645FF040-5081-101B-9F08-00AA002F954E}";
constexpr const wchar_t* kDesktopGroup = tf::cfg::kDesktopGroup;
constexpr const wchar_t* kShellGroup = tf::cfg::kShellGroup;
struct ShellIconSpec { const wchar_t* key; const wchar_t* path; const wchar_t* name; };
constexpr ShellIconSpec kShellIcons[] = {
    {L"ThisPC", kThisPcPath, L"此电脑"},
    {L"RecycleBin", kRecycleBinPath, L"回收站"},
};

// 工作区尺寸变化到小于这个量（DIP）就不算“环境变了”，直接复用上次的列数。
// 任务栏自动隐藏 / 缩放取整会让工作区宽高差几个 DIP，
// 如果每次都重新挑列数，格距会在 76.8 / 80.8 之间跳，布局就会被挤乱。


// 调试用：TF_WIDGET_POS="x,y"（DIP）覆盖第一块 widget 的位置。
// 自动化测像素时需要把 widget 放到"没有被其它应用窗口盖住"的桌面区域，
// 否则屏幕取样读到的是别的窗口的像素，无法判断 widget 自己到底画了什么。
bool DebugPosOverride(POINT& out) {
    wchar_t buf[64]{};
    if (::GetEnvironmentVariableW(L"TF_WIDGET_POS", buf, 64) == 0) return false;
    int x = 0;
    int y = 0;
    if (::swscanf_s(buf, L"%d,%d", &x, &y) != 2) {
        tf::log::Warn(L"TF_WIDGET_POS 格式错误，应为 \"x,y\"");
        return false;
    }
    out = POINT{x, y};
    tf::log::Infof(L"TF_WIDGET_POS 覆盖第一块 widget 位置 -> ({},{})", x, y);
    return true;
}

// 滚轮的坑：WS_EX_NOACTIVATE 的部件永远拿不到焦点，而系统的“悬停滚动”只会把
// WM_MOUSEWHEEL 递给有焦点的那个窗口（终端之类自己就把它当滚动条吃掉了），
// 根本轮不到我们。所以装一个低级鼠标钩子：光标下面若真是我们的文件夹，
// 就把这一格滚轮蒜掉（同时防止它滚到底下的程序），否则原样放行。
tf::core::App* g_app = nullptr;

LRESULT CALLBACK WheelHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && wp == WM_MOUSEWHEEL) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        // 触控板一格能刷出几十条滚轮事件，这条只能在 debug 级开
        tf::log::Debugf(L"[滚轮] 钩子收到 delta={} pt=({},{}) app={}",
                        static_cast<short>(HIWORD(ms->mouseData)), ms->pt.x, ms->pt.y,
                        g_app != nullptr);
    }
    if (code == HC_ACTION && wp == WM_MOUSEWHEEL && g_app != nullptr) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        const float notches =
            static_cast<float>(static_cast<short>(HIWORD(ms->mouseData))) / WHEEL_DELTA;
        if (g_app->HandleWheelAt(ms->pt, notches)) return 1;  // 吃掉了
    }
    // 桌面空白处右键：接管（吞掉），自己弹菜单。
    // 必须吞：放行的话 Explorer 的 DefView 也会处理同一次右键，它弹出的菜单会抢前台，
    // 我们刚弹的菜单立刻被当成失活而关闭 —— 现象就是“点开后立马消失”。
    // 只在我们确实会弹菜单时吞（CanTakeDesktopRightClick），否则一律放行。
    if (code == HC_ACTION && g_app != nullptr && (wp == WM_RBUTTONDOWN || wp == WM_RBUTTONUP)) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        static bool consumedDown = false;
        if (wp == WM_RBUTTONDOWN) {
            consumedDown = g_app->CanTakeDesktopRightClick(ms->pt);
            if (consumedDown) return 1;
        } else if (consumedDown) {
            consumedDown = false;
            if (g_app->CanTakeDesktopRightClick(ms->pt)) {
                g_app->HandleDesktopRightClickAt(ms->pt);
                return 1;  // 吞掉抬键，Explorer 收不到这次右键
            }
        }
    }
    // 桌面空白处双击左键 / 单击中键 -> 新建收纳文件夹。
    // 这两个手势在空桌面上系统本来什么都不做，所以只观测、不吞事件。
    // 桌面空白处单击中键 -> 新建收纳文件夹。空桌面上中键本来没有任何作用，
    // 所以只观测、不吞事件（吞了会把 Explorer 的鼠标状态搞乱）。
    // 有意识不用双击：双击桌面是很多人的习惯动作，容易误触。
    if (code == HC_ACTION && g_app != nullptr && wp == WM_MBUTTONDOWN) {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        g_app->HandleDesktopNewBoxGestureAt(ms->pt);
    }
    return ::CallNextHookEx(nullptr, code, wp, lp);
}

// 把"层叠关系"实测结果写进日志：比截图更可靠，也是 M0 的验收依据。
void LogLayeringDiagnostics(HWND widget, HWND host) {
    if (widget == nullptr || !::IsWindow(widget)) {
        tf::log::Warn(L"[诊断] widget 句柄无效，跳过层叠检查");
        return;
    }

    tf::log::Infof(L"[诊断] 桌面宿主=0x{:X} ({})", reinterpret_cast<uintptr_t>(host),
                   tf::host::ClassName(host));

    HWND parent = ::GetParent(widget);
    tf::log::Infof(L"[诊断] widget=0x{:X} parent=0x{:X} parentClass={}",
                   reinterpret_cast<uintptr_t>(widget), reinterpret_cast<uintptr_t>(parent),
                   tf::host::ClassName(parent));

    RECT r{};
    if (::GetWindowRect(widget, &r)) {
        const POINT center{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
        HWND hit = ::WindowFromPoint(center);
        tf::log::Infof(L"[诊断] 屏幕矩形=({},{})-({},{}) 中心点命中=0x{:X} ({})", r.left, r.top,
                       r.right, r.bottom, reinterpret_cast<uintptr_t>(hit),
                       tf::host::ClassName(hit));
    }
}

std::wstring DisplayNameForPath(const std::wstring& path) {
    const std::filesystem::path p(path);
    std::wstring name = p.stem().wstring();
    if (name.empty()) name = p.filename().wstring();
    if (name.empty()) name = path;
    return name;
}

bool SamePathNoCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    return ::CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                  static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool PathExists(const std::wstring& path) {
    return ::GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsDirectory(const std::wstring& path) {
    const DWORD attrs = ::GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

}  // namespace

namespace tf::core {
App::App(HINSTANCE instance) : instance_(instance) {}

App::~App() {
    g_app = nullptr;
    // 全面接管只在运行期间生效：退出把 HideIcons 写回 0，原生桌面图标回来
    cfg::RestoreDesktopVisibility();
    tf::host::RestoreNativeIconsOnExit();
    if (wheelHook_ != nullptr) {
        ::UnhookWindowsHookEx(wheelHook_);
        wheelHook_ = nullptr;
    }
    tray_.Remove();
    tf::shell::IconStore::Get().Stop();
    TeardownDesktopLayer();
    tf::widget::FrameClock::Get().Shutdown();
    if (controlWnd_ != nullptr && ::IsWindow(controlWnd_)) {
        controlWnd_ = nullptr;
        // Control 窗口随进程退出而销毁，这里不主动 DestroyWindow 以免触发 PostQuitMessage
    }
    tf::gpu::GpuDevice::Get().Shutdown();
    cfg::ReleaseLayoutFile();
}

bool App::CreateControlWindow() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::StaticWndProc;
    wc.hInstance = instance_;
    wc.lpszClassName = kControlClass;

    if (::RegisterClassExW(&wc) == 0) {
        const DWORD err = ::GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            log::Errorf(L"RegisterClassExW(Control) 失败 err={}", err);
            return false;
        }
    }

    // 必须是普通顶层窗口：消息专用窗口(HWND_MESSAGE)收不到 TaskbarCreated 广播
    controlWnd_ = ::CreateWindowExW(0, kControlClass, L"Tilefold", WS_OVERLAPPED, 0, 0, 0, 0,
                                    HWND_DESKTOP, nullptr, instance_, this);
    if (controlWnd_ == nullptr) {
        log::Errorf(L"创建 Control 窗口失败 err={}", ::GetLastError());
        return false;
    }
    return true;
}

bool App::Initialize() {
    settings_ = cfg::LoadSettings();
    widget::paint::SetLabelColor(settings_.labelColor);
    cfg::ApplyDesktopVisibility();
    if (!cfg::AcquireLayoutFile()) {
        ::MessageBoxW(nullptr,
                      L"无法访问或写入桌面布局目录。请检查 data 目录权限，并确认没有其他 Tilefold 实例正在运行。",
                      L"Tilefold 启动失败", MB_OK | MB_ICONERROR);
        return false;
    }
    if (!tf::gpu::GpuDevice::Get().Initialize()) return false;

    if (!CreateControlWindow()) return false;
    tf::host::HideNativeIcons();

    // 图标提取线程：独立 STA，避免 shell 扩展卡住 UI 线程
    tf::shell::IconStore::Get().Start();
    tf::shell::IconStore::Get().SetReadyCallback([this] {
        // 工作线程不能直接碰窗口，只投递消息
        if (controlWnd_ != nullptr && ::IsWindow(controlWnd_)) {
            ::PostMessageW(controlWnd_, kIconReadyMsg, 0, 0);
        }
    });

    taskbarCreatedMsg_ = ::RegisterWindowMessageW(L"TaskbarCreated");
    log::Infof(L"TaskbarCreated 消息号 = {}", taskbarCreatedMsg_);

    // 桌面右键菜单里的「新建收纳文件夹」：tilefold.exe --new-box 会往这条消息投递
    newBoxMsg_ = ::RegisterWindowMessageW(tf::ipc::kNewBoxMessage);
    tf::desktop_menu::Install();

    // 低级鼠标钩子：光标在文件夹上时自己吃掉滚轮（WS_EX_NOACTIVATE 收不到 WM_MOUSEWHEEL）
    g_app = this;
    wheelHook_ = ::SetWindowsHookExW(WH_MOUSE_LL, &WheelHookProc, instance_, 0);
    if (wheelHook_ == nullptr) {
        log::Warnf(L"滚轮钩子安装失败 err={}，滚轮翻页将不可用", ::GetLastError());
    } else {
        log::Info(L"滚轮钩子已安装");
    }

    // 运行期数据全在 <项目>\data 里：第一次启动就把 box 目录建出来，
    // 用户直接把分组文件夹丢进去即可（日志里给出完整路径，省得去找）
    log::Infof(L"数据目录={}，布局={}", tf::paths::DataDir(), tf::paths::LayoutPath());

    BuildDesktopLayer();

    // 右下角托盘图标：右键 打开设置 / 开机自启动 / 退出
    tray_.onSettings = [this] { OpenSettings(); };
    tray_.onExit = [this] { Quit(); };
    // 桌面右键菜单被 Win11 的 XAML 新菜单挡着透传不出来，所以“新建收纳文件夹”
    // 除了桌面背景菜单，还给两个更稳的入口：托盘菜单、桌面空白处双击/中键
    tray_.onNewBox = [this] { OnNewBoxRequested(); };

    tray_.onAutostartChanged = [this](bool enabled) {
        // 只在日志里留一笔：菜单下次弹出时会重查注册表，不需要另外记状态
        log::Infof(L"托盘：开机自启动 {}", enabled ? L"已开启" : L"已关闭");
    };
    tray_.Add(controlWnd_);

    if (!settings_.onboardingComplete) {
        if (OpenSettings(true)) {
            settings_.onboardingComplete = true;
            cfg::SaveSettings(settings_);
        }
    }

    // 上次开过自启、但 exe 被挪过位置（或 Debug/Release 换着跑）：把 Run 值改成当前路径，
    // 否则开机时 Windows 找不到文件，静默失败，用户只会看到“开机没自启”
    tf::core::autostart::SyncIfEnabled();

    // 看门狗：Explorer 重启后 TaskbarCreated 偶尔会漏，兜一层
    ::SetTimer(controlWnd_, kWatchdogTimerId, kWatchdogIntervalMs, nullptr);
    return true;
}

void App::RefreshGrid() {
    displays_ = desktop::Enumerate();
    const auto oldMonitors = layout_.monitors;
    const bool legacy = std::any_of(layout_.entries.begin(), layout_.entries.end(), [](const cfg::Entry& e) {
        return (e.kind == L"folder" || e.kind == L"icon" || e.kind == L"shell" ||
                e.kind == L"desktop") && e.monitor.empty();
    });
    desktop::Configure(displays_, layout_);
    grid_ = displays_.front().grid;
    gridDirty_ = legacy || oldMonitors.size() != layout_.monitors.size();
    if (!gridDirty_) {
        for (std::size_t i = 0; i < oldMonitors.size(); ++i) {
            const auto& a = oldMonitors[i];
            const auto& b = layout_.monitors[i];
            if (a.monitor != b.monitor || a.grid.cols != b.grid.cols ||
                std::fabs(a.grid.pitch - b.grid.pitch) > 0.05f ||
                a.grid.workW != b.grid.workW || a.grid.workH != b.grid.workH) {
                gridDirty_ = true;
                break;
            }
        }
    }
}

bool App::PlaceEntry(cfg::Entry& entry, RECT boundsPx, int spanCols, int spanRows) {
    const POINT center{(boundsPx.left + boundsPx.right) / 2, (boundsPx.top + boundsPx.bottom) / 2};
    const POINT screenPx{boundsPx.left, boundsPx.top};
    const auto& display = desktop::ForPoint(displays_, center);
    const auto& grid = display.grid;
    grid::ClampSpan(grid, spanCols, spanRows);
    POINT want = grid::ClampCell(grid, grid::CellAt(grid, desktop::LocalDip(display, screenPx)),
                                spanCols, spanRows);
    const auto* keep = cfg::FindEntry(visibleLayout_, entry.kind, entry.group, entry.item);
    POINT target{};
    if (!placement::NearestFreeCell(grid, visibleLayout_, want, spanCols, spanRows, keep, target))
        return false;
    entry.col = target.x;
    entry.row = target.y;
    entry.spanCols = spanCols;
    entry.spanRows = spanRows;
    entry.monitor = display.id;
    const auto visible = std::find_if(visibleLayout_.entries.begin(), visibleLayout_.entries.end(),
        [&](const cfg::Entry& candidate) {
            return candidate.kind == entry.kind && candidate.group == entry.group &&
                   candidate.item == entry.item;
        });
    if (visible != visibleLayout_.entries.end()) *visible = entry;
    cfg::SaveLayout(layout_);
    return true;
}

void App::BuildDesktopLayer() {
    host_ = tf::host::Find();
    tf::host::HideNativeIcons();
    layout_ = tf::cfg::LoadLayout();
    // 分组和条目都来自 layout.tsv；真实桌面只用来发现未分组图标
    RebuildGroups();
    desktopItems_ = tf::box::ScanDesktopItems();
    RefreshGrid();

    // 图标先全部入队：工作线程按顺序提取，先到的先画
    for (const tf::box::Group& g : groups_) {
        for (const tf::box::Item& item : g.items) {
            tf::shell::IconStore::Get().Request(item.path);
        }
    }
    for (const auto& item : kShellIcons) tf::shell::IconStore::Get().Request(item.path);
    for (const auto& item : desktopItems_) tf::shell::IconStore::Get().Request(item.path);

    // 新分组没有位置记录时分配一格
    bool dirty = gridDirty_;
    gridDirty_ = false;
    for (const tf::box::Group& g : groups_) {
        if (tf::cfg::FindEntry(layout_, L"folder", g.name) != nullptr) continue;
        tf::cfg::Entry e;
        e.kind = L"folder";
        e.group = g.name;
        e.spanCols = kDefaultSpanCols;
        e.spanRows = kDefaultSpanRows;
        e.monitor = displays_.front().id;
        tf::placement::ClampFolderSpan(grid_, e.spanCols, e.spanRows);
        POINT cell{};
        tf::placement::FindFreeCell(grid_, layout_, e.spanCols, e.spanRows, cell);
        e.col = cell.x;
        e.row = cell.y;
        log::Infof(L"新分组 [{}] 自动放到格子 ({},{})", g.name, e.col, e.row);
        tf::cfg::SetEntry(layout_, std::move(e));
        dirty = true;
    }

    for (const auto& item : kShellIcons) {
        if (tf::cfg::FindEntry(layout_, L"shell", L"system", item.key) != nullptr) continue;
        tf::cfg::Entry e;
        e.kind = L"shell";
        e.group = L"system";
        e.item = item.key;
        e.spanCols = 1;
        e.spanRows = 1;
        e.monitor = displays_.front().id;
        POINT cell{};
        tf::placement::FindFreeCell(grid_, layout_, 1, 1, cell);
        e.col = cell.x;
        e.row = cell.y;
        tf::cfg::SetEntry(layout_, std::move(e));
        dirty = true;
    }

    // 桌面上没被收进大文件夹的快捷方式，全部当 1×1 独立图标平铺。
    // 全新安装时 data\box 是空的，这一批就是用户看到的全部内容，不能丢。
    for (const auto& item : desktopItems_) {
        // 已归进虚拟分组的那批不要另外铺开
        if (tf::cfg::GroupForPath(layout_, item.path) != nullptr) continue;
        if (tf::cfg::FindEntry(layout_, L"desktop", kDesktopGroup, item.path) != nullptr) continue;
        tf::cfg::Entry e;
        e.kind = L"desktop";
        e.group = kDesktopGroup;
        e.item = item.path;
        e.spanCols = 1;
        e.spanRows = 1;
        e.monitor = displays_.front().id;
        POINT cell{};
        tf::placement::FindFreeCell(grid_, layout_, 1, 1, cell);
        e.col = cell.x;
        e.row = cell.y;
        tf::cfg::SetEntry(layout_, std::move(e));
        dirty = true;
    }

    // 清掉指向已删除文件的条目（用户手动删了快捷方式的情况）
    const std::size_t before = layout_.entries.size();
    layout_.entries.erase(
        std::remove_if(layout_.entries.begin(), layout_.entries.end(),
                       [this](const cfg::Entry& e) {
                           if (e.kind != L"icon") return false;
                           const box::Group* g = FindGroup(e.group);
                           if (g == nullptr) return true;
                           return std::none_of(g->items.begin(), g->items.end(),
                                               [&](const box::Item& it) {
                                                   return it.path == e.item;
                                               });
                       }),
        layout_.entries.end());
    if (layout_.entries.size() != before) dirty = true;

    const std::size_t beforeDesktop = layout_.entries.size();
    // 归进大文件夹的条目在桌面层没有自己的一格，历史记录要一并清掉
    layout_.entries.erase(std::remove_if(layout_.entries.begin(), layout_.entries.end(), [this](const cfg::Entry& e) {
        if (e.kind != L"desktop") return false;
        return std::none_of(desktopItems_.begin(), desktopItems_.end(), [&](const box::Item& item) {
            return item.path == e.item && tf::cfg::GroupForPath(layout_, item.path) == nullptr;
        });
    }), layout_.entries.end());
    if (layout_.entries.size() != beforeDesktop) dirty = true;

    if (dirty) tf::cfg::SaveLayout(layout_);

    RelayoutAll();

    // 层叠关系实测（保留 M0 的诊断，便于回归）
    if (!folders_.empty()) {
        LogLayeringDiagnostics(folders_.front()->Hwnd(), host_.hwnd);
    }
}

const box::Group* App::FindGroup(const std::wstring& name) const {
    const auto it = std::find_if(groups_.begin(), groups_.end(),
                                 [&](const box::Group& g) { return g.name == name; });
    return it == groups_.end() ? nullptr : &*it;
}

void App::RebuildGroups() {
    groups_.clear();
    groups_.reserve(layout_.groups.size());
    for (const cfg::GroupState& source : layout_.groups) {
        box::Group group;
        group.name = source.name;
        std::vector<cfg::GroupItem> ordered = source.items;
        std::stable_sort(ordered.begin(), ordered.end(), [](const cfg::GroupItem& a, const cfg::GroupItem& b) {
            if (a.order != b.order) return a.order < b.order;
            return DisplayNameForPath(a.path) < DisplayNameForPath(b.path);
        });
        for (const cfg::GroupItem& sourceItem : ordered) {
            if (!PathExists(sourceItem.path)) {
                log::Warnf(L"分组 [{}] 的条目不存在，等待清理: {}", source.name, sourceItem.path);
                continue;
            }
            box::Item item;
            item.path = sourceItem.path;
            item.displayName = DisplayNameForPath(sourceItem.path);
            item.isDir = IsDirectory(sourceItem.path);
            group.items.push_back(std::move(item));
        }
        groups_.push_back(std::move(group));
    }
    log::Infof(L"虚拟分组缓存已重建：{} 个分组，{} 个条目", groups_.size(),
               [&] { size_t n = 0; for (const auto& g : groups_) n += g.items.size(); return n; }());
}

void App::OnNewBoxRequested() {
    if (shuttingDown_ || rebuilding_) return;

    const auto name = tf::ui::PromptForText(controlWnd_, L"新建收纳文件夹", L"文件夹名称：",
                                            tf::box::kDefaultGroupName);
    if (!name.has_value()) return;

    const std::wstring base = tf::box::SanitizeGroupName(*name);
    auto nameExists = [&](const std::wstring& candidate) {
        for (const cfg::GroupState& group : layout_.groups) {
            if (SamePathNoCase(group.name, candidate)) return true;
        }
        return false;
    };
    std::wstring unique = base;
    for (int index = 2; nameExists(unique); ++index) {
        unique = base + std::format(L" ({})", index);
    }

    cfg::EnsureGroup(layout_, unique);
    if (!cfg::SaveLayout(layout_)) {
        cfg::RemoveGroup(layout_, unique);
        ::MessageBoxW(controlWnd_, L"创建收纳文件夹失败，layout.tsv 无法写入，详情见日志。",
                      L"Tilefold", MB_OK | MB_ICONWARNING);
        return;
    }
    log::Infof(L"新建虚拟收纳文件夹：[{}]", unique);
    RestartDesktopLayer(L"新建了收纳文件夹");
}
void App::RelayoutAll() {
    folders_.clear();
    icons_.clear();
    if (displays_.empty()) RefreshGrid();
    visibleLayout_ = desktop::Resolve(displays_, layout_);

    POINT debugPos{};
    const bool hasDebugPos = DebugPosOverride(debugPos);
    bool debugUsed = false;

    // 文字带高 = 相邻 widget 之间那条缝（RectForSpan 每边留 margin，两边加起来正好 2×margin）。
    // 文件夹名字、独立图标名字都画在这条带里：字不吃卡片面积，也不会压到下一行。


    // 1) 大文件夹
    for (const box::Group& src : groups_) {
        const cfg::Entry* entry = tf::cfg::FindEntry(visibleLayout_, L"folder", src.name);
        // 空分组也要画：右键新建出来的文件夹一开始就是空的，不画就只当看不见（新建没反应）
        if (entry == nullptr) continue;

        // 已被拖出去的图标不再出现在文件夹里
        box::Group filtered = src;
        filtered.items.erase(std::remove_if(filtered.items.begin(), filtered.items.end(),
                                            [&](const box::Item& it) {
                                                return tf::cfg::IsExtracted(layout_, src.name,
                                                                            it.path);
                                            }),
                             filtered.items.end());

        const auto& display = desktop::ForEntry(displays_, *entry);
        const int spanCols = entry->spanCols;
        const int spanRows = entry->spanRows;
        const POINT cell{entry->col, entry->row};
        RECT bounds = desktop::BoundsPx(display, *entry);
        const float labelBandDip = 2.0f * display.grid.margin;

        const std::size_t itemCount = filtered.items.size();
        auto widget =
            std::make_unique<widget::BigFolderWidget>(std::move(filtered), bounds, spanCols, spanRows);
        widget::BigFolderWidget* raw = widget.get();
        raw->SetLabelBandDip(labelBandDip);

        if (hasDebugPos && !debugUsed) {
            const int w = bounds.right - bounds.left;
            const int h = bounds.bottom - bounds.top;
            raw->SetBoundsPx(RECT{debugPos.x, debugPos.y, debugPos.x + w, debugPos.y + h});
            debugUsed = true;
        }

        widget->SetMovedHandler([this, raw] { OnFolderMoved(raw); });
        widget->SetResizeHandler(
            [this, raw](int sc, int sr) { OnFolderResize(raw, sc, sr); });
        widget->SetItemDragOutHandler(
            [this, raw](std::size_t index, POINT drop) { OnItemDragOut(raw, index, drop); });
        widget->SetExpandHandler([this, raw] { OnFolderExpand(raw); });
        widget->SetDeleteHandler([this, raw] { OnFolderDeleteRequested(raw); });
        widget->SetNewBoxHandler([this] { OnNewBoxRequested(); });
        widget->SetShellVerbHandler([this] { PostShellVerbRescan(); });

        if (!widget->Show(host_.fallback ? nullptr : host_.hwnd, instance_)) {
            log::Errorf(L"大文件夹 [{}] 窗口创建失败", src.name);
            continue;
        }
        log::Infof(L"大文件夹 [{}] {} 项 跨度 {}×{} -> 格子 ({},{}) 矩形 ({},{})-({},{})", src.name,
                   itemCount, spanCols, spanRows, cell.x, cell.y, bounds.left, bounds.top,
                   bounds.right, bounds.bottom);
        folders_.push_back(std::move(widget));
    }

    // 2) 被拖出来的独立图标
    for (const cfg::Entry& e : visibleLayout_.entries) {
        if (e.kind != L"icon") continue;
        const box::Group* g = FindGroup(e.group);
        if (g == nullptr) continue;
        const auto it = std::find_if(g->items.begin(), g->items.end(), [&](const box::Item& item) {
            return item.path == e.item;
        });
        if (it == g->items.end()) continue;

        const auto& display = desktop::ForEntry(displays_, e);
        const POINT cell{e.col, e.row};
        const RECT bounds = desktop::BoundsPx(display, e);
        const float labelBandDip = 2.0f * display.grid.margin;

        auto icon = std::make_unique<widget::SingleIconWidget>(e.group, *it, bounds);
        widget::SingleIconWidget* raw = icon.get();
        raw->SetLabelBandDip(labelBandDip);
        icon->SetMovedHandler([this, raw] { OnIconMoved(raw); });
        icon->SetReturnHandler([this, raw] { OnIconReturn(raw); });
        icon->SetShellVerbHandler([this] { PostShellVerbRescan(); });
        if (!icon->Show(host_.fallback ? nullptr : host_.hwnd, instance_)) {
            log::Errorf(L"独立图标 [{}] 窗口创建失败", e.item);
            continue;
        }
        log::Infof(L"独立图标 [{}] -> 格子 ({},{})", e.item, cell.x, cell.y);
        icons_.push_back(std::move(icon));
    }

    for (const auto& item : kShellIcons) {
        const cfg::Entry* e = tf::cfg::FindEntry(visibleLayout_, L"shell", L"system", item.key);
        if (e == nullptr) continue;
        box::Item shellItem;
        shellItem.path = item.path;
        shellItem.displayName = item.name;
        const auto& display = desktop::ForEntry(displays_, *e);
        auto icon = std::make_unique<widget::SingleIconWidget>(L"", std::move(shellItem),
                                                               desktop::BoundsPx(display, *e));
        icon->SetLabelBandDip(2.0f * display.grid.margin);
        widget::SingleIconWidget* raw = icon.get();
        icon->SetMovedHandler([this, raw] { OnShellIconMoved(raw); });
        // 此电脑/回收站是 shell 命名空间条目：右键给出来源 explorer 的原生菜单；
        // 它们没有双击“打开”这种动词，所以额外补一条「打开」
        icon->SetOpenExtra(true);
        icon->SetShellVerbHandler([this] { PostShellVerbRescan(); });
        if (icon->Show(host_.fallback ? nullptr : host_.hwnd, instance_))
            icons_.push_back(std::move(icon));
        else
            log::Errorf(L"系统图标 [{}] 窗口创建失败", item.name);
    }

    for (const auto& entry : visibleLayout_.entries) {
        if (entry.kind != L"desktop") continue;
        const auto item = std::find_if(desktopItems_.begin(), desktopItems_.end(), [&](const box::Item& x) {
            return x.path == entry.item;
        });
        if (item == desktopItems_.end()) continue;
        const auto& display = desktop::ForEntry(displays_, entry);
        auto icon = std::make_unique<widget::SingleIconWidget>(kDesktopGroup, *item,
                                                               desktop::BoundsPx(display, entry));
        icon->SetLabelBandDip(2.0f * display.grid.margin);
        widget::SingleIconWidget* raw = icon.get();
        icon->SetMovedHandler([this, raw] { OnDesktopIconMoved(raw); });
        icon->SetShellVerbHandler([this] { PostShellVerbRescan(); });
        if (icon->Show(host_.fallback ? nullptr : host_.hwnd, instance_)) icons_.push_back(std::move(icon));
    }

    log::Infof(L"桌面层就绪：{} 个大文件夹 + {} 个独立图标", folders_.size(), icons_.size());
}

void App::TeardownDesktopLayer() {
    CloseExpandedFolder();
    folders_.clear();
    icons_.clear();
    groups_.clear();
    host_ = {};
}

void App::RestartDesktopLayer(const wchar_t* reason) {
    if (shuttingDown_) return;
    log::Warnf(L"重建桌面层：{}", reason);
    TeardownDesktopLayer();
    BuildDesktopLayer();
}

void App::PostRelayout() {
    if (controlWnd_ != nullptr && ::IsWindow(controlWnd_)) {
        ::PostMessageW(controlWnd_, kRelayoutMsg, 0, 0);
    }
}

void App::PostShellVerbRescan() {
    if (controlWnd_ != nullptr && ::IsWindow(controlWnd_)) {
        ::PostMessageW(controlWnd_, kShellVerbMsg, 0, 0);
    }
}

// 卸载一个软件不会给我们任何通知：分组条目存的可能是 C:\Program Files\Foo\foo.exe 这种
// 桌面目录以外的完整路径，扫桌面永远发现不了它没了吗。所以看门狗得自己 stat 一下。
// 只数“桌面目录以外”的条目：其余的交给了 ScanDesktopItems 的差异检测，不用重复系统调用。
std::size_t App::CountVanishedGroupItems() const {
    std::size_t vanished = 0;
    for (const cfg::GroupState& group : layout_.groups) {
        for (const cfg::GroupItem& item : group.items) {
            const std::wstring& path = item.path;
            if (path.empty()) continue;
            // 远程路径不查：SMB 掉线时 GetFileAttributesW 能把 UI 线程卡住几十秒，
            // 而这段代码跑在 2.5 秒一次的定时器里
            if (path.size() > 1 && path[0] == L'\\' && path[1] == L'\\') continue;
            // 桌面目录里的一切归 ScanDesktopItems 的差异检测管（上面那段已经扫过了），
            // 这里只看它看不见的：指向桌面以外的完整路径
            const bool coveredByDesktopScan =
                std::any_of(desktopItems_.begin(), desktopItems_.end(),
                            [&](const box::Item& it) { return it.path == path; });
            if (coveredByDesktopScan) continue;
            if (!PathExists(path)) ++vanished;
        }
    }
    return vanished;
}

void App::PruneMissingEntries() {
    desktopItems_ = tf::box::ScanDesktopItems();

    std::size_t removedGroupItems = 0;
    for (cfg::GroupState& group : layout_.groups) {
        const std::size_t beforeItems = group.items.size();
        std::erase_if(group.items, [](const cfg::GroupItem& item) { return !PathExists(item.path); });
        removedGroupItems += beforeItems - group.items.size();
    }
    RebuildGroups();

    const std::size_t before = layout_.entries.size();
    std::erase_if(layout_.entries, [&](const cfg::Entry& e) {
        if (e.kind == L"shell") return false;
        const bool groupAlive =
            e.group == kDesktopGroup ||
            std::any_of(groups_.begin(), groups_.end(),
                        [&](const box::Group& g) { return g.name == e.group; });
        if (e.kind == L"order") return true;
        if (e.kind == L"folder") return !groupAlive;
        if (e.kind == L"desktop") {
            const bool exists = std::any_of(desktopItems_.begin(), desktopItems_.end(),
                [&](const box::Item& it) { return it.path == e.item; });
            return !exists || cfg::GroupForPath(layout_, e.item) != nullptr;
        }
        if (e.kind == L"icon") {
            const box::Group* g = FindGroup(e.group);
            if (g == nullptr) return true;
            return std::none_of(g->items.begin(), g->items.end(),
                                [&](const box::Item& it) { return it.path == e.item; });
        }
        return false;
    });

    if (layout_.entries.size() != before || removedGroupItems > 0) {
        cfg::SaveLayout(layout_);
        if (removedGroupItems > 0) {
            log::Infof(L"清理掉 {} 个指向已不存在文件的分组条目", removedGroupItems);
        }
        if (layout_.entries.size() != before) {
            log::Infof(L"清理掉 {} 条失效布局记录", before - layout_.entries.size());
        }
    }
}
// ---- z 序守卫 ----
//
// ---- 网格交互 ----

void App::OnFolderMoved(widget::BigFolderWidget* folder) {
    if (rebuilding_) return;
    auto it = std::find_if(layout_.entries.begin(), layout_.entries.end(), [&](const cfg::Entry& e) {
        return e.kind == L"folder" && e.group == folder->Name();
    });
    if (it == layout_.entries.end()) return;

    const RECT cur = folder->BoundsPx();
    PlaceEntry(*it, cur, folder->SpanCols(), folder->SpanRows());
    // 拖拽落点可能把跨度夹小（贴边），面板得同步回记录里的真实跨度
    folder->SetSpan(it->spanCols, it->spanRows);
    folder->SetBoundsPx(desktop::BoundsPx(desktop::ForEntry(displays_, *it), *it));
}

void App::OnFolderResize(widget::BigFolderWidget* folder, int spanCols, int spanRows) {
    if (rebuilding_) return;
    auto it = std::find_if(layout_.entries.begin(), layout_.entries.end(), [&](const cfg::Entry& e) {
        return e.kind == L"folder" && e.group == folder->Name();
    });
    if (it == layout_.entries.end()) return;
    const RECT cur = folder->BoundsPx();
    if (!PlaceEntry(*it, cur, spanCols, spanRows)) {
        // 周围腾不出这么大块地方（跨度没改成）：把面板同步回记录里的真实值，
        // 免得右键菜单已经勾了新尺寸、画出来还是旧的
        folder->SetSpan(it->spanCols, it->spanRows);
        log::Warnf(L"调整文件夹 [{}] 到 {}×{} 失败：周围没有空位", folder->Name(), spanCols, spanRows);
        return;
    }
    // 面板内部的列/行（以及右键菜单里的“当前 W×H”、下一次拖拽用的跨度）都来自
    // SetSpan 之前那份记录：只改窗口大小会出现“窗口变大了，里面的格子还是旧的行列”，
    // 看起来就是“改了大小不生效，得刷新一次才对”。
    folder->SetSpan(it->spanCols, it->spanRows);
    folder->SetBoundsPx(desktop::BoundsPx(desktop::ForEntry(displays_, *it), *it));
}

void App::OnItemDragOut(widget::BigFolderWidget* folder, std::size_t index, POINT dropPx) {
    if (rebuilding_) return;
    const box::Group& g = folder->Group();
    if (index >= g.items.size()) return;
    const box::Item& item = g.items[index];

    cfg::Entry e;
    e.kind = L"icon";
    e.group = g.name;
    e.item = item.path;
    e.spanCols = 1;
    e.spanRows = 1;

    // 桌面上的快捷方式被自动分进某个分类时（desktopSource），拖到别的大文件夹就真的把文件搬过去：
    // 否则用户没有任何办法重组这些图标（之前是直接 return，表现就是“拖不进去”）
    // 拖到另一个大文件夹上：直接搬进那个分组，不生成独立图标
    if (widget::BigFolderWidget* hit = FolderAt(dropPx); hit == folder) {
        log::Infof(L"图标 [{}] 拖回原文件夹 [{}]，取消拖出", item.displayName, g.name);
        return;
    } else if (hit != nullptr) {
        const std::wstring toGroup = hit->Name();
        if (MoveIconIntoGroup(item.path, toGroup)) {
            log::Infof(L"图标 [{}] 从 [{}] 移到文件夹 [{}]", item.displayName, g.name, toGroup);
            RescanGroups();
            PostRelayout();
        } else {
            log::Warnf(L"图标 [{}] 挪到文件夹 [{}] 失败（可能已有同名文件）", item.displayName,
                       toGroup);
        }
        return;
    }

    const auto& display = desktop::ForPoint(displays_, dropPx);
    const POINT cell = grid::ClampCell(display.grid,
        grid::CellAt(display.grid, desktop::LocalDip(display, dropPx)), 1, 1);
    POINT target{};
    if (!placement::NearestFreeCell(display.grid, visibleLayout_, cell, 1, 1, nullptr, target)) return;
    e.col = target.x;
    e.row = target.y;
    e.monitor = display.id;

    const std::wstring itemName = item.displayName;
    const std::wstring groupName = g.name;

    tf::cfg::SetEntry(layout_, std::move(e));
    tf::cfg::SaveLayout(layout_);

    log::Infof(L"图标 [{}] 从文件夹 [{}] 拖出到格子 ({},{})", itemName, groupName, target.x,
               target.y);

    // 图标从一个文件夹搬到桌面：文件夹内容变了，整层重排最省事也最不容易出错。
    // 但必须在消息循环里做——现在是 widget 自己的回调栈，销毁它会把自己的栈踩掉。
    PostRelayout();
}

void App::OnFolderDeleteRequested(widget::BigFolderWidget* folder) {
    if (rebuilding_ || shuttingDown_ || folder == nullptr) return;
    // 不能在这里直接干活：这层栈属于那块 widget，重扫会把它析构掉
    pendingDeleteGroup_ = folder->Name();
    ::PostMessageW(controlWnd_, kDeleteGroupMsg, 0, 0);
}

void App::OnFolderDeleteConfirmed() {
    std::wstring name = std::move(pendingDeleteGroup_);
    pendingDeleteGroup_.clear();
    if (rebuilding_ || shuttingDown_ || name.empty()) return;

    const box::Group* group = FindGroup(name);
    if (group == nullptr) return;
    const std::size_t itemCount = group->items.size();

    const std::wstring msg = std::format(
        L"删除收纳文件夹「{}」？\n\n分组记录会被移除，里面的 {} 个图标会回到桌面层；文件本身不会被删除。",
        name, itemCount);
    if (::MessageBoxW(controlWnd_, msg.c_str(), L"删除文件夹",
                      MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        log::Infof(L"删除文件夹 [{}]：用户取消", name);
        return;
    }

    const cfg::Layout previous = layout_;
    cfg::RemoveGroup(layout_, name);
    if (!cfg::SaveLayout(layout_)) {
        layout_ = previous;
        ::MessageBoxW(controlWnd_, L"删除文件夹失败：layout.tsv 无法写入，详情见日志。",
                      L"Tilefold", MB_OK | MB_ICONWARNING);
        return;
    }

    log::Infof(L"删除虚拟文件夹 [{}]：{} 个图标回到桌面层，未删除任何文件", name, itemCount);
    RestartDesktopLayer(L"删除收纳文件夹");
}
void App::OnIconMoved(widget::SingleIconWidget* icon) {
    if (rebuilding_ || icon == nullptr) return;

    const std::wstring itemName = icon->Item().displayName;
    const std::wstring fromGroup = icon->GroupName();
    const std::wstring srcPath = icon->Item().path;

    const RECT cur = icon->BoundsPx();
    const POINT center{(cur.left + cur.right) / 2, (cur.top + cur.bottom) / 2};
    if (widget::BigFolderWidget* hit = FolderAt(center); hit != nullptr) {
        const std::wstring toGroup = hit->Name();
        if (toGroup == fromGroup) {
            tf::cfg::RemoveEntry(layout_, L"icon", fromGroup, srcPath);
            tf::cfg::SaveLayout(layout_);
            log::Infof(L"独立图标 [{}] 拖回文件夹 [{}]", itemName, fromGroup);
        } else if (MoveIconIntoGroup(srcPath, toGroup)) {
            log::Infof(L"独立图标 [{}] 从 [{}] 移到文件夹 [{}]", itemName, fromGroup, toGroup);
            RescanGroups();
        } else {
            log::Warnf(L"独立图标 [{}] 拖到文件夹 [{}] 失败，保持原位", itemName, toGroup);
            return;
        }
        PostRelayout();
        return;
    }

    auto it = std::find_if(layout_.entries.begin(), layout_.entries.end(), [&](const cfg::Entry& e) {
        return e.kind == L"icon" && e.group == fromGroup && e.item == srcPath;
    });
    if (it == layout_.entries.end()) return;

    PlaceEntry(*it, cur, 1, 1);
    icon->SetBoundsPx(desktop::BoundsPx(desktop::ForEntry(displays_, *it), *it));
}
void App::OnDesktopIconMoved(widget::SingleIconWidget* icon) {
    if (rebuilding_ || icon == nullptr) return;
    const std::wstring path = icon->Item().path;
    const std::wstring itemName = icon->Item().displayName;

    // 拖到大文件夹上 = 收进去：文件真的从桌面目录搬进 data\box\<分组>
    const RECT cur = icon->BoundsPx();
    const POINT center{(cur.left + cur.right) / 2, (cur.top + cur.bottom) / 2};
    if (widget::BigFolderWidget* hit = FolderAt(center); hit != nullptr) {
        const std::wstring toGroup = hit->Name();
        if (MoveIconIntoGroup(path, toGroup)) {
            tf::cfg::RemoveEntry(layout_, L"desktop", kDesktopGroup, path);
            cfg::SaveLayout(layout_);
            log::Infof(L"桌面图标 [{}] 收进文件夹 [{}]", itemName, toGroup);
            RescanGroups();
            PostRelayout();
        } else {
            log::Warnf(L"桌面图标 [{}] 收进文件夹 [{}] 失败（可能已有同名文件），保持原位", itemName,
                       toGroup);
            PostRelayout();  // 回到原格子，别停在文件夹上面
        }
        return;
    }

    auto it = std::find_if(layout_.entries.begin(), layout_.entries.end(), [&](const cfg::Entry& e) {
        return e.kind == L"desktop" && e.item == path;
    });
    if (it == layout_.entries.end()) return;
    PlaceEntry(*it, icon->BoundsPx(), 1, 1);
    icon->SetBoundsPx(desktop::BoundsPx(desktop::ForEntry(displays_, *it), *it));
}

void App::OnShellIconMoved(widget::SingleIconWidget* icon) {
    if (rebuilding_ || icon == nullptr) return;
    const auto shell = std::find_if(std::begin(kShellIcons), std::end(kShellIcons), [&](const ShellIconSpec& item) {
        return icon->Item().path == item.path;
    });
    if (shell == std::end(kShellIcons)) return;
    auto entry = std::find_if(layout_.entries.begin(), layout_.entries.end(), [&](const cfg::Entry& e) {
        return e.kind == L"shell" && e.group == L"system" && e.item == shell->key;
    });
    if (entry == layout_.entries.end()) return;

    // 此电脑/回收站是 shell 命名空间里的虚拟条目，没有可以搬的文件，收不进文件夹
    const RECT cur = icon->BoundsPx();
    const POINT center{(cur.left + cur.right) / 2, (cur.top + cur.bottom) / 2};
    if (FolderAt(center) != nullptr) {
        log::Infof(L"系统图标 [{}] 不能收进文件夹（是系统命名空间条目）", icon->Item().displayName);
        icon->SetBoundsPx(desktop::BoundsPx(desktop::ForEntry(displays_, *entry), *entry));
        return;
    }

    PlaceEntry(*entry, icon->BoundsPx(), 1, 1);
    icon->SetBoundsPx(desktop::BoundsPx(desktop::ForEntry(displays_, *entry), *entry));
}

widget::BigFolderWidget* App::FolderAt(POINT screenPx) const {
    for (const auto& f : folders_) {
        if (f == nullptr) continue;
        const RECT r = f->BoundsPx();
        if (screenPx.x >= r.left && screenPx.x < r.right && screenPx.y >= r.top && screenPx.y < r.bottom) {
            return f.get();
        }
    }
    return nullptr;
}

void App::RescanGroups() {
    RebuildGroups();
    desktopItems_ = tf::box::ScanDesktopItems();
    for (const box::Group& group : groups_) {
        for (const box::Item& item : group.items) {
            tf::shell::IconStore::Get().Request(item.path);
        }
    }
}

bool App::MoveIconIntoGroup(const std::wstring& srcPath, const std::wstring& groupName) {
    if (cfg::FindGroup(layout_, groupName) == nullptr) return false;
    if (!PathExists(srcPath)) return false;
    std::wstring oldGroup;
    if (const cfg::GroupState* current = cfg::GroupForPath(layout_, srcPath)) oldGroup = current->name;
    if (oldGroup == groupName) return true;
    if (!cfg::MoveGroupItem(layout_, srcPath, groupName)) return false;
    if (!cfg::SaveLayout(layout_)) return false;
    tf::shell::IconStore::Get().Request(srcPath);
    return true;
}
void App::OnIconReturn(widget::SingleIconWidget* icon) {
    if (rebuilding_) return;
    const std::wstring itemName = icon->Item().displayName;
    const std::wstring itemPath = icon->Item().path;
    const std::wstring groupName = icon->GroupName();
    tf::cfg::RemoveEntry(layout_, L"icon", groupName, itemPath);
    tf::cfg::SaveLayout(layout_);
    log::Infof(L"独立图标 [{}] 放回文件夹 [{}]", itemName, groupName);
    PostRelayout();
}

bool App::HandleWheelAt(POINT screenPx, float notches) {
    if (notches == 0.0f) return false;
    // 放大面板是能激活的窗口，滚轮走正常路径（WM_MOUSEWHEEL），不在这里处理
    for (const auto& f : folders_) {
        if (f == nullptr) continue;
        RECT r{};
        if (!::GetWindowRect(f->Hwnd(), &r)) continue;  // Include the actual label band.
        if (screenPx.x >= r.left && screenPx.x < r.right && screenPx.y >= r.top && screenPx.y < r.bottom) {
            return f->OnWheel(notches);
        }
    }
    return false;
}

bool App::IsDesktopBackgroundPoint(POINT screenPx) {
    if (host_.hwnd == nullptr || !::IsWindow(host_.hwnd)) return false;

    RECT hostRect{};
    if (!::GetWindowRect(host_.hwnd, &hostRect)) return false;
    if (!::PtInRect(&hostRect, screenPx)) return false;

    // 落在部件上就把鼠标事件留给部件自己（大文件夹尺寸菜单 / 图标菜单）
    if (FolderAt(screenPx) != nullptr) return false;
    for (const auto& icon : icons_) {
        if (icon == nullptr) continue;
        const RECT r = icon->BoundsPx();
        if (::PtInRect(&r, screenPx)) return false;
    }

    HWND hit = ::WindowFromPoint(screenPx);
    if (hit != nullptr && hit != host_.hwnd) {
        const std::wstring cls = tf::host::ClassName(hit);
        // Tilefold.Widget / Tilefold.Panel ...：自己的部件（包含放大面板），交回它自己
        if (cls.rfind(L"Tilefold.", 0) == 0) return false;
        // 桌面本体（Progman / WorkerW / DefView）都是宿主的子窗口；
        // 不是的话说明光标其实在别的应用的窗口上，不能抢人家的鼠标事件
        if (!::IsChild(host_.hwnd, hit)) return false;
    }

    // 原生桌面图标：那些归 Explorer，双击要打开文件，不能拿来新建
    const HWND defView = tf::host::FindDefView(host_.hwnd);
    if (defView != nullptr && tf::host::IconAtPoint(defView, screenPx)) return false;

    return true;
}

bool App::HandleDesktopNewBoxGestureAt(POINT screenPx) {
    if (shuttingDown_ || rebuilding_ || menuOpen_) return false;
    if (!IsDesktopBackgroundPoint(screenPx)) return false;
    log::Infof(L"桌面空白处中键：新建收纳文件夹 pt=({},{})", screenPx.x, screenPx.y);
    ::PostMessageW(controlWnd_, kNewBoxGestureMsg, 0, 0);
    return true;
}

bool App::HandleDesktopRightClickAt(POINT screenPx) {
    // Tilefold 全面接管桌面：原生桌面图标层（SHELLDLL_DefView）一直是隐藏的，
    // 而桌面空白处的右键菜单正是 DefView 画的 —— 它不在了，桌面右键就什么都不弹
    // （连系统的“刷新/显示设置”都没有），我们注册的 DesktopBackground\Shell 项
    // 也没机会显示。所以桌面背景的右键菜单一直由我们自己弹（shell/DesktopContextMenu）。
    if (!CanTakeDesktopRightClick(screenPx)) return false;

    menuOpen_ = true;
    log::Infof(L"桌面右键：命中桌面背景 pt=({},{}),hit={} -> 投递菜单", screenPx.x, screenPx.y,
               tf::host::ClassName(::WindowFromPoint(screenPx)));
    ::PostMessageW(controlWnd_, kDesktopMenuMsg, 0,
                   MAKELPARAM(static_cast<WORD>(screenPx.x), static_cast<WORD>(screenPx.y)));
    return true;
}

bool App::CanTakeDesktopRightClick(POINT screenPx) {
    if (shuttingDown_ || rebuilding_ || menuOpen_) return false;
    if (host_.hwnd == nullptr || !::IsWindow(host_.hwnd)) return false;

    // 原生图标层被我们隐藏了，Explorer 不会再给桌面背景弹菜单，没得抢；
    // 剩下只需要确认这个点是桌面背景（不是自己的部件、也不是别人的窗口）。

    // 桌面菜单正在弹着的时候，右键归菜单自己（不能又开一个模态循环）
    if (::GetActiveWindow() != nullptr && tf::host::ClassName(::GetActiveWindow()) == L"#32768") {
        return false;
    }

    return IsDesktopBackgroundPoint(screenPx);
}

void App::OnDesktopMenuRequested(POINT screenPx) {
    log::Info(L"桌面右键：开始弹菜单");
    const tf::desktop_menu::DesktopCommand cmd =
        tf::desktop_menu::ShowBackgroundMenu(controlWnd_, screenPx);
    menuOpen_ = false;
    log::Infof(L"桌面右键：菜单关闭 cmd={}", static_cast<int>(cmd));

    switch (cmd) {
        case tf::desktop_menu::DesktopCommand::NewBox:
            OnNewBoxRequested();
            break;

        case tf::desktop_menu::DesktopCommand::Refresh:
            RestartDesktopLayer(L"右键菜单：刷新");
            break;
        case tf::desktop_menu::DesktopCommand::DesktopChanged:
            // 选的是系统动词（新建文件夹 / 粘贴 / 刷新 …）：磁盘可能变了，重扫一遍
            RestartDesktopLayer(L"桌面右键：系统动词改变了桌面");
            break;
        case tf::desktop_menu::DesktopCommand::None:
            break;
    }
}

void App::OnFolderExpand(widget::BigFolderWidget* folder) {
    if (rebuilding_ || folder == nullptr) return;
    CloseExpandedFolder();

    const RECT bounds = folder->BoundsPx();
    const auto& display = desktop::ForPoint(displays_, POINT{bounds.left, bounds.top});
    auto panel = std::make_unique<widget::ExpandedFolder>(folder->Group(), display.workPx);
    const std::wstring group = folder->Name();
    panel->SetReorderedHandler([this, group](std::vector<std::wstring> order) {
        OnExpandedReordered(group, std::move(order));
    });
    panel->SetShellVerbHandler([this] { PostShellVerbRescan(); });
    // 面板是在它自己的 wndproc 里请求关闭的，不能在那条调用栈上把它销毁
    panel->SetClosedHandler([this] {
        if (controlWnd_ != nullptr && ::IsWindow(controlWnd_)) {
            ::PostMessageW(controlWnd_, kPanelClosedMsg, 0, 0);
        }
    });

    if (!panel->Show(instance_)) {
        log::Errorf(L"放大面板 [{}] 创建失败", group);
        return;
    }
    expanded_ = std::move(panel);
}

void App::CloseExpandedFolder() {
    if (!expanded_) return;
    expanded_->Close();
    expanded_.reset();
}

void App::OnExpandedReordered(const std::wstring& group, std::vector<std::wstring> order) {
    if (order.empty()) return;
    tf::cfg::SetGroupItemOrder(layout_, group, order);
    tf::cfg::SaveLayout(layout_);
    log::Infof(L"文件夹 [{}] 图标顺序已保存：{} 项", group, order.size());
    RebuildGroups();
    PostRelayout();
}

LRESULT CALLBACK App::StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        auto* self = static_cast<App*>(cs->lpCreateParams);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self != nullptr) self->controlWnd_ = hwnd;
    }

    auto* self = reinterpret_cast<App*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) return ::DefWindowProcW(hwnd, msg, wp, lp);
    return self->WndProc(msg, wp, lp);
}

LRESULT App::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        tray_.Readd();
        RestartDesktopLayer(L"收到 TaskbarCreated（Explorer 已重启）");
        return 0;
    }
    if (newBoxMsg_ != 0 && msg == newBoxMsg_) {
        OnNewBoxRequested();
        return 0;
    }
    if (msg == kNewBoxGestureMsg) {
        // 输入框是模态的：它开着的时候再按中键会排进队列，关掉后一下子弹第二个。
        // 已经有一个在屏幕上就丢掉这次请求。
        if (::FindWindowW(L"Tilefold.Prompt", nullptr) == nullptr) OnNewBoxRequested();
        return 0;
    }
    if (msg == kDesktopMenuMsg) {
        OnDesktopMenuRequested(POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
        return 0;
    }
    if (msg == kDeleteGroupMsg) {
        OnFolderDeleteConfirmed();
        return 0;
    }
    if (msg == kShellVerbMsg) {
        // 系统动词（删除/重命名/剪切/新建）改的是真磁盘：重新扫盘并重排。
        // 顺手把指向已经不存在的东西的绑定清掉，免得网格里留着永久空洞
        PruneMissingEntries();
        RestartDesktopLayer(L"右键菜单：系统动词改了磁盘");
        return 0;
    }
    if (tray_.HandleMessage(msg, wp, lp)) {
        return 0;
    }

    switch (msg) {
        case kIconReadyMsg:
            // 有真实图标了，整层重绘（每块面板自己缓存了位图，重绘很便宜）
            for (auto& w : folders_) w->Invalidate();
            for (auto& w : icons_) w->Invalidate();
            return 0;

        case kRelayoutMsg:
            rebuilding_ = true;
            RelayoutAll();
            rebuilding_ = false;
            return 0;

        case kPanelClosedMsg:
            expanded_.reset();
            return 0;

        case WM_TIMER:
            if (wp == kDisplayTimerId) {
                ::KillTimer(controlWnd_, kDisplayTimerId);
                RestartDesktopLayer(L"显示器工作区或缩放变更");
                return 0;
            }
            if (wp == kWatchdogTimerId) {
                if (!tf::host::IsHostAlive(host_.hwnd)) {
                    RestartDesktopLayer(L"看门狗检测到桌面宿主已失效");
                } else {
                    const auto latestDesktopItems = tf::box::ScanDesktopItems();
                    const bool changed = latestDesktopItems.size() != desktopItems_.size() ||
                        !std::equal(latestDesktopItems.begin(), latestDesktopItems.end(), desktopItems_.begin(),
                            [](const box::Item& a, const box::Item& b) { return a.path == b.path; });
                    if (changed) {
                        // 先清掉指向已不存在文件的记录（桌面文件被删了 / 被卸掉的软件），
                        // 再重建：不清理的话 layout.tsv 里会永远留着那些条目，
                        // 下次新分成条目时会挤出一堆空洞。
                        PruneMissingEntries();
                        RestartDesktopLayer(L"检测到 Windows 桌面内容变化");
                    } else if (++watchdogTicks_ % kVanishedCheckEveryTicks == 0 &&
                               CountVanishedGroupItems() > 0) {
                        // 分组条目指向的是完整路径（可能根本不在桌面目录里，比如卸载了的
                        // 软件），桌面毫无变化，也是我们自己 stat 才知道。先清掉死记录再重建，
                        // 不然 layout.tsv 里会永远戳着这些条目。
                        PruneMissingEntries();
                        RestartDesktopLayer(L"检测到分组条目指向的文件已不存在");
                    }
                }
            }
            return 0;

        case WM_DISPLAYCHANGE:
            ::SetTimer(controlWnd_, kDisplayTimerId, 250, nullptr);
            return 0;

        case WM_SETTINGCHANGE:
            ::SetTimer(controlWnd_, kDisplayTimerId, 250, nullptr);
            return 0;

        case WM_ENDSESSION:
            shuttingDown_ = true;
            return 0;

        case WM_DESTROY:
            shuttingDown_ = true;
            ::PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return ::DefWindowProcW(controlWnd_, msg, wp, lp);
}

bool App::OpenSettings(bool firstRun) {
    log::Info(L"托盘：打开设置窗口");
    return settingsWindow_.Show(controlWnd_, instance_, settings_, [this](const cfg::Settings& settings) {
        settings_ = settings;
        widget::paint::SetLabelColor(settings_.labelColor);
        cfg::ApplyDesktopVisibility();
        tf::host::HideNativeIcons();
        for (auto& folder : folders_) folder->Invalidate();
        for (auto& icon : icons_) icon->Invalidate();
        if (expanded_) expanded_->Invalidate();
    }, firstRun);
}

void App::Quit() {
    log::Info(L"托盘：退出");
    shuttingDown_ = true;
    // 退出就把桌面右键菜单那一项摘掉：目录被删/被拷走后留着就是死项
    tf::desktop_menu::Remove();
    tray_.Remove();
    CloseExpandedFolder();
    if (controlWnd_ != nullptr && ::IsWindow(controlWnd_)) {
        ::DestroyWindow(controlWnd_);  // -> WM_DESTROY -> PostQuitMessage
    } else {
        ::PostQuitMessage(0);
    }
}

int App::Run() {
    log::Info(L"消息循环启动");
    MSG msg{};
    while (true) {
        const BOOL r = ::GetMessageW(&msg, nullptr, 0, 0);
        if (r == 0) break;
        if (r == -1) {
            log::Errorf(L"GetMessage 返回 -1 err={}", ::GetLastError());
            break;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
    log::Info(L"消息循环结束");
    return 0;
}

}  // namespace tf::core
