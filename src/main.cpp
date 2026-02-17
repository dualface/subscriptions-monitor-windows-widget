#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <thread>
#include <iostream>
#include <sstream>
#include <fstream>
#include <ctime>
#include <csignal>
#include <map>
#include "subscription.h"
#include "http_client.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// 全局日志文件
static std::ofstream g_logFile;
static bool g_debugMode = false;
static FILE* g_consoleOut = nullptr;

// 日志函数 - 输出到控制台和文件
void Log(const char* fmt, ...) {
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
    }
}

// 初始化日志系统
bool InitLogging(bool debugMode) {
    g_debugMode = debugMode;
    
    g_logFile.open("AISubscriptionMonitor.log", std::ios::out | std::ios::app);
    if (!g_logFile.is_open()) {
        return false;
    }
    
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

void CloseLogging() {
    if (g_logFile.is_open()) {
        Log("========================================");
        Log("Application shutting down");
        Log("========================================");
        g_logFile.close();
    }
}

void CrashHandler(int signal) {
    Log("FATAL ERROR: Signal %d caught", signal);
    Log("Application crashed!");
    CloseLogging();
    exit(1);
}

LONG WINAPI ExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    Log("FATAL ERROR: Exception code 0x%08X", pExceptionInfo->ExceptionRecord->ExceptionCode);
    Log("Exception at address: 0x%p", pExceptionInfo->ExceptionRecord->ExceptionAddress);
    CloseLogging();
    return EXCEPTION_EXECUTE_HANDLER;
}

void SetupCrashHandlers() {
    SetUnhandledExceptionFilter(ExceptionHandler);
    signal(SIGSEGV, CrashHandler);
    signal(SIGABRT, CrashHandler);
    signal(SIGFPE, CrashHandler);
    signal(SIGILL, CrashHandler);
}

const wchar_t kClassName[] = L"AISubscriptionMonitor";
const wchar_t kWindowTitle[] = L"AI Subscription Monitor";
const int kWindowWidth = 800;
const int kWindowHeight = 600;
const int kRefreshIntervalMs = 60000;

// 控件ID
#define ID_LISTVIEW      1001
#define ID_STATUS_LABEL  1002
#define ID_REFRESH_BTN   1003
#define ID_PROGRESS_BAR  1004

struct AppState {
    std::vector<Subscription> subscriptions;
    std::unique_ptr<HttpClient> httpClient;
    std::wstring apiHost;
    std::wstring apiPath;
    int apiPort;
    bool isLoading;
    std::string lastError;
    HWND hwnd;
    HWND hListView;
    HWND hStatusLabel;
    HWND hProgressBar;
    HWND hRefreshBtn;
    bool debugMode;
    HFONT hFont;
    HFONT hFontBold;
    
    AppState() : apiPort(80), isLoading(false), hwnd(nullptr), 
                 hListView(nullptr), hStatusLabel(nullptr), 
                 hProgressBar(nullptr), hRefreshBtn(nullptr),
                 debugMode(false), hFont(nullptr), hFontBold(nullptr) {
        httpClient = std::make_unique<HttpClient>();
    }
    
    ~AppState() {
        if (hFont) DeleteObject(hFont);
        if (hFontBold) DeleteObject(hFontBold);
    }
};

static AppState* g_app = nullptr;

