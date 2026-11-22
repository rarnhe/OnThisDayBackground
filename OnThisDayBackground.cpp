#include "OnThisDayBackground.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <utility>

#ifdef _WIN32
#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <shlobj.h>
#include <shellapi.h>
#include <servprov.h>
#include <shobjidl.h>
#include <winstring.h>
#include <wrl/client.h>

#include <cwchar>
#include <memory>
#include <string_view>
#endif

namespace on_this_day {
namespace fs = std::filesystem;

namespace {
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

std::vector<fs::path> ScanPhotos(const fs::path& folder, MonthDay date,
    const PhotoDateReader& readDate) {
    std::vector<fs::path> matches;
    std::vector<fs::path> directories{folder};
    // Walk each directory separately so a bad child cannot abort the entire scan.
    while (!directories.empty()) {
        auto directory = std::move(directories.back());
        directories.pop_back();
        std::error_code error;
        fs::directory_iterator entry(directory, fs::directory_options::skip_permission_denied, error);
        const fs::directory_iterator end;
        for (; !error && entry != end; entry.increment(error)) {
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
                if (readDate(entry->path()) == date) matches.push_back(entry->path());
            }
        }
    }
    std::ranges::sort(matches);
    return matches;
}

void PhotoRotation::Reset(std::vector<fs::path> photos, const fs::path& previous) {
    photos_ = std::move(photos);
    const auto found = std::ranges::find(photos_, previous);
    next_ = found == photos_.end() ? 0 : (found - photos_.begin() + 1) % photos_.size();
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

#ifdef _WIN32

namespace on_this_day {
using Microsoft::WRL::ComPtr;

std::optional<MonthDay> ReadDateTaken(const std::filesystem::path& photo) {
    ComPtr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreFromParsingName(photo.c_str(), nullptr, GPS_DEFAULT,
            IID_PPV_ARGS(store.GetAddressOf())))) {
        return std::nullopt;
    }

    PROPVARIANT value{};
    SYSTEMTIME utc{}, local{};
    const bool valid = SUCCEEDED(store->GetValue(PKEY_Photo_DateTaken, &value)) &&
        value.vt == VT_FILETIME && (value.filetime.dwLowDateTime || value.filetime.dwHighDateTime) &&
        FileTimeToSystemTime(&value.filetime, &utc) &&
        SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local);
    PropVariantClear(&value);
    if (!valid) return std::nullopt;
    return MonthDay{local.wMonth, local.wDay};
}

} // namespace on_this_day

namespace {
using namespace on_this_day;
namespace fs = std::filesystem;

struct CalendarDate {
    unsigned year;
    MonthDay monthDay;
};

CalendarDate Today() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return {time.wYear, {time.wMonth, time.wDay}};
}

HANDLE OpenCommandLineOutput() {
    HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(output, &mode)) return output;
    const DWORD type = GetFileType(output);
    if (type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE) return output;
    // A GUI executable can share an existing console without creating a new one.
    return AttachConsole(ATTACH_PARENT_PROCESS) ? GetStdHandle(STD_OUTPUT_HANDLE) : nullptr;
}

bool WriteOutput(HANDLE output, const std::wstring& text) {
    if (!output || output == INVALID_HANDLE_VALUE) return false;
    const auto length = static_cast<DWORD>(text.size());
    DWORD mode = 0, written = 0;
    if (GetConsoleMode(output, &mode)) {
        return WriteConsoleW(output, text.data(), length, &written, nullptr) && written == length;
    }
    const int utf8Size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(length),
        nullptr, 0, nullptr, nullptr);
    if (utf8Size <= 0) return false;
    std::string utf8(utf8Size, '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(length),
            utf8.data(), utf8Size, nullptr, nullptr)) return false;
    return WriteFile(output, utf8.data(), static_cast<DWORD>(utf8Size), &written, nullptr) &&
        written == static_cast<DWORD>(utf8Size);
}

