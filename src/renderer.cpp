#include "renderer.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdio>
#include <algorithm>

// Prevent Windows macros from conflicting with std::min/std::max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

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

static std::wstring FormatNumber(double value, const std::string& /*metricName*/) {
    std::wstringstream ss;
    long long intValue = static_cast<long long>(value);
    if (intValue >= 1000000) {
        ss << std::fixed << std::setprecision(1) << intValue / 1000000.0 << L"M";
    } else if (intValue >= 1000) {
        ss << std::fixed << std::setprecision(1) << intValue / 1000.0 << L"K";
    } else if (value == static_cast<double>(intValue)) {
        // Integer value — display without decimal point
        ss << intValue;
    } else {
        ss << std::fixed << std::setprecision(1) << value;
    }
    return ss.str();
}

static std::wstring CalculateRemainingTime(const std::string& resetsAt, bool compact = false) {
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
    if (resetTimestamp <= now) return compact ? L"soon" : L"Refreshing soon";

    double diffSeconds = difftime(resetTimestamp, now);
    int totalMinutes = static_cast<int>(diffSeconds / 60);
    int diffHours   = totalMinutes / 60;
    int diffMinutes = totalMinutes % 60;

    std::wstringstream ss;
    if (compact) {
        // Compact: just "2h15m" or "15m" or "<1m"
        if (diffHours > 0)        ss << diffHours << L"h" << diffMinutes << L"m";
        else if (diffMinutes > 0) ss << diffMinutes << L"m";
        else                      ss << L"<1m";
    } else {
        if (diffHours > 0)        ss << diffHours << L"h " << diffMinutes << L"m";
        else if (diffMinutes > 0) ss << diffMinutes << L"m";
        else                      ss << L"<1m";
        ss << L" refresh";
    }
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
        int metricCount = 0;
        for (const auto& metric : sub.metrics) {
            // In compact mode, skip metrics without a limit (e.g. Total Tokens, API Requests)
            if (compact_ && !metric.amount.limit.has_value()) continue;
            ++metricCount;
        }
        if (compact_ && metricCount == 0) continue;  // skip empty services
        h += HeaderHeight();
        h += metricCount * (BarHeight() + ItemSpacing());
        h += ServiceSpacing();
    }
    // Add bottom margin
    h += Margin();
    return h;
}

// ---------------------------------------------------------------------------
// Content width calculation (compact mode auto-sizing)
// ---------------------------------------------------------------------------

int ProgressBarRenderer::CalculateContentWidth(HDC hdc, const std::vector<Subscription>& subscriptions) const {
    int maxW = 0;
    int pad = compact_ ? 6 : 10;

    // Measure service header text widths
    HFONT hOldFont = (HFONT)SelectObject(hdc, compact_ ? hFontNormal_ : hFontBold_);
    for (const auto& sub : subscriptions) {
        std::wstring name = ToWString(sub.display_name);
        if (!compact_) name += L" - " + ToWString(sub.plan.name);
        SIZE sz;
        GetTextExtentPoint32W(hdc, name.c_str(), (int)name.length(), &sz);
        if (sz.cx > maxW) maxW = sz.cx;
    }

    // Measure metric row widths: label + gap + "100.0%" + gap + reset_time_area
    SelectObject(hdc, hFontNormal_);
    // Pre-measure a worst-case percentage string
    std::wstring pctSample = L"100.0%";
    SIZE pctSize;
    GetTextExtentPoint32W(hdc, pctSample.c_str(), (int)pctSample.length(), &pctSize);
    int resetArea = 90;  // reserved for compact reset time like "23h59m"

    for (const auto& sub : subscriptions) {
        for (const auto& metric : sub.metrics) {
            if (compact_ && !metric.amount.limit.has_value()) continue;
            std::wstring label = compact_ ? ToWString(metric.window.label)
                : ToWString(metric.name) + L" (" + ToWString(metric.window.label) + L")";
            SIZE sz;
            GetTextExtentPoint32W(hdc, label.c_str(), (int)label.length(), &sz);
            int rowW = sz.cx + pad + pctSize.cx + pad + resetArea;
            if (rowW > maxW) maxW = rowW;
        }
    }
    SelectObject(hdc, hOldFont);

    // Add margins and inner padding
    return maxW + 2 * Margin() + 2 * pad;
}

// ---------------------------------------------------------------------------
// Render (with scroll offset)
// ---------------------------------------------------------------------------

