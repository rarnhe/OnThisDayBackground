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
#include <wrl/implements.h>
#include <uiautomation.h>

#include <cwchar>
#include <memory>
#include <string_view>
#include <type_traits>
#include <mutex>
#include <cstdint>
#include <stop_token>
#include <thread>
#include <cwchar>
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

#ifdef _WIN32

namespace on_this_day {
using Microsoft::WRL::ComPtr;

bool IsExplorerRefreshLabel(std::wstring_view label, std::wstring_view localized) {
    const auto normalize = [](std::wstring_view text) {
        std::wstring name(text.substr(0, text.find(L'\t')));
        std::erase(name, L'&');
        if (name.size() >= 3 && name[name.size() - 3] == L'(' && name.back() == L')') {
            name.resize(name.size() - 3);
        }
        while (!name.empty() && name.back() == L' ') name.pop_back();
        return name;
    };
    const auto name = normalize(label);
    return name == L"\u5237\u65b0" || name == L"\u91cd\u65b0\u6574\u7406" ||
        CompareStringOrdinal(name.c_str(), -1, L"Refresh", -1, TRUE) == CSTR_EQUAL ||
        (!localized.empty() && name == normalize(localized));
}

bool IsRefreshMenuClick(HWND menu, RECT bounds, HWND pressedWindow, POINT pressedAt, POINT releasedAt) {
    return menu && pressedWindow == menu && PtInRect(&bounds, pressedAt) && PtInRect(&bounds, releasedAt);
}

DesktopInterface DesktopInterfaceForBuild(unsigned long build) {
    if (build >= 26100) return DesktopInterface::windows11Modern;
    if (build == 22621 || build == 22631) return DesktopInterface::windows11Before24H2;
    if (build == 22000) return DesktopInterface::windows11Initial;
    return DesktopInterface::unavailable;
}

bool IsCurrentDesktopGuid(const GUID& shell, const std::optional<GUID>& registered,
    const std::vector<GUID>& desktops) {
    return !IsEqualGUID(shell, GUID_NULL) && (!registered || IsEqualGUID(shell, *registered)) &&
        std::ranges::count_if(desktops, [&](const GUID& id) { return IsEqualGUID(shell, id); }) == 1;
}

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

MonthDay Today() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return {time.wMonth, time.wDay};
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

bool PrintMatches(HANDLE output, MonthDay date, const std::vector<fs::path>& photos) {
    wchar_t day[16]{};
    swprintf_s(day, L"%02u-%02u", date.month, date.day);
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
    if (!error && fs::is_directory(folder, error)) return folder.lexically_normal();
    return {};
}

constexpr wchar_t kDesktopKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";
constexpr wchar_t kQueryServiceOperation[] = L"QueryService";
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

constexpr GUID kManager21H2{0xB2F925B9, 0x5A0F, 0x4D2E,
    {0x9F, 0x4D, 0x2B, 0x15, 0x07, 0x59, 0x3C, 0x10}};

constexpr GUID kDesktopIdentity{0x3F07F4BE, 0xB107, 0x441A,
    {0xAF, 0x0F, 0x39, 0xD8, 0x25, 0x29, 0x07, 0x2C}};
constexpr GUID kDesktopIdentity21H2{0x536D3495, 0xB208, 0x4CC9,
    {0xAE, 0x26, 0xDE, 0x81, 0x11, 0x27, 0x5B, 0xF8}};

struct DesktopIdentity : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE IsViewVisible() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetId(GUID*) = 0;
};

struct DesktopWithWallpaper : DesktopIdentity {
    virtual HRESULT STDMETHODCALLTYPE GetName() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(HSTRING*) = 0;
};

struct DesktopWithWallpaper21H2 : DesktopIdentity {
    virtual HRESULT STDMETHODCALLTYPE IsRemote() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetName() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetWallpaperPath(HSTRING*) = 0;
};

