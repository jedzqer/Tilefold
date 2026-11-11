#pragma once

#include <string>

namespace tf::cfg {

enum class LabelColor { Automatic = 0, Black = 1, White = 2 };

struct Settings {
    LabelColor labelColor = LabelColor::Automatic;
    bool onboardingComplete = false;
};

std::wstring SettingsPath();
Settings LoadSettings();
bool SaveSettings(const Settings& settings);

// 桌面由 Tilefold 全面接管：启动时把 Explorer 的原生桌面图标永久隐藏
// （HKCU\...\Explorer\Advanced\HideIcons=1），不再提供开关。
// 这样桌面空白处的右键菜单也必须由我们自己弹（见 shell/DesktopContextMenu）。
void ApplyDesktopVisibility();

// 退出时把 HideIcons 写回 0（原生桌面回来），DefView 也一并恢复显示。
// 全面接管只在 Tilefold 运行期间生效，关机后不把用户的桌面留在“没有任何图标”的状态。
void RestoreDesktopVisibility();

}  // namespace tf::cfg
