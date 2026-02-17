#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windows.h>
#include <windowsx.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "http_client.h"
#include "renderer.h"
#include "resource.h"
#include "subscription.h"

#pragma comment(lib, "dwmapi.lib")

// DWMWA_USE_IMMERSIVE_DARK_MODE: supported on Windows 10 20H1+ and Windows 11.
// The attribute value 20 is the official one (19 was used in earlier insider builds).
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

// System tray (notification area) constants
#define WM_TRAYICON (WM_USER + 100)
#define IDM_TRAY_SHOW 40001
#define IDM_TRAY_EXIT 40002
#define IDM_COMPACT_MODE 40003

#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// Enable Per-Monitor DPI Awareness for Windows 10 (1607+)
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT) - 4)
#endif

// Global log file
static std::ofstream g_logFile;
static bool g_debugMode = false;
static std::atomic<bool> g_shuttingDown {false};
static FILE* g_consoleOut = nullptr;

// Log rotation constants
static const char* g_logFilePath = "AISubscriptionsMonitor.log";
static const char* g_oldLogFilePath = "AISubscriptionsMonitor.log.old";
static const std::streamoff g_maxLogSize = 5 * 1024 * 1024;  // 5 MB limit

// Check and rotate log file if needed
static void CheckAndRotateLog()
{
    if (!g_logFile.is_open())
        return;

    // Get current file position (size)
    g_logFile.flush();
    auto currentPos = g_logFile.tellp();
    if (currentPos < g_maxLogSize)
        return;

    // Close current log
    g_logFile.close();

    // Remove old backup if exists
    DeleteFileA(g_oldLogFilePath);

    // Rename current log to backup
    MoveFileA(g_logFilePath, g_oldLogFilePath);

    // Reopen new log file
    g_logFile.open(g_logFilePath, std::ios::out | std::ios::app);
    if (g_logFile.is_open()) {
        g_logFile << "[";
        char timeStr[64];
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        g_logFile << timeStr;
        g_logFile << "] Log rotated (previous log saved to .log.old)" << std::endl;
    }
}

// Mutex protecting shared state written by the background HTTP thread
// and read by the UI thread (subscriptions, lastError, contentHeight).
static std::mutex g_dataMutex;

// Background refresh thread handle (joined before shutdown)
static std::thread g_refreshThread;

// Logging function - outputs to console and file
void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

    char logLine[8192];
    snprintf(logLine, sizeof(logLine), "[%s] %s", timeStr, buffer);

    if (g_debugMode && g_consoleOut) {
        fprintf(g_consoleOut, "%s\n", logLine);
        fflush(g_consoleOut);
    }

    if (g_logFile.is_open()) {
        g_logFile << logLine << std::endl;
        g_logFile.flush();
        CheckAndRotateLog();
    }
}

// Initialize logging system
bool InitLogging(bool debugMode)
{
    g_debugMode = debugMode;

    g_logFile.open(g_logFilePath, std::ios::out | std::ios::app);
    if (!g_logFile.is_open()) {
        return false;
    }

    if (debugMode) {
        AllocConsole();
        freopen_s(&g_consoleOut, "CONOUT$", "w", stdout);
        FILE* dummyErr = nullptr;
        FILE* dummyIn = nullptr;
        freopen_s(&dummyErr, "CONOUT$", "w", stderr);
        freopen_s(&dummyIn, "CONIN$", "r", stdin);

        Log("========================================");
        Log("Debug Console Started");
        Log("========================================");
    }

    return true;
}

void CloseLogging()
{
    if (g_logFile.is_open()) {
        Log("========================================");
        Log("Application shutting down");
        Log("========================================");
        g_logFile.close();
    }
}

void CrashHandler(int signal)
{
    Log("FATAL ERROR: Signal %d caught", signal);
    Log("Application crashed!");
    CloseLogging();
    _exit(1);  // Use _exit() instead of exit() — safe from signal handlers
}

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    Log("FATAL ERROR: Exception code 0x%08X", pExceptionInfo->ExceptionRecord->ExceptionCode);
    Log("Exception at address: 0x%p", pExceptionInfo->ExceptionRecord->ExceptionAddress);
    CloseLogging();
    return EXCEPTION_EXECUTE_HANDLER;
}

void SetupCrashHandlers()
{
    SetUnhandledExceptionFilter(ExceptionHandler);
    signal(SIGSEGV, CrashHandler);
    signal(SIGABRT, CrashHandler);
    signal(SIGFPE, CrashHandler);
    signal(SIGILL, CrashHandler);
}

const wchar_t kClassName[] = L"AISubscriptionsMonitor";
const wchar_t kWindowTitle[] = L"AI Subscriptions Monitor";
const int kMinWindowWidth = 620;           // Minimum window width (normal)
const int kMinWindowHeight = 400;          // Minimum window height (normal)
const int kMinWindowWidthCompact = 240;    // Minimum window width (compact)
const int kMinWindowHeightCompact = 120;   // Minimum window height (compact)
const int kWindowWidth = kMinWindowWidth;  // Initial window width
const int kWindowHeight = 600;             // Initial window height
const int kRefreshIntervalMs = 60000;

// ---------------------------------------------------------------------------
// Window state persistence (%LOCALAPPDATA%\AISubscriptionsMonitor\settings.txt)
// ---------------------------------------------------------------------------

struct SavedSettings
{
    // Window geometry
    int x, y, w, h;
    bool pinned;
    bool compact;
    bool windowValid;  // true if x/y/w/h loaded successfully

    // Theme preference
    std::string theme;  // "light", "dark", or "" (system)

    // API URL (empty if not saved)
    std::string apiUrl;
};

// Build the full path to the settings file
static std::wstring GetSettingsPath()
{
    wchar_t* localAppData = nullptr;
    // SHGetKnownFolderPath requires <shlobj.h> -- included via <windows.h> with WIN32_LEAN_AND_MEAN off
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData)) || !localAppData) {
        return std::wstring();
    }
    std::wstring dir = std::wstring(localAppData) + L"\\AISubscriptionsMonitor";
    CoTaskMemFree(localAppData);

    // Ensure directory exists
    CreateDirectoryW(dir.c_str(), nullptr);  // OK if already exists

    return dir + L"\\settings.txt";
}

static void SaveSettings(HWND hwnd, bool pinned, bool compact, ThemeMode themeMode, const std::string& apiUrl)
{
    WINDOWPLACEMENT wp = {sizeof(wp)};
    if (!GetWindowPlacement(hwnd, &wp))
        return;

    RECT rc = wp.rcNormalPosition;

    std::wstring path = GetSettingsPath();
    if (path.empty())
        return;

    std::ofstream f(path);
    if (!f.is_open())
        return;

    f << "x=" << rc.left << "\n";
    f << "y=" << rc.top << "\n";
    f << "w=" << (rc.right - rc.left) << "\n";
    f << "h=" << (rc.bottom - rc.top) << "\n";
    f << "pinned=" << (pinned ? 1 : 0) << "\n";
    f << "compact=" << (compact ? 1 : 0) << "\n";
    switch (themeMode) {
    case ThemeMode::Light:
        f << "theme=light\n";
        break;
    case ThemeMode::Dark:
        f << "theme=dark\n";
        break;
    default:
        f << "theme=system\n";
        break;
    }
    if (!apiUrl.empty()) {
        f << "api_url=" << apiUrl << "\n";
    }
}

