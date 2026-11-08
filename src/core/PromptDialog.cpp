#include "core/PromptDialog.h"

#include <algorithm>

#include "core/Dpi.h"
#include "core/Log.h"

namespace {

constexpr wchar_t kPromptClass[] = L"Tilefold.Prompt";
constexpr int kEditId = 1001;
constexpr int kOkId = IDOK;
constexpr int kCancelId = IDCANCEL;

// 客户区尺寸（DIP）
constexpr int kClientWidth = 360;
constexpr int kClientHeight = 130;
constexpr int kMargin = 16;
constexpr int kButtonWidth = 88;
constexpr int kButtonHeight = 30;
constexpr int kButtonGap = 8;

struct PromptState {
    HWND edit = nullptr;
    HWND okButton = nullptr;
    HWND cancelButton = nullptr;
    HFONT font = nullptr;
    std::wstring text;
    bool accepted = false;
    bool done = false;
};

void AcceptPrompt(PromptState* state) {
    if (state->edit == nullptr) return;
    const int length = ::GetWindowTextLengthW(state->edit);
    std::wstring buffer(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
    const int copied =
        ::GetWindowTextW(state->edit, buffer.data(), static_cast<int>(buffer.size()));
    buffer.resize(static_cast<std::size_t>(std::max(0, copied)));
    state->text = std::move(buffer);
    state->accepted = true;
    state->done = true;
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* state = reinterpret_cast<PromptState*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (state == nullptr) return ::DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
        case WM_COMMAND: {
            if (LOWORD(wp) == kOkId) {
                AcceptPrompt(state);
                return 0;
            }
            if (LOWORD(wp) == kCancelId) {
                state->done = true;
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            // 窗口不在这里销毁：模态循环看到 done 自己收尾，避免循环里窗口已经没了
            state->done = true;
            return 0;
        case WM_DESTROY:
        case WM_NCDESTROY:
            if (state->font != nullptr) {
                ::DeleteObject(state->font);
                state->font = nullptr;
            }
            return ::DefWindowProcW(hwnd, msg, wp, lp);
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

float PickScale(HWND owner) {
    // owner 经常是那个 0x0、不显示的 controlWnd_，问它 DPI 得到的可能是主屏而不是光标那块屏，
    // 所以只在 owner 真的能看到时才信它
    if (owner != nullptr && ::IsWindowVisible(owner) != FALSE) {
        const float scale = tf::dpi::ScaleForWindow(owner);
        if (scale > 0.0f) return scale;
    }
    POINT cursor{};
    ::GetCursorPos(&cursor);
    return tf::dpi::ScaleForPoint(cursor);
}

void CenterOnOwnerOrCursor(HWND hwnd, HWND owner, int width, int height) {
    RECT target{};
    bool have = false;
    // 空矩形（0x0 的 controlWnd_）当没给：否则会以它的中心 (0,0) 为圆心算，窗口直接飞出屏幕
    if (owner != nullptr && ::IsWindowVisible(owner) != FALSE && ::GetWindowRect(owner, &target) &&
        target.right > target.left && target.bottom > target.top) {
        have = true;
    }
    if (!have) {
        POINT cursor{};
        if (::GetCursorPos(&cursor)) {
            MONITORINFO mi{sizeof(mi)};
            if (::GetMonitorInfoW(::MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi)) {
                target = mi.rcWork;
                have = true;
            }
        }
    }
    if (!have) return;

    int x = target.left + ((target.right - target.left) - width) / 2;
    int y = target.top + ((target.bottom - target.top) - height) / 2;

    // 再夹回工作区：owner 可能贴着屏幕边，不夹就会有一部分落在屏幕外
    MONITORINFO mi{sizeof(mi)};
    if (::GetMonitorInfoW(::MonitorFromPoint(POINT{x + width / 2, y + height / 2},
                                             MONITOR_DEFAULTTONEAREST),
                          &mi)) {
        const RECT& work = mi.rcWork;
        if (x + width > work.right) x = work.right - width;
        if (y + height > work.bottom) y = work.bottom - height;
        if (x < work.left) x = work.left;
        if (y < work.top) y = work.top;
    }

    ::SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
}

}  // namespace

namespace tf::ui {

std::optional<std::wstring> PromptForText(HWND owner, const wchar_t* title, const wchar_t* prompt,
                                          const std::wstring& initial) {
    const float scale = PickScale(owner);
    const auto px = [scale](int dip) { return tf::dpi::Dip2Px(dip, scale); };

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &PromptProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kPromptClass;
    if (::RegisterClassExW(&wc) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        log::Warnf(L"输入框：注册窗口类失败 err={}", ::GetLastError());
        return std::nullopt;
    }

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;
    RECT frame{0, 0, px(kClientWidth), px(kClientHeight)};
    ::AdjustWindowRectExForDpi(&frame, style, FALSE, exStyle,
                               static_cast<UINT>(96.0f * scale + 0.5f));
    const int windowW = frame.right - frame.left;
    const int windowH = frame.bottom - frame.top;

    PromptState state;
    HWND hwnd = ::CreateWindowExW(exStyle, kPromptClass, title != nullptr ? title : L"Tilefold",
                                  style, CW_USEDEFAULT, CW_USEDEFAULT, windowW, windowH, owner,
                                  nullptr, wc.hInstance, &state);
    if (hwnd == nullptr) {
        log::Warnf(L"输入框：创建窗口失败 err={}", ::GetLastError());
        return std::nullopt;
    }

    const int rowPrompt = px(kMargin);
    const int rowEdit = rowPrompt + px(26);
    const int rowButtons = rowEdit + px(28) + px(14);
    const int right = px(kClientWidth) - px(kMargin);
    const int buttonW = px(kButtonWidth);
    const int buttonH = px(kButtonHeight);

    ::CreateWindowExW(0, L"STATIC", prompt != nullptr ? prompt : L"", WS_CHILD | WS_VISIBLE,
                      px(kMargin), rowPrompt, right - px(kMargin), px(20), hwnd, nullptr,
                      wc.hInstance, nullptr);
    state.edit = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(),
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                   px(kMargin), rowEdit, right - px(kMargin), px(24), hwnd,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)),
                                   wc.hInstance, nullptr);
    state.cancelButton = ::CreateWindowExW(
        0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
        right - buttonW, rowButtons, buttonW, buttonH, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCancelId)), wc.hInstance, nullptr);
    state.okButton = ::CreateWindowExW(
        0, L"BUTTON", L"确定",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        right - buttonW - px(kButtonGap) - buttonW, rowButtons, buttonW, buttonH, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOkId)), wc.hInstance, nullptr);

    state.font = ::CreateFontW(-px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                               DEFAULT_PITCH, L"Microsoft YaHei UI");
    if (state.font != nullptr) {
        for (HWND child : {state.edit, state.okButton, state.cancelButton}) {
            ::SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        }
        for (HWND child = ::GetWindow(hwnd, GW_CHILD); child != nullptr;
             child = ::GetWindow(child, GW_HWNDNEXT)) {
            ::SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state.font), TRUE);
        }
    }

    CenterOnOwnerOrCursor(hwnd, owner, windowW, windowH);
    ::ShowWindow(hwnd, SW_SHOWNORMAL);
    ::SetForegroundWindow(hwnd);
    ::SetFocus(state.edit);
    ::SendMessageW(state.edit, EM_SETSEL, 0, -1);  // 预填名字整段选中，直接打字就是改名

    log::Infof(L"输入框：等待名字输入（{}）", title != nullptr ? title : L"");

    MSG msg{};
    while (!state.done && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // 回车/ESC 自己处理：这是普通窗口不是对话框，IsDialogMessage 的默认按钮逻辑不可靠
        if (msg.message == WM_KEYDOWN && (msg.hwnd == hwnd || ::IsChild(hwnd, msg.hwnd))) {
            if (msg.wParam == VK_RETURN) {
                AcceptPrompt(&state);
                continue;
            }
            if (msg.wParam == VK_ESCAPE) {
                state.done = true;
                continue;
            }
        }
        if (!::IsDialogMessageW(hwnd, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    ::DestroyWindow(hwnd);

    // 循环是被 WM_QUIT 结束的（比如正在退出）：把这条消息还回去，别把主消息循环卡住
    if (!state.done && msg.message == WM_QUIT) {
        ::PostQuitMessage(static_cast<int>(msg.wParam));
    }

    if (!state.accepted) {
        log::Info(L"输入框：用户取消");
        return std::nullopt;
    }
    return state.text;
}

}  // namespace tf::ui
