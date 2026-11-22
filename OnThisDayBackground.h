#pragma once

#include "OnThisDayBackground_Core.h"

#include <windows.h>
#include <string>

namespace on_this_day {

std::optional<MonthDay> ReadPhotoDate(const std::filesystem::path& photo);
HBITMAP LoadPhoto(const std::filesystem::path& photo);
std::optional<std::wstring> CurrentVirtualDesktop();
bool DesktopHasFocus();

class DesktopBackground {
public:
    explicit DesktopBackground(HINSTANCE instance) : instance_(instance) {}
    ~DesktopBackground();
    DesktopBackground(const DesktopBackground&) = delete;
    DesktopBackground& operator=(const DesktopBackground&) = delete;

    bool Show(const std::filesystem::path& photo);
    void Hide();
    void UpdatePlacement();

private:
    bool Place();
    void Detach();
    static LRESULT CALLBACK PaintWindow(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    HINSTANCE instance_;
    HWND window_ = nullptr;
    HBITMAP bitmap_ = nullptr;
};

} // namespace on_this_day
