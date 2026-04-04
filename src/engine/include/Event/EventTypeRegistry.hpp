#pragma once

#include "Event/EventFormat.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace engine {

struct EventTypeMapping {
    std::string_view event_type;
    Event_Core::Type engine_type;
};

inline constexpr EventTypeMapping kCanonicalEventTypeMappings[] = {
    {EventTypes::SYSTEM_STARTUP, static_cast<Event_Core::Type>(1001)},
    {EventTypes::SYSTEM_SHUTDOWN, static_cast<Event_Core::Type>(1002)},
    {EventTypes::SYSTEM_HEARTBEAT, static_cast<Event_Core::Type>(1003)},
    {EventTypes::SYSTEM_ERROR, static_cast<Event_Core::Type>(1004)},
    {EventTypes::SYSTEM_WARNING, static_cast<Event_Core::Type>(1005)},
    {EventTypes::SYSTEM_INFO, static_cast<Event_Core::Type>(1006)},
    {EventTypes::MARKET_TICK, static_cast<Event_Core::Type>(2001)},
    {"market.bar.1m", static_cast<Event_Core::Type>(2002)},
    {"market.bar.5m", static_cast<Event_Core::Type>(2003)},
    {"market.bar.1h", static_cast<Event_Core::Type>(2004)},
    {"market.bar.1d", static_cast<Event_Core::Type>(2005)},
    {EventTypes::MARKET_SNAPSHOT, static_cast<Event_Core::Type>(2006)},
    {EventTypes::MARKET_BAR, static_cast<Event_Core::Type>(2007)},
    {EventTypes::MARKET_DEPTH, static_cast<Event_Core::Type>(2008)},
    {EventTypes::MARKET_TRADE, static_cast<Event_Core::Type>(2009)},
    {EventTypes::MARKET_QUOTE, static_cast<Event_Core::Type>(2010)},
    {EventTypes::MARKET_WATCH_ENSURE, static_cast<Event_Core::Type>(2011)},
    {EventTypes::TRADING_MARKET_TICK, static_cast<Event_Core::Type>(2101)},
    {EventTypes::TRADING_MARKET_BAR, static_cast<Event_Core::Type>(2102)},
    {EventTypes::TRADING_MARKET_CONNECTED, static_cast<Event_Core::Type>(2103)},
    {EventTypes::TRADING_MARKET_DISCONNECTED, static_cast<Event_Core::Type>(2104)},
    {EventTypes::ORDER_NEW, static_cast<Event_Core::Type>(3001)},
    {EventTypes::ORDER_FILL, static_cast<Event_Core::Type>(3002)},
    {EventTypes::ORDER_CANCEL, static_cast<Event_Core::Type>(3003)},
    {EventTypes::ORDER_REJECT, static_cast<Event_Core::Type>(3004)},
    {EventTypes::ORDER_UPDATE, static_cast<Event_Core::Type>(3005)},
    {EventTypes::TRADING_ORDER_SUBMIT_REQUEST, static_cast<Event_Core::Type>(3106)},
    {EventTypes::TRADING_ORDER_CANCEL_REQUEST, static_cast<Event_Core::Type>(3108)},
    {EventTypes::TRADING_ORDER_UPDATED, static_cast<Event_Core::Type>(3101)},
    {EventTypes::TRADING_POSITION_UPDATED, static_cast<Event_Core::Type>(3102)},
    {EventTypes::TRADING_ACCOUNT_UPDATED, static_cast<Event_Core::Type>(3103)},
    {EventTypes::TRADING_EXECUTION_REPORT, static_cast<Event_Core::Type>(3104)},
    {EventTypes::TRADING_SESSION_ERROR, static_cast<Event_Core::Type>(3105)},
    {EventTypes::STRATEGY_SIGNAL, static_cast<Event_Core::Type>(4001)},
    {EventTypes::STRATEGY_POSITION, static_cast<Event_Core::Type>(4002)},
    {EventTypes::STRATEGY_PNL, static_cast<Event_Core::Type>(4003)},
    {EventTypes::STRATEGY_STATUS, static_cast<Event_Core::Type>(4004)},
    {EventTypes::STRATEGY_PARAM, static_cast<Event_Core::Type>(4005)},
    {EventTypes::RISK_WARNING, static_cast<Event_Core::Type>(5001)},
    {EventTypes::RISK_LIMIT, static_cast<Event_Core::Type>(5002)},
    {EventTypes::RISK_BREACH, static_cast<Event_Core::Type>(5003)},
    {EventTypes::RISK_CHECK, static_cast<Event_Core::Type>(5004)},
    {EventTypes::RISK_ALERT, static_cast<Event_Core::Type>(5005)},
    {EventTypes::RISK_APPROVAL, static_cast<Event_Core::Type>(5006)},
    {EventTypes::RISK_REJECT, static_cast<Event_Core::Type>(5007)},
    {EventTypes::BACKTEST_START, static_cast<Event_Core::Type>(6001)},
    {EventTypes::BACKTEST_RESULT, static_cast<Event_Core::Type>(6002)},
    {EventTypes::BACKTEST_ERROR, static_cast<Event_Core::Type>(6003)},
    {EventTypes::BACKTEST_PROGRESS, static_cast<Event_Core::Type>(6004)},
    {EventTypes::DATA_UPDATE, static_cast<Event_Core::Type>(7001)},
    {EventTypes::DATA_SYNC, static_cast<Event_Core::Type>(7002)},
    {EventTypes::DATA_ERROR, static_cast<Event_Core::Type>(7003)},
    {EventTypes::PERFORMANCE_METRIC, static_cast<Event_Core::Type>(8001)},
    {EventTypes::PERFORMANCE_ALERT, static_cast<Event_Core::Type>(8002)},
};