// Explorer's private ABI changes across releases. Unused declarations preserve
// the slots before SetDesktopWallpaper and are never called.
struct DesktopManager : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCount(UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(DesktopIdentity**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(IObjectArray**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetAdjacentDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE SwitchDesktop() = 0;
};

// Build 22000 includes a monitor argument and does not have
// GetAllCurrentDesktops, which appeared in a later preview build.
struct DesktopManager21H2 : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetCount(HMONITOR, UINT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE MoveViewToDesktop() = 0;
    virtual HRESULT STDMETHODCALLTYPE CanViewMoveDesktops() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetCurrentDesktop(HMONITOR, DesktopIdentity**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktops(HMONITOR, IObjectArray**) = 0;
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
    virtual HRESULT STDMETHODCALLTYPE FindDesktop(const GUID*, DesktopIdentity**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDesktopSwitchIncludeExcludeViews() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopName() = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDesktopWallpaper(DesktopIdentity*, HSTRING) = 0;
};

struct ApplyResult {
    HRESULT status;
    const wchar_t* operation;
    bool stop = false;
};

using ApplyWallpaper = std::function<ApplyResult(const fs::path&)>;

DWORD WindowsBuild() {
    using QueryVersion = LONG (WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    const auto query = reinterpret_cast<QueryVersion>(GetProcAddress(ntdll, "RtlGetVersion"));
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    return query && query(&info) == 0 ? info.dwBuildNumber : 0;
}

std::optional<GUID> RegisteredCurrentDesktop() {
    const auto read = [](const wchar_t* key) -> std::optional<GUID> {
        GUID id{};
        DWORD bytes = sizeof(id);
        if (RegGetValueW(HKEY_CURRENT_USER, key, L"CurrentVirtualDesktop", RRF_RT_REG_BINARY,
                nullptr, &id, &bytes) == ERROR_SUCCESS && bytes == sizeof(id) &&
            !IsEqualGUID(id, GUID_NULL)) return id;
        return std::nullopt;
    };
    if (const auto current = read(kDesktopKey)) return current;
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return std::nullopt;
    const auto key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" +
        std::to_wstring(session) + L"\\VirtualDesktops";
    return read(key.c_str());
}

template<class Base>
HRESULT CurrentDesktopId(DesktopWallpaperManager<Base>& manager, GUID& id) {
    ComPtr<DesktopIdentity> current;
    HRESULT status;
    if constexpr (std::is_same_v<Base, DesktopManager21H2>) {
        status = manager.GetCurrentDesktop(nullptr, current.GetAddressOf());
    } else {
        status = manager.GetCurrentDesktop(current.GetAddressOf());
    }
    if (FAILED(status)) return status;
    return current ? current->GetId(&id) : E_UNEXPECTED;
}

template<class Desktop>
HRESULT ReadDesktopWallpaper(Desktop& desktop, std::wstring& value) {
    HSTRING path = nullptr;
    const HRESULT status = desktop.GetWallpaperPath(&path);
    if (SUCCEEDED(status)) value = WindowsGetStringRawBuffer(path, nullptr);
    WindowsDeleteString(path);
    return status;
}

template<class Base>
HRESULT GetVirtualDesktopWallpaper(IServiceProvider& shell, REFIID iid, std::optional<DWORD> registeredCount,
    UINT& count, ApplyWallpaper& apply, const wchar_t*& operation, GUID& targetId) {
    constexpr bool initial = std::is_same_v<Base, DesktopManager21H2>;
    using Desktop = std::conditional_t<initial, DesktopWithWallpaper21H2, DesktopWithWallpaper>;
    const auto& desktopIid = initial ? kDesktopIdentity21H2 : kDesktopIdentity;
    ComPtr<DesktopWallpaperManager<Base>> manager;
    operation = kQueryServiceOperation;
    HRESULT status = shell.QueryService(kDesktopManagerService, iid,
        reinterpret_cast<void**>(manager.GetAddressOf()));
    if (FAILED(status)) return status;
    if (!manager) return E_UNEXPECTED;

    ComPtr<IObjectArray> desktops;
    operation = L"GetDesktops";
    if constexpr (initial) {
        status = manager->GetDesktops(nullptr, desktops.GetAddressOf());
    } else {
        status = manager->GetDesktops(desktops.GetAddressOf());
    }
    if (FAILED(status)) return status;
    if (!desktops) return E_UNEXPECTED;
    status = desktops->GetCount(&count);
    if (FAILED(status)) return status;
    if (count == 0) return E_UNEXPECTED;
    if (count == 1) {
        return registeredCount && *registeredCount > 1 ? HRESULT_FROM_WIN32(ERROR_RETRY) : S_OK;
    }

    std::vector<GUID> ids;
    std::vector<ComPtr<Desktop>> entries;
    for (UINT index = 0; index < count; ++index) {
        operation = L"GetDesktops/GetId";
        ComPtr<Desktop> item;
        status = desktops->GetAt(index, desktopIid, reinterpret_cast<void**>(item.GetAddressOf()));
        if (FAILED(status)) return status;
        if (!item) return E_UNEXPECTED;
        GUID id{};
        status = item->GetId(&id);
        if (FAILED(status)) return status;
        ids.push_back(id);
        entries.push_back(std::move(item));
    }

    operation = L"GetCurrentDesktop/GetId";
    GUID current{};
    status = CurrentDesktopId(*manager.Get(), current);
    if (FAILED(status)) return status;
    if (!IsCurrentDesktopGuid(current, RegisteredCurrentDesktop(), ids)) {
        return HRESULT_FROM_WIN32(ERROR_RETRY);
    }

    operation = L"FindDesktop by GUID";
    ComPtr<DesktopIdentity> target;
    status = manager->FindDesktop(&current, target.GetAddressOf());
    if (FAILED(status)) return status;
    if (!target) return E_UNEXPECTED;
    GUID resolved{};
    status = target->GetId(&resolved);
    if (FAILED(status)) return status;
    if (!IsEqualGUID(resolved, current)) return E_UNEXPECTED;

    // A Shell-wide side effect must not be counted as a successful rotation.
    const auto targetIndex = static_cast<std::size_t>(std::ranges::find_if(ids, [&](const GUID& id) {
        return IsEqualGUID(id, current);
    }) - ids.begin());
    apply = [manager, target, current, targetIndex, entries = std::move(entries)](const fs::path& photo)
        -> ApplyResult {
        std::vector<std::wstring> before(entries.size());
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i == targetIndex) continue;
            const HRESULT snapshot = ReadDesktopWallpaper(*entries[i].Get(), before[i]);
            if (FAILED(snapshot)) return {snapshot, L"Read other desktop wallpaper", true};
        }

        GUID active{};
        const auto now = CurrentDesktopId(*manager.Get(), active);
        const auto registered = RegisteredCurrentDesktop();
        if (FAILED(now) || !IsEqualGUID(active, current) ||
            (registered && !IsEqualGUID(*registered, current))) {
            return {FAILED(now) ? now : HRESULT_FROM_WIN32(ERROR_RETRY), L"Recheck current desktop GUID", true};
        }

        HSTRING path = nullptr;
        const HRESULT created = WindowsCreateString(photo.c_str(),
            static_cast<UINT32>(photo.native().size()), &path);
        if (FAILED(created)) return {created, L"WindowsCreateString"};
        const HRESULT result = manager->SetDesktopWallpaper(target.Get(), path);
        WindowsDeleteString(path);

        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i == targetIndex) continue;
            std::wstring after;
            const HRESULT checked = ReadDesktopWallpaper(*entries[i].Get(), after);
            if (FAILED(checked)) return {checked, L"Verify other desktop wallpaper", true};
            if (after != before[i]) {
                return {E_UNEXPECTED, L"Explorer changed another desktop's wallpaper", true};
            }
        }
        if (FAILED(result)) return {result, L"SetDesktopWallpaper"};
        std::wstring applied;
        const HRESULT checked = ReadDesktopWallpaper(*entries[targetIndex].Get(), applied);
        if (FAILED(checked)) return {checked, L"Verify target desktop wallpaper", true};
        if (CompareStringOrdinal(applied.c_str(), -1, photo.c_str(), -1, TRUE) != CSTR_EQUAL) {
            return {E_UNEXPECTED, L"Target desktop did not retain the requested wallpaper", true};
        }
        return {result, L"SetDesktopWallpaper"};
    };
    targetId = current;
    return S_OK;
}

HRESULT GetDesktopConfiguration(DWORD build, std::optional<DWORD> registeredCount,
    UINT& count, ApplyWallpaper& apply, const wchar_t*& operation, GUID& targetId) {
    const auto family = DesktopInterfaceForBuild(build);
    if (family == DesktopInterface::unavailable) {
        operation = L"Select virtual desktop interface";
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    ComPtr<IServiceProvider> shell;
    operation = L"CoCreateInstance(ImmersiveShell)";
    const HRESULT status = CoCreateInstance(kImmersiveShell, nullptr, CLSCTX_LOCAL_SERVER,
        IID_PPV_ARGS(shell.GetAddressOf()));
    if (FAILED(status)) return status;

    if (family == DesktopInterface::windows11Modern) {
        // New builds can reuse this ABI. Require the matching IID and working
        // read-only operations before taking the wallpaper setter path.
        return GetVirtualDesktopWallpaper<DesktopManager24H2>(*shell.Get(),
            kManager24H2, registeredCount, count, apply, operation, targetId);
    }
    if (family == DesktopInterface::windows11Initial) {
        return GetVirtualDesktopWallpaper<DesktopManager21H2>(*shell.Get(),
            kManager21H2, registeredCount, count, apply, operation, targetId);
    }

    HRESULT result = GetVirtualDesktopWallpaper<DesktopManager>(*shell.Get(),
        kManager23H2, registeredCount, count, apply, operation, targetId);
    if (result == E_NOINTERFACE && operation == kQueryServiceOperation) {
        result = GetVirtualDesktopWallpaper<DesktopManager>(*shell.Get(),
            kManager22H2, registeredCount, count, apply, operation, targetId);
    }
    if (result == E_NOINTERFACE && operation == kQueryServiceOperation) {
        // Updated 23H2 also used this IID, before the extra 24H2 slot was added.
        result = GetVirtualDesktopWallpaper<DesktopManager>(*shell.Get(),
            kManager24H2, registeredCount, count, apply, operation, targetId);
    }
    return result;
}

std::optional<DWORD> RegisteredDesktopCount() {
    const auto read = [](const wchar_t* key) -> std::optional<DWORD> {
        DWORD bytes = 0;
        const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, key, L"VirtualDesktopIDs",
            RRF_RT_REG_BINARY, nullptr, nullptr, &bytes);
        if (status != ERROR_SUCCESS || bytes == 0 || bytes % sizeof(GUID) != 0) return std::nullopt;
        return static_cast<DWORD>(bytes / sizeof(GUID));
    };
    const auto global = read(kDesktopKey);
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return global;
    const auto key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" +
        std::to_wstring(session) + L"\\VirtualDesktops";
    const auto local = read(key.c_str());
    if (global && local) return std::max(*global, *local);
    return global ? global : local;
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
            apply = [wallpaper, id = std::wstring(monitor.get())](const fs::path& photo) -> ApplyResult {
                return {wallpaper->SetWallpaper(id.c_str(), photo.c_str()), L"IDesktopWallpaper::SetWallpaper"};
            };
            return S_OK;
        }
    }
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
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

