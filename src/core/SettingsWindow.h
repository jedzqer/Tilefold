#pragma once

#include <windows.h>

#include <functional>

#include "core/Settings.h"

namespace tf::core {

class SettingsWindow {
public:
    using ChangedHandler = std::function<void(const cfg::Settings&)>;
    bool Show(HWND owner, HINSTANCE instance, cfg::Settings settings, ChangedHandler changed,
              bool firstRun = false);
    void Close();
    bool IsOpen() const;

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);
    void ApplySelection();
    void ApplyToggle(int id);
    void LayoutControls();
    void CenterAndFit();
    void FitToWorkArea();
    void UpdateFont();
    void ScrollTo(int position);
    HFONT font_ = nullptr;
    int scroll_ = 0;
    int contentHeight_ = 0;
    bool layingOut_ = false;
    bool applying_ = false;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    cfg::Settings settings_{};
    ChangedHandler changed_;
};

}  // namespace tf::core
