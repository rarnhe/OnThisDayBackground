#pragma once

#include <filesystem>
#include <functional>
#include <optional>
#include <stop_token>
#include <string_view>
#include <vector>

namespace on_this_day {

struct MonthDay {
    unsigned month;
    unsigned day;
    bool operator==(const MonthDay&) const = default;
};

std::optional<MonthDay> DateInFilename(std::wstring_view stem);

using ReadPhotoDate = std::function<std::optional<MonthDay>(const std::filesystem::path&)>;

std::vector<std::filesystem::path> ScanPhotos(const std::filesystem::path& folder,
    MonthDay date, const ReadPhotoDate& readDate, std::stop_token stop = {});

class PhotoRotation {
public:
    void Reset(std::vector<std::filesystem::path> photos);
    bool Advance(const std::function<bool(const std::filesystem::path&)>& display);

private:
    std::vector<std::filesystem::path> photos_;
    std::size_t next_ = 0;
};

} // namespace on_this_day
