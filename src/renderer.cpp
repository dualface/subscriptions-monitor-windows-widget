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

ProgressBarRenderer::ProgressBarRenderer() 
    : windowWidth_(800), windowHeight_(600) {}

void ProgressBarRenderer::SetWindowSize(int width, int height) {
    windowWidth_ = width;
    windowHeight_ = height;
}

void ProgressBarRenderer::Render(HDC hdc, const std::vector<Subscription>& subscriptions) {
    int currentY = kMargin;
    
    for (const auto& sub : subscriptions) {
        auto displayMetrics = sub.getDisplayMetrics();
        if (displayMetrics.empty()) continue;
        
        if (currentY + kHeaderHeight > 0) {
            RenderServiceHeader(hdc, kMargin, currentY, windowWidth_ - 2 * kMargin, sub);
        }
        currentY += kHeaderHeight;
        
        for (const auto& metric : displayMetrics) {
            if (currentY + kBarHeight > 0) {
                RenderMetric(hdc, kMargin, currentY, windowWidth_ - 2 * kMargin, metric, sub.display_name);
            }
            currentY += kBarHeight + kItemSpacing;
        }
        
        currentY += kServiceSpacing;
    }
}

void ProgressBarRenderer::RenderServiceHeader(HDC hdc, int x, int y, int width, const Subscription& sub) {
    RECT rect = { x, y, x + width, y + kHeaderHeight };
    
    std::wstringstream ss;
    ss << std::wstring(sub.display_name.begin(), sub.display_name.end());
    ss << L" - ";
    ss << std::wstring(sub.plan.name.begin(), sub.plan.name.end());
    
    DrawTextLeft(hdc, ss.str(), rect, kTextColor, 16);
}

void ProgressBarRenderer::RenderMetric(HDC hdc, int x, int y, int width, 
                                        const Metric& metric, const std::string& serviceName) {
    int percentage = metric.percentage().value_or(0);
    
    COLORREF barColor;
    if (percentage < 50) {
        barColor = kBarLowColor;
    } else if (percentage < 80) {
        barColor = kBarMediumColor;
    } else {
        barColor = kBarHighColor;
    }
    
    DrawProgressBar(hdc, x, y, width, kBarHeight, percentage, barColor);
    
    RECT textRect = { x + 10, y, x + width - 10, y + kBarHeight };
    
    std::wstringstream ss;
    ss << std::wstring(metric.getDisplayTitle().begin(), metric.getDisplayTitle().end());
    ss << L" (";
    ss << std::wstring(metric.window.label.begin(), metric.window.label.end());
    ss << L")";
    
    DrawTextLeft(hdc, ss.str(), textRect, kTextColor, 13);
    
    std::wstringstream percentSs;
    percentSs << percentage << L"%";
    
    RECT percentRect = { x, y, x + width - 10, y + kBarHeight };
    DrawTextCentered(hdc, percentSs.str(), percentRect, kTextColor, 13);
    
    if (metric.window.resets_at.has_value()) {
        RECT subRect = { x + 10, y + 20, x + width - 10, y + kBarHeight };
        std::wstring resetText = L"Resets: " + 
            std::wstring(metric.window.formatResetTime().begin(), 
                        metric.window.formatResetTime().end());
        DrawTextLeft(hdc, resetText, subRect, kSubTextColor, 11);
    }
}

void ProgressBarRenderer::DrawProgressBar(HDC hdcDest, int x, int y, int width, int height,
                                           int percentage, COLORREF barColor) {
    HDC hdcMem = CreateCompatibleDC(hdcDest);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcDest, width, height);
    SelectObject(hdcMem, hbmMem);
    
    RECT bgRect = { 0, 0, width, height };
    HBRUSH hBgBrush = CreateSolidBrush(kBarBgColor);
    FillRect(hdcMem, &bgRect, hBgBrush);
    DeleteObject(hBgBrush);
    
    int fillWidth = (width * percentage) / 100;
    if (fillWidth > 0) {
        RECT fillRect = { 0, 0, fillWidth, height };
        HBRUSH hFillBrush = CreateSolidBrush(barColor);
        FillRect(hdcMem, &fillRect, hFillBrush);
        DeleteObject(hFillBrush);
    }
    
    BitBlt(hdcDest, x, y, width, height, hdcMem, 0, 0, SRCCOPY);
    
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

void ProgressBarRenderer::DrawTextCentered(HDC hdc, const std::wstring& text, RECT& rect,
                                            COLORREF color, int fontSize) {
    HFONT hFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);

    DrawTextW(hdc, text.c_str(), -1, &rect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}

void ProgressBarRenderer::DrawTextLeft(HDC hdc, const std::wstring& text, RECT& rect,
                                        COLORREF color, int fontSize) {
    HFONT hFont = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);

    DrawTextW(hdc, text.c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldFont);
    DeleteObject(hFont);
}
