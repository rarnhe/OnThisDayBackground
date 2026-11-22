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
#include <wincodec.h>
#include <wrl/client.h>

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <map>
#include <memory>
#include <string_view>
#include <thread>
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
                if (readDate(entry->path()) == date) matches.push_back(entry->path());
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

#ifdef _WIN32

namespace on_this_day {
using Microsoft::WRL::ComPtr;

std::optional<MonthDay> ReadItemDate(const std::filesystem::path& photo) {
    ComPtr<IPropertyStore> store;
    if (FAILED(SHGetPropertyStoreFromParsingName(photo.c_str(), nullptr, GPS_DEFAULT,
            IID_PPV_ARGS(store.GetAddressOf())))) {
        return std::nullopt;
    }

    PROPVARIANT value{};
    SYSTEMTIME utc{}, local{};
    const bool valid = SUCCEEDED(store->GetValue(PKEY_ItemDate, &value)) &&
        value.vt == VT_FILETIME && (value.filetime.dwLowDateTime || value.filetime.dwHighDateTime) &&
        FileTimeToSystemTime(&value.filetime, &utc) &&
        SystemTimeToTzSpecificLocalTimeEx(nullptr, &utc, &local);
    PropVariantClear(&value);
    if (!valid) return std::nullopt;
    return MonthDay{local.wMonth, local.wDay};
}

HBITMAP LoadPhoto(const std::filesystem::path& photo) {
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(photo.c_str(), nullptr, GENERIC_READ,
            WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) ||
        FAILED(factory->CreateFormatConverter(&converter))) {
        return nullptr;
    }

    ComPtr<IWICBitmapSource> source;
    if (FAILED(frame.As(&source))) return nullptr;
    ComPtr<IWICMetadataQueryReader> metadata;
    if (SUCCEEDED(frame->GetMetadataQueryReader(&metadata))) {
        PROPVARIANT orientation{};
        const auto jpeg = metadata->GetMetadataByName(L"/app1/ifd/{ushort=274}", &orientation);
        if (FAILED(jpeg)) {
            PropVariantClear(&orientation);
            metadata->GetMetadataByName(L"/ifd/{ushort=274}", &orientation);
        }
        constexpr WICBitmapTransformOptions transforms[] = {
            WICBitmapTransformRotate0, WICBitmapTransformRotate0, WICBitmapTransformFlipHorizontal,
            WICBitmapTransformRotate180, WICBitmapTransformFlipVertical,
            static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 | WICBitmapTransformFlipHorizontal),
            WICBitmapTransformRotate90,
            static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 | WICBitmapTransformFlipHorizontal),
            WICBitmapTransformRotate270
        };
        if (orientation.vt == VT_UI2 && orientation.uiVal > 1 && orientation.uiVal < 9) {
            ComPtr<IWICBitmapFlipRotator> rotated;
            if (SUCCEEDED(factory->CreateBitmapFlipRotator(&rotated)) &&
                SUCCEEDED(rotated->Initialize(frame.Get(), transforms[orientation.uiVal]))) {
                rotated.As(&source);
            }
        }
        PropVariantClear(&orientation);
    }

    UINT width = 0, height = 0;
    if (FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGR,
            WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0 ||
        width > std::numeric_limits<UINT>::max() / 4 ||
        height > static_cast<UINT>(std::numeric_limits<LONG>::max()) ||
        height > std::numeric_limits<UINT>::max() / (width * 4)) {
        return nullptr;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = static_cast<LONG>(width);
    info.bmiHeader.biHeight = -static_cast<LONG>(height);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (bitmap && FAILED(converter->CopyPixels(nullptr, width * 4, width * 4 * height,
            static_cast<BYTE*>(pixels)))) {
        DeleteObject(bitmap);
        return nullptr;
    }
    return bitmap;
}
} // namespace on_this_day