static SavedSettings LoadSettings()
{
    SavedSettings ss = {};
    ss.windowValid = false;

    std::wstring path = GetSettingsPath();
    if (path.empty())
        return ss;

    std::ifstream f(path);
    if (!f.is_open())
        return ss;

    std::string line;
    bool gotX = false, gotY = false, gotW = false, gotH = false;
    while (std::getline(f, line)) {
        if (line.empty())
            continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        try {
            if (key == "x") {
                ss.x = std::stoi(val);
                gotX = true;
            }
            else if (key == "y") {
                ss.y = std::stoi(val);
                gotY = true;
            }
            else if (key == "w") {
                ss.w = std::stoi(val);
                gotW = true;
            }
            else if (key == "h") {
                ss.h = std::stoi(val);
                gotH = true;
            }
            else if (key == "pinned") {
                ss.pinned = (std::stoi(val) != 0);
            }
            else if (key == "compact") {
                ss.compact = (std::stoi(val) != 0);
            }
            else if (key == "theme") {
                ss.theme = val;
            }
            else if (key == "api_url") {
                ss.apiUrl = val;
            }
        }
        catch (const std::exception&) {
            // Ignore corrupted settings entries (e.g. non-numeric values)
        }
    }

    if (gotX && gotY && gotW && gotH && ss.w > 0 && ss.h > 0) {
        int minW = ss.compact ? kMinWindowWidthCompact : kMinWindowWidth;
        int minH = ss.compact ? kMinWindowHeightCompact : kMinWindowHeight;
        if (ss.w < minW)
            ss.w = minW;
        if (ss.h < minH)
            ss.h = minH;

        RECT testRect = {ss.x, ss.y, ss.x + ss.w, ss.y + ss.h};
        HMONITOR hMon = MonitorFromRect(&testRect, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        if (GetMonitorInfo(hMon, &mi)) {
            RECT& work = mi.rcWork;
            if (ss.x + ss.w > work.right)
                ss.x = work.right - ss.w;
            if (ss.y + ss.h > work.bottom)
                ss.y = work.bottom - ss.h;
            if (ss.x < work.left)
                ss.x = work.left;
            if (ss.y < work.top)
                ss.y = work.top;
            if (ss.w > work.right - work.left)
                ss.w = work.right - work.left;
            if (ss.h > work.bottom - work.top)
                ss.h = work.bottom - work.top;
        }
        ss.windowValid = true;
    }

    return ss;
}

// ---------------------------------------------------------------------------
// Application state
// ---------------------------------------------------------------------------

struct AppState
{
    std::vector<Subscription> subscriptions;
    std::unique_ptr<ProgressBarRenderer> renderer;
    std::unique_ptr<HttpClient> httpClient;
    std::wstring apiHost;
    std::wstring apiPath;
    int apiPort;
    bool apiIsHttps;
    std::string apiUrl;  // Original URL string (for saving to config)
    std::atomic<bool> isLoading;
    std::string lastError;
    HWND hwnd;
    bool debugMode;

    // Theme
    ThemeMode themeMode;

    // Scroll
    int scrollOffset;   // current vertical scroll position (pixels)
    int contentHeight;  // total content height (pixels)

    // Background brush (kept in sync with theme for WM_ERASEBKGND)
    HBRUSH hBgBrush;

    // Whether initial window resize (to fit content) has been done
    bool initialResizeDone;

    // Custom scrollbar drag state
    bool scrollDragging;      // true while thumb is being dragged
    int dragStartMouseY;      // mouse Y at drag start
    int dragStartOffset;      // scrollOffset at drag start
    bool scrollThumbHovered;  // true when mouse is over the thumb

    // Window drag state
    bool windowDragging;   // true while window is being dragged
    int dragStartMouseX;   // mouse X at drag start (client coords)
    int dragStartMouseY2;  // mouse Y at drag start (client coords)

    // Pin (always-on-top)
    bool isPinned;

    // Saved normal-mode window rect (for restoring when leaving compact)
    RECT savedNormalRect;
    bool hasSavedNormalRect;

    // System tray (notification area)
    NOTIFYICONDATAW nid;
    bool trayIconCreated;

    AppState()
        : apiPort(80), apiIsHttps(false), isLoading(false), hwnd(nullptr), debugMode(false),
          themeMode(ThemeMode::System), scrollOffset(0), contentHeight(0), hBgBrush(nullptr), initialResizeDone(false),
          scrollDragging(false), dragStartMouseY(0), dragStartOffset(0), scrollThumbHovered(false),
          windowDragging(false), dragStartMouseX(0), dragStartMouseY2(0), isPinned(false), savedNormalRect {},
          hasSavedNormalRect(false), nid {}, trayIconCreated(false)
    {
        renderer = std::make_unique<ProgressBarRenderer>();
        httpClient = std::make_unique<HttpClient>();
    }

    ~AppState()
    {
        if (hBgBrush)
            DeleteObject(hBgBrush);
    }
};

static AppState* g_app = nullptr;

// Forward declarations
static void UpdateTrayIcon();

// ---------------------------------------------------------------------------
// Theme helpers
// ---------------------------------------------------------------------------

// Apply the resolved theme to the renderer and repaint
static void ApplyTheme()
{
    if (!g_app)
        return;

    bool dark = IsDarkModeActive(g_app->themeMode);
    const ColorScheme& scheme = GetColorScheme(dark);
    g_app->renderer->SetColorScheme(scheme);

    // Recreate the background brush used by window class
    if (g_app->hBgBrush)
        DeleteObject(g_app->hBgBrush);
    g_app->hBgBrush = CreateSolidBrush(scheme.bgColor);

    // Update window class brush so resize flicker matches the theme
    if (g_app->hwnd) {
        SetClassLongPtrW(g_app->hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)g_app->hBgBrush);

        // Set title bar to dark/light mode (Windows 10 20H1+ / Windows 11)
        BOOL useDark = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(g_app->hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

        // Switch app icon to match theme: light icon (dark shapes) for light
        // mode, dark icon (light shapes) for dark mode
        HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(g_app->hwnd, GWLP_HINSTANCE);
        int iconRes = dark ? IDI_APPICON : IDI_APPICON_LIGHT;
        HICON hIcon = LoadIcon(hInst, MAKEINTRESOURCE(iconRes));
        if (hIcon) {
            SendMessage(g_app->hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessage(g_app->hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }

        // Update tray icon to match theme
        UpdateTrayIcon();
    }

    Log("Theme applied: %s", dark ? "dark" : "light");
}

// ---------------------------------------------------------------------------
// System tray (notification area) helpers
// ---------------------------------------------------------------------------

static void CreateTrayIcon(HWND hwnd)
{
    if (!g_app)
        return;

    bool dark = IsDarkModeActive(g_app->themeMode);
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    int iconRes = dark ? IDI_APPICON : IDI_APPICON_LIGHT;

    ZeroMemory(&g_app->nid, sizeof(g_app->nid));
    g_app->nid.cbSize = sizeof(NOTIFYICONDATAW);
    g_app->nid.hWnd = hwnd;
    g_app->nid.uID = 1;
    g_app->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_app->nid.uCallbackMessage = WM_TRAYICON;
    g_app->nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(iconRes));
    wcscpy_s(g_app->nid.szTip, L"AI Subscriptions Monitor");

    Shell_NotifyIconW(NIM_ADD, &g_app->nid);
    g_app->trayIconCreated = true;
    Log("Tray icon created");
}

static void RemoveTrayIcon()
{
    if (!g_app || !g_app->trayIconCreated)
        return;
    Shell_NotifyIconW(NIM_DELETE, &g_app->nid);
    g_app->trayIconCreated = false;
    Log("Tray icon removed");
}

static void UpdateTrayIcon()
{
    if (!g_app || !g_app->trayIconCreated)
        return;

    bool dark = IsDarkModeActive(g_app->themeMode);
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(g_app->hwnd, GWLP_HINSTANCE);
    int iconRes = dark ? IDI_APPICON : IDI_APPICON_LIGHT;

    g_app->nid.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(iconRes));
    g_app->nid.uFlags = NIF_ICON;
    Shell_NotifyIconW(NIM_MODIFY, &g_app->nid);
    Log("Tray icon updated for %s theme", dark ? "dark" : "light");
}

static void ShowAppWindow(HWND hwnd)
{
    // Ensure the window has no taskbar button: keep WS_EX_TOOLWINDOW
    // while adding back visibility.
    ShowWindow(hwnd, SW_SHOW);
    ShowWindow(hwnd, SW_RESTORE);  // in case it was minimised
    SetForegroundWindow(hwnd);
    Log("Window shown from tray");
}

static void HideAppWindow(HWND hwnd)
{
    if (g_app) {
        // Save settings before hiding so geometry is persisted
        SaveSettings(hwnd, g_app->isPinned, g_app->renderer->IsCompact(), g_app->themeMode, g_app->apiUrl);
    }
    ShowWindow(hwnd, SW_HIDE);
    Log("Window hidden to tray");
}

static void ShowTrayContextMenu(HWND hwnd)
{
    POINT pt;
    GetCursorPos(&pt);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu)
        return;

    bool compact = g_app ? g_app->renderer->IsCompact() : false;
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, L"Show Window");
    AppendMenuW(hMenu, MF_STRING | (compact ? MF_CHECKED : MF_UNCHECKED), IDM_COMPACT_MODE, L"Compact Mode");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

    // Required for the menu to disappear when clicking outside
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    // Per MSDN: send a benign message to force the menu to close properly
    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