int ApplyNextWallpaper(PhotoRotation& rotation) {
    if (rotation.Empty()) return 0;
    const HANDLE output = nullptr;

    ApplyWallpaper apply;
    UINT desktops = 0;
    const DWORD build = WindowsBuild();
    const wchar_t* operation = L"";
    const auto registeredDesktops = RegisteredDesktopCount();
    GUID targetId{};
    const HRESULT desktopStatus = GetDesktopConfiguration(
        build, registeredDesktops, desktops, apply, operation, targetId);
    const bool multipleDesktops = desktops > 1 || (registeredDesktops && *registeredDesktops > 1);
    const bool unknownScope = desktops == 0 && !registeredDesktops &&
        (build == 0 || build >= 22000);
    if (!apply && (multipleDesktops || unknownScope)) {
        return ReportFailure(output, L"Cannot target the current virtual desktop. Windows build " +
            std::to_wstring(build) + L", " + operation + L" (" +
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

    wchar_t targetLabel[39]{};
    if (virtualDesktop) StringFromGUID2(targetId, targetLabel, ARRAYSIZE(targetLabel));
    ApplyResult lastFailure{E_FAIL, L"Apply wallpaper"};
    const bool updated = rotation.Advance([&](const fs::path& photo) {
        if (lastFailure.stop) return false;
        const auto result = apply(photo);
        if (FAILED(result.status)) {
            lastFailure = result;
            const auto diagnostic = std::wstring(result.operation) + L" could not apply " + photo.native() +
                L" (Windows build " + std::to_wstring(build) + L", " + DescribeError(result.status) + L").\r\n";
            OutputDebugStringW(diagnostic.c_str());
            return false;
        }
        const auto message = virtualDesktop ?
            L"Updated current virtual desktop " + std::wstring(targetLabel) + L": " :
            L"Updated primary monitor: ";
        OutputDebugStringW((message + photo.native() + L"\r\n").c_str());
        return true;
    });
    if (updated) return 0;
    return ReportFailure(output, L"Windows could not apply any matching photo. Windows build " +
        std::to_wstring(build) + L", " + lastFailure.operation + L" (" +
        DescribeError(lastFailure.status) + L").", false);
}
constexpr wchar_t kResidentClass[] = L"OnThisDayBackground.Resident";
constexpr wchar_t kResidentMutex[] = L"Local\\OnThisDayBackground.Resident";
constexpr UINT kNextWallpaper = WM_APP + 1;
constexpr UINT kDesktopRefreshInvoked = WM_APP + 2;

std::wstring LocalizedRefreshLabel() {
    HMODULE shell = LoadLibraryExW(L"shell32.dll", nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!shell) return {};
    wchar_t label[128]{};
    if (HMENU menu = LoadMenuW(shell, MAKEINTRESOURCEW(215))) {
        GetMenuStringW(menu, 28931, label, ARRAYSIZE(label), MF_BYCOMMAND);
        DestroyMenu(menu);
    }
    FreeLibrary(shell);
    return label;
}

class RefreshEventHandler final : public Microsoft::WRL::RuntimeClass<
    Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IUIAutomationEventHandler> {
public:
    explicit RefreshEventHandler(HWND receiver) : receiver_(receiver), localized_(LocalizedRefreshLabel()) {}

    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement* sender, EVENTID event) override {
        if (!sender || event != UIA_Invoke_InvokedEventId) return S_OK;
        CONTROLTYPEID type = 0;
        int pid = 0;
        if (FAILED(sender->get_CachedControlType(&type)) || type != UIA_MenuItemControlTypeId ||
            FAILED(sender->get_CachedProcessId(&pid)) || pid <= 0) return S_OK;
        BSTR name = nullptr;
        if (SUCCEEDED(sender->get_CachedName(&name)) && name) {
            try {
                if (IsExplorerRefreshLabel({name, SysStringLen(name)}, localized_)) {
                    PostMessageW(receiver_, kDesktopRefreshInvoked, static_cast<WPARAM>(pid), 0);
                }
            } catch (const std::exception&) {
                SysFreeString(name);
                return E_FAIL;
            }
        }
        SysFreeString(name);
        return S_OK;
    }

private:
    HWND receiver_;
    std::wstring localized_;
};

struct RefreshMenuHit {
    HWND window;
    RECT bounds;
};

struct DesktopRefreshInput {
    HWND receiver = nullptr;
    HANDLE wake = nullptr;
    std::mutex mutex;
    DWORD explorerPid = 0;
    ULONGLONG menuStarted = 0;
    POINT origin{};
    HWND pressedWindow = nullptr;
    POINT pressedAt{};
    std::optional<RefreshMenuHit> hit;
};

HWND RootAtPoint(POINT point) {
    return GetAncestor(WindowFromPoint(point), GA_ROOT);
}

DWORD WindowProcess(HWND window) {
    DWORD pid = 0;
    if (window) GetWindowThreadProcessId(window, &pid);
    return pid;
}

DWORD ExplorerDesktopAt(POINT point) {
    const HWND root = RootAtPoint(point);
    const HWND shell = GetShellWindow();
    const DWORD pid = WindowProcess(root);
    return pid && pid == WindowProcess(shell) && (root == shell ||
        FindWindowExW(root, nullptr, L"SHELLDLL_DefView", nullptr)) ? pid : 0;
}

std::optional<RECT> ClassicRefreshBounds(HWND window, std::wstring_view localized) {
    wchar_t className[32]{};
    GetClassNameW(window, className, ARRAYSIZE(className));
    if (std::wstring_view(className) != L"#32768") return std::nullopt;

    DWORD_PTR result = 0;
    constexpr UINT getMenuHandle = 0x01E1; // MN_GETHMENU
    if (!SendMessageTimeoutW(window, getMenuHandle, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 80, &result) ||
        !result) return std::nullopt;
    const auto menu = reinterpret_cast<HMENU>(result);
    const int count = GetMenuItemCount(menu);
    for (int index = 0; index < count; ++index) {
        const auto state = GetMenuState(menu, index, MF_BYPOSITION);
        if (state == static_cast<UINT>(-1) || (state & (MF_GRAYED | MF_DISABLED | MF_POPUP))) continue;
        wchar_t label[128]{};
        RECT bounds{};
        if (GetMenuStringW(menu, index, label, ARRAYSIZE(label), MF_BYPOSITION) &&
            IsExplorerRefreshLabel(label, localized) && GetMenuItemRect(window, menu, index, &bounds) &&
            !IsRectEmpty(&bounds)) return bounds;
    }
    return std::nullopt;
}

std::optional<RECT> AutomationRefreshItem(IUIAutomationElement& item, DWORD explorerPid,
    std::wstring_view localized) {
    int pid = 0;
    BOOL enabled = FALSE;
    RECT bounds{};
    BSTR label = nullptr;
    if (FAILED(item.get_CurrentProcessId(&pid)) || static_cast<DWORD>(pid) != explorerPid ||
        FAILED(item.get_CurrentIsEnabled(&enabled)) || !enabled ||
        FAILED(item.get_CurrentBoundingRectangle(&bounds)) || IsRectEmpty(&bounds) ||
        FAILED(item.get_CurrentName(&label))) return std::nullopt;
    const std::unique_ptr<OLECHAR, decltype(&SysFreeString)> name(label, &SysFreeString);
    if (name && IsExplorerRefreshLabel({name.get(), SysStringLen(name.get())}, localized)) return bounds;
    return std::nullopt;
}

std::optional<RECT> AutomationRefreshBounds(IUIAutomation& automation, IUIAutomationCondition& menuItems,
    POINT point, DWORD explorerPid, std::wstring_view localized) {
    ComPtr<IUIAutomationElement> element;
    ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(automation.ElementFromPoint(point, element.GetAddressOf())) ||
        FAILED(automation.get_ControlViewWalker(walker.GetAddressOf())) || !walker) return std::nullopt;

    for (int depth = 0; element && depth < 8; ++depth) {
        CONTROLTYPEID type = 0;
        if (FAILED(element->get_CurrentControlType(&type))) break;
        if (type == UIA_MenuItemControlTypeId) {
            if (const auto bounds = AutomationRefreshItem(*element.Get(), explorerPid, localized)) return bounds;
        } else if (type == UIA_MenuControlTypeId) {
            ComPtr<IUIAutomationElementArray> items;
            int count = 0;
            if (SUCCEEDED(element->FindAll(TreeScope_Descendants, &menuItems, items.GetAddressOf())) &&
                items && SUCCEEDED(items->get_Length(&count))) {
                for (int i = 0; i < count; ++i) {
                    ComPtr<IUIAutomationElement> item;
                    if (SUCCEEDED(items->GetElement(i, item.GetAddressOf())) && item) {
                        if (const auto bounds = AutomationRefreshItem(*item.Get(), explorerPid, localized)) {
                            return bounds;
                        }
                    }
                }
            }
            break;
        }
        ComPtr<IUIAutomationElement> parent;
        if (FAILED(walker->GetParentElement(element.Get(), parent.GetAddressOf()))) break;
        element = std::move(parent);
    }
    return std::nullopt;
}

