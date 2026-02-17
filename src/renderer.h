#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include "subscription.h"

// 进度条渲染器
class ProgressBarRenderer {
public:
    ProgressBarRenderer();
    
    // 设置窗口尺寸
    void SetWindowSize(int width, int height);
    
    // 渲染所有订阅服务的进度条
    void Render(HDC hdc, const std::vector<Subscription>& subscriptions);
    
private:
    // 渲染单个指标进度条
    void RenderMetric(HDC hdc, int x, int y, int width, const Metric& metric, const std::string& serviceName);
    
    // 渲染服务标题
    void RenderServiceHeader(HDC hdc, int x, int y, int width, const Subscription& sub);
    
    // 双缓冲绘制
    void DrawProgressBar(HDC hdcDest, int x, int y, int width, int height, 
                         int percentage, COLORREF barColor);
    
    // 工具函数
    void DrawTextCentered(HDC hdc, const std::wstring& text, RECT& rect, 
                          COLORREF color, int fontSize = 14);
    void DrawTextLeft(HDC hdc, const std::wstring& text, RECT& rect, 
                      COLORREF color, int fontSize = 12);
    
    // 颜色定义
    static const COLORREF kBgColor;
    static const COLORREF kBarBgColor;
    static const COLORREF kBarLowColor;
    static const COLORREF kBarMediumColor;
    static const COLORREF kBarHighColor;
    static const COLORREF kTextColor;
    static const COLORREF kSubTextColor;
    
    // 布局常量
    static const int kMargin = 16;
    static const int kBarHeight = 40;
    static const int kHeaderHeight = 30;
    static const int kItemSpacing = 12;
    static const int kServiceSpacing = 24;
    
    int windowWidth_;
    int windowHeight_;
};
