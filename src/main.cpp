#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <regex>
#include <fstream>
#include <ctime>
#include <csignal>
#include "subscription.h"
#include "http_client.h"
#include "renderer.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// 全局日志文件
static std::ofstream g_logFile;
static bool g_debugMode = false;
static FILE* g_consoleOut = nullptr;

// 日志函数 - 输出到控制台和文件
void Log(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    // 格式化消息
    char buffer[4096];
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    // 获取当前时间
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_s(&timeinfo, &now);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    // 构建带时间的日志行
    char logLine[8192];
    snprintf(logLine, sizeof(logLine), "[%s] %s", timeStr, buffer);
    
    // 输出到控制台（如果是debug模式）
    if (g_debugMode && g_consoleOut) {
        fprintf(g_consoleOut, "%s\n", logLine);
        fflush(g_consoleOut);
    }
    
    // 写入文件
    if (g_logFile.is_open()) {
        g_logFile << logLine << std::endl;
        g_logFile.flush();
    }
}

// 初始化日志系统
bool InitLogging(bool debugMode) {
    g_debugMode = debugMode;
    
    // 打开日志文件
    g_logFile.open("AISubscriptionMonitor.log", std::ios::out | std::ios::app);
    if (!g_logFile.is_open()) {
        return false;
    }
    
    // 如果是debug模式，创建控制台
    if (debugMode) {
        AllocConsole();
        freopen_s(&g_consoleOut, "CONOUT$", "w", stdout);
        freopen_s(&g_consoleOut, "CONOUT$", "w", stderr);
        freopen_s(&g_consoleOut, "CONIN$", "r", stdin);
        
        Log("========================================");
        Log("Debug Console Started");
        Log("========================================");
    }
    
    return true;
}

// 关闭日志系统
void CloseLogging() {
    if (g_logFile.is_open()) {
        Log("========================================");
        Log("Application shutting down");
        Log("========================================");
        g_logFile.close();
    }
}

// 崩溃处理程序
void CrashHandler(int signal) {
    Log("FATAL ERROR: Signal %d caught", signal);
    Log("Application crashed!");
    CloseLogging();
    exit(1);
}

// Windows异常处理
LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    Log("FATAL ERROR: Exception code 0x%08X", pExceptionInfo->ExceptionRecord->ExceptionCode);
    Log("Exception at address: 0x%p", pExceptionInfo->ExceptionRecord->ExceptionAddress);
    CloseLogging();
    return EXCEPTION_EXECUTE_HANDLER;
}

// 设置崩溃处理程序
void SetupCrashHandlers() {
    SetUnhandledExceptionFilter(ExceptionHandler);
    signal(SIGSEGV, CrashHandler);
    signal(SIGABRT, CrashHandler);
    signal(SIGFPE, CrashHandler);
    signal(SIGILL, CrashHandler);
}

const wchar_t kClassName[] = L"AISubscriptionMonitor";
const wchar_t kWindowTitle[] = L"AI Subscription Monitor";
const int kWindowWidth = 700;
const int kWindowHeight = 500;
const int kRefreshIntervalMs = 60000;

struct AppState {
    std::vector<Subscription> subscriptions;
    std::unique_ptr<ProgressBarRenderer> renderer;
    std::unique_ptr<HttpClient> httpClient;
    std::wstring apiHost;
    std::wstring apiPath;
    int apiPort;
    bool isLoading;
    std::string lastError;
    HWND hwnd;
    bool debugMode;
    
    AppState() : apiPort(80), isLoading(false), hwnd(nullptr), debugMode(false) {
        renderer = std::make_unique<ProgressBarRenderer>();
        httpClient = std::make_unique<HttpClient>();
    }
};

static AppState* g_app = nullptr;