namespace on_this_day {
namespace {
constexpr wchar_t kBackgroundClass[] = L"OnThisDayBackground.PhotoLayer";
constexpr wchar_t kDesktopKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops";

struct ShellLayer {
    HWND parent = nullptr;
    HWND icons = nullptr;
};

bool SameProcess(HWND first, HWND second) {
    DWORD firstPid = 0, secondPid = 0;
    GetWindowThreadProcessId(first, &firstPid);
    GetWindowThreadProcessId(second, &secondPid);
    return firstPid != 0 && firstPid == secondPid;
}

ShellLayer FindShellLayer(HWND progman) {
    if (GetWindowLongPtrW(progman, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) {
        const HWND icons = FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr);
        // On the raised desktop the layer must be a sibling between DefView and WorkerW.
        if (icons && FindWindowExW(progman, icons, L"WorkerW", nullptr)) return {progman, icons};
        return {};
    }

    for (HWND root = GetTopWindow(nullptr); root; root = GetWindow(root, GW_HWNDNEXT)) {
        if (!SameProcess(progman, root) ||
            !FindWindowExW(root, nullptr, L"SHELLDLL_DefView", nullptr)) continue;
        for (HWND worker = FindWindowExW(nullptr, root, L"WorkerW", nullptr); worker;
             worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) {
            if (SameProcess(progman, worker) && IsWindowVisible(worker) &&
                !FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr)) return {worker, nullptr};
        }
    }
    return {};
}
} // namespace

std::optional<std::wstring> CurrentVirtualDesktop() {
    GUID desktop{};
    auto read = [&desktop](const wchar_t* key, const wchar_t* name) {
        DWORD bytes = sizeof(desktop);
        const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, key, name,
            RRF_RT_REG_BINARY, nullptr, &desktop, &bytes);
        return status == ERROR_SUCCESS && bytes != sizeof(desktop) ? ERROR_INVALID_DATA : status;
    };

    bool found = read(kDesktopKey, L"CurrentVirtualDesktop") == ERROR_SUCCESS;
    DWORD session = 0;
    if (!found && ProcessIdToSessionId(GetCurrentProcessId(), &session)) {
        auto key = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\SessionInfo\\" +
            std::to_wstring(session) + L"\\VirtualDesktops";
        found = read(key.c_str(), L"CurrentVirtualDesktop") == ERROR_SUCCESS;
    }
    if (!found) {
        const auto result = read(kDesktopKey, L"VirtualDesktopIDs");
        if (result == ERROR_FILE_NOT_FOUND || result == ERROR_PATH_NOT_FOUND) {
            // A fresh Explorer profile can have no virtual-desktop registry state.
            desktop = GUID{};
        } else if (result != ERROR_SUCCESS) {
            return std::nullopt;
        }
    }

    wchar_t id[39]{};
    if (!StringFromGUID2(desktop, id, ARRAYSIZE(id))) return std::nullopt;
    return std::wstring(id);
}

bool DesktopHasFocus() {
    const HWND shell = GetShellWindow();
    const HWND foreground = GetForegroundWindow();
    if (!shell || !foreground || !SameProcess(shell, foreground)) return false;
    wchar_t name[32]{};
    GetClassNameW(foreground, name, ARRAYSIZE(name));
    return (foreground == shell || std::wstring_view(name) == L"Progman" ||
        std::wstring_view(name) == L"WorkerW") &&
        FindWindowExW(foreground, nullptr, L"SHELLDLL_DefView", nullptr);
}

DesktopBackground::~DesktopBackground() {
    Hide();
    Detach();
}

void DesktopBackground::Hide() {
    if (window_ && IsWindow(window_)) ShowWindow(window_, SW_HIDE);
    if (bitmap_) DeleteObject(bitmap_);
    bitmap_ = nullptr;
}

void DesktopBackground::Detach() {
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    window_ = nullptr;
}

