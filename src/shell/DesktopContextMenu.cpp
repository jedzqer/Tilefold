#include "shell/DesktopContextMenu.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <string_view>

#include "core/Ipc.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/PromptDialog.h"
#include "shell/BoxScan.h"
#include "shell/GroupFolder.h"
#include "shell/ShellMenu.h"

namespace {

// HKCU\Software\Classes\DesktopBackground\Shell\<这一项>
constexpr wchar_t kMenuSubKey[] =
    L"Software\\Classes\\DesktopBackground\\Shell\\Tilefold.NewBox";
constexpr wchar_t kMenuLabel[] = L"新建收纳文件夹";
constexpr wchar_t kNewBoxFlag[] = L"--new-box";

bool SetString(HKEY root, const wchar_t* subKey, const wchar_t* name, const std::wstring& value) {
    HKEY key = nullptr;
    const LSTATUS open = ::RegCreateKeyExW(root, subKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                                           &key, nullptr);
    if (open != ERROR_SUCCESS) {
        tf::log::Warnf(L"注册表：打开 {} 失败 err={}", subKey, open);
        return false;
    }
    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS status = ::RegSetValueExW(key, name, 0, REG_SZ,
                                            reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    ::RegCloseKey(key);
    if (status != ERROR_SUCCESS) {
        tf::log::Warnf(L"注册表：写 {} 失败 err={}", name != nullptr ? name : L"(默认)", status);
        return false;
    }
    return true;
}

// 带空格的路径必须加引号，否则资源管理器按空格切参数
std::wstring Quoted(const std::wstring& path) { return L"\"" + path + L"\""; }

void DeleteKeyTree(HKEY root, const wchar_t* subKey) {
    const LSTATUS status = ::RegDeleteTreeW(root, subKey);
    if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND) {
        tf::log::Warnf(L"注册表：删除 {} 失败 err={}", subKey, status);
    }
}

// 菜单需要一个属于本线程的真窗口当 owner。现在弹菜单的宿主窗口由
// SystemMenu 自己建（见 shell/ShellMenu.cpp），这里不再需要自备 owner 窗口。

}  // namespace

namespace tf::desktop_menu {

bool Install() {
    const std::wstring exe = tf::paths::ExePath();
    if (exe.empty()) {
        log::Warn(L"桌面右键菜单：拿不到 exe 路径，跳过注册");
        return false;
    }

    bool ok = SetString(HKEY_CURRENT_USER, kMenuSubKey, nullptr, kMenuLabel);
    ok = SetString(HKEY_CURRENT_USER, kMenuSubKey, L"Icon", Quoted(exe) + L",0") && ok;
    ok = SetString(HKEY_CURRENT_USER, kMenuSubKey, L"Position", L"Top") && ok;
    ok = SetString(HKEY_CURRENT_USER, (std::wstring(kMenuSubKey) + L"\\command").c_str(), nullptr,
                   Quoted(exe) + L" " + kNewBoxFlag) &&
         ok;

    if (ok) {
        // 资源管理器缓存了右键菜单项，不通知一声可能得重启 explorer 才看见
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        log::Infof(L"桌面右键菜单已注册：{} -> {} {}", kMenuLabel, exe, kNewBoxFlag);
    }
    return ok;
}

void Remove() {
    DeleteKeyTree(HKEY_CURRENT_USER, kMenuSubKey);
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool IsNewBoxInvocation() {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (argv == nullptr) return false;
    bool found = false;
    for (int i = 1; i < argc && !found; ++i) {
        found = std::wstring_view(argv[i]) == kNewBoxFlag;
    }
    ::LocalFree(argv);
    return found;
}

int RunNewBoxCommand() {
    const UINT newBoxMsg = ::RegisterWindowMessageW(tf::ipc::kNewBoxMessage);
    const HWND instance = tf::ipc::FindRunningInstance();
    if (instance != nullptr && newBoxMsg != 0) {
        ::PostMessageW(instance, newBoxMsg, 0, 0);
        log::Info(L"--new-box：已把请求转给正在运行的实例");
        return 0;
    }

    // 没有主实例在跑：自己弹输入框把目录建出来，下次启动 Tilefold 就能看到。
    // 不直接建默认名，是因为用户点的就是「新建」——名字该由他来起。
    log::Info(L"--new-box：没有运行中的实例，就地建目录");
    const auto name = tf::ui::PromptForText(nullptr, L"新建收纳文件夹", L"文件夹名称：",
                                           tf::box::kDefaultGroupName);
    if (!name.has_value()) return 0;  // 用户取消，不是错误

    const std::wstring created = tf::box::CreateGroupFolder(tf::box::DefaultRoot(), *name);
    if (created.empty()) {
        ::MessageBoxW(nullptr, L"创建文件夹失败，详情见 data\\logs\\tilefold.log。",
                      L"Tilefold", MB_OK | MB_ICONWARNING);
        return 1;
    }
    log::Infof(L"--new-box：已创建 {}", created);
    return 0;
}

DesktopCommand ShowBackgroundMenu(HWND owner, POINT screenPt, bool nativeIconsHidden) {
    // 自有项的 id 走 0xE000 高段：系统动词占 [1, 0x6FFF]，互不干扰
    constexpr UINT kIdOpenBox = 0xE001;
    constexpr UINT kIdRefresh = 0xE002;
    constexpr UINT kIdToggleIcons = 0xE003;

    // 菜单主体交给 explorer：查看 / 排序方式 / 刷新 / 粘贴 / 新建 / 显示设置 /
    // 个性化 / 属性 —— 和桌面空白处右键一模一样。我们自己那几条插在最上面。
    tf::shell::SystemMenu menu;
    if (!menu.BuildBackground(owner, std::wstring())) {
        log::Warn(L"桌面右键：取不到系统背景菜单，只剩自有项");
    }

    // 自己的项放最上面（用户要求：先我们的，再原版的）。
    // QueryContextMenu 只会往后追加，所以生成完再插回去：0..2 是我们的，3 是分隔线。
    //
    // 注意：“新建收纳文件夹”不放这里。它已经通过 HKCU\...\DesktopBackground\Shell\Tilefold.NewBox
    // 注册进系统菜单了（带图标、排在原版项里），再插一条就是重复。
    menu.PrependExtra(L"打开收纳文件夹(&O)", kIdOpenBox, 0);
    menu.PrependExtra(L"刷新(&E)", kIdRefresh, 1);
    menu.PrependExtra(nativeIconsHidden ? L"显示系统桌面图标(&D)" : L"隐藏系统桌面图标(&D)",
                      kIdToggleIcons, 2);
    menu.PrependSeparator(3);

    const UINT cmd = menu.Track(screenPt);
    if (menu.IsShellCommand(cmd)) {
        // 系统动词：新建文件夹、粘贴、刷新、显示设置… 干完可能改了桌面
        if (menu.Invoke(cmd)) return DesktopCommand::DesktopChanged;
        return DesktopCommand::None;
    }

    switch (cmd) {
        case kIdOpenBox: return DesktopCommand::OpenBoxRoot;
        case kIdRefresh: return DesktopCommand::Refresh;
        case kIdToggleIcons: return DesktopCommand::ToggleNativeIcons;
        default: return DesktopCommand::None;
    }
}

}  // namespace tf::desktop_menu
