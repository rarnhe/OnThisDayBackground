#pragma once

#include <filesystem>
#include <functional>
#include <optional>
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

class PhotoRotation {
public:
    void Reset(std::vector<std::filesystem::path> photos);
    bool Empty() const { return photos_.empty(); }
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

enum class DesktopInterface { unavailable, windows11Initial, windows11Before24H2, windows11Modern };
DesktopInterface DesktopInterfaceForBuild(unsigned long build);

bool IsCurrentDesktopGuid(const GUID& shell, const std::optional<GUID>& registered,
    const std::vector<GUID>& desktops);

std::optional<MonthDay> ReadDateTaken(const std::filesystem::path& photo);

} // namespace on_this_day
#endif
