#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include "subscription.h"

// Theme mode: user preference
enum class ThemeMode {
    Light,
    Dark,
    System  // Follow system setting
};

// Color scheme for light/dark themes
struct ColorScheme {
    COLORREF bgColor;          // Window background
    COLORREF barBgColor;       // Progress bar track background
    COLORREF barLowColor;      // Bar fill < 50%
    COLORREF barMediumColor;   // Bar fill 50-80%
    COLORREF barHighColor;     // Bar fill >= 80%
    COLORREF textColor;        // Primary text
    COLORREF subTextColor;     // Secondary / dimmed text
    COLORREF loadingTextColor; // "Loading..." text
    COLORREF errorTextColor;   // Error message text
    COLORREF noDataTextColor;  // "No data available" text
    COLORREF resetTimeColor;   // Remaining-time text on progress bar

    // Custom scrollbar
    COLORREF scrollTrackColor; // Scrollbar track (gutter)
    COLORREF scrollThumbColor; // Scrollbar thumb (normal)
    COLORREF scrollThumbHover; // Scrollbar thumb (hovered / dragging)
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
static const int kScrollbarWidth = 8;       // Thin modern scrollbar
static const int kScrollbarMinThumb = 30;   // Minimum thumb height in pixels

// ---------------------------------------------------------------------------
// Scrollbar hit-test result
// ---------------------------------------------------------------------------
enum class ScrollHitZone {
    None,
    Track,      // Clicked on the gutter (above or below thumb)
    Thumb,      // Clicked on the thumb itself
};

// Progress bar renderer with proper Chinese text support
class ProgressBarRenderer {
public:
    ProgressBarRenderer();
    ~ProgressBarRenderer();
    
    // Set window dimensions
    void SetWindowSize(int width, int height);
    
    // Handle DPI change (recreate fonts with new DPI)
    void OnDpiChanged(UINT newDpi);
    
    // Set the active color scheme
    void SetColorScheme(const ColorScheme& scheme);
    const ColorScheme& GetActiveScheme() const { return colors_; }
    
    // Render all subscription progress bars.
    // scrollOffset: vertical pixel offset applied before drawing.
    // Returns total content height (for scroll range calculation).
    int Render(HDC hdc, const std::vector<Subscription>& subscriptions, int scrollOffset = 0);
    
    // Calculate total content height without rendering
    int CalculateContentHeight(const std::vector<Subscription>& subscriptions) const;

    // ----- Custom scrollbar -----

    // Draw the scrollbar on top of the already-rendered content.
    // Only draws when contentHeight > clientHeight.
    void DrawScrollbar(HDC hdc, int clientWidth, int clientHeight,
                       int contentHeight, int scrollOffset, bool thumbHovered);

    // Hit-test: given a mouse point, return what part of the scrollbar was hit.
    ScrollHitZone HitTestScrollbar(int mouseX, int mouseY,
                                   int clientWidth, int clientHeight,
                                   int contentHeight, int scrollOffset) const;

    // Convert a mouse-Y during thumb drag to a scroll offset.
    int ScrollOffsetFromThumbDrag(int mouseY, int dragStartMouseY, int dragStartOffset,
                                  int clientHeight, int contentHeight) const;

    // Get the scrollbar track rectangle (for cursor change detection).
    RECT GetScrollbarRect(int clientWidth, int clientHeight) const;
    
private:
    // Render single metric progress bar
    void RenderMetric(HDC hdc, int x, int y, int width, const Metric& metric, const std::string& serviceName);
    
    // Render service header
    void RenderServiceHeader(HDC hdc, int x, int y, int width, const Subscription& sub);
    
    // Double-buffered drawing
    void DrawProgressBar(HDC hdcDest, int x, int y, int width, int height, 
                         double percentage, COLORREF barColor);

    // Scrollbar geometry helpers
    void CalcThumbRect(int clientHeight, int contentHeight, int scrollOffset,
                       int trackX, RECT& outThumb) const;

    // Active color scheme
    ColorScheme colors_;
    
    // Layout constants
    static const int kMargin = 16;
    static const int kBarHeight = 44;
    static const int kHeaderHeight = 32;
    static const int kItemSpacing = 14;
    static const int kServiceSpacing = 24;
    
    int windowWidth_;
    int windowHeight_;
    
    // Fonts
    HFONT hFontNormal_;
    HFONT hFontBold_;
    HFONT hFontSmall_;
    
    // DPI scale factor (1.0 = 96 DPI, standard)
    float dpiScale_;
    
    void CreateFonts();
    void DestroyFonts();
};
