#include <windows.h>
#include <windowsx.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include "subscription.h"
#include "http_client.h"
#include "renderer.h"

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
    
    g_app->isLoading = true;
    InvalidateRect(g_app->hwnd, nullptr, TRUE);
    
    std::thread([]() {
        bool success = false;
        std::string response = g_app->httpClient->GetSync(
            g_app->apiHost, 
            g_app->apiPath, 
            g_app->apiPort, 
            success);
        
        if (success) {
            try {
                g_app->subscriptions = ParseSubscriptions(response);
                g_app->lastError.clear();
            } catch (const std::exception& e) {
                g_app->lastError = e.what();
            }
        } else {
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
            RECT textRect = { 0, height / 2 - 20, width, height / 2 + 20 };
            SetTextColor(hdcMem, RGB(100, 100, 100));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawText(hdcMem, L"Loading...", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else if (!g_app->lastError.empty()) {
            RECT textRect = { 20, height / 2 - 40, width - 20, height / 2 + 40 };
            SetTextColor(hdcMem, RGB(244, 67, 54));
            SetBkMode(hdcMem, TRANSPARENT);
            std::wstring errorW(g_app->lastError.begin(), g_app->lastError.end());
            DrawText(hdcMem, errorW.c_str(), -1, &textRect, DT_CENTER | DT_WORDBREAK);
        } else if (!g_app->subscriptions.empty()) {
            g_app->renderer->Render(hdcMem, g_app->subscriptions);
        } else {
            RECT textRect = { 0, height / 2 - 20, width, height / 2 + 20 };
            SetTextColor(hdcMem, RGB(150, 150, 150));
            SetBkMode(hdcMem, TRANSPARENT);
            DrawText(hdcMem, L"No data available", -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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

bool ParseCommandLine(AppState& app) {
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    
    if (argc < 2) {
        MessageBoxW(nullptr, L"Usage: AISubscriptionMonitor.exe <host> [port] [path]\n\n"
                            L"Example: AISubscriptionMonitor.exe api.example.com 8080 /subscriptions",
                            L"Command Line Error", MB_OK | MB_ICONERROR);
        return false;
    }
    
    app.apiHost = argv[1];
    
    if (argc >= 3) {
        app.apiPort = _wtoi(argv[2]);
    } else {
        app.apiPort = 80;
    }
    
    if (argc >= 4) {
        app.apiPath = argv[3];
    } else {
        app.apiPath = L"/";
    }
    
    LocalFree(argv);
    return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    AppState app;
    g_app = &app;
    
    if (!ParseCommandLine(app)) {
        return 1;
    }
    
    if (!app.httpClient->Initialize()) {
        MessageBoxW(nullptr, L"Failed to initialize HTTP client", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    WNDCLASSEX wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    wc.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    
    if (!RegisterClassEx(&wc)) {
        MessageBoxW(nullptr, L"Window registration failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }
    
    HWND hwnd = CreateWindowEx(
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
