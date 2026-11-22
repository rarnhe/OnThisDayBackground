#include "OnThisDayBackground.h"

#include <cstdint>
#include <objbase.h>

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
