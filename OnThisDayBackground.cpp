#include "OnThisDayBackground.h"

#include <shellapi.h>
#include <cwchar>
#include <map>
#include <objbase.h>
#include <memory>
#include <thread>
#include <utility>

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
                    result = ScanPhotos(state.folder, date.monthDay, ReadPhotoDate, stop);
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
    if (GetRawInputData(handle, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != size ||
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
    if (argv && argc == 2) folder = fs::absolute(argv.get()[1], error);
    if (error || folder.empty() || !fs::is_directory(folder, error)) {
        MessageBoxW(nullptr, L"Usage: OnThisDayBackground.exe \"path to a photo folder\"",
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
