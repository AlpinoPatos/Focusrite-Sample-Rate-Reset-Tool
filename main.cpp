#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cwchar>

#define IDI_APP_ICON 101

constexpr UINT WM_TRAYICON       = WM_USER + 1;
constexpr UINT WM_RESET_DONE     = WM_USER + 2;
constexpr int  HOTKEY_ID         = 1;

constexpr UINT ID_TRAY_RESET     = 1001;
constexpr UINT ID_TRAY_HOTKEY    = 1002;
constexpr UINT ID_TRAY_NOTIF     = 1003;
constexpr UINT ID_TRAY_DELAY_08  = 1101;
constexpr UINT ID_TRAY_DELAY_12  = 1102;
constexpr UINT ID_TRAY_DELAY_16  = 1103;
constexpr UINT ID_TRAY_DELAY_20  = 1104;
constexpr UINT ID_TRAY_EXIT      = 1099;

typedef long ASIOBool;
typedef double ASIOSampleRate;
typedef long ASIOError;
constexpr ASIOError ASE_OK = 0;

struct IASIO : public IUnknown {
    virtual ASIOBool init(void* sysHandle) = 0;
    virtual void getDriverName(char* name) = 0;
    virtual long getDriverVersion() = 0;
    virtual void getErrorMessage(char* string) = 0;
    virtual ASIOError start() = 0;
    virtual ASIOError stop() = 0;
    virtual ASIOError getChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual ASIOError getLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual ASIOError getBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual ASIOError canSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual ASIOError getSampleRate(ASIOSampleRate* sampleRate) = 0;
    virtual ASIOError setSampleRate(ASIOSampleRate sampleRate) = 0;
};

HWND g_hwnd = nullptr;
UINT g_wmTaskbarCreated = 0;
NOTIFYICONDATAW g_nid = {};
std::atomic<bool> g_isResetting{false};

UINT g_hkMods = MOD_CONTROL | MOD_ALT;
UINT g_hkVk   = 'R';
int  g_delayMs = 1200;
bool g_showNotifications = false;
bool g_hasError = false;

HICON g_baseIcon = nullptr;
HICON g_errorIcon = nullptr;
HBRUSH g_dlgBgBrush = nullptr;

std::wstring g_lastStatus = L"Ready";
UINT g_tempMods = 0;
UINT g_tempVk   = 0;

std::wstring GetConfigPath() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, &path.front(), MAX_PATH);
    path.resize(len);
    size_t pos = path.find_last_of(L"\\/");
    return (pos != std::wstring::npos ? path.substr(0, pos + 1) : L"") + L"config.ini";
}

void SaveConfig() {
    std::wstring cfg = GetConfigPath();
    WritePrivateProfileStringW(L"Hotkey", L"Modifiers", std::to_wstring(g_hkMods).c_str(), cfg.c_str());
    WritePrivateProfileStringW(L"Hotkey", L"VK", std::to_wstring(g_hkVk).c_str(), cfg.c_str());
    WritePrivateProfileStringW(L"Settings", L"DelayMs", std::to_wstring(g_delayMs).c_str(), cfg.c_str());
    WritePrivateProfileStringW(L"Settings", L"Notifications", g_showNotifications ? L"1" : L"0", cfg.c_str());
}

void LoadConfig() {
    std::wstring cfg = GetConfigPath();
    bool exists = (GetFileAttributesW(cfg.c_str()) != INVALID_FILE_ATTRIBUTES);

    g_hkMods = GetPrivateProfileIntW(L"Hotkey", L"Modifiers", MOD_CONTROL | MOD_ALT, cfg.c_str());
    g_hkVk   = GetPrivateProfileIntW(L"Hotkey", L"VK", 'R', cfg.c_str());
    g_delayMs = static_cast<int>(GetPrivateProfileIntW(L"Settings", L"DelayMs", 1200, cfg.c_str()));
    if (g_delayMs < 200 || g_delayMs > 5000) {
        g_delayMs = 1200;
    }
    g_showNotifications = (GetPrivateProfileIntW(L"Settings", L"Notifications", 0, cfg.c_str()) != 0);

    if (!exists) {
        SaveConfig();
    }
}

