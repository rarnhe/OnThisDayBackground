#include "OnThisDayBackground.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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
    return passed && !ReadDateTaken(photo) && !LoadPhoto(folder / "missing.bmp");
}

bool TestDateTaken(const fs::path& fixture, const fs::path& folder) {
    const auto photo = folder / fixture.filename();
    fs::copy_file(fixture, photo, fs::copy_options::overwrite_existing);
    const MonthDay dateTaken{10, 7};
    return ReadDateTaken(photo) == dateTaken &&
        ScanPhotos(folder, dateTaken, ReadDateTaken) == std::vector{photo} &&
        ScanPhotos(folder, {8, 10}, ReadDateTaken).empty() &&
        !ReadDateTaken(folder / "missing-2015-10-07.jpg");
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

bool Launch(const fs::path& program, const fs::path& folder, PROCESS_INFORMATION& process,
    std::wstring_view option = {}, HANDLE output = nullptr) {
    std::wstring command = L"\"" + program.wstring() + L"\"";
    if (!option.empty()) command += L" " + std::wstring(option);
    if (!folder.empty()) command += L" \"" + folder.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    if (output) {
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = output;
        startup.hStdError = output;
    }
    if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, output != nullptr, 0,
            nullptr, nullptr, &startup, &process)) return false;
    CloseHandle(process.hThread);
    return true;
}

bool ExitedSuccessfully(HANDLE process, DWORD timeout) {
    DWORD code = 1;
    return WaitForSingleObject(process, timeout) == WAIT_OBJECT_0 &&
        GetExitCodeProcess(process, &code) && code == 0;
}

bool TestList(const fs::path& app, const fs::path& fixture, const fs::path& folder) {
    std::ifstream jpeg(fixture, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(jpeg), {}};
    constexpr std::string_view original = "2015:10:07 12:00:00";
    SYSTEMTIME local{};
    GetLocalTime(&local);
    char today[20]{};
    sprintf_s(today, "%04u:%02u:%02u 12:00:00", static_cast<unsigned>(local.wYear),
        static_cast<unsigned>(local.wMonth), static_cast<unsigned>(local.wDay));
    auto tag = bytes.find(original);
    if (tag == std::string::npos) return false;
    while (tag != std::string::npos) {
        bytes.replace(tag, original.size(), today);
        tag = bytes.find(original, tag + original.size());
    }

    const auto photos = folder / L"photos";
    fs::create_directory(photos);
    const auto first = photos / L"a-\u7167\u7247.jpg";
    const auto second = photos / L"z.jpg";
    for (const auto& photo : {second, first}) {
        if (!std::ofstream(photo, std::ios::binary).write(bytes.data(), bytes.size())) return false;
    }

    const auto log = folder / "stdout.txt";
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const HANDLE raw = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) return false;
    std::unique_ptr<void, decltype(&CloseHandle)> output(raw, &CloseHandle);
    PROCESS_INFORMATION process{};
    if (!Launch(app, photos, process, L"--list", output.get())) return false;
    const bool exited = ExitedSuccessfully(process.hProcess, 5000);
    if (!exited) TerminateProcess(process.hProcess, 1);
    CloseHandle(process.hProcess);
    output.reset();

    std::ifstream result(log, std::ios::binary);
    const std::string printed{std::istreambuf_iterator<char>(result), {}};
    const auto expected = first.u8string() + u8"\r\n" + second.u8string() + u8"\r\n";
    const std::string paths(expected.begin(), expected.end());
    return exited && printed.starts_with("System.Photo.DateTaken ") &&
        printed.ends_with("2 matching photo(s)\r\n" + paths);
}

bool TestTerminalRefresh(const fs::path& app, const fs::path& folder) {
    // The hosted runner can invoke CTest with redirected stdio and no console.
    DWORD processIds[1]{};
    if (!GetConsoleProcessList(processIds, ARRAYSIZE(processIds)) && !AllocConsole()) return false;
    const auto log = folder / "verify-stdout.txt";
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    const HANDLE rawOutput = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (rawOutput == INVALID_HANDLE_VALUE) return false;
    const std::unique_ptr<void, decltype(&CloseHandle)> output(rawOutput, &CloseHandle);
    const HANDLE rawInput = CreateFileW(L"CONIN$", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (rawInput == INVALID_HANDLE_VALUE) return false;
    const std::unique_ptr<void, decltype(&CloseHandle)> input(rawInput, &CloseHandle);
    PROCESS_INFORMATION process{};
    if (!Launch(app, folder, process, L"--verify", output.get())) return false;

    const auto waitForLog = [&](std::string_view message) {
        for (int attempt = 0; attempt < 100; ++attempt) {
            std::ifstream file(log, std::ios::binary);
            const std::string content{std::istreambuf_iterator<char>(file), {}};
            if (content.find(message) != std::string::npos) return true;
            if (WaitForSingleObject(process.hProcess, 50) != WAIT_TIMEOUT) break;
        }
        return false;
    };

    bool passed = waitForLog("Verification ready.");
    if (passed) {
        INPUT_RECORD events[2]{};
        events[0].EventType = KEY_EVENT;
        events[0].Event.KeyEvent.bKeyDown = TRUE;
        events[0].Event.KeyEvent.wRepeatCount = 1;
        events[0].Event.KeyEvent.wVirtualKeyCode = 'B';
        events[0].Event.KeyEvent.uChar.UnicodeChar = L'b';
        events[1] = events[0];
        events[1].Event.KeyEvent.bKeyDown = FALSE;
        DWORD written = 0;
        passed = WriteConsoleInputW(input.get(), events, ARRAYSIZE(events), &written) &&
            written == ARRAYSIZE(events) && waitForLog("[Terminal b] Refresh received.");
    }

    if (const HWND window = FindWindowW(L"OnThisDayBackground.Controller", nullptr)) {
        DWORD processId = 0;
        GetWindowThreadProcessId(window, &processId);
        if (processId == process.dwProcessId) PostMessageW(window, WM_CLOSE, 0, 0);
    }
    if (!ExitedSuccessfully(process.hProcess, 3000)) {
        TerminateProcess(process.hProcess, 1);
        passed = false;
    }
    CloseHandle(process.hProcess);
    return passed;
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
    } else if (argv && count == 3 && std::wstring_view(argv[1]) == L"--verify") {
        passed = TestTerminalRefresh(argv[2], folder);
    } else if (argv && count == 4 && std::wstring_view(argv[1]) == L"--list") {
        passed = TestList(argv[2], argv[3], folder);
    } else if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        if (argv && count == 3 && std::wstring_view(argv[1]) == L"--date-taken") {
            passed = TestDateTaken(argv[2], folder);
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
