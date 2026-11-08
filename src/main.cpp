#include <windows.h>

#include <memory>

#include "core/App.h"
#include "core/Log.h"
#include "shell/DesktopContextMenu.h"

namespace {

constexpr wchar_t kSingleInstanceMutex[] = L"Local\\Tilefold.SingleInstance";

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    // manifest 里已经声明 PerMonitorV2，这里再显式设一次（对某些宿主进程可提前生效）
    ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    tf::log::Initialize();
    tf::log::Info(L"===== Tilefold 启动 =====");

    // 桌面右键「新建收纳文件夹」拉起来的就是这条路径：它不是一个完整实例，
    // 所以必须在单实例互斥体之前分流（主实例通常正在跑）。
    if (tf::desktop_menu::IsNewBoxInvocation()) {
        const int code = tf::desktop_menu::RunNewBoxCommand();
        tf::log::Info(L"===== Tilefold 退出（--new-box）=====");
        tf::log::Shutdown();
        return code;
    }

    HANDLE mutex = ::CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (mutex == nullptr) {
        tf::log::Errorf(L"CreateMutex 失败 err={}", ::GetLastError());
        return 1;
    }
    if (::GetLastError() == ERROR_ALREADY_EXISTS) {
        tf::log::Warn(L"已经有一个 Tilefold 在运行，本次退出");
        ::CloseHandle(mutex);
        return 0;
    }

    // STA：Shell API / 拖放 / 右键菜单都需要
    const HRESULT oleHr = ::OleInitialize(nullptr);
    if (FAILED(oleHr)) {
        tf::log::Errorf(L"OleInitialize 失败 hr=0x{:08X}", static_cast<unsigned>(oleHr));
    }

    int exitCode = 1;
    {
        auto app = std::make_unique<tf::core::App>(instance);
        if (app->Initialize()) {
            exitCode = app->Run();
        } else {
            tf::log::Error(L"初始化失败，退出");
        }
    }

    if (SUCCEEDED(oleHr)) ::OleUninitialize();

    tf::log::Info(L"===== Tilefold 退出 =====");
    tf::log::Shutdown();
    ::CloseHandle(mutex);
    return exitCode;
}
