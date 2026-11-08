#include "shell/ShellMenu.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include "core/Log.h"

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kMenuHostClass[] = L"Tilefold.MenuHost";

// QueryContextMenu 从 idCmdFirst 开始给系统动词编号，上限是 0x6FFF（COMDLG 约定），
// 我们自己的项放到高段，永远不会和系统动词的 id 撞。
constexpr UINT kShellCommandFirst = 1;
constexpr UINT kShellCommandLast = 0x6FFF;

struct MenuHostContext {
    ComPtr<IContextMenu2> menu2;
    ComPtr<IContextMenu3> menu3;
    // 我们自己往菜单最上面插了几项。
    // shell 的 IContextMenu 是拿 WM_INITMENUPOPUP 的 LOWORD(lParam) 当「第几项」来定位
    // 子菜单的；我们在前面插了东西、位置就整体后移了，直接转发过去它就会认错项，
    // 结果就是「新建」「Windows PowerShell」这类子菜单点开是空的。
    // 所以转发前把这几个位置减回去。
    HMENU root = nullptr;
    int prependCount = 0;
};

// 弹出菜单期间临时借一个窗口当 owner，只干一件事：把菜单消息转发给 shell 扩展。
// 我们自己的 widget 窗口 WndProc 不认 WM_INITMENUPOPUP / WM_DRAWITEM / WM_MEASUREITEM，
// 自绘型的扩展收不到转发就会画不出来（个别第三方扩展甚至会直接崩）。
LRESULT CALLBACK MenuHostProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* ctx = reinterpret_cast<MenuHostContext*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (ctx != nullptr) {
        switch (msg) {
            case WM_INITMENUPOPUP: {
                WPARAM wp2 = wp;
                LPARAM lp2 = lp;
                if (ctx->root != nullptr && ctx->prependCount > 0) {
                    const UINT pos = static_cast<UINT>(LOWORD(lp));
                    MENUITEMINFOW info{};
                    info.cbSize = sizeof(info);
                    info.fMask = MIIM_SUBMENU;
                    // 只有「根菜单里的子项」才需要减偏移；子菜单内部的项位置没动过。
                    if (::GetMenuItemInfoW(ctx->root, pos, TRUE, &info) &&
                        info.hSubMenu == reinterpret_cast<HMENU>(wp)) {
                        if (pos < static_cast<UINT>(ctx->prependCount)) {
                            return 0;  // 我们自己那几项没子菜单，不用打扰 shell
                        }
                        lp2 = MAKELPARAM(pos - static_cast<UINT>(ctx->prependCount), HIWORD(lp));
                    }
                }
                if (ctx->menu3 != nullptr) {
                    LRESULT result = 0;
                    if (SUCCEEDED(ctx->menu3->HandleMenuMsg2(msg, wp2, lp2, &result))) return result;
                } else if (ctx->menu2 != nullptr) {
                    if (SUCCEEDED(ctx->menu2->HandleMenuMsg(msg, wp2, lp2))) return 0;
                }
                break;
            }
            case WM_DRAWITEM:
            case WM_MEASUREITEM:
            case WM_MENUCHAR: {
                if (ctx->menu3 != nullptr) {
                    LRESULT result = 0;
                    if (SUCCEEDED(ctx->menu3->HandleMenuMsg2(msg, wp, lp, &result))) return result;
                } else if (ctx->menu2 != nullptr) {
                    if (SUCCEEDED(ctx->menu2->HandleMenuMsg(msg, wp, lp))) return 0;
                }
                break;
            }
            default:
                break;
        }
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

// 把窗口切成前台窗口。
// SetForegroundWindow 有个限制：只有“前台进程”或“刚刚收到最后一次输入”的进程才能切前台。
// 图标右键时点击本来就是打给我们的，没问题；但**桌面背景右键**那一下是 Explorer 接的，
// 我们不是前台进程，直接 SetForegroundWindow 会失败 —— 菜单拿不到前台，
// 后续任何鼠标消息都会把它顶掉（表现：菜单一闪就消失）。
// 经典解法：临时把前台线程的输入队列接到自己线程上，切完再解开。
[[maybe_unused]] void EnsureForeground(HWND hwnd) {
    if (hwnd == nullptr) return;
    if (::GetForegroundWindow() == hwnd) return;
    ::SetForegroundWindow(hwnd);
    if (::GetForegroundWindow() == hwnd) return;

    const HWND fg = ::GetForegroundWindow();
    const DWORD fgThread = fg != nullptr ? ::GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD myThread = ::GetCurrentThreadId();
    bool attached = false;
    if (fgThread != 0 && fgThread != myThread) {
        attached = ::AttachThreadInput(fgThread, myThread, TRUE) != FALSE;
    }
    ::SetForegroundWindow(hwnd);
    ::BringWindowToTop(hwnd);
    if (attached) ::AttachThreadInput(fgThread, myThread, FALSE);
    if (::GetForegroundWindow() != hwnd) {
        tf::log::Warnf(L"右键菜单：切前台窗口失败（菜单可能一闪就消失）");
    }
}

HWND CreateMenuHost(MenuHostContext* ctx) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MenuHostProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = kMenuHostClass;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        tf::log::Warnf(L"右键菜单：注册宿主窗口类失败 err={}", ::GetLastError());
        return nullptr;
    }
    HWND hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW, kMenuHostClass, L"", WS_POPUP, 0, 0, 0, 0,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    if (hwnd != nullptr) {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));
    }
    return hwnd;
}

