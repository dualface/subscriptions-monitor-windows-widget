#include "renderer.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdio>

// ---------------------------------------------------------------------------
// Color schemes
// ---------------------------------------------------------------------------

static const ColorScheme kLightScheme = {
    RGB(250, 250, 250),   // bgColor
    RGB(224, 224, 224),   // barBgColor
    RGB(76, 175, 80),     // barLowColor   (green)
    RGB(255, 193, 7),     // barMediumColor (amber)
    RGB(244, 67, 54),     // barHighColor  (red)
    RGB(33, 33, 33),      // textColor
    RGB(117, 117, 117),   // subTextColor
    RGB(100, 100, 100),   // loadingTextColor
    RGB(244, 67, 54),     // errorTextColor
    RGB(150, 150, 150),   // noDataTextColor
    RGB(0, 0, 0),         // resetTimeColor
    // scrollbar
    RGB(235, 235, 235),   // scrollTrackColor
    RGB(190, 190, 190),   // scrollThumbColor
    RGB(160, 160, 160),   // scrollThumbHover
};

static const ColorScheme kDarkScheme = {
    RGB(30, 30, 30),      // bgColor
    RGB(55, 55, 55),      // barBgColor
    RGB(56, 142, 60),     // barLowColor   (muted green)
    RGB(255, 160, 0),     // barMediumColor (orange)
    RGB(229, 57, 53),     // barHighColor  (red)
    RGB(230, 230, 230),   // textColor
    RGB(160, 160, 160),   // subTextColor
    RGB(160, 160, 160),   // loadingTextColor
    RGB(239, 83, 80),     // errorTextColor
    RGB(120, 120, 120),   // noDataTextColor
    RGB(220, 220, 220),   // resetTimeColor
    // scrollbar
    RGB(40, 40, 40),      // scrollTrackColor
    RGB(80, 80, 80),      // scrollThumbColor
    RGB(110, 110, 110),   // scrollThumbHover
};

const ColorScheme& GetLightScheme() { return kLightScheme; }
const ColorScheme& GetDarkScheme()  { return kDarkScheme; }
const ColorScheme& GetColorScheme(bool dark) { return dark ? kDarkScheme : kLightScheme; }

// ---------------------------------------------------------------------------
// System dark-mode detection (Windows 10 1809+)
// Registry: HKCU\Software\Microsoft\Windows\CurrentVersion\Themes\Personalize
//   AppsUseLightTheme  DWORD  0 = dark, 1 = light
// ---------------------------------------------------------------------------

bool IsSystemDarkMode() {
    HKEY hKey = nullptr;
    LONG res = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey);
    if (res != ERROR_SUCCESS) return false;

    DWORD value = 1; // default to light
    DWORD size = sizeof(value);
    RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr,
                     reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(hKey);
    return value == 0;
}

bool IsDarkModeActive(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Dark:   return true;
        case ThemeMode::Light:  return false;
        case ThemeMode::System: return IsSystemDarkMode();
    }
    return false;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::wstring ToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
    return wstr;
}

static std::wstring FormatNumber(double value, const std::string& metricName) {
    std::wstringstream ss;
    if (metricName == "Total Tokens") {
        long long intValue = static_cast<long long>(value);
        if (intValue >= 1000000) {
            ss << std::fixed << std::setprecision(1) << intValue / 1000000.0 << L"M";
        } else {
            ss << intValue;
        }
    } else if (metricName == "API Requests") {
        ss << static_cast<long long>(value);
    } else {
        ss << std::fixed << std::setprecision(1) << value;
    }
    return ss.str();
}

static std::wstring CalculateRemainingTime(const std::string& resetsAt) {
    if (resetsAt.empty()) return L"";

    struct tm resetTime = {0};
    int year, month, day, hour, minute, second;
    if (sscanf_s(resetsAt.c_str(), "%d-%d-%dT%d:%d:%d",
                 &year, &month, &day, &hour, &minute, &second) != 6) {
        return L"";
    }
    resetTime.tm_year = year - 1900;
    resetTime.tm_mon  = month - 1;
    resetTime.tm_mday = day;
    resetTime.tm_hour = hour;
    resetTime.tm_min  = minute;
    resetTime.tm_sec  = second;
    resetTime.tm_isdst = 0;

    time_t resetTimestamp = _mkgmtime(&resetTime);
    time_t now = time(nullptr);
    if (resetTimestamp <= now) return L"Refreshing soon";

    double diffSeconds = difftime(resetTimestamp, now);
    int diffHours   = static_cast<int>(diffSeconds / 3600);
    int diffMinutes = static_cast<int>((diffSeconds - diffHours * 3600) / 60);

    std::wstringstream ss;
    if (diffHours > 0)        ss << diffHours << L"h " << diffMinutes << L"m";
    else if (diffMinutes > 0) ss << diffMinutes << L"m";
    else                      ss << L"<1m";
    ss << L" refresh";
    return ss.str();
}