void TrackRefreshMenu(DesktopRefreshInput& input, HANDLE stop, IUIAutomation* automation) {
    ComPtr<IUIAutomationCondition> menuItems;
    if (automation) {
        VARIANT type{};
        type.vt = VT_I4;
        type.lVal = UIA_MenuItemControlTypeId;
        automation->CreatePropertyCondition(UIA_ControlTypePropertyId, type, menuItems.GetAddressOf());
    }
    const auto localized = LocalizedRefreshLabel();
    const HANDLE signals[]{stop, input.wake};
    DWORD interval = INFINITE;
    for (;;) {
        const DWORD wait = WaitForMultipleObjects(ARRAYSIZE(signals), signals, FALSE, interval);
        if (wait != WAIT_OBJECT_0 + 1 && wait != WAIT_TIMEOUT) return;

        DWORD pid = 0;
        ULONGLONG started = 0;
        POINT origin{};
        {
            std::lock_guard lock(input.mutex);
            if (input.explorerPid && GetTickCount64() - input.menuStarted <= 30000) {
                pid = input.explorerPid;
                started = input.menuStarted;
                origin = input.origin;
            }
        }
        interval = pid ? 35 : INFINITE;
        if (!pid) continue;

        POINT cursor{};
        if (!GetCursorPos(&cursor)) continue;
        for (const auto point : {cursor, origin}) {
            const HWND window = RootAtPoint(point);
            if (WindowProcess(window) != pid) continue;
            {
                std::lock_guard lock(input.mutex);
                if (input.menuStarted == started && input.hit && input.hit->window == window) break;
            }
            auto bounds = ClassicRefreshBounds(window, localized);
            if (!bounds && menuItems) {
                bounds = AutomationRefreshBounds(*automation, *menuItems.Get(), point, pid, localized);
            }
            if (!bounds) continue;
            std::lock_guard lock(input.mutex);
            if (input.explorerPid == pid && input.menuStarted == started) {
                input.hit = RefreshMenuHit{window, *bounds};
            }
            break;
        }
    }
}