inline constexpr EventTypeMapping kLegacyEventTypeAliases[] = {
    {"system_startup", static_cast<Event_Core::Type>(1001)},
    {"system_shutdown", static_cast<Event_Core::Type>(1002)},
    {"error", static_cast<Event_Core::Type>(1004)},
    {"market_tick", static_cast<Event_Core::Type>(2001)},
    {"market_bar", static_cast<Event_Core::Type>(2007)},
    {"market_snapshot", static_cast<Event_Core::Type>(2006)},
    {"market_depth", static_cast<Event_Core::Type>(2008)},
    {"market_trade", static_cast<Event_Core::Type>(2009)},
    {"market_quote", static_cast<Event_Core::Type>(2010)},
    {"order_created", static_cast<Event_Core::Type>(3001)},
    {"order_updated", static_cast<Event_Core::Type>(3005)},
    {"order_filled", static_cast<Event_Core::Type>(3002)},
    {"order_cancelled", static_cast<Event_Core::Type>(3003)},
    {"order.filled", static_cast<Event_Core::Type>(3002)},
    {"order.cancelled", static_cast<Event_Core::Type>(3003)},
    {"order.rejected", static_cast<Event_Core::Type>(3004)},
    {"order.partially_filled", static_cast<Event_Core::Type>(3005)},
    {"signal_generated", static_cast<Event_Core::Type>(4001)},
    {"strategy_started", static_cast<Event_Core::Type>(4004)},
    {"strategy_stopped", static_cast<Event_Core::Type>(4004)},
    {"strategy.position_update", static_cast<Event_Core::Type>(4002)},
    {"strategy.pnl_update", static_cast<Event_Core::Type>(4003)},
    {"risk_alert", static_cast<Event_Core::Type>(5005)},
    {"position_updated", static_cast<Event_Core::Type>(3102)},
    {"risk.limit_reached", static_cast<Event_Core::Type>(5002)},
    {"risk.order_blocked", static_cast<Event_Core::Type>(5003)},
};

inline constexpr bool event_type_has_prefix(std::string_view value, std::string_view prefix)
{
    return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

inline std::optional<Event_Core::Type> lookup_registered_event_type(std::string_view event_type)
{
    for (const auto& mapping : kCanonicalEventTypeMappings) {
        if (mapping.event_type == event_type) {
            return mapping.engine_type;
        }
    }

    return std::nullopt;
}

inline std::optional<Event_Core::Type> lookup_legacy_event_type_alias(std::string_view event_type)
{
    for (const auto& mapping : kLegacyEventTypeAliases) {
        if (mapping.event_type == event_type) {
            return mapping.engine_type;
        }
    }

    return std::nullopt;
}

inline Event_Core::Type infer_event_family_type(std::string_view event_type)
{
    if (event_type_has_prefix(event_type, "system.") ||
        event_type_has_prefix(event_type, "system_") ||
        event_type == "config_updated" ||
        event_type == "module_loaded") {
        return Event_Core::Type::SYSTEM;
    }

    if (event_type_has_prefix(event_type, "market.") ||
        event_type_has_prefix(event_type, "market_")) {
        return Event_Core::Type::MARKETDATA;
    }

    if (event_type_has_prefix(event_type, "trading.") ||
        event_type_has_prefix(event_type, "order.") ||
        event_type_has_prefix(event_type, "order_")) {
        return Event_Core::Type::TRADING;
    }

    if (event_type_has_prefix(event_type, "strategy.") ||
        event_type == "signal_generated" ||
        event_type == "strategy_started" ||
        event_type == "strategy_stopped") {
        return Event_Core::Type::SIGNAL;
    }

    if (event_type_has_prefix(event_type, "risk.") ||
        event_type == "risk_alert") {
        return Event_Core::Type::RISK;
    }

    if (event_type_has_prefix(event_type, "backtest.")) {
        return Event_Core::Type::BACKTEST;
    }

    if (event_type_has_prefix(event_type, "data.")) {
        return Event_Core::Type::DATA;
    }

    if (event_type_has_prefix(event_type, "performance.")) {
        return Event_Core::Type::PERFORMANCE;
    }

    if (event_type_has_prefix(event_type, "user.") ||
        event_type_has_prefix(event_type, "user_")) {
        return Event_Core::Type::USER;
    }

    return Event_Core::Type::CUSTOM;
}

inline Event_Core::Type resolve_event_type(std::string_view event_type)
{
    if (const auto registered = lookup_registered_event_type(event_type)) {
        return *registered;
    }

    if (const auto alias = lookup_legacy_event_type_alias(event_type)) {
        return *alias;
    }

    return infer_event_family_type(event_type);
}

inline std::unordered_map<std::string, Event_Core::Type> build_registered_event_type_map()
{
    std::unordered_map<std::string, Event_Core::Type> result;
    result.reserve(sizeof(kCanonicalEventTypeMappings) / sizeof(kCanonicalEventTypeMappings[0]));

    for (const auto& mapping : kCanonicalEventTypeMappings) {
        result.emplace(std::string(mapping.event_type), mapping.engine_type);
    }

    return result;
}

} // namespace engine