void InvokeShellVerb(IContextMenu* menu, HWND owner, UINT commandId) {
    // CMIC_MASK_* 在旧 SDK 里没有定义（新版换成了 SEE_MASK_*），直接用数值位：
    // CMIC_MASK_UNICODE = SEE_MASK_UNICODE = 0x00004000，
    // CMIC_MASK_ASYNCOK = SEE_MASK_ASYNCOK = 0x00100000
    constexpr ULONG kMaskUnicode = 0x00004000;
    constexpr ULONG kMaskAsyncOk = 0x00100000;
    CMINVOKECOMMANDINFOEX info{};
    info.cbSize = sizeof(info);
    // ASYNCOK：让 shell 自己决定同步/异步，别把 UI 线程卡在启动某个 app 上。
    info.fMask = kMaskAsyncOk | kMaskUnicode;
    info.hwnd = owner;
    info.lpVerb = MAKEINTRESOURCEA(commandId - kShellCommandFirst);
    info.lpVerbW = MAKEINTRESOURCEW(commandId - kShellCommandFirst);
    info.nShow = SW_SHOWNORMAL;
    const HRESULT hr = menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&info));
    if (FAILED(hr)) {
        tf::log::Warnf(L"右键菜单：动词执行失败 id={} hr=0x{:08X}", commandId,
                       static_cast<unsigned>(hr));
    }
}

}  // namespace

namespace tf::shell {

struct SystemMenu::Impl {
    HWND owner = nullptr;
    HWND host = nullptr;
    HMENU menu = nullptr;
    ComPtr<IContextMenu> shell;
    ComPtr<IContextMenu2> shell2;
    ComPtr<IContextMenu3> shell3;
    MenuHostContext hostContext;
    // QueryContextMenu 实际发出去的最大 id（系统动词是 [1, shellLast]）
    UINT shellLast = 0;
    bool extrasStarted = false;

    ~Impl() {
        if (host != nullptr) ::DestroyWindow(host);
        if (menu != nullptr) ::DestroyMenu(menu);
    }

    // 弹菜单/执行动词时用的 owner：宿主窗口能转发菜单消息，优先用它
    HWND PopupOwner() const { return host != nullptr ? host : owner; }

    void AttachShellMenu(ComPtr<IContextMenu> shellMenu) {
        shell = std::move(shellMenu);
        shell.As(&shell2);
        shell.As(&shell3);
        hostContext.menu2 = shell2;
        hostContext.menu3 = shell3;
    }

