/**
 * subscription.h
 * AI Subscription Service Data Structure Definition
 * Using nlohmann/json for JSON parsing
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "json.hpp"

using json = nlohmann::json;

// Amount value (could be integer or float)
struct Amount
{
    double used;
    std::optional<double> limit;
    std::optional<double> remaining;
    std::string unit;

    // NOTE: formatting is handled by the renderer (renderer.cpp FormatNumber)
};

// Time window
struct Window
{
    std::string id;
    std::string label;
    std::optional<std::string> resets_at;

    // Format reset time (simplified display)
    std::string formatResetTime() const;
};

// Metric
struct Metric
{
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
struct Plan
{
    std::string name;
    std::string type;
    std::optional<std::string> renews_at;
};

// Cost info
struct Cost
{
    double total;
    std::string currency;
    // period field not needed for now
};

// Subscription service
struct Subscription
{
    std::string provider_id;
    std::string display_name;
    std::string name;
    std::string timestamp;
    Plan plan;
    std::vector<Metric> metrics;
    std::optional<Cost> cost;
    std::string status;
    std::optional<std::string> error;  // Error message from provider
};

// Helper: safely extract a numeric value from a JSON field that may be
// a number or a numeric string (e.g. "123.4").
inline double json_to_double(const json& val)
{
    if (val.is_number()) {
        return val.get<double>();
    }
    if (val.is_string()) {
        try {
            return std::stod(val.get<std::string>());
        }
        catch (...) {
            return 0.0;
        }
    }
    return 0.0;
}

// JSON parsing functions
inline void from_json(const json& j, Amount& a)
{
    a.used = json_to_double(j.at("used"));
    if (j.contains("limit") && !j.at("limit").is_null()) {
        a.limit = json_to_double(j.at("limit"));
    }
    if (j.contains("remaining") && !j.at("remaining").is_null()) {
        a.remaining = json_to_double(j.at("remaining"));
    }
    j.at("unit").get_to(a.unit);
}

inline void from_json(const json& j, Window& w)
{
    j.at("id").get_to(w.id);
    j.at("label").get_to(w.label);
    if (j.contains("resets_at") && !j.at("resets_at").is_null()) {
        w.resets_at = j.at("resets_at").get<std::string>();
    }
}

inline void from_json(const json& j, Metric& m)
{
    j.at("name").get_to(m.name);
    j.at("window").get_to(m.window);
    j.at("amount").get_to(m.amount);
}

inline void from_json(const json& j, Plan& p)
{
    j.at("name").get_to(p.name);
    j.at("type").get_to(p.type);
    if (j.contains("renews_at") && !j.at("renews_at").is_null()) {
        p.renews_at = j.at("renews_at").get<std::string>();
    }
}

inline void from_json(const json& j, Cost& c)
{
    j.at("total").get_to(c.total);
    j.at("currency").get_to(c.currency);
}

inline void from_json(const json& j, Subscription& s)
{
    j.at("provider_id").get_to(s.provider_id);
    j.at("display_name").get_to(s.display_name);
    j.at("name").get_to(s.name);
    j.at("timestamp").get_to(s.timestamp);

    // Plan is optional (may be missing when there's an error)
    if (j.contains("plan") && !j.at("plan").is_null()) {
        s.plan = j.at("plan").get<Plan>();
    }
    else {
        s.plan = Plan {"", "", std::nullopt};
    }

    // Metrics may be null when there's an error
    if (j.contains("metrics") && !j.at("metrics").is_null()) {
        j.at("metrics").get_to(s.metrics);
    }
    else {
        s.metrics.clear();
    }

    if (j.contains("cost") && !j.at("cost").is_null()) {
        s.cost = j.at("cost").get<Cost>();
    }

    // Status is optional, defaults to empty string
    if (j.contains("status") && !j.at("status").is_null()) {
        j.at("status").get_to(s.status);
    }
    else {
        s.status = "";
    }

    if (j.contains("error") && !j.at("error").is_null()) {
        s.error = j.at("error").get<std::string>();
    }
}

// Parse function declaration
std::vector<Subscription> ParseSubscriptions(const std::string& jsonString);
