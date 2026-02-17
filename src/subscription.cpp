#include "subscription.h"
#include <sstream>
#include <iomanip>

// 格式化大数字（添加千位分隔符）
static std::string FormatNumber(double num) {
    std::ostringstream oss;
    if (num >= 1000000) {
        oss << std::fixed << std::setprecision(2) << (num / 1000000) << "M";
    } else if (num >= 1000) {
        oss << std::fixed << std::setprecision(1) << (num / 1000) << "K";
    } else {
        oss << std::fixed << std::setprecision(0) << num;
    }
    return oss.str();
}

std::string Amount::formatUsed() const {
    return FormatNumber(used);
}

std::string Amount::formatLimit() const {
    if (limit.has_value()) {
        return FormatNumber(*limit);
    }
    return "∞";
}

std::string Amount::formatRemaining() const {
    if (remaining.has_value()) {
        return FormatNumber(*remaining);
    }
    return "N/A";
}

std::string Window::formatResetTime() const {
    if (!resets_at.has_value()) {
        return "";
    }
    
    // 简化 ISO 8601 时间格式
    // 输入: "2026-02-20T07:36:05Z"
    // 输出: "02-20 07:36"
    const std::string& full = *resets_at;
    if (full.length() >= 16) {
        return full.substr(5, 11); // 从月份开始取 MM-DD HH:MM
    }
    return full;
}

std::optional<double> Metric::percentage() const {
    if (amount.limit.has_value() && *amount.limit > 0) {
        return (amount.used / *amount.limit) * 100.0;
    }
    return std::nullopt;
}

bool Metric::hasProgress() const {
    return amount.limit.has_value();
}

std::string Metric::getDisplayTitle() const {
    return name;
}

std::vector<Subscription> ParseSubscriptions(const std::string& jsonString) {
    return json::parse(jsonString).get<std::vector<Subscription>>();
}