bool DesktopBackground::Place() {
    const HWND progman = GetShellWindow();
    if (!progman) return false;
    auto layer = FindShellLayer(progman);
    if (!layer.parent) {
        // Explorer creates its separate wallpaper layer on demand.
        SendMessageTimeoutW(progman, 0x052C, 0xD, 1, SMTO_ABORTIFHUNG, 200, nullptr);
        layer = FindShellLayer(progman);
    }
    if (!layer.parent) return false;

    MONITORINFO primary{sizeof(MONITORINFO)};
    if (!GetMonitorInfoW(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &primary) ||
        !(primary.dwFlags & MONITORINFOF_PRIMARY)) return false;
    POINT origin{primary.rcMonitor.left, primary.rcMonitor.top};
    if (!ScreenToClient(layer.parent, &origin)) return false;
    const int width = primary.rcMonitor.right - primary.rcMonitor.left;
    const int height = primary.rcMonitor.bottom - primary.rcMonitor.top;

    if (!window_ || GetParent(window_) != layer.parent) {
        Detach();
        WNDCLASSW windowClass{};
        windowClass.hInstance = instance_;
        windowClass.lpfnWndProc = PaintWindow;
        windowClass.lpszClassName = kBackgroundClass;
        if (!RegisterClassW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            kBackgroundClass, nullptr, WS_CHILD, origin.x, origin.y, width, height,
            layer.parent, nullptr, instance_, this);
        if (!window_ || !SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA)) {
            Detach();
            return false;
        }
    }

    RECT bounds{};
    if (GetWindowRect(window_, &bounds) && EqualRect(&bounds, &primary.rcMonitor) &&
        GetWindow(window_, GW_HWNDPREV) == layer.icons && IsWindowVisible(window_)) return true;
    return SetWindowPos(window_, layer.icons ? layer.icons : HWND_TOP, origin.x, origin.y,
        width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE;
}

bool DesktopBackground::Show(const std::filesystem::path& photo) {
    HBITMAP next = LoadPhoto(photo);
    if (!next) return false;
    const HBITMAP previous = bitmap_;
    bitmap_ = next;
    if (!Place()) {
        bitmap_ = previous;
        DeleteObject(next);
        return false;
    }
    if (previous) DeleteObject(previous);
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void DesktopBackground::UpdatePlacement() {
    if (bitmap_ && !Place()) Detach();
}

LRESULT CALLBACK DesktopBackground::PaintWindow(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message != WM_PAINT) return DefWindowProcW(window, message, wParam, lParam);

    PAINTSTRUCT paint{};
    const HDC canvas = BeginPaint(window, &paint);
    const auto* layer = reinterpret_cast<const DesktopBackground*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    BITMAP photo{};
    if (layer && layer->bitmap_ && GetObjectW(layer->bitmap_, sizeof(photo), &photo)) {
        RECT bounds{};
        GetClientRect(window, &bounds);
        const auto width = static_cast<std::int64_t>(bounds.right);
        const auto height = static_cast<std::int64_t>(bounds.bottom);
        if (width > 0 && height > 0) {
            int sourceWidth = photo.bmWidth, sourceHeight = photo.bmHeight;
            if (sourceWidth * height > sourceHeight * width) {
                sourceWidth = static_cast<int>(sourceHeight * width / height);
            } else {
                sourceHeight = static_cast<int>(sourceWidth * height / width);
            }
            const HDC source = CreateCompatibleDC(canvas);
            if (source) {
                const HGDIOBJ previous = SelectObject(source, layer->bitmap_);
                SetStretchBltMode(canvas, HALFTONE);
                SetBrushOrgEx(canvas, 0, 0, nullptr);
                StretchBlt(canvas, 0, 0, bounds.right, bounds.bottom, source,
                    (photo.bmWidth - sourceWidth) / 2, (photo.bmHeight - sourceHeight) / 2,
                    sourceWidth > 0 ? sourceWidth : 1, sourceHeight > 0 ? sourceHeight : 1, SRCCOPY);
                SelectObject(source, previous);
                DeleteDC(source);
            }
        }
    }
    EndPaint(window, &paint);
    return 0;
}
} // namespace on_this_day

namespace {
using namespace on_this_day;
namespace fs = std::filesystem;

constexpr wchar_t kControllerClass[] = L"OnThisDayBackground.Controller";
constexpr wchar_t kMutexName[] = L"Local\\OnThisDayBackground";
constexpr UINT kRefresh = WM_APP + 1;
constexpr UINT kScanComplete = WM_APP + 2;
constexpr UINT kTray = WM_APP + 3;
constexpr UINT kShowMenu = WM_APP + 4;
constexpr UINT_PTR kTick = 1;

struct CalendarDate {
    unsigned year;
    MonthDay monthDay;
    bool operator==(const CalendarDate&) const = default;
};

CalendarDate Today() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    return {time.wYear, {time.wMonth, time.wDay}};
}

struct Controller {
    Controller(HINSTANCE instance, fs::path directory) :
        folder(std::move(directory)), background(instance) {}