void WatchExplorerInvocations(DesktopRefreshInput& input, HANDLE stop) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return;
    {
        ComPtr<IUIAutomation> automation;
        ComPtr<IUIAutomationElement> root;
        ComPtr<IUIAutomationCacheRequest> cache;
        ComPtr<RefreshEventHandler> handler;
        bool subscribed = false;
        try {
            if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(automation.GetAddressOf()))) &&
                SUCCEEDED(automation->GetRootElement(root.GetAddressOf())) &&
                SUCCEEDED(automation->CreateCacheRequest(cache.GetAddressOf())) &&
                SUCCEEDED(cache->AddProperty(UIA_NamePropertyId)) &&
                SUCCEEDED(cache->AddProperty(UIA_ControlTypePropertyId)) &&
                SUCCEEDED(cache->AddProperty(UIA_ProcessIdPropertyId))) {
                handler = Microsoft::WRL::Make<RefreshEventHandler>(input.receiver);
                subscribed = handler && SUCCEEDED(automation->AddAutomationEventHandler(UIA_Invoke_InvokedEventId,
                    root.Get(), TreeScope_Subtree, cache.Get(), handler.Get()));
            }
            TrackRefreshMenu(input, stop, automation.Get());
        } catch (const std::exception&) {
            OutputDebugStringW(L"Could not monitor Explorer's Refresh menu item.");
        }
        if (subscribed) automation->RemoveAutomationEventHandler(UIA_Invoke_InvokedEventId, root.Get(), handler.Get());
    }
    CoUninitialize();
}

