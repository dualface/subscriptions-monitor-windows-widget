/**
 * subscription.h
 * AI Subscription Service Data Structure Definition
 * Using nlohmann/json for JSON parsing
 */

#pragma once

#include <string>
#include <vector>

// Workaround for LSP not recognizing std::optional in MSVC
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
    #include <optional>
#else
    #error "C++17 or later is required"
#endif

#include "json.hpp"

using json = nlohmann::json;

// Amount value (could be integer or float)
struct Amount {
    double used;
    std::optional<double> limit;
    std::optional<double> remaining;
    std::string unit;
    
    // Format amount display
    std::string formatUsed() const;
    std::string formatLimit() const;
    std::string formatRemaining() const;
};

// Time window
struct Window {
    std::string id;
    std::string label;
    std::optional<std::string> resets_at;
    
    // Format reset time (simplified display)
    std::string formatResetTime() const;
};

// Metric
struct Metric {
    std::string name;
    Window window;
    Amount amount;
    
    // Calculate percentage (if limit exists)
    std::optional<double> percentage() const;
    
    // Whether to show progress bar (only if limit exists)
    bool hasProgress() const;
    
    // Get display title
    std::string getDisplayTitle() const;
};

// Plan
struct Plan {
    std::string name;
    std::string type;
    std::optional<std::string> renews_at;
};

// Cost info
struct Cost {
    double total;
    std::string currency;
    // period field not needed for now
};

// Subscription service
struct Subscription {
    std::string provider_id;
    std::string display_name;
    std::string name;
    std::string timestamp;
    Plan plan;
    std::vector<Metric> metrics;
    std::optional<Cost> cost;
    std::string status;
    

};

// JSON parsing functions
inline void from_json(const json& j, Amount& a) {
    j.at("used").get_to(a.used);
    if (j.contains("limit") && !j.at("limit").is_null()) {
        a.limit = j.at("limit").get<double>();
    }
    if (j.contains("remaining") && !j.at("remaining").is_null()) {
        a.remaining = j.at("remaining").get<double>();
    }
    j.at("unit").get_to(a.unit);
}

inline void from_json(const json& j, Window& w) {
    j.at("id").get_to(w.id);
    j.at("label").get_to(w.label);
    if (j.contains("resets_at") && !j.at("resets_at").is_null()) {
        w.resets_at = j.at("resets_at").get<std::string>();
    }
}

inline void from_json(const json& j, Metric& m) {
    j.at("name").get_to(m.name);
    j.at("window").get_to(m.window);
    j.at("amount").get_to(m.amount);
}

inline void from_json(const json& j, Plan& p) {
    j.at("name").get_to(p.name);
    j.at("type").get_to(p.type);
    if (j.contains("renews_at") && !j.at("renews_at").is_null()) {
        p.renews_at = j.at("renews_at").get<std::string>();
    }
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
    if (j.contains("cost") && !j.at("cost").is_null()) {
        s.cost = j.at("cost").get<Cost>();
    }
    j.at("status").get_to(s.status);
}

// Parse function declaration
std::vector<Subscription> ParseSubscriptions(const std::string& jsonString);