// ---------------------------------------------------------------------------
// ProgressBarRenderer
// ---------------------------------------------------------------------------

ProgressBarRenderer::ProgressBarRenderer()
    : windowWidth_(800), windowHeight_(600), compact_(false),
      hFontNormal_(nullptr), hFontBold_(nullptr), hFontSmall_(nullptr),
      dpiScale_(1.0f), colors_(kLightScheme) {
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        dpiScale_ = GetDeviceCaps(hdc, LOGPIXELSX) / 96.0f;
        ReleaseDC(nullptr, hdc);
    }
    CreateFonts();
}

ProgressBarRenderer::~ProgressBarRenderer() {
    DestroyFonts();
}

void ProgressBarRenderer::SetColorScheme(const ColorScheme& scheme) {
    colors_ = scheme;
}

void ProgressBarRenderer::SetCompact(bool compact) {
    if (compact_ == compact) return;
    compact_ = compact;
}

void ProgressBarRenderer::CreateFonts() {
    const int baseNormalSize = 18;
    const int baseBoldSize   = 20;
    const int baseSmallSize  = 14;

    int normalSize = static_cast<int>(baseNormalSize * dpiScale_);
    int boldSize   = static_cast<int>(baseBoldSize   * dpiScale_);
    int smallSize  = static_cast<int>(baseSmallSize  * dpiScale_);

    hFontNormal_ = CreateFontW(
        normalSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");

    hFontBold_ = CreateFontW(
        boldSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");

    hFontSmall_ = CreateFontW(
        smallSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
}

void ProgressBarRenderer::OnDpiChanged(UINT newDpi) {
    dpiScale_ = newDpi / 96.0f;
    DestroyFonts();
    CreateFonts();
}

void ProgressBarRenderer::DestroyFonts() {
    if (hFontNormal_) { DeleteObject(hFontNormal_); hFontNormal_ = nullptr; }
    if (hFontBold_)   { DeleteObject(hFontBold_);   hFontBold_   = nullptr; }
    if (hFontSmall_)  { DeleteObject(hFontSmall_);  hFontSmall_  = nullptr; }
}

void ProgressBarRenderer::SetWindowSize(int width, int height) {
    windowWidth_  = width;
    windowHeight_ = height;
}

// ---------------------------------------------------------------------------
// Content height calculation (no drawing)
// ---------------------------------------------------------------------------

int ProgressBarRenderer::CalculateContentHeight(const std::vector<Subscription>& subscriptions) const {
    int h = Margin();
    for (const auto& sub : subscriptions) {
        h += HeaderHeight();
        for (size_t i = 0; i < sub.metrics.size(); ++i) {
            h += BarHeight() + ItemSpacing();
        }
        h += ServiceSpacing();
    }
    // Add bottom margin
    h += Margin();
    return h;
}

// ---------------------------------------------------------------------------
// Render (with scroll offset)
// ---------------------------------------------------------------------------

int ProgressBarRenderer::Render(HDC hdc, const std::vector<Subscription>& subscriptions, int scrollOffset) {
    int currentY = Margin() - scrollOffset;

    for (const auto& sub : subscriptions) {
        if (currentY + HeaderHeight() > 0 && currentY < windowHeight_) {
            RenderServiceHeader(hdc, Margin(), currentY, windowWidth_ - 2 * Margin(), sub);
        }
        currentY += HeaderHeight();

        for (const auto& metric : sub.metrics) {
            if (currentY + BarHeight() > 0 && currentY < windowHeight_) {
                RenderMetric(hdc, Margin(), currentY, windowWidth_ - 2 * Margin(), metric, sub.display_name);
            }
            currentY += BarHeight() + ItemSpacing();
        }
        currentY += ServiceSpacing();
    }

    // Return total content height
    return currentY + scrollOffset + Margin();
}

// ---------------------------------------------------------------------------
// Service header
// ---------------------------------------------------------------------------

void ProgressBarRenderer::RenderServiceHeader(HDC hdc, int x, int y, int width, const Subscription& sub) {
    RECT rect = { x, y, x + width, y + HeaderHeight() };
    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontBold_);

    std::wstringstream ss;
    ss << ToWString(sub.display_name) << L" - " << ToWString(sub.plan.name);

    SetTextColor(hdc, colors_.textColor);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, ss.str().c_str(), -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(hdc, hOldFont);
}

// ---------------------------------------------------------------------------
// Single metric
// ---------------------------------------------------------------------------

void ProgressBarRenderer::RenderMetric(HDC hdc, int x, int y, int width,
                                        const Metric& metric, const std::string& serviceName) {
    bool hasLimit = metric.amount.limit.has_value();
    double percentage = hasLimit ? metric.percentage().value_or(0.0) : 0.0;
    int barH = BarHeight();
    int pad = compact_ ? 6 : 10;   // inner horizontal padding

    if (hasLimit) {
        COLORREF barColor;
        if (percentage < 50.0)      barColor = colors_.barLowColor;
        else if (percentage < 80.0) barColor = colors_.barMediumColor;
        else                        barColor = colors_.barHighColor;
        DrawProgressBar(hdc, x, y, width, barH, percentage, barColor);
    } else {
        RECT bgRect = { x, y, x + width, y + barH };
        HBRUSH hBgBrush = CreateSolidBrush(colors_.barBgColor);
        FillRect(hdc, &bgRect, hBgBrush);
        DeleteObject(hBgBrush);
    }

    HFONT hOldFont = (HFONT)SelectObject(hdc, hFontNormal_);
    SetTextColor(hdc, colors_.textColor);
    SetBkMode(hdc, TRANSPARENT);

    if (compact_) {
        // Compact: "window_label   percentage%   remaining_time"
        std::wstring labelW = ToWString(metric.window.label);

        SIZE labelSize;
        GetTextExtentPoint32W(hdc, labelW.c_str(), (int)labelW.length(), &labelSize);
        int labelWidth = labelSize.cx + pad * 2;

        RECT leftRect = { x + pad, y, x + labelWidth, y + barH };
        DrawTextW(hdc, labelW.c_str(), -1, &leftRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Middle: percentage or raw usage
        std::wstringstream midSs;
        if (hasLimit) {
            midSs << std::fixed << std::setprecision(1) << percentage << L"%";
        } else {
            midSs << FormatNumber(metric.amount.used, metric.name)
                  << L" " << ToWString(metric.amount.unit);
        }
        RECT midRect = { x + labelWidth + pad, y, x + width - 140, y + barH };
        DrawTextW(hdc, midSs.str().c_str(), -1, &midRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Right: remaining time
        if (metric.window.resets_at.has_value()) {
            std::wstring remaining = CalculateRemainingTime(*metric.window.resets_at);
            if (!remaining.empty()) {
                RECT rightRect = { x + width - 140, y, x + width - pad, y + barH };
                SetTextColor(hdc, colors_.resetTimeColor);
                DrawTextW(hdc, remaining.c_str(), -1, &rightRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }
    } else {
        // Normal: "MetricName (window)   percentage%   remaining time"
        std::wstringstream leftSs;
        leftSs << ToWString(metric.name) << L" (" << ToWString(metric.window.label) << L")";

        SIZE textSize;
        GetTextExtentPoint32W(hdc, leftSs.str().c_str(), (int)leftSs.str().length(), &textSize);
        int leftTextWidth = textSize.cx + pad * 2;

        RECT leftRect = { x + pad, y, x + leftTextWidth, y + barH };
        DrawTextW(hdc, leftSs.str().c_str(), -1, &leftRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Middle: percentage or usage
        std::wstringstream middleSs;
        if (hasLimit) {
            middleSs << std::fixed << std::setprecision(2) << percentage << L"%";
        } else {
            middleSs << FormatNumber(metric.amount.used, metric.name)
                     << L" " << ToWString(metric.amount.unit);
        }

        int middleStart = x + leftTextWidth + pad;
        RECT middleRect = { middleStart, y, x + width - 150, y + barH };
        DrawTextW(hdc, middleSs.str().c_str(), -1, &middleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Right: remaining time
        if (metric.window.resets_at.has_value()) {
            std::wstring remainingTime = CalculateRemainingTime(*metric.window.resets_at);
            if (!remainingTime.empty()) {
                RECT rightRect = { x + width - 140, y, x + width - pad, y + barH };
                SetTextColor(hdc, colors_.resetTimeColor);
                DrawTextW(hdc, remainingTime.c_str(), -1, &rightRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }

    SelectObject(hdc, hOldFont);
}

// ---------------------------------------------------------------------------
// Progress bar (double-buffered)
// ---------------------------------------------------------------------------

void ProgressBarRenderer::DrawProgressBar(HDC hdcDest, int x, int y, int width, int height,
                                           double percentage, COLORREF barColor) {
    HDC hdcMem = CreateCompatibleDC(hdcDest);
    HBITMAP hbmMem = CreateCompatibleBitmap(hdcDest, width, height);
    HBITMAP hbmOld = (HBITMAP)SelectObject(hdcMem, hbmMem);

    RECT bgRect = { 0, 0, width, height };
    HBRUSH hBgBrush = CreateSolidBrush(colors_.barBgColor);
    FillRect(hdcMem, &bgRect, hBgBrush);
    DeleteObject(hBgBrush);

    int fillWidth = static_cast<int>((width * percentage) / 100.0);
    if (fillWidth > 0) {
        RECT fillRect = { 0, 0, fillWidth, height };
        HBRUSH hFillBrush = CreateSolidBrush(barColor);
        FillRect(hdcMem, &fillRect, hFillBrush);
        DeleteObject(hFillBrush);
    }

    BitBlt(hdcDest, x, y, width, height, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hbmOld);
    DeleteObject(hbmMem);
    DeleteDC(hdcMem);
}

// ---------------------------------------------------------------------------
// Custom scrollbar geometry
// ---------------------------------------------------------------------------

void ProgressBarRenderer::CalcThumbRect(int clientHeight, int contentHeight, int scrollOffset,
                                         int trackX, RECT& outThumb) const {
    if (contentHeight <= clientHeight || clientHeight <= 0) {
        SetRectEmpty(&outThumb);
        return;
    }
    // Thumb height proportional to visible fraction
    int thumbH = max(kScrollbarMinThumb,
                     (int)((long long)clientHeight * clientHeight / contentHeight));
    int trackRange = clientHeight - thumbH;
    int maxScroll  = contentHeight - clientHeight;
    int thumbY     = (maxScroll > 0) ? (int)((long long)scrollOffset * trackRange / maxScroll) : 0;

    outThumb.left   = trackX;
    outThumb.top    = thumbY;
    outThumb.right  = trackX + kScrollbarWidth;
    outThumb.bottom = thumbY + thumbH;
}

RECT ProgressBarRenderer::GetScrollbarRect(int clientWidth, int clientHeight) const {
    RECT r;
    r.left   = clientWidth - kScrollbarWidth;
    r.top    = 0;
    r.right  = clientWidth;
    r.bottom = clientHeight;
    return r;
}

// ---------------------------------------------------------------------------
// Draw scrollbar
// ---------------------------------------------------------------------------

void ProgressBarRenderer::DrawScrollbar(HDC hdc, int clientWidth, int clientHeight,
                                         int contentHeight, int scrollOffset, bool thumbHovered) {
    if (contentHeight <= clientHeight) return; // No scrollbar needed

    int trackX = clientWidth - kScrollbarWidth;

    // Track
    RECT trackRect = { trackX, 0, clientWidth, clientHeight };
    HBRUSH hTrackBrush = CreateSolidBrush(colors_.scrollTrackColor);
    FillRect(hdc, &trackRect, hTrackBrush);
    DeleteObject(hTrackBrush);

    // Thumb
    RECT thumbRect;
    CalcThumbRect(clientHeight, contentHeight, scrollOffset, trackX, thumbRect);

    COLORREF thumbColor = thumbHovered ? colors_.scrollThumbHover : colors_.scrollThumbColor;
    HBRUSH hThumbBrush = CreateSolidBrush(thumbColor);
    FillRect(hdc, &thumbRect, hThumbBrush);
    DeleteObject(hThumbBrush);
}

// ---------------------------------------------------------------------------
// Hit-test scrollbar
// ---------------------------------------------------------------------------

ScrollHitZone ProgressBarRenderer::HitTestScrollbar(int mouseX, int mouseY,
                                                     int clientWidth, int clientHeight,
                                                     int contentHeight, int scrollOffset) const {
    if (contentHeight <= clientHeight) return ScrollHitZone::None;

    int trackX = clientWidth - kScrollbarWidth;
    if (mouseX < trackX || mouseX >= clientWidth) return ScrollHitZone::None;
    if (mouseY < 0 || mouseY >= clientHeight)      return ScrollHitZone::None;

    RECT thumbRect;
    CalcThumbRect(clientHeight, contentHeight, scrollOffset, trackX, thumbRect);

    if (mouseY >= thumbRect.top && mouseY < thumbRect.bottom)
        return ScrollHitZone::Thumb;

    return ScrollHitZone::Track;
}

// ---------------------------------------------------------------------------
// Convert mouse-Y during drag to scroll offset
// ---------------------------------------------------------------------------

int ProgressBarRenderer::ScrollOffsetFromThumbDrag(int mouseY, int dragStartMouseY, int dragStartOffset,
                                                    int clientHeight, int contentHeight) const {
    if (contentHeight <= clientHeight) return 0;

    int thumbH    = max(kScrollbarMinThumb,
                        (int)((long long)clientHeight * clientHeight / contentHeight));
    int trackRange = clientHeight - thumbH;
    int maxScroll  = contentHeight - clientHeight;

    if (trackRange <= 0) return 0;

    int deltaY = mouseY - dragStartMouseY;
    int newOffset = dragStartOffset + (int)((long long)deltaY * maxScroll / trackRange);

    if (newOffset < 0) newOffset = 0;
    if (newOffset > maxScroll) newOffset = maxScroll;
    return newOffset;
}
