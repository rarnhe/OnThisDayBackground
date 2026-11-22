#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <vector>

namespace on_this_day {

struct MonthDay {
    unsigned month;
    unsigned day;
    bool operator==(const MonthDay&) const = default;
};

using PhotoDateReader = std::function<std::optional<MonthDay>(const std::filesystem::path&)>;

std::vector<std::filesystem::path> ScanPhotos(const std::filesystem::path& folder,
    MonthDay date, const PhotoDateReader& readDate, std::stop_token stop = {});

class PhotoRotation {
public:
    void Reset(std::vector<std::filesystem::path> photos);
    bool Advance(const std::function<bool(const std::filesystem::path&)>& display);

private:
    std::vector<std::filesystem::path> photos_;
    std::size_t next_ = 0;
};

} // namespace on_this_day

#ifdef _WIN32
#include <windows.h>
#include <string>

namespace on_this_day {

std::optional<MonthDay> ReadItemDate(const std::filesystem::path& photo);
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
#endif
