#include "OnThisDayBackground.h"

#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <objbase.h>
#include <shellapi.h>
#include <string_view>

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
    HBITMAP bitmap = LoadPhoto(photo);
    BITMAP info{};
    const bool passed = bitmap &&
        GetObjectW(bitmap, sizeof(info), &info) && info.bmWidth == 2 && info.bmHeight == 1;
    if (bitmap) DeleteObject(bitmap);
    return passed && !LoadPhoto(folder / "missing.bmp");
}

bool TestItemDate(const fs::path& fixture, const fs::path& folder) {
    const auto photo = folder / fixture.filename();
    fs::copy_file(fixture, photo, fs::copy_options::overwrite_existing);
    const MonthDay itemDate{10, 7};
    return ReadItemDate(photo) == itemDate &&
        ScanPhotos(folder, itemDate, ReadItemDate) == std::vector{photo} &&
        ScanPhotos(folder, {8, 10}, ReadItemDate).empty() &&
        !ReadItemDate(folder / "missing-2015-10-07.jpg");
}

bool IsWindowsGui(const fs::path& program) {
    std::ifstream file(program, std::ios::binary);
    IMAGE_DOS_HEADER dos{};
    if (!file.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    file.seekg(dos.e_lfanew);
    DWORD signature = 0;
    IMAGE_FILE_HEADER header{};
    if (!file.read(reinterpret_cast<char*>(&signature), sizeof(signature)) ||
        signature != IMAGE_NT_SIGNATURE ||
        !file.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;

    constexpr auto offset = offsetof(IMAGE_OPTIONAL_HEADER32, Subsystem);
    static_assert(offset == offsetof(IMAGE_OPTIONAL_HEADER64, Subsystem));
    WORD subsystem = 0;
    if (header.SizeOfOptionalHeader < offset + sizeof(subsystem)) return false;
    file.seekg(offset, std::ios::cur);
    return file.read(reinterpret_cast<char*>(&subsystem), sizeof(subsystem)) &&
        subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI;
}

bool Launch(const fs::path& program, const fs::path& folder, PROCESS_INFORMATION& process) {
    std::wstring command = L"\"" + program.wstring() + L"\"";
    if (!folder.empty()) command += L" \"" + folder.wstring() + L"\"";
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
    // Explorer starts the .exe without arguments or console-hiding flags.
    if (!IsWindowsGui(app) || !Launch(app, {}, process)) return false;
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
    int count = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (argv && count == 3 && std::wstring_view(argv[1]) == L"--gui") {
        passed = TestGui(argv[2], folder);
    } else if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        if (argv && count == 3 && std::wstring_view(argv[1]) == L"--item-date") {
            passed = TestItemDate(argv[2], folder);
        } else if (argc == 1) {
            passed = TestPhotoDecoder(folder);
        }
        CoUninitialize();
    }
    if (argv) LocalFree(argv);
    fs::remove_all(folder);
    if (!passed) std::cerr << "Windows integration test failed\n";
    return passed ? 0 : 1;
}