std::wstring FormatHotkey(UINT mods, UINT vk) {
    std::wstring res;
    if (mods & MOD_CONTROL) res += L"Ctrl+";
    if (mods & MOD_ALT)     res += L"Alt+";
    if (mods & MOD_SHIFT)   res += L"Shift+";

    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    std::wstring keyName(64, L'\0');
    int len = GetKeyNameTextW(scan << 16, &keyName.front(), 64);
    if (len > 0) {
        keyName.resize(len);
        res += keyName;
    } else {
        res += std::to_wstring(vk);
    }
    return res;
}

HICON CreateFallbackBarIcon(int cx, int cy) {
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cx, cy);
    HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, nullptr);

    HGDIOBJ oldObj = SelectObject(hdcMem, hbmColor);
    RECT fullRc = {0, 0, cx, cy};
    HBRUSH bgBrush = CreateSolidBrush(RGB(32, 36, 44));
    FillRect(hdcMem, &fullRc, bgBrush);
    DeleteObject(bgBrush);

    HBRUSH barBrush = CreateSolidBrush(RGB(235, 240, 248));
    int w = std::max(2, cx / 6);
    int gap = std::max(1, cx / 9);
    int totalW = 3 * w + 2 * gap;
    int startX = (cx - totalW) / 2;

    int h1 = cy / 3;
    int h2 = (cy * 2) / 3;
    int h3 = cy / 2;

    RECT b1 = {startX, (cy - h1) / 2, startX + w, (cy + h1) / 2};
    RECT b2 = {startX + w + gap, (cy - h2) / 2, startX + w + gap + w, (cy + h2) / 2};
    RECT b3 = {startX + 2 * (w + gap), (cy - h3) / 2, startX + 2 * (w + gap) + w, (cy + h3) / 2};

    FillRect(hdcMem, &b1, barBrush);
    FillRect(hdcMem, &b2, barBrush);
    FillRect(hdcMem, &b3, barBrush);
    DeleteObject(barBrush);

    SelectObject(hdcMem, oldObj);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask = hbmMask;
    HICON hIcon = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    return hIcon;
}

HICON CreateErrorBadgeIcon(HICON srcIcon, int cx, int cy) {
    if (!srcIcon) return nullptr;

    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmColor = CreateCompatibleBitmap(hdcScreen, cx, cy);
    HBITMAP hbmMask = CreateBitmap(cx, cy, 1, 1, nullptr);

    HGDIOBJ oldObj = SelectObject(hdcMem, hbmColor);
    RECT fullRc = {0, 0, cx, cy};
    HBRUSH clearBrush = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdcMem, &fullRc, clearBrush);
    DeleteObject(clearBrush);

    DrawIconEx(hdcMem, 0, 0, srcIcon, cx, cy, 0, nullptr, DI_NORMAL);

    // En altta belirgin kırmızı hata çubuğu/şeridi
    int barH = std::max(3, cy / 4);
    RECT errRc = {0, cy - barH, cx, cy};
    HBRUSH errBrush = CreateSolidBrush(RGB(220, 38, 38));
    FillRect(hdcMem, &errRc, errBrush);
    DeleteObject(errBrush);

    SelectObject(hdcMem, oldObj);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmColor = hbmColor;
    ii.hbmMask = hbmMask;
    HICON hErr = CreateIconIndirect(&ii);

    DeleteObject(hbmColor);
    DeleteObject(hbmMask);
    return hErr;
}

HICON LoadAppIcon(HINSTANCE hInst) {
    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    HICON hIcon = static_cast<HICON>(LoadImageW(
        hInst,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        cx,
        cy,
        LR_DEFAULTCOLOR
    ));
    if (!hIcon) {
        hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APP_ICON));
    }
    if (!hIcon) {
        hIcon = CreateFallbackBarIcon(cx, cy);
    }
    return hIcon;
}