thread_local DesktopRefreshInput* mouseInput = nullptr;

LRESULT CALLBACK ObserveDesktopMouse(int code, WPARAM message, LPARAM detail) {
    if (code == HC_ACTION && mouseInput &&
        (message == WM_RBUTTONDOWN || message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
        auto& input = *mouseInput;
        const auto point = reinterpret_cast<const MSLLHOOKSTRUCT*>(detail)->pt;
        if (message == WM_RBUTTONDOWN) {
            const auto pid = ExplorerDesktopAt(point);
            {
                std::lock_guard lock(input.mutex);
                input.explorerPid = pid;
                input.menuStarted = GetTickCount64();
                input.origin = point;
                input.hit.reset();
                input.pressedWindow = nullptr;
            }
            SetEvent(input.wake);
        } else if (message == WM_LBUTTONDOWN) {
            const HWND window = RootAtPoint(point);
            std::lock_guard lock(input.mutex);
            if (input.explorerPid) {
                input.pressedWindow = window;
                input.pressedAt = point;
                SetEvent(input.wake);
            }
        } else {
            std::lock_guard lock(input.mutex);
            if (input.explorerPid && input.hit && IsRefreshMenuClick(input.hit->window, input.hit->bounds,
                    input.pressedWindow, input.pressedAt, point)) {
                PostMessageW(input.receiver, kDesktopRefreshInvoked, input.explorerPid, 0);
            }
            input.pressedWindow = nullptr;
        }
    }
    return CallNextHookEx(nullptr, code, message, detail);
}

HWND FindResident() {
    return FindWindowExW(HWND_MESSAGE, nullptr, kResidentClass, nullptr);
}

struct Resident {
    PhotoRotation rotation;
    std::size_t pending = 0;
    bool busy = true;
    bool stopping = false;
    DesktopRefreshInput refreshInput;
};

void ProcessRequests(HWND window, Resident& state) {
    if (state.busy) return;
    state.busy = true;
    while (state.pending && !state.stopping) {
        --state.pending;
        try {
            ApplyNextWallpaper(state.rotation);
        } catch (const std::exception&) {
            ReportFailure(nullptr, L"Could not apply a photo from the cached list.", false);
        }
    }
    state.busy = false;
    if (state.stopping) DestroyWindow(window);
}

LRESULT CALLBACK ResidentProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<Resident*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state) {
        switch (message) {
        case kDesktopRefreshInvoked: {
            auto& input = state->refreshInput;
            std::lock_guard lock(input.mutex);
            if (!input.explorerPid || wParam != input.explorerPid ||
                GetTickCount64() - input.menuStarted > 30000) return 0;
            input.explorerPid = 0;
            input.hit.reset();
            [[fallthrough]];
        }
        case kNextWallpaper:
            ++state->pending;
            ProcessRequests(window, *state);
            return 0;
        case WM_CLOSE:
            state->stopping = true;
            if (!state->busy) DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int RunResident(HINSTANCE instance, const fs::path& folder, HANDLE reportPipe) {
    std::unique_ptr<void, decltype(&CloseHandle)> report(reportPipe, &CloseHandle);
    HANDLE raw = CreateMutexW(nullptr, TRUE, kResidentMutex);
    const DWORD created = GetLastError();
    const std::unique_ptr<void, decltype(&CloseHandle)> mutex(raw, &CloseHandle);
    if (!mutex) return ReportFailure(nullptr, L"Could not open the background instance lock.", false);
    if (created == ERROR_ALREADY_EXISTS) {
        const auto deadline = GetTickCount64() + 5000;
        for (;;) {
            const DWORD wait = WaitForSingleObject(mutex.get(), 0);
            if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) break;
            if (wait != WAIT_TIMEOUT || GetTickCount64() >= deadline) {
                return ReportFailure(nullptr, L"Could not reach the running background instance.", false);
            }
            if (const HWND resident = FindResident()) {
                if (PostMessageW(resident, kNextWallpaper, 0, 0)) return 0;
            }
            Sleep(25);
        }
    }
    const std::unique_ptr<void, decltype(&ReleaseMutex)> lock(mutex.get(), &ReleaseMutex);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        return ReportFailure(nullptr, L"Could not initialize the Windows shell interfaces.", false);
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Resident state;
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = ResidentProc;
    windowClass.lpszClassName = kResidentClass;
    HWND window = nullptr;
    if (RegisterClassW(&windowClass)) {
        window = CreateWindowExW(0, kResidentClass, nullptr, 0, 0, 0, 0, 0,
            HWND_MESSAGE, nullptr, instance, &state);
    }

    int result = 1;
    if (!window) {
        ReportFailure(nullptr, L"Could not start the background message window.", false);
    } else {
        try {
            const auto date = Today();
            auto photos = ScanPhotos(folder, date, ReadDateTaken);
            if (report) PrintMatches(report.get(), date, photos);
            state.rotation.Reset(std::move(photos));

            const std::unique_ptr<void, decltype(&CloseHandle)> stop(
                CreateEventW(nullptr, TRUE, FALSE, nullptr), &CloseHandle);
            const std::unique_ptr<void, decltype(&CloseHandle)> wake(
                CreateEventW(nullptr, FALSE, FALSE, nullptr), &CloseHandle);
            auto& input = state.refreshInput;
            input.receiver = window;
            input.wake = wake.get();
            const std::unique_ptr<std::remove_pointer_t<HHOOK>, decltype(&UnhookWindowsHookEx)> hook(
                stop && wake ? SetWindowsHookExW(WH_MOUSE_LL, ObserveDesktopMouse, instance, 0) : nullptr,
                &UnhookWindowsHookEx);
            std::jthread watcher;
            if (hook) {
                mouseInput = &input;
                watcher = std::jthread([&input, signal = stop.get()](std::stop_token token) {
                    const std::stop_callback wakeThread(token, [signal] { SetEvent(signal); });
                    WatchExplorerInvocations(input, signal);
                });
            }
            report.reset();
            state.busy = false;
            ProcessRequests(window, state);
            MSG message{};
            BOOL received;
            while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            mouseInput = nullptr;
            if (received == 0) result = static_cast<int>(message.wParam);
        } catch (const std::exception&) {
            mouseInput = nullptr;
            ReportFailure(nullptr, L"Could not scan the requested photo folder.", false);
        }
        if (IsWindow(window)) DestroyWindow(window);
    }
    CoUninitialize();
    return result;
}

std::wstring QuotePath(const fs::path& path) {
    auto quoted = L"\"" + path.native();
    // Backslashes immediately before the closing quote must be doubled.
    const auto last = path.native().find_last_not_of(L'\\');
    quoted.append(path.native().size() - (last == std::wstring::npos ? 0 : last + 1), L'\\');
    quoted += L'"';
    return quoted;
}

void PrintResidentReport(HANDLE pipe, HANDLE output) {
    char buffer[4096]{};
    std::string pending;
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read) {
        if (!output) continue;
        pending.append(buffer, read);
        std::size_t begin = 0;
        for (auto end = pending.find('\n'); end != std::string::npos; end = pending.find('\n', begin)) {
            const auto length = static_cast<int>(end + 1 - begin);
            const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pending.data() + begin,
                length, nullptr, 0);
            if (size > 0) {
                std::wstring line(size, L'\0');
                if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, pending.data() + begin,
                        length, line.data(), size)) WriteOutput(output, line);
            }
            begin = end + 1;
        }
        pending.erase(0, begin);
    }
}