// 进度条子类化窗口过程 - 用于显示百分比文本
LRESULT CALLBACK ProgressBarSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                          UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
    switch (msg) {
        case WM_PAINT: {
            // 让默认绘制先执行
            LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            
            // 然后绘制百分比文字
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rect;
            GetClientRect(hwnd, &rect);
            
            // 获取当前进度
            int pos = (int)SendMessage(hwnd, PBM_GETPOS, 0, 0);
            int min = (int)SendMessage(hwnd, PBM_GETRANGE, TRUE, 0);
            int max = (int)SendMessage(hwnd, PBM_GETRANGE, FALSE, 0);
            int percentage = max > min ? ((pos - min) * 100 / (max - min)) : 0;
            
            // 绘制百分比文字
            wchar_t text[32];
            swprintf_s(text, L"%d%%", percentage);
            
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(0, 0, 0));
            DrawTextW(hdc, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ProgressBarSubclassProc, uIdSubclass);
            break;
    }
    
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// 更新列表视图
void UpdateListView() {
    if (!g_app || !g_app->hListView) return;
    
    Log("Updating ListView with %zu subscriptions", g_app->subscriptions.size());
    
    // 清除现有项
    ListView_DeleteAllItems(g_app->hListView);
    
    if (g_app->isLoading) {
        // 显示加载状态
        ShowWindow(g_app->hProgressBar, SW_SHOW);
        SendMessage(g_app->hProgressBar, PBM_SETMARQUEE, TRUE, 0);
        SetWindowTextW(g_app->hStatusLabel, L"Loading...");
        return;
    }
    
    ShowWindow(g_app->hProgressBar, SW_HIDE);
    
    if (!g_app->lastError.empty()) {
        std::wstring errorW(g_app->lastError.begin(), g_app->lastError.end());
        SetWindowTextW(g_app->hStatusLabel, (L"Error: " + errorW).c_str());
        return;
    }
    
    if (g_app->subscriptions.empty()) {
        SetWindowTextW(g_app->hStatusLabel, L"No data available");
        return;
    }
    
    // 计算总指标数
    int totalMetrics = 0;
    for (const auto& sub : g_app->subscriptions) {
        totalMetrics += (int)sub.metrics.size();
    }
    
    wchar_t statusText[256];
    swprintf_s(statusText, L"Loaded %zu services, %d metrics", 
               g_app->subscriptions.size(), totalMetrics);
    SetWindowTextW(g_app->hStatusLabel, statusText);
    
    // 添加数据到ListView
    int itemIndex = 0;
    for (const auto& sub : g_app->subscriptions) {
        std::wstring serviceName(sub.display_name.begin(), sub.display_name.end());
        std::wstring planName(sub.plan.name.begin(), sub.plan.name.end());
        
        // 服务标题行
        LVITEMW lvItem = {0};
        lvItem.mask = LVIF_TEXT;
        lvItem.iItem = itemIndex;
        lvItem.pszText = (LPWSTR)serviceName.c_str();
        ListView_InsertItem(g_app->hListView, &lvItem);
        
        // 设置计划列
        std::wstring planText = L"Plan: " + planName;
        LVITEMW lvItemPlan = {};
        lvItemPlan.iItem = itemIndex;
        lvItemPlan.iSubItem = 1;
        lvItemPlan.pszText = const_cast<wchar_t*>(planText.c_str());
        SendMessageW(g_app->hListView, LVM_SETITEMTEXT, itemIndex, (LPARAM)&lvItemPlan);
        itemIndex++;
        
        // 添加每个指标
        for (const auto& metric : sub.metrics) {
            std::wstring metricName(metric.name.begin(), metric.name.end());
            std::wstring windowLabel(metric.window.label.begin(), metric.window.label.end());
            
            // 指标名称
            lvItem.iItem = itemIndex;
            lvItem.pszText = (LPWSTR)metricName.c_str();
            lvItem.state = 0;
            ListView_InsertItem(g_app->hListView, &lvItem);
            
            // 计算百分比
            auto pct = metric.percentage();
            int percentage = pct.value_or(0);
            
            // 构建详情文本
            wchar_t detailText[256];
            if (metric.amount.limit.has_value()) {
                swprintf_s(detailText, L"%s - %.1f/%.1f %s (%d%%)",
                          windowLabel.c_str(),
                          metric.amount.used,
                          *metric.amount.limit,
                          std::wstring(metric.amount.unit.begin(), metric.amount.unit.end()).c_str(),
                          percentage);
            } else {
                swprintf_s(detailText, L"%s - %.1f %s",
                          windowLabel.c_str(),
                          metric.amount.used,
                          std::wstring(metric.amount.unit.begin(), metric.amount.unit.end()).c_str());
            }
            // 设置详情文本
            LVITEMW lvItemDetail = {};
            lvItemDetail.iItem = itemIndex;
            lvItemDetail.iSubItem = 1;
            lvItemDetail.pszText = detailText;
            SendMessageW(g_app->hListView, LVM_SETITEMTEXT, itemIndex, (LPARAM)&lvItemDetail);
            
            itemIndex++;
        }
        
        // 添加空行分隔
        if (&sub != &g_app->subscriptions.back()) {
            LVITEMW lvItemEmpty = {};
            lvItemEmpty.mask = LVIF_TEXT;
            lvItemEmpty.iItem = itemIndex;
            wchar_t emptyText[] = L"";
            lvItemEmpty.pszText = emptyText;
            ListView_InsertItem(g_app->hListView, &lvItemEmpty);
            
            LVITEMW lvItemEmpty2 = {};
            lvItemEmpty2.iItem = itemIndex;
            lvItemEmpty2.iSubItem = 1;
            lvItemEmpty2.pszText = emptyText;
            SendMessageW(g_app->hListView, LVM_SETITEMTEXT, itemIndex, (LPARAM)&lvItemEmpty2);
            itemIndex++;
        }
    }
}