void RefreshData() {
    if (!g_app || g_app->isLoading) return;

    Log("Refreshing data...");
    g_app->isLoading = true;
    InvalidateRect(g_app->hwnd, nullptr, TRUE);

    std::thread([]() {
        bool success = false;
        Log("Sending HTTP request to %ls:%d%ls", g_app->apiHost.c_str(), g_app->apiPort, g_app->apiPath.c_str());

        try {
            std::string response = g_app->httpClient->GetSync(
                g_app->apiHost,
                g_app->apiPath,
                g_app->apiPort,
                success);

            Log("HTTP request completed. Success: %s", success ? "true" : "false");
            Log("Response size: %zu bytes", response.size());

            if (success) {
                try {
                    Log("Parsing subscriptions...");
                    g_app->subscriptions = ParseSubscriptions(response);
                    Log("Successfully parsed %zu subscriptions", g_app->subscriptions.size());

                    for (const auto& sub : g_app->subscriptions) {
                        Log("  - %ls (%ls): %zu metrics",
                            std::wstring(sub.display_name.begin(), sub.display_name.end()).c_str(),
                            std::wstring(sub.plan.name.begin(), sub.plan.name.end()).c_str(),
                            sub.metrics.size());
                    }

                    g_app->lastError.clear();
                } catch (const std::exception& e) {
                    Log("Failed to parse response: %s", e.what());
                    g_app->lastError = e.what();
                }
            } else {
                Log("Failed to fetch data from server");
                g_app->lastError = "Failed to fetch data from server";
            }
        } catch (const std::exception& e) {
            Log("Exception in HTTP thread: %s", e.what());
            g_app->lastError = e.what();
        }

        g_app->isLoading = false;
        if (g_app->hwnd) {
            InvalidateRect(g_app->hwnd, nullptr, TRUE);
        }
    }).detach();
}