void UpdateTrayState() {
    g_nid.hIcon = (g_hasError && g_errorIcon) ? g_errorIcon : g_baseIcon;
    std::wstring tip = g_hasError
        ? L"Focusrite Reset - Error occurred (" + FormatHotkey(g_hkMods, g_hkVk) + L")"
        : L"Focusrite Reset (" + FormatHotkey(g_hkMods, g_hkVk) + L")";
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void RestoreTrayIcon() {
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.hIcon = (g_hasError && g_errorIcon) ? g_errorIcon : g_baseIcon;
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    UpdateTrayState();
}

bool FindFocusriteCLSID(CLSID& outClsid) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    std::wstring subKeyBuf(256, L'\0');
    DWORD index = 0;
    bool found = false;

    while (true) {
        DWORD nameLen = 256;
        if (RegEnumKeyExW(hKey, index++, &subKeyBuf.front(), &nameLen, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            break;

        std::wstring name = subKeyBuf.substr(0, nameLen);
        std::wstring lowerName = name;
        for (auto& c : lowerName) c = towlower(c);

        if (lowerName.find(L"focusrite") != std::wstring::npos && lowerName.find(L"usb") != std::wstring::npos) {
            HKEY hSub;
            if (RegOpenKeyExW(hKey, name.c_str(), 0, KEY_READ, &hSub) == ERROR_SUCCESS) {
                std::wstring clsidBuf(64, L'\0');
                DWORD dataSize = 63 * sizeof(wchar_t);
                if (RegQueryValueExW(hSub, L"CLSID", nullptr, nullptr, (LPBYTE)&clsidBuf.front(), &dataSize) == ERROR_SUCCESS) {
                    size_t charCount = std::min<size_t>(dataSize / sizeof(wchar_t), 63);
                    clsidBuf[charCount] = L'\0';
                    if (SUCCEEDED(CLSIDFromString(clsidBuf.c_str(), &outClsid))) {
                        found = true;
                    }
                }
                RegCloseKey(hSub);
            }
        }
        if (found) break;
    }
    RegCloseKey(hKey);
    return found;
}

void ResetWorker(HWND notifyWnd, int delayMs) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::wstring* resultMsg = new std::wstring();

    CLSID clsid;
    if (!FindFocusriteCLSID(clsid)) {
        *resultMsg = L"Error occurred: Focusrite USB ASIO driver not found.";
        if (SUCCEEDED(hr)) CoUninitialize();
        PostMessageW(notifyWnd, WM_RESET_DONE, 0, (LPARAM)resultMsg);
        return;
    }

    IASIO* asio = nullptr;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, clsid, (void**)&asio)) || !asio) {
        *resultMsg = L"Error occurred: Failed to instantiate ASIO driver.";
        if (SUCCEEDED(hr)) CoUninitialize();
        PostMessageW(notifyWnd, WM_RESET_DONE, 0, (LPARAM)resultMsg);
        return;
    }

    if (!asio->init(GetDesktopWindow())) {
        *resultMsg = L"Error occurred: Failed to initialize Focusrite hardware.";
        asio->Release();
        if (SUCCEEDED(hr)) CoUninitialize();
        PostMessageW(notifyWnd, WM_RESET_DONE, 0, (LPARAM)resultMsg);
        return;
    }

    ASIOSampleRate origRate = 0.0;
    if (asio->getSampleRate(&origRate) != ASE_OK || origRate <= 0.0) {
        *resultMsg = L"Error occurred: Failed to read current sample rate.";
        asio->Release();
        if (SUCCEEDED(hr)) CoUninitialize();
        PostMessageW(notifyWnd, WM_RESET_DONE, 0, (LPARAM)resultMsg);
        return;
    }

    ASIOSampleRate tempRate = (static_cast<int>(origRate) == 48000) ? 44100.0 : 48000.0;
    if (asio->canSampleRate(tempRate) == ASE_OK && asio->setSampleRate(tempRate) == ASE_OK) {
        Sleep(static_cast<DWORD>(delayMs));
        if (asio->setSampleRate(origRate) == ASE_OK) {
            Sleep(200);
            *resultMsg = L"Reset: " + std::to_wstring((int)origRate) + L" Hz -> " + std::to_wstring((int)tempRate) + L" Hz -> " + std::to_wstring((int)origRate) + L" Hz";
            asio->Release();
            if (SUCCEEDED(hr)) CoUninitialize();
            PostMessageW(notifyWnd, WM_RESET_DONE, 1, (LPARAM)resultMsg);
            return;
        } else {
            *resultMsg = L"Error occurred: Failed to restore original sample rate.";
        }
    } else {
        *resultMsg = L"Error occurred: Failed to switch to temporary sample rate.";
    }

    asio->Release();
    if (SUCCEEDED(hr)) CoUninitialize();
    PostMessageW(notifyWnd, WM_RESET_DONE, 0, (LPARAM)resultMsg);
}