void RefreshData() {
    if (!g_app || g_app->isLoading) return;

    Log("Refreshing data...");
    g_app->isLoading = true;
    g_app->lastError.clear();
    
    // 更新UI显示加载状态
    PostMessage(g_app->hwnd, WM_USER + 1, 0, 0);

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
                    g_app->lastError.clear();
                } catch (const std::exception& e) {
                    Log("Failed to parse response: %s", e.what());
                    g_app->lastError = e.what();
                    g_app->subscriptions.clear();
                }
            } else {
                Log("Failed to fetch data from server");
                g_app->lastError = "Failed to fetch data from server";
                g_app->subscriptions.clear();
            }
        } catch (const std::exception& e) {
            Log("Exception in HTTP thread: %s", e.what());
            g_app->lastError = e.what();
            g_app->subscriptions.clear();
        }

        g_app->isLoading = false;
        
        // 通知主线程更新UI
        if (g_app->hwnd) {
            PostMessage(g_app->hwnd, WM_USER + 1, 0, 0);
        }
    }).detach();
}

// 创建字体
void CreateFonts() {
    if (!g_app) return;
    
    // 创建普通字体（支持中文）
    g_app->hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
    
    // 创建粗体字体
    g_app->hFontBold = CreateFontW(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei");
}

// 创建控件
void CreateControls(HWND hwnd) {
    if (!g_app) return;
    
    Log("Creating controls...");
    
    // 获取客户区大小
    RECT clientRect;
    GetClientRect(hwnd, &clientRect);
    int width = clientRect.right - clientRect.left;
    int height = clientRect.bottom - clientRect.top;
    
    // 创建状态标签
    g_app->hStatusLabel = CreateWindowW(L"STATIC", L"Ready",
                                       WS_VISIBLE | WS_CHILD | SS_LEFT,
                                       10, 10, width - 100, 20,
                                       hwnd, (HMENU)ID_STATUS_LABEL, 
                                       GetModuleHandle(nullptr), nullptr);
    
    // 创建刷新按钮
    g_app->hRefreshBtn = CreateWindowW(L"BUTTON", L"Refresh (F5)",
                                       WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
                                       width - 90, 5, 80, 25,
                                       hwnd, (HMENU)ID_REFRESH_BTN,
                                       GetModuleHandle(nullptr), nullptr);
    
    // 创建进度条
    g_app->hProgressBar = CreateWindowW(PROGRESS_CLASSW, nullptr,
                                         WS_VISIBLE | WS_CHILD | PBS_MARQUEE,
                                         10, 40, width - 20, 20,
                                         hwnd, (HMENU)ID_PROGRESS_BAR,
                                         GetModuleHandle(nullptr), nullptr);
    ShowWindow(g_app->hProgressBar, SW_HIDE);
    
    // 创建ListView
    g_app->hListView = CreateWindowW(WC_LISTVIEWW, L"",
                                      WS_VISIBLE | WS_CHILD | LVS_REPORT | LVS_NOSORTHEADER |
                                      LVS_SHOWSELALWAYS | WS_BORDER,
                                      10, 70, width - 20, height - 80,
                                      hwnd, (HMENU)ID_LISTVIEW,
                                      GetModuleHandle(nullptr), nullptr);
    
    // 设置ListView扩展样式
    ListView_SetExtendedListViewStyle(g_app->hListView, 
                                      LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    
    // 添加列
    LVCOLUMNW lvCol = {0};
    lvCol.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    
    lvCol.pszText = (LPWSTR)L"Metric";
    lvCol.cx = 200;
    lvCol.iSubItem = 0;
    ListView_InsertColumn(g_app->hListView, 0, &lvCol);
    
    lvCol.pszText = (LPWSTR)L"Details";
    lvCol.cx = width - 220;
    lvCol.iSubItem = 1;
    ListView_InsertColumn(g_app->hListView, 1, &lvCol);
    
    // 设置字体
    if (g_app->hFont) {
        SendMessage(g_app->hStatusLabel, WM_SETFONT, (WPARAM)g_app->hFont, TRUE);
        SendMessage(g_app->hRefreshBtn, WM_SETFONT, (WPARAM)g_app->hFont, TRUE);
        SendMessage(g_app->hListView, WM_SETFONT, (WPARAM)g_app->hFont, TRUE);
    }
    
    Log("Controls created successfully");
}

// 调整控件大小
void ResizeControls(int width, int height) {
    if (!g_app) return;
    
    // 状态标签
    SetWindowPos(g_app->hStatusLabel, nullptr, 10, 10, width - 100, 20, SWP_NOZORDER);
    
    // 刷新按钮
    SetWindowPos(g_app->hRefreshBtn, nullptr, width - 90, 5, 80, 25, SWP_NOZORDER);
    
    // 进度条
    SetWindowPos(g_app->hProgressBar, nullptr, 10, 40, width - 20, 20, SWP_NOZORDER);
    
    // ListView
    SetWindowPos(g_app->hListView, nullptr, 10, 70, width - 20, height - 80, SWP_NOZORDER);
    
    // 调整列宽
    ListView_SetColumnWidth(g_app->hListView, 1, width - 220);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    try {
        switch (msg) {
            case WM_CREATE:
                Log("WM_CREATE received");
                if (g_app) {
                    g_app->hwnd = hwnd;
                    CreateControls(hwnd);
                    SetTimer(hwnd, 1, kRefreshIntervalMs, nullptr);
                    RefreshData();
                }
                return 0;
                
            case WM_SIZE: {
                int width = LOWORD(lParam);
                int height = HIWORD(lParam);
                Log("WM_SIZE: %dx%d", width, height);
                
                if (wParam != SIZE_MINIMIZED) {
                    ResizeControls(width, height);
                }
                return 0;
            }
            
            case WM_COMMAND:
                if (LOWORD(wParam) == ID_REFRESH_BTN) {
                    Log("Refresh button clicked");
                    RefreshData();
                }
                return 0;
                
            case WM_USER + 1:
                // 更新UI
                Log("WM_USER+1: Updating UI");
                UpdateListView();
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
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

// URL解析函数
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
    
    if (urlToParse.find(L"https://") == 0) {
        result.isHttps = true;
        result.port = 443;
        urlToParse = urlToParse.substr(8);
    } else if (urlToParse.find(L"http://") == 0) {
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
    
    ParsedUrl parsed = ParseUrl(urlArg);
    app.apiHost = parsed.host;
    app.apiPort = parsed.port;
    app.apiPath = parsed.path;
    app.debugMode = debugMode;
    
    LocalFree(argv);
    return true;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nCmdShow) {
    SetupCrashHandlers();
    
    // 初始化Common Controls
    INITCOMMONCONTROLSEX iccex = { sizeof(iccex), ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&iccex);
    
    // 解析命令行
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
    Log("Starting AI Subscription Monitor...");
    Log("Debug mode: %s", debugMode ? "enabled" : "disabled");
    
    AppState app;
    g_app = &app;
    
    // 创建字体
    CreateFonts();
    
    if (!ParseCommandLine(app, debugMode)) {
        Log("Failed to parse command line arguments");
        CloseLogging();
        return 1;
    }
    
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
