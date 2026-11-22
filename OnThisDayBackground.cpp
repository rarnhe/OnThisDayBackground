//#include <windows.h>
//#include <shobjidl.h>
//#include <wrl/client.h>
//#include <iostream>
//#include <string>
//#include <filesystem>
//#include <ctime>
//
//#pragma comment(lib, "Rpcrt4.lib")
//#pragma comment(lib, "Shlwapi.lib")
//
//namespace fs = std::filesystem;
//using Microsoft::WRL::ComPtr;
//
//// --- 1. 获取当前桌面 GUID (保持你的逻辑) ---
//std::wstring GetCurrentDesktopGuidString() {
//    HKEY hKey;
//    BYTE guid[16];
//    DWORD size = sizeof(guid);
//    std::wstring result = L"";
//    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\VirtualDesktops", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
//        if (RegQueryValueExW(hKey, L"CurrentVirtualDesktop", NULL, NULL, guid, &size) == ERROR_SUCCESS) {
//            RPC_WSTR guidStr;
//            if (UuidToStringW((UUID*)guid, &guidStr) == RPC_S_OK) {
//                result = L"{" + std::wstring((wchar_t*)guidStr) + L"}";
//                RpcStringFreeW(&guidStr);
//            }
//        }
//        RegCloseKey(hKey);
//    }
//    return result;
//}
//
//// --- 2. 核心改进：无闪烁设置壁纸 ---
//BOOL SetWallpaperSmooth(const std::wstring& imagePath) {
//    // A. 物理写入注册表：确保下次重启或切换回来时正确
//    std::wstring desktopGuid = GetCurrentDesktopGuidString();
//    if (!desktopGuid.empty()) {
//        std::wstring regPath = L"Control Panel\\Desktop\\VirtualDesktopBackgrounds\\" + desktopGuid;
//        HKEY hKey;
//        if (RegCreateKeyExW(HKEY_CURRENT_USER, regPath.c_str(), 0, NULL, REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
//            RegSetValueExW(hKey, L"BackgroundImage", 0, REG_SZ, (const BYTE*)imagePath.c_str(), (DWORD)(imagePath.length() + 1) * sizeof(wchar_t));
//            RegCloseKey(hKey);
//        }
//    }
//
//    // B. 使用 IDesktopWallpaper 实现平滑过渡 (不闪烁的关键)
//    // 这种方法会触发系统原生的 Cross-Fade 动画
//    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
//    BOOL success = FALSE;
//    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
//        ComPtr<IDesktopWallpaper> pDesktopWallpaper;
//        hr = CoCreateInstance(CLSID_DesktopWallpaper, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pDesktopWallpaper));
//        if (SUCCEEDED(hr)) {
//            // 参数1为 NULL 表示所有监视器，或者指定 MonitorID
//            // 这种调用不会触发 SPI_SENDWININICHANGE 的那种暴力重绘
//            hr = pDesktopWallpaper->SetWallpaper(NULL, imagePath.c_str());
//            success = SUCCEEDED(hr);
//        }
//        CoUninitialize();
//    }
//    return success;
//}
//
//// --- 3. 查找逻辑 (保持不变) ---
//std::wstring FindHistoryTodayImage(const std::wstring& folderPath) {
//    time_t now = time(0);
//    tm ltm; localtime_s(&ltm, &now);
//    wchar_t dateStr[10];
//    swprintf_s(dateStr, L"%02d-%02d", ltm.tm_mon + 1, ltm.tm_mday);
//    try {
//        if (!fs::exists(folderPath)) return L"";
//        for (const auto& entry : fs::directory_iterator(folderPath)) {
//            if (entry.is_regular_file() && entry.path().filename().wstring().find(dateStr) != std::wstring::npos) {
//                return entry.path().wstring();
//            }
//        }
//    }
//    catch (...) {}
//    return L"";
//}
//
//int main() {
//    setlocale(LC_ALL, "");
//    std::wstring photoFolder = L"C:\\Users\\" + std::wstring(_wgetenv(L"USERNAME")) + L"\\Pictures";
//    std::wstring targetImage = FindHistoryTodayImage(photoFolder);
//
//    if (!targetImage.empty()) {
//        std::wcout << L"正在平滑更新壁纸: " << targetImage << std::endl;
//        if (SetWallpaperSmooth(targetImage)) {
//            std::wcout << L"设置成功，渐变更新中。" << std::endl;
//        }
//        else {
//            std::wcout << L"设置失败。" << std::endl;
//        }
//    }
//    return 0;
//}