int ProgressBarRenderer::Render(HDC hdc, const std::vector<Subscription>& subscriptions, int scrollOffset) {
    int currentY = Margin() - scrollOffset;

    for (const auto& sub : subscriptions) {
        // In compact mode, check if this service has any displayable metrics
        if (compact_) {
            bool hasDisplayable = false;
            for (const auto& m : sub.metrics) {
                if (m.amount.limit.has_value()) { hasDisplayable = true; break; }
            }
            if (!hasDisplayable) continue;
        }

        if (currentY + HeaderHeight() > 0 && currentY < windowHeight_) {
            RenderServiceHeader(hdc, Margin(), currentY, windowWidth_ - 2 * Margin(), sub);
        }
        currentY += HeaderHeight();

        for (const auto& metric : sub.metrics) {
            if (compact_ && !metric.amount.limit.has_value()) continue;
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
    HFONT hOldFont = (HFONT)SelectObject(hdc, compact_ ? hFontNormal_ : hFontBold_);

    std::wstringstream ss;
    if (compact_)
        ss << ToWString(sub.display_name);
    else
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

    // DPI-scaled layout offsets for reset-time areas
    int resetAreaCompact = static_cast<int>(90 * dpiScale_);
    int resetAreaNormal  = static_cast<int>(150 * dpiScale_);
    int resetStartNormal = static_cast<int>(140 * dpiScale_);

    if (compact_) {
        // Compact layout: draw text AFTER progress bar to ensure visibility
        // Reserve space: [label][percentage][reset_time]
        std::wstring labelW = ToWString(metric.window.label);
        SIZE labelSize;
        GetTextExtentPoint32W(hdc, labelW.c_str(), (int)labelW.length(), &labelSize);
        
        std::wstringstream pctSs;
        pctSs << std::fixed << std::setprecision(0) << percentage << L"%";
        SIZE pctSize;
        GetTextExtentPoint32W(hdc, pctSs.str().c_str(), (int)pctSs.str().length(), &pctSize);
        
        std::wstring remainingW;
        SIZE remainingSize = {0, 0};
        if (metric.window.resets_at.has_value()) {
            remainingW = CalculateRemainingTime(*metric.window.resets_at, true);
            if (!remainingW.empty()) {
                GetTextExtentPoint32W(hdc, remainingW.c_str(), (int)remainingW.length(), &remainingSize);
            }
        }
        
        // Calculate layout with proper spacing
        int totalTextWidth = labelSize.cx + pad + pctSize.cx + pad + remainingSize.cx;
        int availableWidth = width - 2 * pad;
        
        // If text is too wide, reduce label area but keep percentage visible
        int labelMaxWidth = labelSize.cx;
        if (totalTextWidth > availableWidth && remainingSize.cx > 0) {
            // Prioritize: percentage > remaining time > label
            labelMaxWidth = availableWidth - pctSize.cx - pad - remainingSize.cx - pad;
            if (labelMaxWidth < 30) {
                // Not enough space for all, drop remaining time
                remainingW.clear();
                labelMaxWidth = availableWidth - pctSize.cx - pad;
            }
        }
        
        int labelEnd = x + pad + std::min(labelMaxWidth, static_cast<int>(labelSize.cx));
        int pctStart = labelEnd + pad;
        int pctEnd = pctStart + pctSize.cx;
        int remainingStart = remainingW.empty() ? pctEnd : pctEnd + pad;
        
        // Draw label (clipped if needed)
        if (labelMaxWidth > 0) {
            RECT labelRect = { x + pad, y, labelEnd, y + barH };
            DrawTextW(hdc, labelW.c_str(), -1, &labelRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        
        // Draw percentage (always visible, right-aligned in its space)
        RECT pctRect = { pctStart, y, pctEnd + 5, y + barH };  // +5 for safety margin
        DrawTextW(hdc, pctSs.str().c_str(), -1, &pctRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        
        // Draw remaining time (if space permits)
        if (!remainingW.empty()) {
            RECT remainingRect = { remainingStart, y, x + width - pad, y + barH };
            SetTextColor(hdc, colors_.resetTimeColor);
            DrawTextW(hdc, remainingW.c_str(), -1, &remainingRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
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
        RECT middleRect = { middleStart, y, x + width - resetAreaNormal, y + barH };
        DrawTextW(hdc, middleSs.str().c_str(), -1, &middleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        // Right: remaining time
        if (metric.window.resets_at.has_value()) {
            std::wstring remainingTime = CalculateRemainingTime(*metric.window.resets_at);
            if (!remainingTime.empty()) {
                RECT rightRect = { x + width - resetStartNormal, y, x + width - pad, y + barH };
                SetTextColor(hdc, colors_.resetTimeColor);
                DrawTextW(hdc, remainingTime.c_str(), -1, &rightRect, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }
    }

    SelectObject(hdc, hOldFont);
}

// ---------------------------------------------------------------------------
// Progress bar (draws directly into the caller's DC — the caller already
// provides a double-buffered memory DC from OnPaint)
// ---------------------------------------------------------------------------

void ProgressBarRenderer::DrawProgressBar(HDC hdc, int x, int y, int width, int height,
                                           double percentage, COLORREF barColor) {
    RECT bgRect = { x, y, x + width, y + height };
    HBRUSH hBgBrush = CreateSolidBrush(colors_.barBgColor);
    FillRect(hdc, &bgRect, hBgBrush);
    DeleteObject(hBgBrush);

    int fillWidth = static_cast<int>((width * percentage) / 100.0);
    if (fillWidth > 0) {
        RECT fillRect = { x, y, x + fillWidth, y + height };
        HBRUSH hFillBrush = CreateSolidBrush(barColor);
        FillRect(hdc, &fillRect, hFillBrush);
        DeleteObject(hFillBrush);
    }
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
    int thumbH = std::max(kScrollbarMinThumb,
                          static_cast<int>((static_cast<long long>(clientHeight) * clientHeight) / contentHeight));
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

    int thumbH    = std::max(kScrollbarMinThumb,
                             static_cast<int>((static_cast<long long>(clientHeight) * clientHeight) / contentHeight));
    int trackRange = clientHeight - thumbH;
    int maxScroll  = contentHeight - clientHeight;

    if (trackRange <= 0) return 0;

    int deltaY = mouseY - dragStartMouseY;
    int newOffset = dragStartOffset + (int)((long long)deltaY * maxScroll / trackRange);

    if (newOffset < 0) newOffset = 0;
    if (newOffset > maxScroll) newOffset = maxScroll;
    return newOffset;
}