    fs::path folder;
    DesktopBackground background;
    HWND window = nullptr;
    NOTIFYICONDATAW tray{};
    UINT taskbarCreated = 0;
    bool f5Down = false;
    bool scanning = false;
    std::vector<std::wstring> pendingRefreshes;
    CalendarDate date{};
    PhotoRotation rotation;
    std::optional<std::wstring> desktop;
    std::map<std::wstring, fs::path> displayed;
    std::vector<fs::path> scanned;
    std::jthread scanner;
};

void AddTrayIcon(Controller& state) {
    state.tray.cbSize = sizeof(state.tray);
    state.tray.hWnd = state.window;
    state.tray.uID = 1;
    state.tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    state.tray.uCallbackMessage = kTray;
    state.tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(state.tray.szTip, L"OnThisDayBackground");
    Shell_NotifyIconW(NIM_ADD, &state.tray);
}

void UpdateVirtualDesktop(Controller& state) {
    auto desktop = CurrentVirtualDesktop();
    if (desktop == state.desktop) {
        state.background.UpdatePlacement();
        return;
    }
    state.background.Hide();
    state.desktop = std::move(desktop);
    if (state.desktop) {
        if (const auto saved = state.displayed.find(*state.desktop); saved != state.displayed.end()) {
            state.background.Show(saved->second);
        }
    }
}

bool BeginScan(Controller& state, CalendarDate date) {
    if (state.scanning || state.date == date) return true;
    try {
        state.scanner = std::jthread([&state, date](std::stop_token stop) {
            std::vector<fs::path> result;
            if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
                try {
                    result = ScanPhotos(state.folder, date.monthDay, ReadItemDate, stop);
                } catch (const std::exception& error) {
                    OutputDebugStringA(error.what());
                }
                CoUninitialize();
            }
            state.scanned = std::move(result);
            PostMessageW(state.window, kScanComplete, 0, 0);
        });
    } catch (const std::system_error&) {
        return false;
    }
    state.date = date;
    state.scanning = true;
    state.rotation.Reset({});
    return true;
}

void RefreshDesktop(Controller& state, const std::wstring& desktop) {
    UpdateVirtualDesktop(state);
    state.rotation.Advance([&](const fs::path& photo) {
        if (state.desktop == desktop) {
            if (!state.background.Show(photo)) return false;
        } else {
            // A refresh queued during the scan still belongs to the desktop that requested it.
            HBITMAP bitmap = LoadPhoto(photo);
            if (!bitmap) return false;
            DeleteObject(bitmap);
        }
        state.displayed[desktop] = photo;
        return true;
    });
}

void Refresh(Controller& state) {
    const auto desktop = CurrentVirtualDesktop();
    if (!desktop || !BeginScan(state, Today())) return;
    if (state.scanning) {
        state.pendingRefreshes.push_back(*desktop);
    } else {
        RefreshDesktop(state, *desktop);
    }
}

void OnKeyboardInput(Controller& state, HRAWINPUT handle) {
    RAWINPUT input{};
    UINT size = sizeof(input);
    const UINT copied = GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER));
    if (copied == static_cast<UINT>(-1) || copied < offsetof(RAWINPUT, data) + sizeof(RAWKEYBOARD) ||
        input.header.dwType != RIM_TYPEKEYBOARD || input.data.keyboard.VKey != VK_F5) return;
    if (input.data.keyboard.Flags & RI_KEY_BREAK) {
        state.f5Down = false;
    } else if (!std::exchange(state.f5Down, true) && !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
        !(GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
        !(GetAsyncKeyState(VK_LWIN) & 0x8000) && !(GetAsyncKeyState(VK_RWIN) & 0x8000) &&
        DesktopHasFocus()) {
        Refresh(state);
    }
}

void ShowTrayMenu(Controller& state) {
    const HMENU menu = CreatePopupMenu();
    if (!menu) return;
    AppendMenuW(menu, MF_STRING, 1, L"Refresh wallpaper");
    AppendMenuW(menu, MF_STRING, 2, L"Exit");
    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(state.window);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        cursor.x, cursor.y, 0, state.window, nullptr);
    PostMessageW(state.window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (command == 1) PostMessageW(state.window, kRefresh, 0, 0);
    if (command == 2) DestroyWindow(state.window);
}

