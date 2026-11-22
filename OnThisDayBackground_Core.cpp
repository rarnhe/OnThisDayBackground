#include "OnThisDayBackground_Core.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <utility>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace on_this_day {
namespace fs = std::filesystem;

namespace {
bool IsDigit(wchar_t character) {
    return character >= L'0' && character <= L'9';
}

bool IsImageExtension(const fs::path& path) {
    constexpr std::array extensions{L".jpg", L".jpeg", L".jpe", L".jfif", L".png",
        L".bmp", L".dib", L".gif", L".tif", L".tiff", L".heic", L".heif", L".webp",
        L".avif", L".jxr", L".wdp", L".hdp", L".dng"};
    auto extension = path.extension().wstring();
    std::ranges::transform(extension, extension.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return std::ranges::find(extensions, extension) != extensions.end();
}
} // namespace

std::optional<MonthDay> DateInFilename(std::wstring_view stem) {
    for (std::size_t i = 0; i + 5 <= stem.size(); ++i) {
        if ((i > 0 && IsDigit(stem[i - 1])) ||
            (i + 5 < stem.size() && IsDigit(stem[i + 5])) ||
            !IsDigit(stem[i]) || !IsDigit(stem[i + 1]) ||
            !IsDigit(stem[i + 3]) || !IsDigit(stem[i + 4]) ||
            (stem[i + 2] != L'-' && stem[i + 2] != L'.' && stem[i + 2] != L'_')) {
            continue;
        }
        const auto month = static_cast<unsigned>((stem[i] - L'0') * 10 + stem[i + 1] - L'0');
        const auto day = static_cast<unsigned>((stem[i + 3] - L'0') * 10 + stem[i + 4] - L'0');
        if ((std::chrono::month{month} / std::chrono::day{day}).ok()) {
            return MonthDay{month, day};
        }
    }
    return std::nullopt;
}

std::vector<fs::path> ScanPhotos(const fs::path& folder, MonthDay date,
    const PhotoDateReader& readDate, std::stop_token stop) {
    std::vector<fs::path> matches;
    std::vector<fs::path> directories{folder};
    // Walk each directory separately so a bad child cannot abort the entire scan.
    while (!directories.empty() && !stop.stop_requested()) {
        auto directory = std::move(directories.back());
        directories.pop_back();
        std::error_code error;
        fs::directory_iterator entry(directory, fs::directory_options::skip_permission_denied, error);
        const fs::directory_iterator end;
        for (; !error && entry != end && !stop.stop_requested(); entry.increment(error)) {
            std::error_code statusError;
            const auto status = entry->symlink_status(statusError);
            if (fs::is_directory(status)) {
#ifdef _WIN32
                // Junctions and other reparse-point directories can lead outside the photo tree.
                const DWORD attributes = GetFileAttributesW(entry->path().c_str());
                if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                    directories.push_back(entry->path());
                }
#else
                directories.push_back(entry->path());
#endif
            } else if (fs::is_regular_file(status) && IsImageExtension(entry->path())) {
                auto photoDate = readDate(entry->path());
                if (!photoDate) photoDate = DateInFilename(entry->path().stem().wstring());
                if (photoDate == date) matches.push_back(entry->path());
            }
        }
    }
    if (stop.stop_requested()) return {};
    std::ranges::sort(matches);
    return matches;
}

void PhotoRotation::Reset(std::vector<fs::path> photos) {
    photos_ = std::move(photos);
    next_ = 0;
}

bool PhotoRotation::Advance(const std::function<bool(const fs::path&)>& display) {
    for (std::size_t offset = 0; offset < photos_.size(); ++offset) {
        const auto index = (next_ + offset) % photos_.size();
        if (display(photos_[index])) {
            next_ = (index + 1) % photos_.size();
            return true;
        }
    }
    return false;
}

} // namespace on_this_day
