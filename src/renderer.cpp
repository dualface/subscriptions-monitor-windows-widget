#include "renderer.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdio>

const COLORREF ProgressBarRenderer::kBgColor = RGB(240, 240, 240);
const COLORREF ProgressBarRenderer::kBarBgColor = RGB(224, 224, 224);
const COLORREF ProgressBarRenderer::kBarLowColor = RGB(76, 175, 80);
const COLORREF ProgressBarRenderer::kBarMediumColor = RGB(255, 193, 7);
const COLORREF ProgressBarRenderer::kBarHighColor = RGB(244, 67, 54);
const COLORREF ProgressBarRenderer::kTextColor = RGB(33, 33, 33);
const COLORREF ProgressBarRenderer::kSubTextColor = RGB(117, 117, 117);

// Helper function to convert std::string to std::wstring
static std::wstring ToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

// Helper function to format large numbers
static std::wstring FormatNumber(double value, const std::string& metricName) {
    std::wstringstream ss;
    
    // Check if it's Total Tokens or API Requests
    if (metricName == "Total Tokens") {
        // Format as integer, or as millions if large
        long long intValue = static_cast<long long>(value);
        if (intValue >= 1000000) {
            double millions = intValue / 1000000.0;
            ss << std::fixed << std::setprecision(1) << millions << L"M";
        } else {
            ss << intValue;
        }
    } else if (metricName == "API Requests") {
        // Format as integer
        ss << static_cast<long long>(value);
    } else {
        // Default: show with one decimal place
        ss << std::fixed << std::setprecision(1) << value;
    }
    
    return ss.str();
}

// Helper function to calculate remaining time until reset
static std::wstring CalculateRemainingTime(const std::string& resetsAt) {
    if (resetsAt.empty()) return L"";
    
    // Parse ISO 8601 format (e.g., "2026-02-20T07:36:05Z")
    struct tm resetTime = {0};
    int year, month, day, hour, minute, second;
    if (sscanf_s(resetsAt.c_str(), "%d-%d-%dT%d:%d:%d", 
                 &year, &month, &day, &hour, &minute, &second) != 6) {
        return L"";
    }
    
    resetTime.tm_year = year - 1900;
    resetTime.tm_mon = month - 1;
    resetTime.tm_mday = day;
    resetTime.tm_hour = hour;
    resetTime.tm_min = minute;
    resetTime.tm_sec = second;
    resetTime.tm_isdst = 0;
    
    time_t resetTimestamp = _mkgmtime(&resetTime);
    time_t now = time(nullptr);
    
    if (resetTimestamp <= now) {
        return L"Refreshing soon";
    }
    
    double diffSeconds = difftime(resetTimestamp, now);
    int diffHours = static_cast<int>(diffSeconds / 3600);
    int diffMinutes = static_cast<int>((diffSeconds - diffHours * 3600) / 60);
    
    std::wstringstream ss;
    if (diffHours > 0) {
        ss << diffHours << L"h " << diffMinutes << L"m";
    } else if (diffMinutes > 0) {
        ss << diffMinutes << L"m";
    } else {
        ss << L"<1m";
    }
    ss << L" refresh";
    
    return ss.str();
}

ProgressBarRenderer::ProgressBarRenderer() 
    : windowWidth_(800), windowHeight_(600),
      hFontNormal_(nullptr), hFontBold_(nullptr), hFontSmall_(nullptr) {
    CreateFonts();
}

ProgressBarRenderer::~ProgressBarRenderer() {
    DestroyFonts();
}

void ProgressBarRenderer::CreateFonts() {
    // Create normal font with Chinese support (Microsoft YaHei)
    hFontNormal_ = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
    );
    
    // Create bold font
    hFontBold_ = CreateFontW(
        18, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
    );
    
    // Create small font
    hFontSmall_ = CreateFontW(
        12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
    );
}

void ProgressBarRenderer::DestroyFonts() {
    if (hFontNormal_) DeleteObject(hFontNormal_);
    if (hFontBold_) DeleteObject(hFontBold_);
    if (hFontSmall_) DeleteObject(hFontSmall_);
}

void ProgressBarRenderer::SetWindowSize(int width, int height) {
    windowWidth_ = width;
    windowHeight_ = height;
}

int ProgressBarRenderer::GetTotalHeight() const {
    // Calculate based on content - simplified
    return windowHeight_;
}

void ProgressBarRenderer::Render(HDC hdc, const std::vector<Subscription>& subscriptions) {
    int currentY = kMargin;
    
    for (const auto& sub : subscriptions) {
        // Render service header
        if (currentY + kHeaderHeight > 0 && currentY < windowHeight_) {
            RenderServiceHeader(hdc, kMargin, currentY, windowWidth_ - 2 * kMargin, sub);
        }
        currentY += kHeaderHeight;
        
        // Render each metric
        for (const auto& metric : sub.metrics) {
            if (currentY + kBarHeight > 0 && currentY < windowHeight_) {
                RenderMetric(hdc, kMargin, currentY, windowWidth_ - 2 * kMargin, metric, sub.display_name);
            }
            currentY += kBarHeight + kItemSpacing;
        }
        
        currentY += kServiceSpacing;
    }
}

