#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <map>
#include <vector>

namespace on_this_day {

struct MonthDay {
    unsigned month;
    unsigned day;
    bool operator==(const MonthDay&) const = default;
};

using PhotoDateReader = std::function<std::optional<MonthDay>(const std::filesystem::path&)>;

std::vector<std::filesystem::path> ScanPhotos(const std::filesystem::path& folder,
    MonthDay date, const PhotoDateReader& readDate);

struct DatedPhoto {
    std::filesystem::path path;
    MonthDay date;
};

std::vector<DatedPhoto> ScanDatedPhotos(const std::filesystem::path& folder,
    const PhotoDateReader& readDate);

class PhotoRotation {
public:
    void Reset(std::vector<std::filesystem::path> photos);
    bool Empty() const { return photos_.empty(); }
    bool Advance(const std::function<bool(const std::filesystem::path&)>& display);

private:
    std::vector<std::filesystem::path> photos_;
    std::size_t next_ = 0;
};

// A is a one-time index of all photos with DateTaken; B is created per month/day
// only on first use. Each day preserves its own rotation position.
class DailyPhotoCache {
public:
    void Reset(std::vector<DatedPhoto> indexed);
    std::size_t IndexedCount() const { return indexed_.size(); }
    std::size_t DayCacheCount() const { return byDay_.size(); }
    PhotoRotation& ForDay(MonthDay date);
private:
    std::vector<DatedPhoto> indexed_;
    std::map<unsigned, PhotoRotation> byDay_;
};

} // namespace on_this_day

#ifdef _WIN32
#include <windows.h>
#include <string>
#include <string_view>

namespace on_this_day {

enum class DesktopInterface { unavailable, windows11Initial, windows11Before24H2, windows11Modern };
DesktopInterface DesktopInterfaceForBuild(unsigned long build);

bool IsExplorerRefreshLabel(std::wstring_view label, std::wstring_view localized = {});
bool IsRefreshMenuClick(HWND menu, RECT bounds, HWND pressedWindow, POINT pressedAt, POINT releasedAt);

bool IsCurrentDesktopGuid(const GUID& shell, const std::optional<GUID>& registered,
    const std::vector<GUID>& desktops);

std::optional<MonthDay> ReadDateTaken(const std::filesystem::path& photo);

} // namespace on_this_day
#endif
