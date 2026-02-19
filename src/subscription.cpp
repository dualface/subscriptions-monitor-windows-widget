#include <iomanip>
#include <sstream>
#include <stdexcept>

#include "subscription.h"

std::string Window::formatResetTime() const
{
    if (!resets_at.has_value()) {
        return "";
    }

    // 简化 ISO 8601 时间格式
    // 输入: "2026-02-20T07:36:05Z"
    // 输出: "02-20 07:36"
    const std::string& full = *resets_at;
    if (full.length() >= 16) {
        std::string result = full.substr(5, 11);  // "MM-DDTHH:MM"
        // Replace the 'T' separator with a space for display
        if (result.length() >= 6 && result[5] == 'T') {
            result[5] = ' ';
        }
        return result;
    }
    return full;
}

std::optional<double> Metric::percentage() const
{
    if (amount.limit.has_value() && *amount.limit > 0) {
        return (amount.used / *amount.limit) * 100.0;
    }
    return std::nullopt;
}

bool Metric::hasProgress() const
{
    return amount.limit.has_value();
}

std::string Metric::getDisplayTitle() const
{
    return name;
}

std::vector<Subscription> ParseSubscriptions(const std::string& jsonString)
{
    try {
        auto j = json::parse(jsonString);
        if (!j.is_array()) {
            throw std::runtime_error("Expected JSON array, got " + std::string(j.type_name()));
        }
        std::vector<Subscription> subs;
        subs.reserve(j.size());
        for (size_t i = 0; i < j.size(); ++i) {
            try {
                subs.push_back(j[i].get<Subscription>());
            }
            catch (const json::exception&) {
                continue;
            }
        }
        return subs;
    }
    catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("Invalid JSON: ") + e.what());
    }
}