void ProgressBarRenderer::RenderServiceHeader(HDC hdc, int x, int y, int width, const Subscription& sub) {
    RECT rect = { x, y, x + width, y + kHeaderHeight };
    
    // Select bold font
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontBold_);
    
    // Build header text
    std::wstringstream ss;
    ss << ToWString(sub.display_name);
    ss << L" - ";
    ss << ToWString(sub.plan.name);
    
    SetTextColor(hdc, kTextColor);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, ss.str().c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    
    SelectObject(hdc, hOldFont);
}

void ProgressBarRenderer::RenderMetric(HDC hdc, int x, int y, int width, 
                                        const Metric& metric, const std::string& serviceName) {
    // Check if this metric has a limit (for percentage display)
    bool hasLimit = metric.amount.limit.has_value();
    int percentage = hasLimit ? metric.percentage().value_or(0) : 0;
    
    // Only draw progress bar if there's a limit
    if (hasLimit) {
        // Determine bar color based on percentage
        COLORREF barColor;
        if (percentage < 50) {
            barColor = kBarLowColor;
        } else if (percentage < 80) {
            barColor = kBarMediumColor;
        } else {
            barColor = kBarHighColor;
        }
        
        // Draw progress bar background and fill
        DrawProgressBar(hdc, x, y, width, kBarHeight, percentage, barColor);
    } else {
        // Draw simple background for metrics without limits
        RECT bgRect = { x, y, x + width, y + kBarHeight };
        HBRUSH hBgBrush = CreateSolidBrush(kBarBgColor);
        FillRect(hdc, &bgRect, hBgBrush);
        DeleteObject(hBgBrush);
    }
    
    // Select normal font
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontNormal_);
    
    // Build left side: metric name and window label
    std::wstringstream leftSs;
    leftSs << ToWString(metric.name);
    leftSs << L" (";
    leftSs << ToWString(metric.window.label);
    leftSs << L")";
    
    // Calculate width needed for left text
    SIZE textSize;
    GetTextExtentPoint32W(hdc, leftSs.str().c_str(), (int)leftSs.str().length(), &textSize);
    int leftTextWidth = textSize.cx + 20; // Add padding
    
    // Draw left side text
    RECT leftRect = { x + 10, y, x + leftTextWidth, y + kBarHeight };
    SetTextColor(hdc, kTextColor);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, leftSs.str().c_str(), -1, &leftRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    
    // Build middle section: usage value
    std::wstringstream middleSs;
    if (hasLimit) {
        middleSs << percentage << L"%";
    } else {
        // Use formatted number for metrics without limits
        middleSs << FormatNumber(metric.amount.used, metric.name);
        middleSs << L" " << ToWString(metric.amount.unit);
    }
    
    // Draw middle text (center-left area)
    int middleStart = x + leftTextWidth + 10;
    RECT middleRect = { middleStart, y, x + width - 150, y + kBarHeight };
    DrawTextW(hdc, middleSs.str().c_str(), -1, &middleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    
    // Build right side: remaining time until refresh (large, black font)
    if (metric.window.resets_at.has_value()) {
        std::wstring remainingTime = CalculateRemainingTime(*metric.window.resets_at);
        if (!remainingTime.empty()) {
            RECT rightRect = { x + width - 140, y, x + width - 10, y + kBarHeight };
            SetTextColor(hdc, RGB(0, 0, 0)); // Black color
            DrawTextW(hdc, remainingTime.c_str(), -1, &rightRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }
    
    // Restore original font
    SelectObject(hdc, hOldFont);
}

void ProgressBarRenderer::DrawProgressBar(HDC hdcDest, int x, int y, int width, int height,
                                           int percentage, COLORREF barColor) {
    // Create memory DC for double buffering
    HDC hdcMem = CreateCompatibleDC(hdcDest);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcDest, width, height);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);
    
    // Draw background
    RECT bgRect = { 0, 0, width, height };
    HBRUSH hBgBrush = CreateSolidBrush(kBarBgColor);
    FillRect(hdcMem, &bgRect, hBgBrush);
    DeleteObject(hBgBrush);
    
    // Draw filled portion
    int fillWidth = (width * percentage) / 100;
    if (fillWidth > 0) {
        RECT fillRect = { 0, 0, fillWidth, height };
        HBRUSH hFillBrush = CreateSolidBrush(barColor);
        FillRect(hdcMem, &fillRect, hFillBrush);
        DeleteObject(hFillBrush);
    }
    
    // Copy to destination
    BitBlt(hdcDest, x, y, width, height, hdcMem, 0, 0, SRCCOPY);
    
    // Cleanup
    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

void ProgressBarRenderer::DrawTextCentered(HDC hdc, const std::wstring& text, RECT& rect,
                                            COLORREF color, int fontSize) {
    HFONT hFont = CreateFontW(
        fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
    );
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

void ProgressBarRenderer::DrawTextLeft(HDC hdc, const std::wstring& text, RECT& rect,
                                        COLORREF color, int fontSize) {
    HFONT hFont = CreateFontW(
        fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI"
    );
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}
