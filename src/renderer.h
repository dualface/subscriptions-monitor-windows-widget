#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <memory>
#include "subscription.h"

// Progress bar renderer with proper Chinese text support
class ProgressBarRenderer {
public:
    ProgressBarRenderer();
    ~ProgressBarRenderer();
    
    // Set window dimensions
    void SetWindowSize(int width, int height);
    
    // Handle DPI change (recreate fonts with new DPI)
    void OnDpiChanged(UINT newDpi);
    
    // Render all subscription progress bars
    void Render(HDC hdc, const std::vector<Subscription>& subscriptions);
    
    // Get total height for virtual scrolling (optional)
    int GetTotalHeight() const;
    
private:
    // Render single metric progress bar
    void RenderMetric(HDC hdc, int x, int y, int width, const Metric& metric, const std::string& serviceName);
    
    // Render service header
    void RenderServiceHeader(HDC hdc, int x, int y, int width, const Subscription& sub);
    
    // Double-buffered drawing
    void DrawProgressBar(HDC hdcDest, int x, int y, int width, int height, 
                         int percentage, COLORREF barColor);
    
    // Utility functions
    void DrawTextCentered(HDC hdc, const std::wstring& text, RECT& rect, 
                          COLORREF color, int fontSize = 14);
    void DrawTextLeft(HDC hdc, const std::wstring& text, RECT& rect, 
                      COLORREF color, int fontSize = 12);
    
    // Color definitions
    static const COLORREF kBgColor;
    static const COLORREF kBarBgColor;
    static const COLORREF kBarLowColor;
    static const COLORREF kBarMediumColor;
    static const COLORREF kBarHighColor;
    static const COLORREF kTextColor;
    static const COLORREF kSubTextColor;
    
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