#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>
#include <filesystem>
#include <string>

#pragma comment(lib, "user32.lib")

namespace fs = std::filesystem;
using Microsoft::WRL::ComPtr;

// 全局变量
std::vector<std::wstring> g_historyImages;
size_t g_currentIndex = 0;
bool g_isCurrentlyOnDesktop = false;

// --- 1. 壁纸切换逻辑 ---
void ApplyNextWallpaper() {
    if (g_historyImages.empty()) return;

    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
        ComPtr<IDesktopWallpaper> pDesktopWallpaper;
        if (SUCCEEDED(CoCreateInstance(CLSID_DesktopWallpaper, NULL, CLSCTX_ALL, IID_PPV_ARGS(&pDesktopWallpaper)))) {
            pDesktopWallpaper->SetWallpaper(NULL, g_historyImages[g_currentIndex].c_str());
            g_currentIndex = (g_currentIndex + 1) % g_historyImages.size();
        }
        CoUninitialize();
    }
}

// --- 2. 核心：事件回调函数 ---
void CALLBACK WinEventProc(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (event == EVENT_SYSTEM_FOREGROUND) {
        char className[256];
        GetClassNameA(hwnd, className, sizeof(className));
        std::string cls(className);

        // 判断当前焦点是否在桌面上
        bool isDesktop = (cls == "WorkerW" || cls == "Progman");

        if (isDesktop && !g_isCurrentlyOnDesktop) {
            // 只有当鼠标在右下角区域且“刚进入桌面”时才触发
            POINT pt;
            GetCursorPos(&pt);
            int sw = GetSystemMetrics(SM_CXSCREEN);
            int sh = GetSystemMetrics(SM_CYSCREEN);

            // 判定点击位置是否在右下角响应区
            if (pt.x >= (sw - 30) && pt.y >= (sh - 30)) {
                std::cout << "[事件触发] 检测到显示桌面，更换背景..." << std::endl;
                ApplyNextWallpaper();
            }
            g_isCurrentlyOnDesktop = true;
        }
        else if (!isDesktop) {
            // 焦点离开了桌面（回到了应用窗口）
            g_isCurrentlyOnDesktop = false;
        }
    }
}

// --- 3. 图片加载 (保持原有逻辑) ---
void LoadTodayImages() {
    std::wstring path = L"C:\\Users\\" + std::wstring(_wgetenv(L"USERNAME")) + L"\\Pictures";
    time_t now = time(0); tm ltm; localtime_s(&ltm, &now);
    wchar_t dateStr[10]; swprintf_s(dateStr, L"%02d-%02d", ltm.tm_mon + 1, ltm.tm_mday);

    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file() && entry.path().filename().wstring().find(dateStr) != std::wstring::npos) {
            g_historyImages.push_back(entry.path().wstring());
        }
    }
}

int main() {
    setlocale(LC_ALL, "");
    LoadTodayImages();

    if (g_historyImages.empty()) {
        std::cout << "未找到今日匹配照片。" << std::endl;
        return 0;
    }

    // 设置全局钩子：监听焦点切换
    HWINEVENTHOOK hHook = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        NULL, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT
    );

    std::cout << "已启动高级监听模式。点击右下角试试吧！" << std::endl;

    // 必须有消息循环才能让 WinEventHook 工作
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWinEvent(hHook);
    return 0;
}