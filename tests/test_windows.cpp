#include "OnThisDayBackground.h"

#include <array>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <shellapi.h>

using namespace on_this_day;
namespace fs = std::filesystem;

bool TestPhotoDecoder(const fs::path& folder) {
    // 2x1 24-bit BMP with one row padded to eight bytes.
    constexpr std::array<unsigned char, 62> bytes{
        'B', 'M', 62, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0,
        2, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 8, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 255, 0, 255, 0, 0, 0
    };
    const auto photo = folder / "2000-11-22.bmp";
    std::ofstream(photo, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    const auto matches = ScanPhotos(folder, {11, 22}, ReadPhotoDate);
    HBITMAP bitmap = LoadPhoto(photo);
    BITMAP info{};
    const bool passed = matches == std::vector{photo} && bitmap &&
        GetObjectW(bitmap, sizeof(info), &info) && info.bmWidth == 2 && info.bmHeight == 1;
    if (bitmap) DeleteObject(bitmap);
    return passed && !LoadPhoto(folder / "missing.bmp");
}

bool Launch(const fs::path& program, const fs::path& folder, PROCESS_INFORMATION& process) {
    std::wstring command = L"\"" + program.wstring() + L"\" \"" + folder.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr,
            nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    return true;
}

bool ExitedSuccessfully(HANDLE process, DWORD timeout) {
    DWORD code = 1;
    return WaitForSingleObject(process, timeout) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process, &code) && code == 0;
}

bool TestGui(const fs::path& app, const fs::path& folder) {
    PROCESS_INFORMATION process{};
    if (!Launch(app, folder, process)) return false;
    HWND window = nullptr;
    for (int i = 0; i < 100; ++i) {
        window = FindWindowW(L"OnThisDayBackground.Controller", nullptr);
        DWORD pid = 0;
        if (window && GetWindowThreadProcessId(window, &pid) && pid == process.dwProcessId) break;
        window = nullptr;
        if (WaitForSingleObject(process.hProcess, 50) != WAIT_TIMEOUT) break;
    }
    bool passed = window && !IsWindowVisible(window);
    if (passed) {
        PROCESS_INFORMATION duplicate{};
        if (Launch(app, folder, duplicate)) {
            passed = ExitedSuccessfully(duplicate.hProcess, 5000);
            if (!passed) TerminateProcess(duplicate.hProcess, 1);
            CloseHandle(duplicate.hProcess);
        } else {
            passed = false;
        }
    }
    if (window) {
        // Close an open tray menu before closing the controller.
        PostMessageW(window, WM_CANCELMODE, 0, 0);
        PostMessageW(window, WM_CLOSE, 0, 0);
    }
    if (!ExitedSuccessfully(process.hProcess, 5000)) {
        TerminateProcess(process.hProcess, 1);
        passed = false;
    }
    CloseHandle(process.hProcess);
    return passed;
}

int main(int argc, char**) {
    const auto folder = fs::temp_directory_path() /
        (L"on-this-day Windows test " + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(folder);
    bool passed = false;
    if (argc == 2) {
        int count = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
        passed = argv && count == 2 && TestGui(argv[1], folder);
        if (argv) LocalFree(argv);
    } else if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        passed = TestPhotoDecoder(folder);
        CoUninitialize();
    }
    fs::remove_all(folder);
    if (!passed) std::cerr << "Windows integration test failed\n";
    return passed ? 0 : 1;
}