// ---------------------------------------------------------------------------
// Scroll helpers
// ---------------------------------------------------------------------------

static void UpdateScrollInfo(HWND hwnd)
{
    if (!g_app)
        return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int clientHeight = rc.bottom - rc.top;

    if (g_app->contentHeight <= clientHeight) {
        // Content fits -- reset offset
        g_app->scrollOffset = 0;
    }
    else {
        // Clamp scroll offset to valid range
        int maxScroll = g_app->contentHeight - clientHeight;
        if (g_app->scrollOffset > maxScroll)
            g_app->scrollOffset = maxScroll;
        if (g_app->scrollOffset < 0)
            g_app->scrollOffset = 0;
    }
}

static void ScrollTo(HWND hwnd, int pos)
{
    if (!g_app)
        return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    int clientHeight = rc.bottom - rc.top;
    int maxScroll = g_app->contentHeight - clientHeight;
    if (maxScroll < 0)
        maxScroll = 0;

    pos = max(0, min(pos, maxScroll));
    if (pos != g_app->scrollOffset) {
        g_app->scrollOffset = pos;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

// ---------------------------------------------------------------------------
// String conversion helpers (needed by URL parsing and RefreshData)
// ---------------------------------------------------------------------------

static std::string WstrToStr(const std::wstring& ws)
{
    if (ws.empty())
        return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), (int)ws.size(), &s[0], len, nullptr, nullptr);
    return s;
}

static std::wstring StrToWstr(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring ws(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &ws[0], len);
    return ws;
}

// ---------------------------------------------------------------------------
// URL parsing (needed by RefreshData)
// ---------------------------------------------------------------------------

struct ParsedUrl
{
    std::wstring host;
    std::wstring path;
    int port;
    bool isHttps;
};

static ParsedUrl ParseUrl(const std::wstring& url)
{
    ParsedUrl result;
    result.port = 80;
    result.path = L"/";
    result.isHttps = false;

    std::wstring urlToParse = url;

    if (urlToParse.find(L"https://") == 0) {
        result.isHttps = true;
        result.port = 443;
        urlToParse = urlToParse.substr(8);
    }
    else if (urlToParse.find(L"http://") == 0) {
        urlToParse = urlToParse.substr(7);
    }

    size_t pathPos = urlToParse.find(L'/');
    if (pathPos != std::wstring::npos) {
        result.path = urlToParse.substr(pathPos);
        urlToParse = urlToParse.substr(0, pathPos);
    }

    size_t portPos = urlToParse.find(L':');
    if (portPos != std::wstring::npos) {
        result.host = urlToParse.substr(0, portPos);
        result.port = _wtoi(urlToParse.substr(portPos + 1).c_str());
    }
    else {
        result.host = urlToParse;
    }

    return result;
}

// ---------------------------------------------------------------------------
// Data refresh
// ---------------------------------------------------------------------------

