#pragma once

#include <windows.h>

#include <memory>
#include <string>
#include <vector>

#include "subscription.h"

// Theme mode: user preference
enum class ThemeMode
{
    Light,
    Dark,
    System  // Follow system setting
};

// Color scheme for light/dark themes
struct ColorScheme
{
    COLORREF bgColor;           // Window background
    COLORREF barBgColor;        // Progress bar track background
    COLORREF barLowColor;       // Bar fill < 50%
    COLORREF barMediumColor;    // Bar fill 50-80%
    COLORREF barHighColor;      // Bar fill >= 80%
    COLORREF textColor;         // Primary text
    COLORREF subTextColor;      // Secondary / dimmed text
    COLORREF loadingTextColor;  // "Loading..." text
    COLORREF errorTextColor;    // Error message text
    COLORREF noDataTextColor;   // "No data available" text
    COLORREF resetTimeColor;    // Remaining-time text on progress bar

    // Custom scrollbar
    COLORREF scrollTrackColor;  // Scrollbar track (gutter)
    COLORREF scrollThumbColor;  // Scrollbar thumb (normal)
    COLORREF scrollThumbHover;  // Scrollbar thumb (hovered / dragging)
};

// Detect whether the OS is currently in dark mode
bool IsSystemDarkMode();

// Resolve the effective dark/light based on ThemeMode + system state
bool IsDarkModeActive(ThemeMode mode);

// Get the color scheme for the resolved theme
const ColorScheme& GetLightScheme();
const ColorScheme& GetDarkScheme();
const ColorScheme& GetColorScheme(bool dark);

// ---------------------------------------------------------------------------
// Custom scrollbar constants
// ---------------------------------------------------------------------------
inline constexpr int kScrollbarWidth = 8;      // Thin modern scrollbar
inline constexpr int kScrollbarMinThumb = 30;  // Minimum thumb height in pixels

// ---------------------------------------------------------------------------
// Scrollbar hit-test result
// ---------------------------------------------------------------------------
enum class ScrollHitZone
{
    None,
    Track,  // Clicked on the gutter (above or below thumb)
    Thumb,  // Clicked on the thumb itself
};

// Utility: create a font with the system default font face ("MS Shell Dlg 2").
// Centralised here to avoid duplicating in multiple .cpp files.
inline HFONT CreateSystemUiFont(int height, int weight = FW_NORMAL)
{
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_OUTLINE_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, VARIABLE_PITCH, L"MS Shell Dlg 2");
}

// Progress bar renderer with proper Chinese text support
class ProgressBarRenderer
{
public:
    ProgressBarRenderer();
    ~ProgressBarRenderer();

    // Set window dimensions
    void SetWindowSize(int width, int height);

    // Handle DPI change (recreate fonts with new DPI)
    void OnDpiChanged(UINT newDpi);

    // Set the active color scheme
    void SetColorScheme(const ColorScheme& scheme);
    const ColorScheme& GetActiveScheme() const
    {
        return colors_;
    }

    // Compact mode: smaller bars, less info, tighter spacing
    void SetCompact(bool compact);
    bool IsCompact() const
    {
        return compact_;
    }

    // Render all subscription progress bars.
    // scrollOffset: vertical pixel offset applied before drawing.
    // Returns total content height (for scroll range calculation).
    int Render(HDC hdc, const std::vector<Subscription>& subscriptions, int scrollOffset = 0);

    // Calculate total content height without rendering
    int CalculateContentHeight(const std::vector<Subscription>& subscriptions) const;

    // Calculate minimum content width (compact mode auto-sizing)
    int CalculateContentWidth(HDC hdc, const std::vector<Subscription>& subscriptions) const;

    // ----- Custom scrollbar -----

    // Draw the scrollbar on top of the already-rendered content.
    // Only draws when contentHeight > clientHeight.
    void DrawScrollbar(HDC hdc, int clientWidth, int clientHeight, int contentHeight, int scrollOffset,
                       bool thumbHovered);

    // Hit-test: given a mouse point, return what part of the scrollbar was hit.
    ScrollHitZone HitTestScrollbar(int mouseX, int mouseY, int clientWidth, int clientHeight, int contentHeight,
                                   int scrollOffset) const;

    // Convert a mouse-Y during thumb drag to a scroll offset.
    int ScrollOffsetFromThumbDrag(int mouseY, int dragStartMouseY, int dragStartOffset, int clientHeight,
                                  int contentHeight) const;

    // Get the scrollbar track rectangle (for cursor change detection).
    RECT GetScrollbarRect(int clientWidth, int clientHeight) const;

private:
    // Render single metric progress bar
    void RenderMetric(HDC hdc, int x, int y, int width, const Metric& metric, const std::string& serviceName);

    // Render service header
    void RenderServiceHeader(HDC hdc, int x, int y, int width, const Subscription& sub);

    // Render error message bar (when provider returns error)
    void RenderErrorBar(HDC hdc, int x, int y, int width, const std::string& errorMessage);

    // Draw a progress bar using cached brushes
    void DrawProgressBar(HDC hdcDest, int x, int y, int width, int height, double percentage, HBRUSH fillBrush);

    // Scrollbar geometry helpers
    void CalcThumbRect(int clientHeight, int contentHeight, int scrollOffset, int trackX, RECT& outThumb) const;

    // Active color scheme
    ColorScheme colors_;

    // Layout helpers (values depend on compact_ flag)
    int Margin() const
    {
        return compact_ ? 8 : 16;
    }
    int BarHeight() const
    {
        return compact_ ? 26 : 44;
    }
    int HeaderHeight() const
    {
        return compact_ ? 26 : 32;
    }  // same as BarHeight in compact
    int ItemSpacing() const
    {
        return compact_ ? 6 : 14;
    }
    int ServiceSpacing() const
    {
        return compact_ ? 0 : 24;
    }

    int windowWidth_;
    int windowHeight_;
    bool compact_;

    // Fonts
    HFONT hFontNormal_;
    HFONT hFontBold_;
    HFONT hFontSmall_;

    // DPI scale factor (1.0 = 96 DPI, standard)
    float dpiScale_;

    // Cached GDI brushes (created in SetColorScheme / CreateBrushes, destroyed in DestroyBrushes)
    HBRUSH hBrushBarBg_;        // Progress bar track background
    HBRUSH hBrushBarLow_;       // Bar fill < 50%
    HBRUSH hBrushBarMedium_;    // Bar fill 50-80%
    HBRUSH hBrushBarHigh_;      // Bar fill >= 80%
    HBRUSH hBrushScrollTrack_;  // Scrollbar track
    HBRUSH hBrushScrollThumb_;  // Scrollbar thumb (normal)
    HBRUSH hBrushScrollHover_;  // Scrollbar thumb (hovered)

    void CreateFonts();
    void DestroyFonts();
    void CreateBrushes();
    void DestroyBrushes();
};