    // 系统动词生成到我们自己的 HMENU 里
    bool Query(UINT flags) {
        if (shell == nullptr || menu == nullptr) return false;
        const HRESULT hr = shell->QueryContextMenu(menu, 0, kShellCommandFirst, kShellCommandLast,
                                                   flags);
        if (FAILED(hr)) return false;
        shellLast = HRESULT_CODE(hr);
        return true;
    }
};

SystemMenu::SystemMenu() : impl_(std::make_unique<Impl>()) {
    impl_->menu = ::CreatePopupMenu();
    impl_->hostContext.root = impl_->menu;
    if (impl_->menu == nullptr) {
        log::Warnf(L"右键菜单：CreatePopupMenu 失败 err={}", ::GetLastError());
    }
}

SystemMenu::~SystemMenu() = default;

bool SystemMenu::BuildItem(HWND owner, const std::wstring& parsingName) {
    Impl& d = *impl_;
    d.owner = owner;
    if (d.menu == nullptr || parsingName.empty()) return false;

    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF attributes = 0;
    const HRESULT parseHr = ::SHParseDisplayName(parsingName.c_str(), nullptr, &pidl, 0, &attributes);
    if (FAILED(parseHr) || pidl == nullptr) {
        log::Warnf(L"右键菜单：SHParseDisplayName 失败 hr=0x{:08X} path={}",
                   static_cast<unsigned>(parseHr), parsingName);
        return false;
    }

    ComPtr<IShellFolder> parent;
    PCUITEMID_CHILD childPidl = nullptr;
    HRESULT hr = ::SHBindToParent(pidl, IID_PPV_ARGS(parent.GetAddressOf()), &childPidl);
    if (FAILED(hr) || parent == nullptr || childPidl == nullptr) {
        log::Warnf(L"右键菜单：SHBindToParent 失败 hr=0x{:08X} path={}",
                   static_cast<unsigned>(hr), parsingName);
        ::CoTaskMemFree(pidl);
        return false;
    }

    ComPtr<IContextMenu> menu;
    hr = parent->GetUIObjectOf(owner, 1, &childPidl, IID_IContextMenu, nullptr,
                               reinterpret_cast<void**>(menu.GetAddressOf()));
    ::CoTaskMemFree(pidl);
    if (FAILED(hr) || menu == nullptr) {
        log::Warnf(L"右键菜单：取 IContextMenu 失败 hr=0x{:08X} path={}",
                   static_cast<unsigned>(hr), parsingName);
        return false;
    }

    d.AttachShellMenu(std::move(menu));
    if (d.host == nullptr) d.host = CreateMenuHost(&d.hostContext);

    // CMF_NORMAL 就是资源管理器里普通右键的那一套；不追加 CMF_EXTENDEDVERBS，
    // 免得把「删除（不进回收站）」这种 Shift+右键的动词也摊出来。
    if (!d.Query(CMF_NORMAL)) {
        log::Warnf(L"右键菜单：QueryContextMenu 失败 path={}", parsingName);
        d.shell.Reset();
        d.shell2.Reset();
        d.shell3.Reset();
        return false;
    }
    return true;
}

bool SystemMenu::BuildBackground(HWND owner, const std::wstring& folderPath) {
    Impl& d = *impl_;
    d.owner = owner;
    if (d.menu == nullptr) return false;

    ComPtr<IShellFolder> folder;
    HRESULT hr = S_OK;
    if (folderPath.empty()) {
        // 桌面根：虚拟桌面本身（右键桌面出来的是「查看/排序/刷新/新建/显示设置…」）
        hr = ::SHGetDesktopFolder(folder.GetAddressOf());
    } else {
        PIDLIST_ABSOLUTE pidl = nullptr;
        SFGAOF attributes = 0;
        hr = ::SHParseDisplayName(folderPath.c_str(), nullptr, &pidl, 0, &attributes);
        if (SUCCEEDED(hr) && pidl != nullptr) {
            hr = ::SHBindToObject(nullptr, pidl, nullptr, IID_PPV_ARGS(folder.GetAddressOf()));
        } else if (pidl == nullptr) {
            hr = E_FAIL;
        }
        if (pidl != nullptr) ::CoTaskMemFree(pidl);
    }
    if (FAILED(hr) || folder == nullptr) {
        log::Warnf(L"右键菜单：取文件夹失败 hr=0x{:08X} path={}", static_cast<unsigned>(hr),
                   folderPath.empty() ? L"(桌面)" : folderPath);
        return false;
    }

    // 首选：文件夹自己的背景菜单。这是文档里最轻的一条路，不建 view。
    ComPtr<IContextMenu> menu;
    hr = folder->CreateViewObject(owner, IID_PPV_ARGS(menu.GetAddressOf()));
    if (FAILED(hr) || menu == nullptr) {
        // 兜底：某些命名空间（尤其桌面根）只认 IShellView → SVGIO_BACKGROUND
        ComPtr<IShellView> view;
        hr = folder->CreateViewObject(owner, IID_PPV_ARGS(view.GetAddressOf()));
        if (SUCCEEDED(hr) && view != nullptr) {
            hr = view->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(menu.GetAddressOf()));
        }
    }
    if (FAILED(hr) || menu == nullptr) {
        log::Warnf(L"右键菜单：取背景菜单失败 hr=0x{:08X} path={}", static_cast<unsigned>(hr),
                   folderPath.empty() ? L"(桌面)" : folderPath);
        return false;
    }

    d.AttachShellMenu(std::move(menu));
    if (d.host == nullptr) d.host = CreateMenuHost(&d.hostContext);

    // CMF_EXPLORE 让「查看」「排序方式」这类只在资源管理器里有意义的动词也生成出来
    if (!d.Query(static_cast<UINT>(CMF_NORMAL | CMF_EXPLORE))) {
        log::Warnf(L"右键菜单：背景 QueryContextMenu 失败 path={}",
                   folderPath.empty() ? L"(桌面)" : folderPath);
        d.shell.Reset();
        d.shell2.Reset();
        d.shell3.Reset();
        return false;
    }
    return true;
}

bool SystemMenu::ShellMenuAvailable() const { return impl_->shellLast > 0; }

