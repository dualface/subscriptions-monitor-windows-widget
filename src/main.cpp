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
#include "subscription.h"
#include "http_client.h"
#include "renderer.h"

// 创建控制台窗口用于调试输出
void CreateConsole() {
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    std::cout << "Debug Console Started" << std::endl;
}

// 便捷日志函数
template<typename... Args>
void Log(const char* fmt, Args... args) {
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), fmt, args...);
    std::cout << buffer << std::endl;
}

#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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
    int scrollOffset;
    bool isLoading;
    std::string lastError;
    HWND hwnd;
    
    AppState() : apiPort(80), scrollOffset(0), isLoading(false), hwnd(nullptr) {
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

        g_app->isLoading = false;
        InvalidateRect(g_app->hwnd, nullptr, TRUE);
    }).detach();
}

void OnPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;

    Log("OnPaint: Window size %dx%d, Subscriptions: %zu, Loading: %s, Error: %s",
        width, height,
        g_app ? g_app->subscriptions.size() : 0,
        g_app && g_app->isLoading ? "true" : "false",
        g_app && !g_app->lastError.empty() ? g_app->lastError.c_str() : "none");

    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdc, width, height);
    SelectObject(hdcMem, hbmMem);

    HBRUSH hBgBrush = CreateSolidBrush(RGB(250, 250, 250));
    FillRect(hdcMem, &clientRect, hBgBrush);
    DeleteObject(hBgBrush);

    if (g_app) {
        g_app->renderer->SetWindowSize(width, height);
        g_app->renderer->SetScrollOffset(g_app->scrollOffset);

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
    }

    BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

    DeleteObject(hbmMem);
    DeleteDC(hdcMem);

    EndPaint(hwnd, &ps);
}

void OnSize(HWND hwnd, UINT state, int cx, int cy) {
    if (g_app && g_app->renderer) {
        g_app->renderer->SetWindowSize(cx, cy);
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void OnVScroll(HWND hwnd, HWND hwndCtl, UINT code, int pos) {
    if (!g_app) return;
    
    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, &si);
    
    int oldPos = si.nPos;
    
    switch (code) {
        case SB_LINEUP: si.nPos -= 20; break;
        case SB_LINEDOWN: si.nPos += 20; break;
        case SB_PAGEUP: si.nPos -= si.nPage; break;
        case SB_PAGEDOWN: si.nPos += si.nPage; break;
        case SB_THUMBTRACK: si.nPos = si.nTrackPos; break;
    }
    
    si.fMask = SIF_POS;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(hwnd, SB_VERT, &si);
    
    if (si.nPos != oldPos) {
        g_app->scrollOffset = si.nPos;
        InvalidateRect(hwnd, nullptr, TRUE);
    }
}

void UpdateScrollBar(HWND hwnd) {
    if (!g_app || !g_app->renderer) return;
    
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int clientHeight = clientRect.bottom - clientRect.top;
    
    int totalHeight = g_app->renderer->GetTotalHeight();
    
    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = totalHeight;
    si.nPage = clientHeight;
    si.nPos = g_app->scrollOffset;
    
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
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
            UpdateScrollBar(hwnd);
            return 0;
            
        case WM_VSCROLL:
            OnVScroll(hwnd, (HWND)lParam, LOWORD(wParam), (short)HIWORD(wParam));
            return 0;
            
        case WM_MOUSEWHEEL:
            if (g_app) {
                int delta = GET_WHEEL_DELTA_WPARAM(wParam);
                g_app->scrollOffset -= delta / 3;
                if (g_app->scrollOffset < 0) g_app->scrollOffset = 0;
                UpdateScrollBar(hwnd);
                InvalidateRect(hwnd, nullptr, TRUE);
            }
            return 0;
            
        case WM_TIMER:
            if (wParam == 1) {
                RefreshData();
            }
            return 0;
            
        case WM_KEYDOWN:
            if (wParam == VK_F5) {
                RefreshData();
            }
            return 0;
            
        case WM_DESTROY:
            KillTimer(hwnd, 1);
            PostQuitMessage(0);
            return 0;
            
        default:
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

bool ParseCommandLine(AppState& app) {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    if (argc < 2) {
        MessageBoxW(nullptr, L"Usage: AISubscriptionMonitor.exe <url>\n\n"
                            L"Example: AISubscriptionMonitor.exe http://api.example.com:8080/subscriptions\n"
                            L"         AISubscriptionMonitor.exe https://api.example.com/api/v1/usage",
                            L"Command Line Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    // 解析完整URL
    ParsedUrl parsed = ParseUrl(argv[1]);
    app.apiHost = parsed.host;
    app.apiPort = parsed.port;
    app.apiPath = parsed.path;
    
    LocalFree(argv);
    return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    // 创建调试控制台
    CreateConsole();
    Log("Starting AI Subscription Monitor...");
    
    AppState app;
    g_app = &app;
    
    if (!ParseCommandLine(app)) {
        Log("Failed to parse command line arguments");
        return 1;
    }
    
    // 打印配置信息
    Log("Parsed Host: %ls", app.apiHost.c_str());
    Log("Parsed Port: %d", app.apiPort);
    Log("Parsed Path: %ls", app.apiPath.c_str());
    
    if (!app.httpClient->Initialize()) {
        MessageBoxW(nullptr, L"Failed to initialize HTTP client", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
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
        MessageBoxW(nullptr, L"Window registration failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE,
        kClassName,
        kWindowTitle,
        WS_OVERLAPPEDWINDOW | WS_VSCROLL,
        CW_USEDEFAULT, CW_USEDEFAULT,
        kWindowWidth, kWindowHeight,
        nullptr, nullptr, hInstance, nullptr);
    
    if (!hwnd) {
        MessageBoxW(nullptr, L"Window creation failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    g_app = nullptr;
    return (int)msg.wParam;
}
