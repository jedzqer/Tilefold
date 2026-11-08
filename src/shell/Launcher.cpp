#include "Launcher.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <thread>

#include "core/Log.h"

namespace {

void RunLaunch(const std::wstring& path) {
    // 测试开关：TF_NO_LAUNCH=1 时只记日志、不真启动。
    // 调动效时需要把“按下 -> 松开 -> 回弹”完整看完，但真启动会把桌面抢走，
    // 弹回的那几帧就再也拍不到了。
    {
        wchar_t flag[8]{};
        if (::GetEnvironmentVariableW(L"TF_NO_LAUNCH", flag, 8) > 0 && flag[0] == L'1') {
            tf::log::Infof(L"[TF_NO_LAUNCH] 跳过启动: {}", path);
            return;
        }
    }

    const HRESULT hrCo = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool needUninit = SUCCEEDED(hrCo);

    // 先用 PIDL 走一遍：.lnk / UWP / shell:AppsFolder 都能被正确解析。
    // SEE_MASK_INVOKEIDLIST 让 shell 用"打开"动词处理 IDList，而不是按文件路径猜。
    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF attrs = 0;
    const HRESULT hrParse = ::SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, &attrs);

    auto run = [&](PIDLIST_ABSOLUTE idlist) {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
        if (idlist != nullptr) sei.fMask |= SEE_MASK_INVOKEIDLIST;
        sei.lpVerb = L"open";
        sei.lpFile = path.c_str();
        sei.lpIDList = idlist;
        sei.nShow = SW_SHOWNORMAL;
        return ::ShellExecuteExW(&sei) != FALSE;
    };

    bool ok = false;
    if (SUCCEEDED(hrParse) && pidl != nullptr) {
        ok = run(pidl);
        ::CoTaskMemFree(pidl);
        if (!ok) {
            tf::log::Warnf(L"带 IDList 启动失败(err={})，回退到按路径启动: {}", ::GetLastError(), path);
            ok = run(nullptr);
        }
    } else {
        tf::log::Warnf(L"SHParseDisplayName 失败 hr=0x{:08X}，直接按路径启动: {}",
                   static_cast<unsigned>(hrParse), path);
        ok = run(nullptr);
    }

    if (ok) {
        tf::log::Infof(L"已启动: {}", path);
    } else {
        tf::log::Errorf(L"启动失败 err={}: {}", ::GetLastError(), path);
    }

    if (needUninit) ::CoUninitialize();
}

}  // namespace

namespace tf::shell {

void LaunchPath(const std::wstring& path) {
    if (path.empty()) return;
    // detached：启动失败不该影响 UI；线程本身是短命的
    std::thread(RunLaunch, path).detach();
}

}  // namespace tf::shell