void OnPaint(HWND hwnd) {
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

    Log("OnPaint: Window size %dx%d", width, height);
    
    // 如果窗口大小为0（最小化），不绘制
    if (width <= 0 || height <= 0) {
        Log("Window size is zero or negative, skipping paint");
        EndPaint(hwnd, &ps);
        return;
    }

    // 创建内存DC和位图
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

    // 填充背景
    HBRUSH hBgBrush = CreateSolidBrush(RGB(250, 250, 250));
    FillRect(hdcMem, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    try {
        g_app->renderer->SetWindowSize(width, height);

        if (g_app->isLoading) {
            Log("Rendering: Loading state");
            RECT textRect = { 0, height / 2 - 20, width, height / 2 + 20 };
            SetTextColor(hdcMem, RGB(100, 100, 100));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, L"Loading...", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (!g_app->lastError.empty()) {
            Log("Rendering: Error state - %s", g_app->lastError.c_str());
            RECT textRect = { 20, height / 2 - 40, width - 20, height / 2 + 40 };
            SetTextColor(hdcMem, RGB(244, 67, 54));
            SetBkMode(hdcMem, TRANSPARENT);
            std::wstring errorW(g_app->lastError.begin(), g_app->lastError.end());
            DrawTextW(hdcMem, errorW.c_str(), -1, &textRect, DT_CENTER | DT_WORDBREAK);
        } else if (!g_app->subscriptions.empty()) {
            Log("Rendering: %zu subscriptions", g_app->subscriptions.size());
            g_app->renderer->Render(hdcMem, g_app->subscriptions);
            Log("Render completed");
        } else {
            Log("Rendering: No data available");
            RECT textRect = { 0, height / 2 - 20, width, height / 2 + 20 };
            SetTextColor(hdcMem, RGB(150, 150, 150));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawTextW(hdcMem, L"No data available", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    } catch (const std::exception& e) {
        Log("EXCEPTION in OnPaint: %s", e.what());
    }

    // 复制到屏幕
    BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

    // 清理
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);

    EndPaint(hwnd, &ps);
}

void OnSize(HWND hwnd, UINT state, int cx, int cy) {
    Log("OnSize: state=%d, cx=%d, cy=%d", state, cx, cy);
    
    // 如果是 SIZE_MINIMIZED，不要尝试重新渲染
    if (state == SIZE_MINIMIZED) {
        Log("Window minimized, skipping size update");
        return;
    }
    
    if (g_app && g_app->renderer) {
        g_app->renderer->SetWindowSize(cx, cy);
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    try {
        switch (msg) {
            case WM_CREATE:
                Log("WM_CREATE received");
                if (g_app) {
                    g_app->hwnd = hwnd;
                    SetTimer(hwnd, 1, kRefreshIntervalMs, nullptr);
                    RefreshData();
                }
                return 0;
                
            case WM_PAINT:
                OnPaint(hwnd);
                return 0;
                
            case WM_SIZE:
                OnSize(hwnd, (UINT)wParam, LOWORD(lParam), HIWORD(lParam));
                return 0;

            case WM_TIMER:
                if (wParam == 1) {
                    Log("Timer triggered, refreshing data");
                    RefreshData();
                }
                return 0;
                
            case WM_KEYDOWN:
                if (wParam == VK_F5) {
                    Log("F5 pressed, refreshing data");
                    RefreshData();
                }
                return 0;
                
            case WM_DESTROY:
                Log("WM_DESTROY received");
                KillTimer(hwnd, 1);
                PostQuitMessage(0);
                return 0;
                
            default:
                return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    } catch (const std::exception& e) {
        Log("EXCEPTION in WndProc (msg=%d): %s", msg, e.what());
        // 尝试继续运行
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// URL解析函数，支持完整URL格式：http://host:port/path 或 https://host:port/path
struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    int port;
    bool isHttps;
};

ParsedUrl ParseUrl(const std::wstring& url) {
    ParsedUrl result;
    result.port = 80;
    result.path = L"/";
    result.isHttps = false;
    
    std::wstring urlToParse = url;
    
    // 检查协议
    if (urlToParse.find(L"https://") == 0) {
        result.isHttps = true;
        result.port = 443;
        urlToParse = urlToParse.substr(8);
    } else if (urlToParse.find(L"http://") == 0) {
        urlToParse = urlToParse.substr(7);
    }
    
    // 查找路径分隔符
    size_t pathPos = urlToParse.find(L'/');
    if (pathPos != std::wstring::npos) {
        result.path = urlToParse.substr(pathPos);
        urlToParse = urlToParse.substr(0, pathPos);
    }
    
    // 查找端口
    size_t portPos = urlToParse.find(L':');
    if (portPos != std::wstring::npos) {
        result.host = urlToParse.substr(0, portPos);
        std::wstring portStr = urlToParse.substr(portPos + 1);
        result.port = _wtoi(portStr.c_str());
    } else {
        result.host = urlToParse;
    }
    
    return result;
}

bool ParseCommandLine(AppState& app, bool& debugMode) {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    // 检查是否有 --debug 参数
    std::wstring urlArg;
    for (int i = 1; i < argc; i++) {
        std::wstring arg = argv[i];
        if (arg == L"--debug") {
            debugMode = true;
        } else if (arg[0] != L'-') {
            urlArg = arg;
        }
    }
    
    if (urlArg.empty()) {
        MessageBoxW(nullptr, L"Usage: AISubscriptionMonitor.exe [--debug] <url>\n\n"
                            L"Example: AISubscriptionMonitor.exe http://api.example.com:8080/subscriptions\n"
                            L"         AISubscriptionMonitor.exe --debug https://api.example.com/api/v1/usage",
                            L"Command Line Error", MB_OK | MB_ICONERROR);
        LocalFree(argv);
        return false;
    }
    
    // 解析完整URL
    ParsedUrl parsed = ParseUrl(urlArg);
    app.apiHost = parsed.host;
    app.apiPort = parsed.port;
    app.apiPath = parsed.path;
    app.debugMode = debugMode;
    
    LocalFree(argv);
    return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // 设置崩溃处理程序
    SetupCrashHandlers();
    
    // 解析命令行参数（提前检查debug模式）
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
    
    // 初始化日志系统
    if (!InitLogging(debugMode)) {
        MessageBoxW(nullptr, L"Failed to initialize logging", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    Log("========================================");
    Log("Starting AI Subscription Monitor...");
    Log("Debug mode: %s", debugMode ? "enabled" : "disabled");
    
    AppState app;
    g_app = &app;
    
    if (!ParseCommandLine(app, debugMode)) {
        Log("Failed to parse command line arguments");
        CloseLogging();
        return 1;
    }
    
    // 打印配置信息
    Log("Parsed Host: %ls", app.apiHost.c_str());
    Log("Parsed Port: %d", app.apiPort);
    Log("Parsed Path: %ls", app.apiPath.c_str());
    
    if (!app.httpClient->Initialize()) {
        Log("ERROR: Failed to initialize HTTP client");
        MessageBoxW(nullptr, L"Failed to initialize HTTP client", L"Error", MB_OK | MB_ICONERROR);
        CloseLogging();
        return 1;
    }
    Log("HTTP client initialized");
    
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    
    if (!RegisterClassExW(&wc)) {
        Log("ERROR: Window registration failed");
        MessageBoxW(nullptr, L"Window registration failed", L"Error", MB_OK | MB_ICONERROR);
        CloseLogging();
        return 1;
    }
    Log("Window class registered");

    HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        kClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        kWindowWidth, kWindowHeight,
        nullptr, nullptr, hInstance, nullptr);
    
    if (!hwnd) {
        Log("ERROR: Window creation failed, GetLastError=%lu", GetLastError());
        MessageBoxW(nullptr, L"Window creation failed", L"Error", MB_OK | MB_ICONERROR);
        CloseLogging();
        return 1;
    }
    Log("Window created successfully");
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    Log("Window shown");
    
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