LRESULT CALLBACK ControllerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* state = reinterpret_cast<Controller*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!state) return DefWindowProcW(window, message, wParam, lParam);
    if (message == state->taskbarCreated && state->taskbarCreated != 0) {
        AddTrayIcon(*state);
        return 0;
    }

    switch (message) {
    case kRefresh:
        Refresh(*state);
        return 0;
    case kScanComplete:
        state->scanner.join();
        state->scanning = false;
        state->rotation.Reset(std::move(state->scanned));
        if (BeginScan(*state, Today()) && !state->scanning) {
            for (const auto& desktop : state->pendingRefreshes) RefreshDesktop(*state, desktop);
            state->pendingRefreshes.clear();
        }
        return 0;
    case WM_INPUT:
        OnKeyboardInput(*state, reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(window, message, wParam, lParam);
    case WM_TIMER:
        BeginScan(*state, Today());
        if (!state->displayed.empty()) UpdateVirtualDesktop(*state);
        return 0;
    case kTray:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu(*state);
        return 0;
    case kShowMenu:
        ShowTrayMenu(*state);
        return 0;
    case WM_DESTROY:
        state->scanner.request_stop();
        KillTimer(window, kTick);
        Shell_NotifyIconW(NIM_DELETE, &state->tray);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

int Run(HINSTANCE instance) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int argc = 0;
    const std::unique_ptr<wchar_t*, decltype(&LocalFree)> argv(
        CommandLineToArgvW(GetCommandLineW(), &argc), &LocalFree);
    std::error_code error;
    fs::path folder;
    if (argv && argc == 1) {
        PWSTR pictures = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_Pictures, KF_FLAG_DONT_VERIFY,
            nullptr, &pictures);
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(pictures, &CoTaskMemFree);
        if (SUCCEEDED(result) && path) folder = path.get();
    } else if (argv && argc == 2) {
        folder = fs::absolute(argv.get()[1], error);
        if (!error && !fs::is_directory(folder, error)) folder.clear();
    }
    if (error || folder.empty()) {
        MessageBoxW(nullptr, L"Open the app to use your Pictures folder, or specify a valid photo folder.",
            L"OnThisDayBackground", MB_OK | MB_ICONERROR);
        return 1;
    }

    Controller state{instance, std::move(folder)};
    state.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = ControllerProc;
    windowClass.lpszClassName = kControllerClass;
    if (!RegisterClassW(&windowClass)) return 1;
    state.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kControllerClass, nullptr, WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, &state);
    if (!state.window) return 1;

    const RAWINPUTDEVICE keyboard{0x01, 0x06, RIDEV_INPUTSINK, state.window};
    int result = 1;
    if (RegisterRawInputDevices(&keyboard, 1, sizeof(keyboard)) &&
        SetTimer(state.window, kTick, 150, nullptr) && BeginScan(state, Today())) {
        AddTrayIcon(state);
        MSG message{};
        BOOL received;
        while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (received == 0) result = static_cast<int>(message.wParam);
    }
    if (IsWindow(state.window)) DestroyWindow(state.window);
    if (state.scanner.joinable()) {
        state.scanner.request_stop();
        state.scanner.join();
    }
    return result;
}

int RunSingleInstance(HINSTANCE instance) {
    HANDLE rawMutex = CreateMutexW(nullptr, TRUE, kMutexName);
    const DWORD status = GetLastError();
    const std::unique_ptr<void, decltype(&CloseHandle)> mutex(rawMutex, &CloseHandle);
    if (!mutex) return 1;
    if (status == ERROR_ALREADY_EXISTS) {
        const auto deadline = GetTickCount64() + 5000;
        for (;;) {
            const DWORD wait = WaitForSingleObject(mutex.get(), 0);
            if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) break;
            if (wait != WAIT_TIMEOUT || GetTickCount64() >= deadline) return 1;
            if (const HWND existing = FindWindowW(kControllerClass, nullptr)) {
                DWORD process = 0;
                GetWindowThreadProcessId(existing, &process);
                if (process) AllowSetForegroundWindow(process);
                if (PostMessageW(existing, kShowMenu, 0, 0)) return 0;
            }
            Sleep(25);
        }
    }

    int result = 1;
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        result = Run(instance);
        CoUninitialize();
    }
    ReleaseMutex(mutex.get());
    return result;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    return RunSingleInstance(instance);
}

#endif // _WIN32
