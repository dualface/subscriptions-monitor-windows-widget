#include "renderer.h"
#include <sstream>
#include <iomanip>

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
    int percentage = metric.percentage().value_or(0);
    
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
    
    // Setup text area
    RECT textRect = { x + 10, y, x + width - 10, y + kBarHeight };
    
    // Select normal font
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontNormal_);
    
    // Build metric name and window label
    std::wstringstream ss;
    ss << ToWString(metric.name);
    ss << L" (";
    ss << ToWString(metric.window.label);
    ss << L")";
    
    SetTextColor(hdc, kTextColor);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, ss.str().c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    
    // Draw percentage on the right
    std::wstringstream percentSs;
    percentSs << percentage << L"%";
    
    RECT percentRect = { x, y, x + width - 10, y + kBarHeight };
    DrawTextW(hdc, percentSs.str().c_str(), -1, &percentRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    
    // Draw reset time if available (small font)
    if (metric.window.resets_at.has_value()) {
        SelectObject(hdc, hFontSmall_);
        RECT subRect = { x + 10, y + 20, x + width - 10, y + kBarHeight };
        std::wstring resetText = L"Resets: " + ToWString(metric.window.formatResetTime());
        SetTextColor(hdc, kSubTextColor);
        DrawTextW(hdc, resetText.c_str(), -1, &subRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
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