void TriggerReset() {
    if (g_isResetting.exchange(true)) return;
    g_lastStatus = L"Resetting...";
    int currentDelay = g_delayMs;
    std::thread(ResetWorker, g_hwnd, currentDelay).detach();
}

void ShowBalloon(const std::wstring& msg) {
    if (!g_showNotifications) return;
    wcsncpy_s(g_nid.szInfo, msg.c_str(), _TRUNCATE);
    wcsncpy_s(g_nid.szInfoTitle, L"Focusrite Reset Tool", _TRUNCATE);
    g_nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

LRESULT CALLBACK HotkeyDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(240, 240, 240));
        std::wstring text = L"Press new key combination:\n\n( " + FormatHotkey(g_tempMods, g_tempVk) + L" )\n\nPress ENTER to save, ESC to cancel";
        RECT rc = {15, 15, 305, 115};
        DrawTextW(hdc, text.c_str(), -1, &rc, DT_CENTER | DT_WORDBREAK);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (g_dlgBgBrush) {
            FillRect(hdc, &rc, g_dlgBgBrush);
            return 1;
        }
        break;
    }
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN: {
        UINT vk = (UINT)wParam;
        if (vk == VK_ESCAPE) {
            DestroyWindow(hwnd);
            return 0;
        }
        if (vk == VK_RETURN && g_tempMods != 0 && g_tempVk != 0) {
            UnregisterHotKey(g_hwnd, HOTKEY_ID);
            if (RegisterHotKey(g_hwnd, HOTKEY_ID, g_tempMods | MOD_NOREPEAT, g_tempVk)) {
                g_hkMods = g_tempMods;
                g_hkVk   = g_tempVk;
                SaveConfig();
                UpdateTrayState();
            } else {
                RegisterHotKey(g_hwnd, HOTKEY_ID, g_hkMods | MOD_NOREPEAT, g_hkVk);
                MessageBoxW(hwnd, L"This hotkey is already in use by another application!", L"Error", MB_ICONWARNING);
            }
            DestroyWindow(hwnd);
            return 0;
        }
        if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN || vk == VK_RWIN)
            return 0;

        UINT mods = 0;
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
        if (GetAsyncKeyState(VK_MENU) & 0x8000)    mods |= MOD_ALT;
        if (GetAsyncKeyState(VK_SHIFT) & 0x8000)   mods |= MOD_SHIFT;

        if (mods != 0) {
            g_tempMods = mods;
            g_tempVk   = vk;
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    }
    case WM_DESTROY:
        RegisterHotKey(g_hwnd, HOTKEY_ID, g_hkMods | MOD_NOREPEAT, g_hkVk);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void OpenHotkeyDialog() {
    static bool registered = false;
    static HWND hDlg = nullptr;

    if (hDlg && IsWindow(hDlg)) {
        SetForegroundWindow(hDlg);
        return;
    }

    UnregisterHotKey(g_hwnd, HOTKEY_ID);
    g_tempMods = g_hkMods;
    g_tempVk   = g_hkVk;

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = HotkeyDlgProc;
        wc.hInstance = hInst;
        wc.hIcon = g_baseIcon;
        wc.hbrBackground = g_dlgBgBrush;
        wc.lpszClassName = L"FRHotkeyDlg";
        if (RegisterClassW(&wc)) {
            registered = true;
        }
    }

    int x = (GetSystemMetrics(SM_CXSCREEN) - 320) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - 140) / 2;
    hDlg = CreateWindowExW(
        WS_EX_TOPMOST,
        L"FRHotkeyDlg",
        L"Set Hotkey",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x, y, 320, 140,
        g_hwnd,
        nullptr,
        hInst,
        nullptr
    );
    if (hDlg) {
        ShowWindow(hDlg, SW_SHOW);
        SetForegroundWindow(hDlg);
    }
}

