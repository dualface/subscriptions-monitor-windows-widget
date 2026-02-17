/**
 * subscription.h
 * AI 订阅服务数据结构定义
 * 使用 nlohmann/json 进行 JSON 解析
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include "json.hpp"

using json = nlohmann::json;

// 用量数值（可能是整数或浮点数）
struct Amount {
    double used;
    std::optional<double> limit;
    std::optional<double> remaining;
    std::string unit;
    
    // 格式化用量显示
    std::string formatUsed() const;
    std::string formatLimit() const;
    std::string formatRemaining() const;
};

// 时间窗口
struct Window {
    std::string id;
    std::string label;
    std::optional<std::string> resets_at;
    
    // 格式化重置时间（简化显示）
    std::string formatResetTime() const;
};

// 指标
struct Metric {
    std::string name;
    Window window;
    Amount amount;
    
    // 计算百分比（如果有 limit）
    std::optional<int> percentage() const {
        if (amount.limit.has_value() && *amount.limit > 0) {
            return static_cast<int>((amount.used / *amount.limit) * 100);
        }
        return std::nullopt;
    }
    
    // 是否有进度条（有 limit 才显示进度）
    bool hasProgress() const {
        return amount.limit.has_value();
    }
    
    // 获取显示标题
    std::string getDisplayTitle() const;
};

// 套餐计划
struct Plan {
    std::string name;
    std::string type;
    std::optional<std::string> renews_at;
};

// 成本信息
struct Cost {
    double total;
    std::string currency;
    // period 字段暂时不需要
};

// 订阅服务
struct Subscription {
    std::string provider_id;
    std::string display_name;
    std::string name;
    std::string timestamp;
    Plan plan;
    std::vector<Metric> metrics;
    std::optional<Cost> cost;
    std::string status;
    
    // 获取可显示的指标（有过滤条件的）
    std::vector<Metric> getDisplayMetrics() const;
};

// JSON 解析函数
inline void from_json(const json& j, Amount& a) {
    j.at("used").get_to(a.used);
    if (j.contains("limit")) j.at("limit").get_to(a.limit);
    if (j.contains("remaining")) j.at("remaining").get_to(a.remaining);
    j.at("unit").get_to(a.unit);
}

inline void from_json(const json& j, Window& w) {
    j.at("id").get_to(w.id);
    j.at("label").get_to(w.label);
    if (j.contains("resets_at")) j.at("resets_at").get_to(w.resets_at);
}

inline void from_json(const json& j, Metric& m) {
    j.at("name").get_to(m.name);
    j.at("window").get_to(m.window);
    j.at("amount").get_to(m.amount);
}

inline void from_json(const json& j, Plan& p) {
    j.at("name").get_to(p.name);
    j.at("type").get_to(p.type);
    if (j.contains("renews_at")) j.at("renews_at").get_to(p.renews_at);
}

inline void from_json(const json& j, Cost& c) {
    j.at("total").get_to(c.total);
    j.at("currency").get_to(c.currency);
}

inline void from_json(const json& j, Subscription& s) {
    j.at("provider_id").get_to(s.provider_id);
    j.at("display_name").get_to(s.display_name);
    j.at("name").get_to(s.name);
    j.at("timestamp").get_to(s.timestamp);
    j.at("plan").get_to(s.plan);
    j.at("metrics").get_to(s.metrics);
    if (j.contains("cost")) j.at("cost").get_to(s.cost);
    j.at("status").get_to(s.status);
}

// 解析函数声明
std::vector<Subscription> ParseSubscriptions(const std::string& jsonString);
