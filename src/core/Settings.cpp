#include "core/Settings.h"

#include <windows.h>
#include <shlobj.h>

#include "core/Paths.h"

namespace tf::cfg {

std::wstring SettingsPath() { return tf::paths::DataDir() + L"\\settings.ini"; }

Settings LoadSettings() {
    Settings settings;
    const std::wstring path = SettingsPath();
    // 已经废弃的键：读到时顺手删掉，免得旧 ini 里躺着一个不再起作用的设置让人误会
    for (const wchar_t* obsolete : {L"hideNativeRecycleBin", L"hideNativeDesktopIcons"}) {
        wchar_t obsoleteValue[8]{};
        if (::GetPrivateProfileStringW(
                L"desktop", obsolete, L"", obsoleteValue,
                static_cast<DWORD>(sizeof(obsoleteValue) / sizeof(obsoleteValue[0])),
                path.c_str()) > 0) {
            ::WritePrivateProfileStringW(L"desktop", obsolete, nullptr, path.c_str());
        }
    }
    const UINT value = ::GetPrivateProfileIntW(L"appearance", L"labelColor", 0,
                                               path.c_str());
    if (value <= static_cast<UINT>(LabelColor::White)) {
        settings.labelColor = static_cast<LabelColor>(value);
    }
    settings.onboardingComplete = ::GetPrivateProfileIntW(
        L"desktop", L"onboardingComplete", 0, path.c_str()) != 0;
    return settings;
}

bool SaveSettings(const Settings& settings) {
    tf::paths::EnsureDir(tf::paths::DataDir());
    const wchar_t* value = L"0";
    if (settings.labelColor == LabelColor::Black) value = L"1";
    if (settings.labelColor == LabelColor::White) value = L"2";
    const std::wstring path = SettingsPath();
    const bool colorSaved = ::WritePrivateProfileStringW(L"appearance", L"labelColor", value,
                                                          path.c_str()) != FALSE;
    ::WritePrivateProfileStringW(L"desktop", L"hideNativeRecycleBin", nullptr, path.c_str());
    ::WritePrivateProfileStringW(L"desktop", L"hideNativeDesktopIcons", nullptr, path.c_str());
    const bool onboardingSaved = ::WritePrivateProfileStringW(
        L"desktop", L"onboardingComplete", settings.onboardingComplete ? L"1" : L"0",
        path.c_str()) != FALSE;
    return colorSaved && onboardingSaved;
}

namespace {

// 写 Explorer\Advanced\HideIcons 并通知 shell：这是“显示桌面图标”那个系统开关的值。
void SetHideIconsValue(DWORD hidden) {
    HKEY advanced = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                          0, nullptr, 0, KEY_SET_VALUE, nullptr, &advanced, nullptr) == ERROR_SUCCESS) {
        ::RegSetValueExW(advanced, L"HideIcons", 0, REG_DWORD,
                         reinterpret_cast<const BYTE*>(&hidden), sizeof(hidden));
        ::RegCloseKey(advanced);
    }
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    ::SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"),
        SMTO_ABORTIFHUNG, 1500, nullptr);
}

}  // namespace

void ApplyDesktopVisibility() {
    constexpr wchar_t recycleGuid[] = L"{645FF040-5081-101B-9F08-00AA002F954E}";
    constexpr const wchar_t* recycleKeys[] = {
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\NewStartPanel",
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\HideDesktopIcons\\ClassicStartMenu",
    };
    for (const auto* keyPath : recycleKeys) {
        HKEY key = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
            ::RegDeleteValueW(key, recycleGuid);
            ::RegCloseKey(key);
        }
    }
    // 恒为 1：原生桌面图标层在 Tilefold 运行时从不显示
    SetHideIconsValue(1u);
}

void RestoreDesktopVisibility() {
    // 退出时把原生桌面还回去：Tilefold 不在跑，就没理由继续藏着用户的图标。
    // 否则关了 Tilefold 桌面会变成一片空白，看上去像文件丢了。
    SetHideIconsValue(0u);
}

}  // namespace tf::cfg