bool PrintMatches(HANDLE output, CalendarDate date, const std::vector<fs::path>& photos) {
    wchar_t day[16]{};
    swprintf_s(day, L"%02u-%02u", date.monthDay.month, date.monthDay.day);
    if (!WriteOutput(output, L"System.Photo.DateTaken " + std::wstring(day) + L": " +
            std::to_wstring(photos.size()) + L" matching photo(s)\r\n")) return false;
    for (const auto& photo : photos) {
        if (!WriteOutput(output, photo.native() + L"\r\n")) return false;
    }
    return true;
}

fs::path ResolveFolder(const wchar_t* argument) {
    if (!argument) {
        PWSTR pictures = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DONT_VERIFY,
            nullptr, &pictures);
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(pictures, &CoTaskMemFree);
        if (SUCCEEDED(result) && path) return path.get();
        return {};
    }

    std::error_code error;
    const auto folder = fs::absolute(argument, error);
    if (!error && fs::is_directory(folder, error)) return folder;
    return {};
}

constexpr wchar_t kDesktopKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";
constexpr wchar_t kSettingsKey[] = L"Software\\OnThisDayBackground";
constexpr wchar_t kLastSelection[] = L"LastSelection";
constexpr GUID kImmersiveShell{0xC2F03A33, 0x21F5, 0x47FA,
    {0xB4, 0xBB, 0x15, 0x63, 0x62, 0xA2, 0xF2, 0x39}};
constexpr GUID kDesktopManagerService{0xC5E0CDCA, 0x7B6E, 0x41B2,
    {0x9F, 0xC4, 0xD9, 0x39, 0x75, 0xCC, 0x46, 0x7B}};
constexpr GUID kManager24H2{0x53F5CA0B, 0x158F, 0x4124,
    {0x90, 0x0C, 0x05, 0x71, 0x58, 0x06, 0x0B, 0x27}};
constexpr GUID kManager23H2{0x4970BA3D, 0xFD4E, 0x4647,
    {0xBE, 0xA3, 0xD8, 0x90, 0x76, 0xEF, 0x4B, 0x9C}};
constexpr GUID kManager22H2{0xA3175F2D, 0x239C, 0x4BD2,
    {0x8A, 0xA0, 0xEE, 0xBA, 0x8B, 0x0B, 0x13, 0x8E}};

// Explorer's private ABI changes across releases. Unused declarations preserve
// the slots before SetDesktopWallpaper and are never called.
struct DesktopManager : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop() = 0;
};

struct DesktopManager24H2 : DesktopManager {
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktopAndMoveForegroundView() = 0;
};

template<class Base>
struct DesktopWallpaperManager : Base {
    virtual HRESULT STDMETHODCALLTYPE CreateDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE RemoveDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE FindDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopSwitchIncludeExcludeViews() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopName() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopWallpaper(IUnknown*, HSTRING) = 0;
};

using ApplyWallpaper = std::function<HRESULT(const fs::path&)>;