HMENU SystemMenu::Menu() const { return impl_->menu; }

void SystemMenu::AppendSeparator() {
    Impl& d = *impl_;
    if (d.menu == nullptr) return;
    ::AppendMenuW(d.menu, MF_SEPARATOR, 0, nullptr);
    d.extrasStarted = true;
}

void SystemMenu::AppendExtra(const std::wstring& label, UINT id) {
    Impl& d = *impl_;
    if (d.menu == nullptr || label.empty()) return;
    if (!d.extrasStarted) {
        ::AppendMenuW(d.menu, MF_SEPARATOR, 0, nullptr);
        d.extrasStarted = true;
    }
    ::AppendMenuW(d.menu, MF_STRING, id, label.c_str());
}

void SystemMenu::PrependExtra(const std::wstring& label, UINT id, UINT position) {
    Impl& d = *impl_;
    if (d.menu == nullptr || label.empty()) return;
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_ID | MIIM_STRING | MIIM_FTYPE;
    item.fType = MFT_STRING;
    item.wID = id;
    item.dwTypeData = const_cast<wchar_t*>(label.c_str());
    const int count = ::GetMenuItemCount(d.menu);
    // 位置越界就当作追加，不至于把菜单弄丢
    if (position > static_cast<UINT>(count < 0 ? 0 : count)) position = static_cast<UINT>(count < 0 ? 0 : count);
    if (!::InsertMenuItemW(d.menu, position, TRUE, &item)) {
        log::Warnf(L"右键菜单：InsertMenuItem 失败 position={} err={}", position, ::GetLastError());
    }
}

void SystemMenu::PrependSeparator(UINT position) {
    Impl& d = *impl_;
    if (d.menu == nullptr) return;
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_FTYPE;
    item.fType = MFT_SEPARATOR;
    const int count = ::GetMenuItemCount(d.menu);
    if (position > static_cast<UINT>(count < 0 ? 0 : count)) position = static_cast<UINT>(count < 0 ? 0 : count);
    if (!::InsertMenuItemW(d.menu, position, TRUE, &item)) {
        log::Warnf(L"右键菜单：插入分隔线失败 position={} err={}", position, ::GetLastError());
    }
}

UINT SystemMenu::Track(POINT screenPt) {
    Impl& d = *impl_;
    if (d.menu == nullptr) return 0;
    if (::GetMenuItemCount(d.menu) <= 0) return 0;

    const HWND popupOwner = d.PopupOwner();
    // 算出「我们自己插在最前面几项」的数量：第一个 id 落在 shell 段的就是 shell 的第一项。
    // 菜单有时序性（用户可能先 Build 再追加再插队），所以放到临弹之前数。
    int prepend = 0;
    const int count = ::GetMenuItemCount(d.menu);
    for (int i = 0; i < count; ++i) {
        const UINT id = ::GetMenuItemID(d.menu, i);
        if (id != static_cast<UINT>(-1) && id >= kShellCommandFirst && id <= d.shellLast) {
            prepend = i;
            break;
        }
    }
    d.hostContext.prependCount = prepend;
    if (popupOwner != nullptr) {
        ::SetForegroundWindow(popupOwner);
    }
    const UINT command = ::TrackPopupMenuEx(
        d.menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN, screenPt.x,
        screenPt.y, popupOwner, nullptr);
    if (popupOwner != nullptr) ::PostMessageW(popupOwner, WM_NULL, 0, 0);
    return command;
}

bool SystemMenu::IsShellCommand(UINT commandId) const {
    return commandId >= kShellCommandFirst && commandId <= impl_->shellLast;
}

bool SystemMenu::Invoke(UINT commandId) {
    Impl& d = *impl_;
    if (!IsShellCommand(commandId) || d.shell == nullptr) return false;
    log::Infof(L"右键菜单：执行系统动词 id={}", commandId);
    InvokeShellVerb(d.shell.Get(), d.PopupOwner(), commandId);
    return true;
}

bool ShowSystemContextMenu(HWND owner, const std::wstring& parsingName,
                          const std::wstring& extraLabel, UINT* chosenId) {
    if (chosenId != nullptr) *chosenId = 0;
    if (parsingName.empty()) return false;

    SystemMenu menu;
    if (!menu.BuildItem(owner, parsingName)) return false;
    if (!extraLabel.empty()) menu.AppendExtra(extraLabel, 0xE000);

    POINT cursor{};
    if (!::GetCursorPos(&cursor)) return false;
    const UINT command = menu.Track(cursor);
    if (command == 0xE000) {
        if (chosenId != nullptr) *chosenId = command;
    } else if (menu.IsShellCommand(command)) {
        menu.Invoke(command);
    }
    return true;
}

}  // namespace tf::shell