void SetDelayPreset(int newDelayMs) {
    g_delayMs = newDelayMs;
    SaveConfig();
}

void ToggleNotifications() {
    g_showNotifications = !g_showNotifications;
    SaveConfig();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (g_wmTaskbarCreated != 0 && msg == g_wmTaskbarCreated) {
        RestoreTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_HOTKEY:
        if (wParam == HOTKEY_ID) TriggerReset();
        return 0;

    case WM_RESET_DONE: {
        std::wstring* res = (std::wstring*)lParam;
        if (res) {
            g_lastStatus = *res;
            g_hasError = (wParam == 0);
            UpdateTrayState();
            ShowBalloon(*res);
            delete res;
        }
        g_isResetting = false;
        return 0;
    }

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK) {
            TriggerReset();
        } else if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            HMENU hMenu = CreatePopupMenu();
            HMENU hDelayMenu = CreatePopupMenu();

            AppendMenuW(hDelayMenu, MF_STRING | (g_delayMs == 800  ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_DELAY_08, L"0.8s (800 ms)");
            AppendMenuW(hDelayMenu, MF_STRING | (g_delayMs == 1200 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_DELAY_12, L"1.2s (1200 ms - Default)");
            AppendMenuW(hDelayMenu, MF_STRING | (g_delayMs == 1600 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_DELAY_16, L"1.6s (1600 ms)");
            AppendMenuW(hDelayMenu, MF_STRING | (g_delayMs == 2000 ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_DELAY_20, L"2.0s (2000 ms)");

            std::wstring resetLabel = L"Reset Sample Rate (" + FormatHotkey(g_hkMods, g_hkVk) + L")";
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESET, resetLabel.c_str());
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_HOTKEY, L"Change Hotkey...");
            AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hDelayMenu, L"Reset Delay");
            AppendMenuW(hMenu, MF_STRING | (g_showNotifications ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_NOTIF, L"Show Notifications");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, g_lastStatus.c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hDelayMenu);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_RESET:    TriggerReset(); break;
        case ID_TRAY_HOTKEY:   OpenHotkeyDialog(); break;
        case ID_TRAY_DELAY_08: SetDelayPreset(800); break;
        case ID_TRAY_DELAY_12: SetDelayPreset(1200); break;
        case ID_TRAY_DELAY_16: SetDelayPreset(1600); break;
        case ID_TRAY_DELAY_20: SetDelayPreset(2000); break;
        case ID_TRAY_NOTIF:    ToggleNotifications(); break;
        case ID_TRAY_EXIT:     DestroyWindow(hwnd); break;
        }
        return 0;

    case WM_DESTROY:
        UnregisterHotKey(hwnd, HOTKEY_ID);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Local\\FocusriteResetToolCppMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    LoadConfig();

    int cx = GetSystemMetrics(SM_CXSMICON);
    int cy = GetSystemMetrics(SM_CYSMICON);
    if (cx <= 0) cx = 16;
    if (cy <= 0) cy = 16;

    g_baseIcon = LoadAppIcon(hInst);
    g_errorIcon = CreateErrorBadgeIcon(g_baseIcon, cx, cy);
    g_dlgBgBrush = CreateSolidBrush(RGB(30, 30, 30));
    g_wmTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = g_baseIcon;
    wc.lpszClassName = L"FocusriteResetCppHiddenWnd";
    RegisterClassW(&wc);

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"FocusriteResetTool", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);

    g_nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = g_baseIcon;
    std::wstring tip = L"Focusrite Reset (" + FormatHotkey(g_hkMods, g_hkVk) + L")";
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    RegisterHotKey(g_hwnd, HOTKEY_ID, g_hkMods | MOD_NOREPEAT, g_hkVk);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_errorIcon) DestroyIcon(g_errorIcon);
    if (g_baseIcon) DestroyIcon(g_baseIcon);
    if (g_dlgBgBrush) DeleteObject(g_dlgBgBrush);
    if (hMutex) CloseHandle(hMutex);
    return 0;
}

