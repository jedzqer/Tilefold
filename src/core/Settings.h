#pragma once

#include <string>

namespace tf::cfg {

enum class LabelColor { Automatic = 0, Black = 1, White = 2 };

struct Settings {
    LabelColor labelColor = LabelColor::Automatic;
    bool hideNativeDesktopIcons = true;
    bool onboardingComplete = false;
};

std::wstring SettingsPath();
Settings LoadSettings();
bool SaveSettings(const Settings& settings);
void ApplyDesktopVisibility(const Settings& settings);

}  // namespace tf::cfg
