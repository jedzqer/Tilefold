#include "core/Settings.h"

#include <windows.h>
#include <shlobj.h>

#include "core/Paths.h"

namespace tf::cfg {

std::wstring SettingsPath() { return tf::paths::DataDir() + L"\\settings.ini"; }

Settings LoadSettings() {
    Settings settings;
    const std::wstring path = SettingsPath();
    wchar_t obsoleteValue[8]{};
    if (::GetPrivateProfileStringW(L"desktop", L"hideNativeRecycleBin", L"", obsoleteValue,
                                   static_cast<DWORD>(sizeof(obsoleteValue) / sizeof(obsoleteValue[0])),
                                   path.c_str()) > 0) {
        ::WritePrivateProfileStringW(L"desktop", L"hideNativeRecycleBin", nullptr, path.c_str());
    }
    const UINT value = ::GetPrivateProfileIntW(L"appearance", L"labelColor", 0,
                                               path.c_str());
    if (value <= static_cast<UINT>(LabelColor::White)) {
        settings.labelColor = static_cast<LabelColor>(value);
    }
    settings.hideNativeDesktopIcons = ::GetPrivateProfileIntW(
        L"desktop", L"hideNativeDesktopIcons", 1, path.c_str()) != 0;
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
    const bool iconsSaved = ::WritePrivateProfileStringW(
        L"desktop", L"hideNativeDesktopIcons", settings.hideNativeDesktopIcons ? L"1" : L"0",
        path.c_str()) != FALSE;
    const bool onboardingSaved = ::WritePrivateProfileStringW(
        L"desktop", L"onboardingComplete", settings.onboardingComplete ? L"1" : L"0",
        path.c_str()) != FALSE;
    return colorSaved && iconsSaved && onboardingSaved;
}

void ApplyDesktopVisibility(const Settings& settings) {
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
    HKEY advanced = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER,
                          L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced",
                          0, nullptr, 0, KEY_SET_VALUE, nullptr, &advanced, nullptr) == ERROR_SUCCESS) {
        const DWORD hidden = settings.hideNativeDesktopIcons ? 1u : 0u;
        ::RegSetValueExW(advanced, L"HideIcons", 0, REG_DWORD,
                         reinterpret_cast<const BYTE*>(&hidden), sizeof(hidden));
        ::RegCloseKey(advanced);
    }
    ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    ::SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"),
        SMTO_ABORTIFHUNG, 1500, nullptr);
}

}  // namespace tf::cfg
