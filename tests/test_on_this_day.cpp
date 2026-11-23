#include "OnThisDayBackground.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace on_this_day;
namespace fs = std::filesystem;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int main() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto folder = fs::temp_directory_path() / ("on-this-day-core-" + std::to_string(stamp));
    int status = 0;
    try {
        fs::create_directories(folder / "a" / "nested");
        for (const auto* path : {"z.JPG", "a/nested/metadata.png", "conflict-11-22.jpg",
                "a/2019-11-22-b.jpg", "file-11-22.txt", "a/111-22.jpeg"}) {
            std::ofstream(folder / path).put('x');
        }
        std::error_code ignored;
        fs::create_directory_symlink(folder, folder / "a" / "loop", ignored);
        const auto readDate = [](const fs::path& photo) -> std::optional<MonthDay> {
            if (photo.stem() == "metadata" || photo.stem() == "z" ||
                photo.stem() == "file-11-22") return MonthDay{11, 22};
            if (photo.stem() == "conflict-11-22") return MonthDay{11, 21};
            return std::nullopt;
        };
        const std::vector expected{folder / "a/nested/metadata.png", folder / "z.JPG"};
        Check(ScanPhotos(folder, {11, 22}, readDate) == expected,
            "recursive scan, sort, image filter or capture-date-only matching");
        Check(ScanPhotos(folder, {12, 1}, readDate).empty(), "no matches");
        int reads = 0;
        const auto fullReader = [&](const fs::path& photo) -> std::optional<MonthDay> {
            ++reads;
            if (photo.stem() == "metadata") return MonthDay{11, 22};
            if (photo.stem() == "z") return MonthDay{11, 23};
            if (photo.stem() == "conflict-11-22") return MonthDay{11, 22};
            return std::nullopt;
        };
        DailyPhotoCache daily;
        const auto indexed = ScanDatedPhotos(folder, fullReader);
        Check(indexed.size() == 3, "A must index all dated photos, including other days");
        Check(indexed[0].path == folder / "a/nested/metadata.png", "A path order");
        Check(indexed[0].date == MonthDay{11, 22}, "A keeps month/day");
        daily.Reset(indexed);
        Check(daily.IndexedCount() == 3 && daily.DayCacheCount() == 0, "B is lazy");
        const auto afterScanReads = reads;
        auto& firstDay = daily.ForDay({11, 22});
        Check(daily.DayCacheCount() == 1, "create first B");
        std::vector<fs::path> daysShown;
        const auto record = [&](const fs::path& path) { daysShown.push_back(path); return true; };
        Check(firstDay.Advance(record) && firstDay.Advance(record), "rotate first date");
        Check(daysShown == std::vector<fs::path>{folder / "a/nested/metadata.png",
            folder / "conflict-11-22.jpg"}, "first date matches in sorted order");
        auto& tomorrow = daily.ForDay({11, 23});
        Check(daily.DayCacheCount() == 2 && tomorrow.Advance(record) &&
            daysShown.back() == folder / "z.JPG", "cross-day B created from A");
        Check(&daily.ForDay({11, 22}) == &firstDay, "same B object reused");
        Check(firstDay.Advance(record) &&
            daysShown.back() == folder / "a/nested/metadata.png", "B keeps rotation across other day");
        Check(reads == afterScanReads, "refresh must not reread disk metadata");
        Check(daily.ForDay({12, 1}).Empty() && daily.DayCacheCount() == 3,
            "empty B is cached too");
        daily.Reset(indexed);
        Check(daily.DayCacheCount() == 0 && daily.IndexedCount() == 3, "new scan invalidates Bs");
        PhotoRotation rotation;
        rotation.Reset(expected);
        std::vector<fs::path> shown;
        const auto show = [&shown](const fs::path& photo) { shown.push_back(photo); return true; };
        Check(!rotation.Advance([](const fs::path&) { return false; }), "failed display");
        Check(rotation.Advance(show) && rotation.Advance(show) && rotation.Advance(show), "advance");
        Check(shown == std::vector{expected[0], expected[1], expected[0]}, "wraparound");
        rotation.Reset(expected);
        Check(rotation.Advance([&](const fs::path& photo) { return photo == expected[1]; }),
            "skip missing or undecodable photo");
        Check(rotation.Advance(show) && shown.back() == expected[0], "advance after skipping");
        rotation.Reset(expected);
        Check(!rotation.Empty() && rotation.Advance(show) && shown.back() == expected[0],
            "fresh resident starts at first photo");
        rotation.Reset({});
        Check(rotation.Empty(), "empty cache");
        Check(!rotation.Advance(show), "empty rotation");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    return status;
}