void RefreshData()
{
    if (!g_app || g_app->isLoading.load())
        return;

    // Join previous refresh thread if it finished but hasn't been joined yet
    if (g_refreshThread.joinable()) {
        g_refreshThread.join();
    }

    Log("Refreshing data...");
    g_app->isLoading.store(true);
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        g_app->lastError.clear();
    }
    InvalidateRect(g_app->hwnd, nullptr, TRUE);

    // Capture values needed by the thread (avoid accessing g_app members from bg thread
    // except through the mutex-protected fields).
    std::wstring host = g_app->apiHost;
    std::wstring path = g_app->apiPath;
    int port = g_app->apiPort;
    bool isHttps = g_app->apiIsHttps;
    HttpClient* client = g_app->httpClient.get();

    g_refreshThread = std::thread([host, path, port, isHttps, client]() {
        if (g_shuttingDown.load())
            return;

        bool success = false;
        Log("Sending HTTP request to %ls:%d%ls", host.c_str(), port, path.c_str());

        std::vector<Subscription> newSubs;
        std::string newError;
        int newContentHeight = 0;

        try {
            std::string response = client->GetSync(host, path, port, isHttps, success);

            Log("HTTP request completed. Success: %s", success ? "true" : "false");
            Log("Response size: %zu bytes", response.size());

            if (success) {
                try {
                    Log("Parsing subscriptions...");
                    newSubs = ParseSubscriptions(response);
                    Log("Successfully parsed %zu subscriptions", newSubs.size());
                }
                catch (const std::exception& e) {
                    Log("Failed to parse response: %s", e.what());
                    newError = e.what();
                }
            }
            else {
                Log("Failed to fetch data from server");
                newError = "Failed to fetch data from server";
            }
        }
        catch (const std::exception& e) {
            Log("Exception in HTTP thread: %s", e.what());
            newError = e.what();
        }

        if (g_shuttingDown.load())
            return;

        // Safely publish results to shared state under the mutex
        {
            std::lock_guard<std::mutex> lock(g_dataMutex);
            if (g_app) {
                g_app->subscriptions = std::move(newSubs);
                g_app->lastError = std::move(newError);
                if (!g_app->subscriptions.empty()) {
                    g_app->contentHeight = g_app->renderer->CalculateContentHeight(g_app->subscriptions);
                }
                else {
                    g_app->contentHeight = 0;
                }
            }
        }

        if (g_app) {
            g_app->isLoading.store(false);
            if (g_app->hwnd && !g_shuttingDown.load()) {
                PostMessage(g_app->hwnd, WM_USER + 1, 0, 0);  // custom "data ready" message
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Paint
// ---------------------------------------------------------------------------

void OnPaint(HWND hwnd)
{
    if (!g_app) {
        Log("ERROR: OnPaint called with null g_app");
        return;
    }

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    if (g_debugMode)
        Log("OnPaint: Window size %dx%d", width, height);

    if (width <= 0 || height <= 0) {
        if (g_debugMode)
            Log("Window size is zero or negative, skipping paint");
        EndPaint(hwnd, &ps);
        return;
    }

    // Create memory DC and bitmap for double buffering
    HDC hdcMem = CreateCompatibleDC(hdc);
    if (!hdcMem) {
        Log("ERROR: Failed to create compatible DC");
        EndPaint(hwnd, &ps);
        return;
    }

    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
    if (!hbmMem) {
        Log("ERROR: Failed to create compatible bitmap (%dx%d)", width, height);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return;
    }

    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

    // Get current color scheme
    const ColorScheme& colors = g_app->renderer->GetActiveScheme();

    // Fill background
    HBRUSH hBgBrush = CreateSolidBrush(colors.bgColor);
    FillRect(hdcMem, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    // Take a snapshot of shared state under the data mutex to avoid
    // data races with the background HTTP thread.
    bool loading = g_app->isLoading.load();
    std::string lastErrorCopy;
    std::vector<Subscription> subsCopy;
    int scrollOff = 0;
    int contentH = 0;
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        lastErrorCopy = g_app->lastError;
        subsCopy = g_app->subscriptions;
        scrollOff = g_app->scrollOffset;
        contentH = g_app->contentHeight;
    }

    try {
        g_app->renderer->SetWindowSize(width, height);

        if (loading) {
            if (g_debugMode)
                Log("Rendering: Loading state");

            HFONT hFont = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
            HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

            RECT textRect = {0, height / 2 - 20, width, height / 2 + 20};
            SetTextColor(hdcMem, colors.loadingTextColor);
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, L"Loading...", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdcMem, hOldFont);
            DeleteObject(hFont);
        }
        else if (!lastErrorCopy.empty()) {
            if (g_debugMode)
                Log("Rendering: Error state - %s", lastErrorCopy.c_str());

            HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
            HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

            RECT textRect = {20, height / 2 - 40, width - 20, height / 2 + 40};
            SetTextColor(hdcMem, colors.errorTextColor);
            SetBkMode(hdcMem, TRANSPARENT);

            std::wstring errorW = StrToWstr(lastErrorCopy);
            DrawTextW(hdcMem, errorW.c_str(), -1, &textRect, DT_CENTER | DT_WORDBREAK);

            SelectObject(hdcMem, hOldFont);
            DeleteObject(hFont);
        }
        else if (!subsCopy.empty()) {
            if (g_debugMode)
                Log("Rendering: %zu subscriptions", subsCopy.size());
            g_app->renderer->Render(hdcMem, subsCopy, scrollOff);
            if (g_debugMode)
                Log("Render completed");
        }
        else {
            if (g_debugMode)
                Log("Rendering: No data available");

            HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
            HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

            RECT textRect = {0, height / 2 - 20, width, height / 2 + 20};
            SetTextColor(hdcMem, colors.noDataTextColor);
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, L"No data available", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            SelectObject(hdcMem, hOldFont);
            DeleteObject(hFont);
        }
    }
    catch (const std::exception& e) {
        Log("EXCEPTION in OnPaint: %s", e.what());
    }

    // Draw custom scrollbar on top of content (only when content overflows)
    if (contentH > height) {
        bool thumbActive = g_app->scrollThumbHovered || g_app->scrollDragging;
        g_app->renderer->DrawScrollbar(hdcMem, width, height, contentH, scrollOff, thumbActive);
    }

    // Copy to screen
    BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

    // Cleanup
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);

    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void OnSize(HWND hwnd, UINT state, int cx, int cy)
{
    Log("OnSize: state=%d, cx=%d, cy=%d", state, cx, cy);

    if (state == SIZE_MINIMIZED) {
        Log("Window minimized, skipping size update");
        return;
    }

    if (g_app && g_app->renderer) {
        g_app->renderer->SetWindowSize(cx, cy);
        UpdateScrollInfo(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

// ---------------------------------------------------------------------------
// Pin to top (always-on-top)
// ---------------------------------------------------------------------------

static const UINT IDM_PIN_TO_TOP = 0x0010;  // Custom system menu command ID

static void UpdateWindowTitle(HWND hwnd)
{
    if (!g_app)
        return;
    if (g_app->isPinned)
        SetWindowTextW(hwnd, L"\U0001F4CC AI Subscriptions Monitor");
    else
        SetWindowTextW(hwnd, kWindowTitle);
}

static void TogglePin(HWND hwnd)
{
    if (!g_app)
        return;
    g_app->isPinned = !g_app->isPinned;

    // Update always-on-top
    SetWindowPos(hwnd, g_app->isPinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // Update system menu check mark
    HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
    if (hSysMenu) {
        CheckMenuItem(hSysMenu, IDM_PIN_TO_TOP, MF_BYCOMMAND | (g_app->isPinned ? MF_CHECKED : MF_UNCHECKED));
    }

    // Update title to show pin indicator
    UpdateWindowTitle(hwnd);
    Log("Pin toggled: %s", g_app->isPinned ? "on" : "off");
}

// ---------------------------------------------------------------------------
// Compact mode helpers
// ---------------------------------------------------------------------------

// Resize the window to exactly fit content (compact mode only).
// Also disables / re-enables resizing and saves/restores normal-mode geometry.
static void ApplyCompactLayout(HWND hwnd)
{
    if (!g_app || !g_app->renderer)
        return;
    bool compact = g_app->renderer->IsCompact();

    // Toggle resizable frame
    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    if (compact)
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    else
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    SetWindowLong(hwnd, GWL_STYLE, style);

    // Recalculate content (under lock since subscriptions may be written by bg thread)
    {
        std::lock_guard<std::mutex> lock(g_dataMutex);
        g_app->contentHeight = g_app->renderer->CalculateContentHeight(g_app->subscriptions);
    }
    g_app->scrollOffset = 0;

    if (compact && !g_app->subscriptions.empty()) {
        // Save current normal-mode window rect before resizing
        if (!g_app->hasSavedNormalRect) {
            GetWindowRect(hwnd, &g_app->savedNormalRect);
            g_app->hasSavedNormalRect = true;
            Log("Saved normal rect: %d,%d %dx%d", g_app->savedNormalRect.left, g_app->savedNormalRect.top,
                g_app->savedNormalRect.right - g_app->savedNormalRect.left,
                g_app->savedNormalRect.bottom - g_app->savedNormalRect.top);
        }

        int contentW = kMinWindowWidthCompact;
        int contentH = g_app->contentHeight;

        // Convert client size to window size
        DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);
        DWORD dwExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        RECT rc = {0, 0, contentW, contentH};
        AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;

        // Cap to screen work area
        HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {sizeof(mi)};
        GetMonitorInfo(hMon, &mi);
        int maxW = mi.rcWork.right - mi.rcWork.left;
        int maxH = mi.rcWork.bottom - mi.rcWork.top;
        if (winW > maxW)
            winW = maxW;
        if (winH > maxH)
            winH = maxH;

        SetWindowPos(hwnd, nullptr, 0, 0, winW, winH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        Log("Compact auto-resize: client=%dx%d window=%dx%d", contentW, contentH, winW, winH);
    }
    else if (!compact) {
        // Leaving compact — restore saved normal-mode window rect
        if (g_app->hasSavedNormalRect) {
            RECT& r = g_app->savedNormalRect;
            SetWindowPos(hwnd, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            g_app->hasSavedNormalRect = false;
            Log("Restored normal rect: %d,%d %dx%d", r.left, r.top, r.right - r.left, r.bottom - r.top);
        }
        else {
            SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
    }
    else if (compact) {
        // Compact but no data yet — apply minimum compact size
        int contentW = kMinWindowWidthCompact;
        int contentH = kMinWindowHeightCompact;  // Use minimum compact height

        DWORD dwStyle = GetWindowLong(hwnd, GWL_STYLE);
        DWORD dwExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
        RECT rc = {0, 0, contentW, contentH};
        AdjustWindowRectEx(&rc, dwStyle, FALSE, dwExStyle);
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;

        SetWindowPos(hwnd, nullptr, 0, 0, winW, winH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        Log("Compact auto-resize (no data): client=%dx%d window=%dx%d", contentW, contentH, winW, winH);
    }

    UpdateScrollInfo(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
}

static void ToggleCompact(HWND hwnd)
{
    if (!g_app || !g_app->renderer)
        return;

    bool compact = !g_app->renderer->IsCompact();
    g_app->renderer->SetCompact(compact);

    // Update system menu check mark
    HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
    if (hSysMenu) {
        CheckMenuItem(hSysMenu, IDM_COMPACT_MODE, MF_BYCOMMAND | (compact ? MF_CHECKED : MF_UNCHECKED));
    }

    ApplyCompactLayout(hwnd);
    Log("Compact mode toggled: %s", compact ? "on" : "off");
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    try {
        switch (msg) {
        case WM_CREATE:
            Log("WM_CREATE received");
            if (g_app) {
                g_app->hwnd = hwnd;
                ApplyTheme();

                // Add custom items to system menu
                HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
                if (hSysMenu) {
                    AppendMenuW(hSysMenu, MF_SEPARATOR, 0, nullptr);
                    AppendMenuW(hSysMenu, MF_STRING, IDM_PIN_TO_TOP, L"Pin to Top\tCtrl+T");
                    AppendMenuW(hSysMenu, MF_STRING, IDM_COMPACT_MODE, L"Compact Mode");
                }

                // Create system tray icon
                CreateTrayIcon(hwnd);

                SetTimer(hwnd, 1, kRefreshIntervalMs, nullptr);
                RefreshData();
            }
            return 0;

        case WM_PAINT:
            OnPaint(hwnd);
            return 0;

        case WM_ERASEBKGND:
            // Handled in OnPaint double-buffer; suppress default erase
            return 1;

        case WM_SIZE:
            OnSize(hwnd, (UINT)wParam, LOWORD(lParam), HIWORD(lParam));
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO* mmi = (MINMAXINFO*)lParam;
            bool compact = g_app && g_app->renderer && g_app->renderer->IsCompact();
            if (compact) {
                // In compact mode: set minimum size to compact minimum,
                // but don't set maximum - allow SetWindowPos to resize
                mmi->ptMinTrackSize.x = kMinWindowWidthCompact;
                mmi->ptMinTrackSize.y = kMinWindowHeightCompact;
            }
            else {
                mmi->ptMinTrackSize.x = kMinWindowWidth;
                mmi->ptMinTrackSize.y = kMinWindowHeight;
            }
            return 0;
        }

        case WM_DPICHANGED: {
            UINT newDpi = HIWORD(wParam);
            Log("WM_DPICHANGED: New DPI = %d", newDpi);

            if (g_app && g_app->renderer) {
                g_app->renderer->OnDpiChanged(newDpi);
            }

            RECT* const prcNewWindow = (RECT*)lParam;
            SetWindowPos(hwnd, nullptr, prcNewWindow->left, prcNewWindow->top, prcNewWindow->right - prcNewWindow->left,
                         prcNewWindow->bottom - prcNewWindow->top, SWP_NOZORDER | SWP_NOACTIVATE);

            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        // ----- Custom scrollbar interaction -----
        case WM_LBUTTONDOWN: {
            if (!g_app)
                return DefWindowProc(hwnd, msg, wParam, lParam);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            int cw = rc.right - rc.left;
            int ch = rc.bottom - rc.top;

            ScrollHitZone zone =
                g_app->renderer->HitTestScrollbar(mx, my, cw, ch, g_app->contentHeight, g_app->scrollOffset);

            if (zone == ScrollHitZone::Thumb) {
                // Start thumb drag
                g_app->scrollDragging = true;
                g_app->dragStartMouseY = my;
                g_app->dragStartOffset = g_app->scrollOffset;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            else if (zone == ScrollHitZone::Track) {
                // Page up/down: jump towards click position
                int pageAmount = ch;
                int thumbH = (std::max)(kScrollbarMinThumb,
                                        static_cast<int>((static_cast<long long>(ch) * ch) / g_app->contentHeight));
                int trackRange = ch - thumbH;
                int maxScroll = g_app->contentHeight - ch;
                int currentThumbY =
                    (maxScroll > 0) ? (int)((long long)g_app->scrollOffset * trackRange / maxScroll) : 0;
                int thumbMid = currentThumbY + thumbH / 2;

                if (my < thumbMid)
                    ScrollTo(hwnd, g_app->scrollOffset - pageAmount);
                else
                    ScrollTo(hwnd, g_app->scrollOffset + pageAmount);
            }
            else {
                // Start window drag (click on non-scrollbar area)
                g_app->windowDragging = true;
                g_app->dragStartMouseX = mx;
                g_app->dragStartMouseY2 = my;
                SetCapture(hwnd);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_app) {
                if (g_app->scrollDragging) {
                    g_app->scrollDragging = false;
                    ReleaseCapture();
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                if (g_app->windowDragging) {
                    g_app->windowDragging = false;
                    ReleaseCapture();
                }
            }
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!g_app)
                return DefWindowProc(hwnd, msg, wParam, lParam);
            int mx = GET_X_LPARAM(lParam);
            int my = GET_Y_LPARAM(lParam);

            if (g_app->scrollDragging) {
                // Drag thumb
                RECT rc;
                GetClientRect(hwnd, &rc);
                int ch = rc.bottom - rc.top;
                int newOffset = g_app->renderer->ScrollOffsetFromThumbDrag(
                    my, g_app->dragStartMouseY, g_app->dragStartOffset, ch, g_app->contentHeight);
                ScrollTo(hwnd, newOffset);
            }
            else if (g_app->windowDragging) {
                // Drag window
                int dx = mx - g_app->dragStartMouseX;
                int dy = my - g_app->dragStartMouseY2;

                // Get current window position
                RECT windowRect;
                GetWindowRect(hwnd, &windowRect);

                // Move window
                SetWindowPos(hwnd, nullptr, windowRect.left + dx, windowRect.top + dy, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            else {
                // Track hover for thumb highlight
                RECT rc;
                GetClientRect(hwnd, &rc);
                int cw = rc.right - rc.left;
                int ch = rc.bottom - rc.top;

                ScrollHitZone zone =
                    g_app->renderer->HitTestScrollbar(mx, my, cw, ch, g_app->contentHeight, g_app->scrollOffset);
                bool wasHovered = g_app->scrollThumbHovered;
                g_app->scrollThumbHovered = (zone == ScrollHitZone::Thumb);

                if (wasHovered != g_app->scrollThumbHovered) {
                    // Only repaint the scrollbar region
                    RECT sbRect = g_app->renderer->GetScrollbarRect(cw, ch);
                    InvalidateRect(hwnd, &sbRect, FALSE);
                }
            }
            return 0;
        }

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);  // positive = scroll up
            int lineHeight = 40;
            int lines = 3;  // scroll 3 lines per notch
            ScrollTo(hwnd, g_app->scrollOffset - (delta / WHEEL_DELTA) * lineHeight * lines);
            return 0;
        }

        // ----- System theme change -----
        case WM_SETTINGCHANGE: {
            // Windows broadcasts this when personalisation settings change.
            // lParam may be a string pointer or a numeric value; guard with
            // IsBadStringPtrW to avoid crashing on non-pointer values.
            if (lParam) {
                LPCWSTR setting = reinterpret_cast<LPCWSTR>(lParam);
                if (!IsBadStringPtrW(setting, 64) && wcscmp(setting, L"ImmersiveColorSet") == 0) {
                    if (g_app && g_app->themeMode == ThemeMode::System) {
                        Log("System theme changed, re-applying theme");
                        ApplyTheme();
                        InvalidateRect(hwnd, nullptr, TRUE);
                    }
                }
            }
            return 0;
        }

        // ----- Custom: data ready from background thread -----
        case WM_USER + 1: {
            bool compact = g_app && g_app->renderer && g_app->renderer->IsCompact();
            if (compact) {
                // Compact mode: always auto-resize to fit content
                ApplyCompactLayout(hwnd);
            }
            else if (g_app && !g_app->initialResizeDone && g_app->contentHeight > 0) {
                g_app->initialResizeDone = true;

                // Calculate required window size from desired client area
                RECT desired = {0, 0, 0, g_app->contentHeight};
                // Keep current window width; only adjust height
                RECT winRect;
                GetWindowRect(hwnd, &winRect);
                int curWidth = winRect.right - winRect.left;
                desired.right = curWidth;  // placeholder, we only care about height
                AdjustWindowRectEx(&desired, GetWindowLong(hwnd, GWL_STYLE), FALSE, GetWindowLong(hwnd, GWL_EXSTYLE));
                int newHeight = desired.bottom - desired.top;

                // Cap to screen work area so the window never exceeds the display
                HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi = {sizeof(mi)};
                GetMonitorInfo(hMon, &mi);
                int maxHeight = mi.rcWork.bottom - mi.rcWork.top;
                if (newHeight > maxHeight)
                    newHeight = maxHeight;

                SetWindowPos(hwnd, nullptr, 0, 0, curWidth, newHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                Log("Initial resize: contentHeight=%d, windowHeight=%d", g_app->contentHeight, newHeight);
            }
            UpdateScrollInfo(hwnd);
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }

        case WM_TIMER:
            if (wParam == 1) {
                Log("Timer triggered, refreshing data");
                RefreshData();
            }
            return 0;

        case WM_SYSCOMMAND: {
            UINT cmd = wParam & 0xFFF0;
            if (cmd == IDM_PIN_TO_TOP) {
                TogglePin(hwnd);
                return 0;
            }
            if (cmd == IDM_COMPACT_MODE) {
                ToggleCompact(hwnd);
                return 0;
            }
            if (cmd == SC_MINIMIZE) {
                // Minimize -> hide to tray instead of taskbar
                HideAppWindow(hwnd);
                return 0;
            }
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }

        // ----- Close -> hide to tray -----
        case WM_CLOSE:
            HideAppWindow(hwnd);
            return 0;

        // ----- Tray icon interaction -----
        case WM_TRAYICON:
            if (lParam == WM_LBUTTONUP) {
                ShowAppWindow(hwnd);
            }
            else if (lParam == WM_RBUTTONUP) {
                ShowTrayContextMenu(hwnd);
            }
            return 0;

        // ----- Tray context menu commands -----
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case IDM_TRAY_SHOW:
                ShowAppWindow(hwnd);
                break;
            case IDM_COMPACT_MODE:
                ToggleCompact(hwnd);
                break;
            case IDM_TRAY_EXIT:
                DestroyWindow(hwnd);
                break;
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_F5) {
                Log("F5 pressed, refreshing data");
                RefreshData();
            }
            else if (wParam == 'T' && (GetKeyState(VK_CONTROL) & 0x8000)) {
                TogglePin(hwnd);
            }
            return 0;

        case WM_DESTROY:
            Log("WM_DESTROY received");
            // Signal background thread to stop, then wait for it
            g_shuttingDown.store(true);
            if (g_refreshThread.joinable()) {
                g_refreshThread.join();
            }
            RemoveTrayIcon();
            // Persist settings before closing
            if (g_app) {
                SaveSettings(hwnd, g_app->isPinned, g_app->renderer->IsCompact(), g_app->themeMode, g_app->apiUrl);
                Log("Settings saved");
            }
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }
    catch (const std::exception& e) {
        Log("EXCEPTION in WndProc (msg=%d): %s", msg, e.what());
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// ---------------------------------------------------------------------------
// URL validation: test if the API endpoint is reachable and returns valid JSON
// ---------------------------------------------------------------------------

static bool ValidateApiUrl(HttpClient& client, const std::wstring& urlStr, std::string& outError)
{
    ParsedUrl parsed = ParseUrl(urlStr);
    if (parsed.host.empty()) {
        outError = "Invalid URL: empty host";
        return false;
    }

    bool success = false;
    std::string response = client.GetSync(parsed.host, parsed.path, parsed.port, parsed.isHttps, success);
    if (!success) {
        outError = "Failed to connect to " + WstrToStr(urlStr);
        return false;
    }

    // Try parsing as JSON array of subscriptions
    try {
        auto subs = ParseSubscriptions(response);
        // Parsing succeeded
        return true;
    }
    catch (const std::exception& e) {
        outError = std::string("Invalid response: ") + e.what();
        return false;
    }
}

// ---------------------------------------------------------------------------
// URL input dialog (Win32 prompt box)
// Uses a simple modal dialog created from a DLGTEMPLATE in memory.
// ---------------------------------------------------------------------------

static std::wstring g_dialogUrl;     // shared between dialog proc and caller
static std::wstring g_dialogPrompt;  // optional extra message shown in the dialog

static INT_PTR CALLBACK UrlDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM)
{
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(hDlg, L"API Endpoint");
        // Center on screen
        RECT rc;
        GetWindowRect(hDlg, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
        SetWindowPos(hDlg, nullptr, (sx - w) / 2, (sy - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        // Populate the edit control with last URL
        SetDlgItemTextW(hDlg, 101, g_dialogUrl.c_str());

        // Show extra prompt (e.g. error message) if any
        if (!g_dialogPrompt.empty()) {
            SetDlgItemTextW(hDlg, 102, g_dialogPrompt.c_str());
        }
        // Select all text in edit box
        SendDlgItemMessageW(hDlg, 101, EM_SETSEL, 0, -1);
        SetFocus(GetDlgItem(hDlg, 101));
        return FALSE;  // we set focus manually
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[2048] = {};
            GetDlgItemTextW(hDlg, 101, buf, 2048);
            g_dialogUrl = buf;
            EndDialog(hDlg, IDOK);
        }
        else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

// Build a dialog template in memory (avoids needing a .rc resource file)
static INT_PTR ShowUrlInputDialog(HINSTANCE hInst, const std::wstring& defaultUrl, const std::wstring& prompt)
{
    g_dialogUrl = defaultUrl;
    g_dialogPrompt = prompt;

// We build a simple dialog: a static label, an edit box, OK and Cancel buttons.
// All coordinates in dialog units.
// Dialog: 320 x 130 DU
//   Static "prompt" label: ID 102, y=7
//   Static "Enter URL:":   y=40
//   Edit:   ID 101,        y=52
//   OK:     IDOK,          y=75
//   Cancel: IDCANCEL,      y=75

// Using the DS_SETFONT style so we can specify the font
#pragma pack(push, 1)
    struct
    {
        DLGTEMPLATE dlg;
        WORD menu;      // 0
        WORD wndClass;  // 0 = default
        WORD title;     // 0 = empty
        WORD fontSize;
        wchar_t fontName[20];  // "MS Shell Dlg"
        // ----- Items follow (5 items) -----
        // Each item must be DWORD-aligned
    } hdr = {};
#pragma pack(pop)

    // We'll use DialogBoxIndirectParam with a manually built template.
    // This is complex, so let's use a simpler approach: build with DialogBoxParam
    // from a global atom. Actually, the simplest reliable approach is just to use
    // a modal message-box loop with CreateWindowExW.

    // --- Simpler approach: Use a dynamically created modeless window as a dialog ---
    // Actually, let's just use the classic approach of building DLGTEMPLATE in a byte buffer.

    // For simplicity and robustness, use a pre-built buffer:
    std::vector<BYTE> buf;
    auto Align4 = [&]() {
        while (buf.size() % 4)
            buf.push_back(0);
    };
    auto AddWord = [&](WORD w) { buf.insert(buf.end(), (BYTE*)&w, (BYTE*)&w + 2); };
    auto AddDword = [&](DWORD d) { buf.insert(buf.end(), (BYTE*)&d, (BYTE*)&d + 4); };
    auto AddWstr = [&](const wchar_t* s) {
        while (*s) {
            AddWord(*s);
            s++;
        }
        AddWord(0);
    };

    // DLGTEMPLATE header
    DWORD style = DS_MODALFRAME | DS_SETFONT | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    AddDword(style);  // style
    AddDword(0);      // dwExtendedStyle
    AddWord(5);       // cdit (5 controls)
    AddWord(0);
    AddWord(0);  // x, y (ignored with DS_CENTER)
    AddWord(280);
    AddWord(105);          // cx, cy (dialog units)
    AddWord(0);            // menu
    AddWord(0);            // window class
    AddWord(0);            // title (empty - we set it in WM_INITDIALOG)
    AddWord(9);            // font size
    AddWstr(L"Segoe UI");  // font name

    // --- Control 1: Static label for error/prompt (ID 102) ---
    Align4();
    AddDword(WS_CHILD | WS_VISIBLE | SS_LEFT);  // style
    AddDword(0);                                // exStyle
    AddWord(10);
    AddWord(7);  // x, y
    AddWord(260);
    AddWord(28);   // cx, cy
    AddWord(102);  // ID
    AddWord(0xFFFF);
    AddWord(0x0082);  // class: Static
    AddWord(0);       // title (empty - set in WM_INITDIALOG)
    AddWord(0);       // extra

    // --- Control 2: Static "Enter API URL:" label ---
    Align4();
    AddDword(WS_CHILD | WS_VISIBLE | SS_LEFT);
    AddDword(0);
    AddWord(10);
    AddWord(38);
    AddWord(260);
    AddWord(10);
    AddWord(103);
    AddWord(0xFFFF);
    AddWord(0x0082);
    AddWstr(L"Enter API URL (e.g. http://host:port/path):");
    AddWord(0);

    // --- Control 3: Edit box (ID 101) ---
    Align4();
    AddDword(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL);
    AddDword(0);
    AddWord(10);
    AddWord(52);
    AddWord(260);
    AddWord(14);
    AddWord(101);
    AddWord(0xFFFF);
    AddWord(0x0081);  // class: Edit
    AddWord(0);       // title (empty - set in WM_INITDIALOG)
    AddWord(0);

    // --- Control 4: OK button ---
    Align4();
    AddDword(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON);
    AddDword(0);
    AddWord(165);
    AddWord(78);
    AddWord(50);
    AddWord(16);
    AddWord(IDOK);
    AddWord(0xFFFF);
    AddWord(0x0080);  // class: Button
    AddWstr(L"OK");
    AddWord(0);

    // --- Control 5: Cancel button ---
    Align4();
    AddDword(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON);
    AddDword(0);
    AddWord(220);
    AddWord(78);
    AddWord(50);
    AddWord(16);
    AddWord(IDCANCEL);
    AddWord(0xFFFF);
    AddWord(0x0080);
    AddWstr(L"Cancel");
    AddWord(0);

    return DialogBoxIndirectParamW(hInst, (DLGTEMPLATE*)buf.data(), nullptr, UrlDlgProc, 0);
}

// ---------------------------------------------------------------------------
// Prompt for URL with validation loop.
// Returns true if a valid URL was obtained, false if user cancelled.
// ---------------------------------------------------------------------------

static bool PromptAndValidateUrl(HINSTANCE hInst, HttpClient& client, std::wstring& outUrl,
                                 const std::wstring& defaultUrl)
{
    std::wstring prompt;
    std::wstring currentUrl = defaultUrl;

    while (true) {
        INT_PTR res = ShowUrlInputDialog(hInst, currentUrl, prompt);
        if (res != IDOK)
            return false;  // user cancelled

        currentUrl = g_dialogUrl;

        // Trim whitespace
        while (!currentUrl.empty() && currentUrl.front() == L' ')
            currentUrl.erase(currentUrl.begin());
        while (!currentUrl.empty() && currentUrl.back() == L' ')
            currentUrl.pop_back();

        if (currentUrl.empty()) {
            prompt = L"URL cannot be empty.";
            continue;
        }

        // Validate by making a test request
        prompt = L"Validating...";
        std::string error;
        if (ValidateApiUrl(client, currentUrl, error)) {
            outUrl = currentUrl;
            return true;
        }

        // Show error, let user retry
        prompt = StrToWstr(error);
    }
}

// ---------------------------------------------------------------------------
// Command-line parsing (URL is now optional)
// ---------------------------------------------------------------------------

bool ParseCommandLine(AppState& app, bool& debugMode, std::wstring& outUrlArg)
{
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    std::wstring themeArg;
    outUrlArg.clear();

    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];
        if (arg == L"--debug") {
            debugMode = true;
        }
        else if (arg == L"--compact") {
            app.renderer->SetCompact(true);
        }
        else if (arg == L"--theme" && i + 1 < argc) {
            themeArg = argv[++i];
        }
        else if (arg[0] != L'-') {
            outUrlArg = arg;
        }
    }

    // Parse theme
    if (themeArg == L"light")
        app.themeMode = ThemeMode::Light;
    else if (themeArg == L"dark")
        app.themeMode = ThemeMode::Dark;
    else
        app.themeMode = ThemeMode::System;

    app.debugMode = debugMode;

    LocalFree(argv);
    return true;
}

// Apply a URL string to the app state
static void ApplyUrl(AppState& app, const std::wstring& urlStr)
{
    ParsedUrl parsed = ParseUrl(urlStr);
    app.apiHost = parsed.host;
    app.apiPort = parsed.port;
    app.apiPath = parsed.path;
    app.apiIsHttps = parsed.isHttps;
    app.apiUrl = WstrToStr(urlStr);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow)
{
    // Enable Per-Monitor DPI Awareness for Windows 10 (1607+) and Windows 11
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI * SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
        SetProcessDpiAwarenessContextFunc setProcessDpiAwarenessContext =
            (SetProcessDpiAwarenessContextFunc)GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (setProcessDpiAwarenessContext) {
            if (!setProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                setProcessDpiAwarenessContext((DPI_AWARENESS_CONTEXT)-3);
            }
        }
        else {
            typedef BOOL(WINAPI * SetProcessDPIAwareFunc)(void);
            SetProcessDPIAwareFunc setProcessDPIAware =
                (SetProcessDPIAwareFunc)GetProcAddress(hUser32, "SetProcessDPIAware");
            if (setProcessDPIAware) {
                setProcessDPIAware();
            }
        }
    }

    SetupCrashHandlers();

    // Quick scan for --debug before full parse (need it for InitLogging)
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    bool debugMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::wstring(argv[i]) == L"--debug") {
            debugMode = true;
            break;
        }
    }
    LocalFree(argv);

    if (!InitLogging(debugMode)) {
        MessageBoxW(nullptr, L"Failed to initialize logging", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    Log("========================================");
    Log("Starting AI Subscriptions Monitor...");
    Log("Debug mode: %s", debugMode ? "enabled" : "disabled");

    AppState app;
    g_app = &app;

    // Parse command line (URL is optional now)
    std::wstring cmdUrlArg;
    ParseCommandLine(app, debugMode, cmdUrlArg);
    Log("Theme mode: %d", (int)app.themeMode);

    // Initialize HTTP client (needed for URL validation)
    if (!app.httpClient->Initialize()) {
        Log("ERROR: Failed to initialize HTTP client");
        MessageBoxW(nullptr, L"Failed to initialize HTTP client", L"Error", MB_OK | MB_ICONERROR);
        CloseLogging();
        return 1;
    }
    Log("HTTP client initialized");

    // Load saved settings
    SavedSettings saved = LoadSettings();

    // Determine API URL: command-line > saved config > prompt user
    std::wstring apiUrlW;
    if (!cmdUrlArg.empty()) {
        apiUrlW = cmdUrlArg;
        Log("Using command-line URL: %ls", apiUrlW.c_str());
    }
    else if (!saved.apiUrl.empty()) {
        apiUrlW = StrToWstr(saved.apiUrl);
        Log("Using saved URL: %ls", apiUrlW.c_str());
    }

    if (apiUrlW.empty()) {
        // No URL from command line or config -- prompt user
        Log("No API URL available, prompting user");
        if (!PromptAndValidateUrl(hInstance, *app.httpClient, apiUrlW, L"")) {
            Log("User cancelled URL input");
            CloseLogging();
            return 0;
        }
    }

    ApplyUrl(app, apiUrlW);
    Log("Parsed Host: %ls", app.apiHost.c_str());
    Log("Parsed Port: %d", app.apiPort);
    Log("Parsed Path: %ls", app.apiPath.c_str());

    // Apply initial theme so background brush is ready before window creation
    bool dark = IsDarkModeActive(app.themeMode);
    const ColorScheme& initScheme = GetColorScheme(dark);
    app.renderer->SetColorScheme(initScheme);
    app.hBgBrush = CreateSolidBrush(initScheme.bgColor);

    WNDCLASSEXW wc = {sizeof(wc)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    int initIconRes = dark ? IDI_APPICON : IDI_APPICON_LIGHT;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(initIconRes));
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = app.hBgBrush;
    wc.lpszClassName = kClassName;
    wc.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(initIconRes));

    if (!RegisterClassExW(&wc)) {
        Log("ERROR: Window registration failed");
        MessageBoxW(nullptr, L"Window registration failed", L"Error", MB_OK | MB_ICONERROR);
        CloseLogging();
        return 1;
    }
    Log("Window class registered");

    // Restore window geometry from saved settings
    int initX = CW_USEDEFAULT, initY = CW_USEDEFAULT;
    int initW = kWindowWidth, initH = kWindowHeight;
    if (saved.windowValid) {
        initX = saved.x;
        initY = saved.y;
        initW = saved.w;
        initH = saved.h;
        app.isPinned = saved.pinned;
        if (saved.compact)
            app.renderer->SetCompact(true);
        Log("Restored window state: %d,%d %dx%d pinned=%d compact=%d", initX, initY, initW, initH, (int)saved.pinned,
            (int)saved.compact);
    }

    // Create a hidden owner window so the main window does not appear in the
    // taskbar.  Owned windows never get a taskbar button, yet they keep a
    // normal-sized title bar (unlike WS_EX_TOOLWINDOW which shrinks it).
    HWND hwndOwner = CreateWindowExW(0, L"STATIC", nullptr, WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    HWND hwnd =
        CreateWindowExW(WS_EX_CLIENTEDGE, kClassName, kWindowTitle, WS_OVERLAPPEDWINDOW, initX, initY, initW, initH,
                        hwndOwner,  // owner -> no taskbar button
                        nullptr, hInstance, nullptr);

    if (!hwnd) {
        Log("ERROR: Window creation failed, GetLastError=%lu", GetLastError());
        MessageBoxW(nullptr, L"Window creation failed", L"Error", MB_OK | MB_ICONERROR);
        CloseLogging();
        return 1;
    }
    Log("Window created successfully");

    // Restore pin state
    if (app.isPinned) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // If compact mode restored, strip resize style and apply compact layout.
    // The window will auto-resize when data arrives (WM_USER+1).
    if (app.renderer->IsCompact()) {
        LONG style = GetWindowLong(hwnd, GWL_STYLE);
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        SetWindowLong(hwnd, GWL_STYLE, style);
        // Apply compact layout immediately to set correct size
        ApplyCompactLayout(hwnd);
    }

    // Skip initial auto-resize if we restored a saved window size (only for non-compact mode)
    if (saved.windowValid && !app.renderer->IsCompact()) {
        app.initialResizeDone = true;
    }

    // Show the window (no taskbar button thanks to hidden owner window)
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Update title and menu after window is visible
    {
        HMENU hSysMenu = GetSystemMenu(hwnd, FALSE);
        if (app.isPinned) {
            UpdateWindowTitle(hwnd);
            if (hSysMenu) {
                CheckMenuItem(hSysMenu, IDM_PIN_TO_TOP, MF_BYCOMMAND | MF_CHECKED);
            }
        }
        if (app.renderer->IsCompact() && hSysMenu) {
            CheckMenuItem(hSysMenu, IDM_COMPACT_MODE, MF_BYCOMMAND | MF_CHECKED);
        }
    }

    Log("Window shown (tray-only, hidden owner suppresses taskbar button)");

    MSG msg;
    BOOL bRet;
    while ((bRet = GetMessage(&msg, nullptr, 0, 0)) != 0) {
        if (bRet == -1) {
            Log("ERROR: GetMessage failed, GetLastError=%lu", GetLastError());
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    Log("Message loop ended");
    g_app = nullptr;
    CloseLogging();
    return (int)msg.wParam;
}