DWORD WindowsBuild() {
    using QueryVersion = LONG (WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    const auto query = reinterpret_cast<QueryVersion>(GetProcAddress(ntdll, "RtlGetVersion"));
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    return query && query(&info) == 0 ? info.dwBuildNumber : 0;
}

template<class Manager>
HRESULT GetVirtualDesktopWallpaper(IServiceProvider& shell, REFIID iid, UINT& count, ApplyWallpaper& apply) {
    ComPtr<Manager> manager;
    HRESULT status = shell.QueryService(kDesktopManagerService, iid,
        reinterpret_cast<void**>(manager.GetAddressOf()));
    if (FAILED(status)) return status;
    status = manager->GetCount(&count);
    if (FAILED(status) || count <= 1) return status;

    // Query the current desktop at the moment of application, after scanning.
    apply = [manager](const fs::path& photo) {
        ComPtr<IUnknown> desktop;
        const HRESULT status = manager->GetCurrentDesktop(desktop.GetAddressOf());
        if (FAILED(status) || !desktop) return FAILED(status) ? status : E_UNEXPECTED;
        HSTRING_HEADER storage{};
        HSTRING path = nullptr;
        const HRESULT result = WindowsCreateStringReference(photo.c_str(),
            static_cast<UINT32>(photo.native().size()), &storage, &path);
        return SUCCEEDED(result) ? manager->SetDesktopWallpaper(desktop.Get(), path) : result;
    };
    return S_OK;
}

HRESULT GetDesktopConfiguration(UINT& count, ApplyWallpaper& apply) {
    const DWORD build = WindowsBuild();
    if (build != 26100 && build != 26200 && build != 22621 && build != 22631) {
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    ComPtr<IServiceProvider> shell;
    const HRESULT status = CoCreateInstance(kImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER,
        IID_PPV_ARGS(shell.GetAddressOf()));
    if (FAILED(status)) return status;
    if (build == 26100 || build == 26200) {
        return GetVirtualDesktopWallpaper<DesktopWallpaperManager<DesktopManager24H2>>(
            *shell.Get(), kManager24H2, count, apply);
    }

    HRESULT result = GetVirtualDesktopWallpaper<DesktopWallpaperManager<DesktopManager>>(
        *shell.Get(), kManager23H2, count, apply);
    if (result == E_NOINTERFACE) {
        result = GetVirtualDesktopWallpaper<DesktopWallpaperManager<DesktopManager>>(
            *shell.Get(), kManager22H2, count, apply);
    }
    if (result == E_NOINTERFACE) {
        // Updated 23H2 also used this IID, before the extra 24H2 slot was added.
        result = GetVirtualDesktopWallpaper<DesktopWallpaperManager<DesktopManager>>(
            *shell.Get(), kManager24H2, count, apply);
    }
    return result;
}

bool IsSingleDesktopInRegistry() {
    DWORD bytes = 0;
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kDesktopKey, L"VirtualDesktopIDs",
        RRF_RT_REG_BINARY, nullptr, nullptr, &bytes);
    if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND) return true;
    return status == ERROR_SUCCESS && (bytes == 0 || bytes == sizeof(GUID));
}

HRESULT GetPrimaryMonitorWallpaper(ApplyWallpaper& apply) {
    MONITORINFO primary{sizeof(primary)};
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary) ||
        !(primary.dwFlags & MONITORINFOF_PRIMARY)) {
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    }

    ComPtr<IDesktopWallpaper> wallpaper;
    HRESULT status = CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(wallpaper.GetAddressOf()));
    if (FAILED(status)) return status;
    UINT count = 0;
    status = wallpaper->GetMonitorDevicePathCount(&count);
    if (FAILED(status)) return status;

    for (UINT index = 0; index < count; ++index) {
        PWSTR path = nullptr;
        status = wallpaper->GetMonitorDevicePathAt(index, &path);
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> monitor(path, &CoTaskMemFree);
        RECT bounds{};
        if (SUCCEEDED(status) && monitor && wallpaper->GetMonitorRECT(monitor.get(), &bounds) == S_OK &&
            EqualRect(&bounds, &primary.rcMonitor)) {
            apply = [wallpaper, id = std::wstring(monitor.get())](const fs::path& photo) {
                return wallpaper->SetWallpaper(id.c_str(), photo.c_str());
            };
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
}

std::wstring DatePrefix(CalendarDate date) {
    wchar_t prefix[24]{};
    swprintf_s(prefix, L"%04u-%02u-%02u\n", date.year, date.monthDay.month, date.monthDay.day);
    return prefix;
}

fs::path ReadLastSelection(CalendarDate date) {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kLastSelection, RRF_RT_REG_SZ,
            nullptr, nullptr, &bytes) != ERROR_SUCCESS || bytes % sizeof(wchar_t) != 0) return {};
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, kLastSelection, RRF_RT_REG_SZ,
            nullptr, value.data(), &bytes) != ERROR_SUCCESS || bytes < sizeof(wchar_t)) return {};
    value.resize(bytes / sizeof(wchar_t) - 1);
    const auto prefix = DatePrefix(date);
    if (!value.starts_with(prefix)) return {};
    return value.substr(prefix.size());
}

std::wstring DescribeError(HRESULT status) {
    wchar_t value[24]{};
    swprintf_s(value, L"0x%08lX", static_cast<unsigned long>(status));
    return value;
}

int ReportFailure(HANDLE output, const std::wstring& message, bool list) {
    if (!WriteOutput(output, message + L"\r\n") && !list) {
        MessageBoxW(nullptr, message.c_str(), L"OnThisDayBackground", MB_OK | MB_ICONERROR);
    }
    return 1;
}