int StartResident(const fs::path& folder, HANDLE output) {
    std::wstring executable(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    if (length == 0 || length >= executable.size()) {
        return ReportFailure(nullptr, L"Could not locate OnThisDayBackground.exe.", false);
    }
    executable.resize(length);
    HANDLE rawRead = nullptr, rawWrite = nullptr;
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    if (!CreatePipe(&rawRead, &rawWrite, &security, 0)) {
        return ReportFailure(output, L"Could not open the scan report pipe.", false);
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> read(rawRead, &CloseHandle);
    std::unique_ptr<void, decltype(&CloseHandle)> write(rawWrite, &CloseHandle);
    auto command = QuotePath(executable) + L" --resident " + QuotePath(folder) + L" " +
        std::to_wstring(reinterpret_cast<UINT_PTR>(rawWrite));

    SIZE_T size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
    auto storage = std::make_unique<BYTE[]>(size);
    const auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.get());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &size)) {
        return ReportFailure(output, L"Could not prepare the background process.", false);
    }
    const std::unique_ptr<std::remove_pointer_t<LPPROC_THREAD_ATTRIBUTE_LIST>,
        decltype(&DeleteProcThreadAttributeList)> cleanup(
        attributes, &DeleteProcThreadAttributeList);
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            &rawWrite, sizeof(rawWrite), nullptr, nullptr)) {
        return ReportFailure(output, L"Could not pass the scan report pipe to the background process.", false);
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    // The only inherited handle is the temporary scan report pipe.
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
            DETACHED_PROCESS | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &startup.StartupInfo, &process)) {
        return ReportFailure(output, L"Could not start the background process.", false);
    }
    CloseHandle(process.hThread);
    const std::unique_ptr<void, decltype(&CloseHandle)> worker(process.hProcess, &CloseHandle);
    write.reset();
    PrintResidentReport(read.get(), output);
    if (WaitForSingleObject(worker.get(), 0) == WAIT_OBJECT_0) {
        DWORD code = 1;
        return GetExitCodeProcess(worker.get(), &code) ? static_cast<int>(code) : 1;
    }
    return 0;
}