int ApplyNextWallpaper(HANDLE output, CalendarDate date, std::vector<fs::path> photos) {
    if (photos.empty()) return 0;
    const std::unique_ptr<void, decltype(&CloseHandle)> mutex(
        CreateMutexW(nullptr, FALSE, L"Local\\OnThisDayBackground.Wallpaper"), &CloseHandle);
    const DWORD wait = mutex ? WaitForSingleObject(mutex.get(), 5000) : WAIT_FAILED;
    if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
        return ReportFailure(output, L"Could not acquire the wallpaper update lock.", false);
    }
    // Closing a mutex handle does not release ownership until the thread exits.
    const std::unique_ptr<void, decltype(&ReleaseMutex)> lock(mutex.get(), &ReleaseMutex);

    ApplyWallpaper apply;
    UINT desktops = 0;
    const HRESULT desktopStatus = GetDesktopConfiguration(desktops, apply);
    if (!apply && (desktops > 1 || ((FAILED(desktopStatus) || desktops == 0) && !IsSingleDesktopInRegistry()))) {
        return ReportFailure(output, L"Cannot target the current virtual desktop on this Windows build (" +
            DescribeError(FAILED(desktopStatus) ? desktopStatus : E_UNEXPECTED) + L").", false);
    }

    const bool virtualDesktop = static_cast<bool>(apply);
    if (!apply) {
        const HRESULT status = GetPrimaryMonitorWallpaper(apply);
        if (FAILED(status)) {
            return ReportFailure(output, L"Cannot find the primary monitor's wallpaper target (" +
                DescribeError(status) + L").", false);
        }
    }

    PhotoRotation rotation;
    rotation.Reset(std::move(photos), ReadLastSelection(date));
    const bool updated = rotation.Advance([&](const fs::path& photo) {
        const HRESULT status = apply(photo);
        if (FAILED(status)) {
            WriteOutput(output, L"Could not apply " + photo.native() + L" (" + DescribeError(status) + L").\r\n");
            return false;
        }
        const auto selection = DatePrefix(date) + photo.native();
        const auto saved = RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, kLastSelection, REG_SZ,
            selection.c_str(), static_cast<DWORD>((selection.size() + 1) * sizeof(wchar_t)));
        std::wstring message = virtualDesktop ? L"Updated current virtual desktop: " : L"Updated primary monitor: ";
        WriteOutput(output, message + photo.native() + L"\r\n");
        if (saved != ERROR_SUCCESS) {
            WriteOutput(output, L"Could not save the next rotation position.\r\n");
        }
        return true;
    });
    return updated ? 0 : ReportFailure(output, L"Windows could not apply any matching photo.", false);
}
} // namespace

#ifdef _MSC_VER
_Use_decl_annotations_
#endif
int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> argv(
        CommandLineToArgvW(GetCommandLineW(), &argc), &LocalFree);
    if (!argv) return 1;

    const HANDLE output = OpenCommandLineOutput();
    const bool list = argc > 1 && std::wstring_view(argv.get()[1]) == L"--list";
    const int folderIndex = list ? 2 : 1;
    const auto folder = argc <= folderIndex + 1 ?
        ResolveFolder(argc > folderIndex ? argv.get()[folderIndex] : nullptr) : fs::path{};
    if (folder.empty()) {
        return ReportFailure(output, L"Usage: OnThisDayBackground.exe [--list] [photo folder]", list);
    }

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        return ReportFailure(output, L"Could not initialize the Windows shell interfaces.", list);
    }
    if (!list) SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int result = 1;
    try {
        const auto date = Today();
        auto photos = ScanPhotos(folder, date.monthDay, ReadDateTaken);
        const bool printed = output && PrintMatches(output, date, photos);
        result = list ? (printed ? 0 : 1) : ApplyNextWallpaper(output, date, std::move(photos));
    } catch (const std::exception&) {
        result = ReportFailure(output, L"Could not scan or apply photos from the requested folder.", list);
    }
    CoUninitialize();
    return result;
}

#endif // _WIN32