int ListPhotos(const fs::path& folder, HANDLE output) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        return ReportFailure(output, L"Could not initialize the Windows shell interfaces.", true);
    }
    int result = 1;
    try {
        const auto date = Today();
        result = PrintMatches(output, date, ScanPhotos(folder, date, ReadDateTaken)) ? 0 : 1;
    } catch (const std::exception&) {
        result = ReportFailure(output, L"Could not scan the requested photo folder.", true);
    }
    CoUninitialize();
    return result;
}
} // namespace

#ifdef _MSC_VER
_Use_decl_annotations_
#endif
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> argv(
        CommandLineToArgvW(GetCommandLineW(), &argc), &LocalFree);
    if (!argv) return 1;

    const std::wstring_view option = argc > 1 ? argv.get()[1] : L"";
    if (option == L"--resident" && argc == 4) {
        wchar_t* end = nullptr;
        const auto handle = _wcstoui64(argv.get()[3], &end, 10);
        if (end == argv.get()[3] || *end || !handle || handle > UINTPTR_MAX) return 1;
        const auto report = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(handle));
        if (GetFileType(report) != FILE_TYPE_PIPE) return 1;
        return RunResident(instance, argv.get()[2], report);
    }

    const bool list = option == L"--list";
    if (!list && argc <= 2) {
        if (const HWND resident = FindResident()) {
            return PostMessageW(resident, kNextWallpaper, 0, 0) ? 0 : 1;
        }
    }

    const HANDLE output = OpenCommandLineOutput();
    const int folderIndex = list ? 2 : 1;
    const auto folder = argc <= folderIndex + 1 ?
        ResolveFolder(argc > folderIndex ? argv.get()[folderIndex] : nullptr) : fs::path{};
    if (folder.empty()) {
        return ReportFailure(output, L"Usage: OnThisDayBackground.exe [--list] [photo folder].", list);
    }
    return list ? ListPhotos(folder, output) : StartResident(folder, output);
}

#endif // _WIN32
