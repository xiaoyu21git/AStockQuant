#include "GmStrategySession.h"

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "../../../../thirdparty/gmsdk/strategy.h"

#include <QDebug>
#include <QDateTime>
#include <QString>
#include <QTimeZone>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

template <typename>
struct FirstArgument;

template <typename Class, typename Return, typename Argument>
struct FirstArgument<Return (Class::*)(Argument)> {
    using type = Argument;
};

using GmTick = std::remove_pointer_t<FirstArgument<decltype(&::Strategy::on_tick)>::type>;
using GmBar = std::remove_pointer_t<FirstArgument<decltype(&::Strategy::on_bar)>::type>;
using GmOrder = std::remove_pointer_t<FirstArgument<decltype(&::Strategy::on_order_status)>::type>;
using GmExecRpt = std::remove_pointer_t<FirstArgument<decltype(&::Strategy::on_execution_report)>::type>;
using GmCash = std::remove_pointer_t<FirstArgument<decltype(&::Strategy::on_cash)>::type>;
using GmPosition = std::remove_pointer_t<FirstArgument<decltype(&::Strategy::on_position)>::type>;

constexpr int GM_MODE_LIVE = 1;
constexpr int GM_MODE_BACKTEST = 2;
constexpr int GM_ORDER_STATUS_NEW = 1;
constexpr int GM_ORDER_STATUS_PARTIALLY_FILLED = 2;
constexpr int GM_ORDER_STATUS_FILLED = 3;
constexpr int GM_ORDER_STATUS_CANCELED = 5;
constexpr int GM_ORDER_STATUS_PENDING_CANCEL = 6;
constexpr int GM_ORDER_STATUS_REJECTED = 8;
constexpr int GM_ORDER_STATUS_PENDING_NEW = 10;
constexpr int GM_ORDER_STATUS_EXPIRED = 12;
constexpr int GM_ORDER_SIDE_BUY = 1;
constexpr int GM_ORDER_SIDE_SELL = 2;
constexpr int GM_ORDER_TYPE_LIMIT = 1;
constexpr int GM_ORDER_TYPE_MARKET = 2;
constexpr int GM_POSITION_SIDE_LONG = 1;
constexpr int GM_POSITION_SIDE_SHORT = 2;
constexpr int GM_EXEC_TYPE_TRADE = 15;
constexpr int GM_POSITION_EFFECT_UNKNOWN = 0;
constexpr int GM_POSITION_EFFECT_OPEN = 1;
constexpr int GM_POSITION_EFFECT_CLOSE = 2;
constexpr int GM_ORDER_DURATION_GFD = 3;
constexpr int GM_ORDER_QUALIFIER_UNKNOWN = 0;

QDateTime current_china_datetime()
{
    static const QTimeZone chinaTimeZone("Asia/Shanghai");
    if (chinaTimeZone.isValid()) {
        return QDateTime::currentDateTimeUtc().toTimeZone(chinaTimeZone);
    }
    return QDateTime::currentDateTime();
}

bool is_likely_china_a_stock_symbol(const std::string& symbol)
{
    const QString normalized = QString::fromStdString(symbol).trimmed().toUpper();
    return normalized.endsWith(QStringLiteral(".SH"))
        || normalized.endsWith(QStringLiteral(".SZ"))
        || normalized.endsWith(QStringLiteral(".BJ"));
}

bool is_likely_china_a_stock_trading_session()
{
    const QDateTime now = current_china_datetime();
    if (!now.isValid()) {
        return true;
    }

    const int dayOfWeek = now.date().dayOfWeek();
    if (dayOfWeek < 1 || dayOfWeek > 5) {
        return false;
    }

    const QTime currentTime = now.time();
    const bool morningSession = currentTime >= QTime(9, 15) && currentTime < QTime(11, 30);
    const bool afternoonSession = currentTime >= QTime(13, 0) && currentTime < QTime(15, 0);
    return morningSession || afternoonSession;
}

bool config_flag_enabled(const std::map<std::string, std::string>& values, const std::string& key)
{
    const auto it = values.find(key);
    if (it == values.end()) {
        return false;
    }

    const QString normalized = QString::fromStdString(it->second).trimmed().toLower();
    return normalized == QStringLiteral("1")
        || normalized == QStringLiteral("true")
        || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("on");
}

bool is_terminal_order_status(const std::string& status)
{
    const QString normalized = QString::fromStdString(status).trimmed().toUpper();
    return normalized == QStringLiteral("FILLED")
        || normalized == QStringLiteral("CANCELLED")
        || normalized == QStringLiteral("REJECTED")
        || normalized == QStringLiteral("EXPIRED");
}

bool is_error_order_status(const std::string& status)
{
    const QString normalized = QString::fromStdString(status).trimmed().toUpper();
    return normalized == QStringLiteral("REJECTED")
        || normalized == QStringLiteral("EXPIRED");
}

std::string resolved_order_status_from_fill_progress(std::string status,
                                                     int64_t quantity,
                                                     int64_t filled_quantity)
{
    QString normalized = QString::fromStdString(status).trimmed().toUpper();
    if (normalized.isEmpty()) {
        normalized = QStringLiteral("PENDING");
    } else if (normalized == QStringLiteral("PARTIALLY_FILLED")) {
        normalized = QStringLiteral("PARTIAL_FILLED");
    }

    if (quantity > 0
        && filled_quantity >= quantity
        && normalized != QStringLiteral("CANCELLED")
        && normalized != QStringLiteral("REJECTED")
        && normalized != QStringLiteral("EXPIRED")) {
        return "FILLED";
    }

    if (filled_quantity > 0
        && (normalized == QStringLiteral("PENDING")
            || normalized == QStringLiteral("SUBMITTED")
            || normalized == QStringLiteral("REQUESTED"))) {
        return "PARTIAL_FILLED";
    }

    return normalized.toStdString();
}

std::string execution_report_identity(const GmExecRpt& report, const std::string& order_id)
{
    const std::string exec_id = report.exec_id[0] == '\0' ? std::string() : std::string(report.exec_id);
    if (!exec_id.empty()) {
        return exec_id;
    }

    std::ostringstream builder;
    builder << order_id
            << ':' << report.created_at
            << ':' << report.side
            << ':' << report.price
            << ':' << report.volume;
    return builder.str();
}

bool should_schedule_order_reconciliation(const thirdparty::OrderResult& order)
{
    return !order.order_id.empty() && !is_terminal_order_status(order.status);
}

bool order_state_changed(const thirdparty::OrderResult& previous, const thirdparty::OrderResult& current)
{
    return previous.status != current.status
        || previous.filled_quantity != current.filled_quantity
        || std::fabs(previous.avg_price - current.avg_price) > 1e-9
        || std::fabs(previous.filled_notional - current.filled_notional) > 1e-9
        || previous.message != current.message
        || previous.update_time != current.update_time;
}

std::string session_state_to_string(thirdparty::TradingSessionState state)
{
    switch (state) {
    case thirdparty::TradingSessionState::Created:
        return "CREATED";
    case thirdparty::TradingSessionState::Initialized:
        return "INITIALIZED";
    case thirdparty::TradingSessionState::Starting:
        return "STARTING";
    case thirdparty::TradingSessionState::Running:
        return "RUNNING";
    case thirdparty::TradingSessionState::Stopping:
        return "STOPPING";
    case thirdparty::TradingSessionState::Stopped:
        return "STOPPED";
    case thirdparty::TradingSessionState::Error:
        return "ERROR";
    }

    return "UNKNOWN";
}

std::string runtime_strategy_id_from_config(const thirdparty::ConfigParams& config)
{
    const auto runtime_it = config.extra_params.find("runtime_strategy_id");
    if (runtime_it != config.extra_params.end() && !runtime_it->second.empty()) {
        return runtime_it->second;
    }

    const auto it = config.extra_params.find("strategy_id");
    return it == config.extra_params.end() ? std::string() : it->second;
}

std::string business_strategy_id_from_config(const thirdparty::ConfigParams& config)
{
    const auto it = config.extra_params.find("bound_strategy_id");
    return it == config.extra_params.end() ? std::string() : it->second;
}

std::string display_strategy_id_from_config(const thirdparty::ConfigParams& config)
{
    const std::string business_strategy_id = business_strategy_id_from_config(config);
    return business_strategy_id.empty() ? runtime_strategy_id_from_config(config) : business_strategy_id;
}

void apply_strategy_identity_to_event(engine::EventFormat& event,
                                     const thirdparty::ConfigParams& config)
{
    const std::string strategy_id = display_strategy_id_from_config(config);
    if (!strategy_id.empty()) {
        event.set("strategy_id", strategy_id);
        event.metadata["strategy_id"] = strategy_id;
    }

    const std::string business_strategy_id = business_strategy_id_from_config(config);
    if (!business_strategy_id.empty()) {
        event.set("business_strategy_id", business_strategy_id);
        event.metadata["business_strategy_id"] = business_strategy_id;
    }

    const std::string runtime_strategy_id = runtime_strategy_id_from_config(config);
    if (!runtime_strategy_id.empty()) {
        event.set("runtime_strategy_id", runtime_strategy_id);
        event.metadata["runtime_strategy_id"] = runtime_strategy_id;
    }
}

void apply_strategy_identity_to_event(engine::EventFormat& event,
                                     const std::string& strategy_id,
                                     const std::string& business_strategy_id,
                                     const std::string& runtime_strategy_id)
{
    if (!strategy_id.empty()) {
        event.set("strategy_id", strategy_id);
        event.metadata["strategy_id"] = strategy_id;
    }

    if (!business_strategy_id.empty()) {
        event.set("business_strategy_id", business_strategy_id);
        event.metadata["business_strategy_id"] = business_strategy_id;
    }

    if (!runtime_strategy_id.empty()) {
        event.set("runtime_strategy_id", runtime_strategy_id);
        event.metadata["runtime_strategy_id"] = runtime_strategy_id;
    }
}

std::string string_from_cstr(const char* value)
{
    return value == nullptr ? std::string() : std::string(value);
}

std::string gm_symbol_from_internal(std::string symbol)
{
    if (symbol.empty()) {
        return symbol;
    }

    std::transform(symbol.begin(), symbol.end(), symbol.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    const auto dot = symbol.find('.');
    if (dot == std::string::npos) {
        return symbol;
    }

    const std::string code = symbol.substr(0, dot);
    const std::string exchange = symbol.substr(dot + 1);
    if (exchange == "SH") {
        return "SHSE." + code;
    }
    if (exchange == "SZ") {
        return "SZSE." + code;
    }
    if (exchange == "BJ") {
        return "BSE." + code;
    }
    if (exchange == "CFFEX" || exchange == "SHFE" || exchange == "DCE"
        || exchange == "CZCE" || exchange == "INE" || exchange == "GFEX") {
        return exchange + "." + code;
    }
    return symbol;
}

std::string internal_symbol_from_gm(std::string symbol)
{
    if (symbol.empty()) {
        return symbol;
    }

    std::transform(symbol.begin(), symbol.end(), symbol.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    const auto dot = symbol.find('.');
    if (dot == std::string::npos) {
        return symbol;
    }

    const std::string exchange = symbol.substr(0, dot);
    const std::string code = symbol.substr(dot + 1);
    if (exchange == "SHSE") {
        return code + ".SH";
    }
    if (exchange == "SZSE") {
        return code + ".SZ";
    }
    if (exchange == "BSE") {
        return code + ".BJ";
    }
    if (exchange == "CFFEX" || exchange == "SHFE" || exchange == "DCE"
        || exchange == "CZCE" || exchange == "INE" || exchange == "GFEX") {
        return code + "." + exchange;
    }
    return symbol;
}

std::string exchange_from_symbol(const std::string& symbol)
{
    if (symbol.rfind("SHSE.", 0) == 0) {
        return "SHSE";
    }
    if (symbol.rfind("SZSE.", 0) == 0) {
        return "SZSE";
    }
    if (symbol.rfind("BSE.", 0) == 0) {
        return "BSE";
    }
    if (symbol.rfind("CFFEX.", 0) == 0) {
        return "CFFEX";
    }
    if (symbol.rfind("SHFE.", 0) == 0) {
        return "SHFE";
    }
    if (symbol.rfind("DCE.", 0) == 0) {
        return "DCE";
    }
    if (symbol.rfind("CZCE.", 0) == 0) {
        return "CZCE";
    }
    if (symbol.rfind("INE.", 0) == 0) {
        return "INE";
    }
    if (symbol.rfind("GFEX.", 0) == 0) {
        return "GFEX";
    }

    const auto dot = symbol.find('.');
    if (dot == std::string::npos || dot + 1 >= symbol.size()) {
        return std::string();
    }

    const std::string suffix = symbol.substr(dot + 1);
    if (suffix == "SH") {
        return "SHSE";
    }
    if (suffix == "SZ") {
        return "SZSE";
    }
    if (suffix == "BJ") {
        return "BSE";
    }
    if (suffix == "CFFEX" || suffix == "SHFE" || suffix == "DCE"
        || suffix == "CZCE" || suffix == "INE" || suffix == "GFEX") {
        return suffix;
    }
    return suffix;
}

int int_from_metadata(const std::map<std::string, std::string>& metadata,
                      const std::string& key,
                      int default_value)
{
    const auto it = metadata.find(key);
    if (it == metadata.end() || it->second.empty()) {
        return default_value;
    }

    try {
        return std::stoi(it->second);
    } catch (...) {
        return default_value;
    }
}

double double_from_metadata(const std::map<std::string, std::string>& metadata,
                            const std::string& key,
                            double default_value = 0.0)
{
    const auto it = metadata.find(key);
    if (it == metadata.end() || it->second.empty()) {
        return default_value;
    }

    bool ok = false;
    const double parsed = QString::fromStdString(it->second).trimmed().toDouble(&ok);
    return ok ? parsed : default_value;
}

std::string string_from_metadata(const std::map<std::string, std::string>& metadata,
                                 const std::string& key,
                                 const std::string& default_value = {})
{
    const auto it = metadata.find(key);
    if (it == metadata.end() || it->second.empty()) {
        return default_value;
    }
    return it->second;
}

std::string now_string()
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string build_order_id(const std::string& session_id)
{
    static std::atomic<int> counter{0};
    std::ostringstream stream;
    stream << session_id << "_ORD_" << now_string() << '_' << ++counter;
    return stream.str();
}

std::string timestamp_to_string(long long timestamp)
{
    return timestamp > 0 ? std::to_string(timestamp) : now_string();
}

int64_t timestamp_ms_from_string(const std::string& value)
{
    if (value.empty()) {
        return 0;
    }

    bool ok = false;
    const qlonglong parsed = QString::fromStdString(value).trimmed().toLongLong(&ok);
    return ok ? static_cast<int64_t>(parsed) : 0;
}

constexpr int64_t kExecutionReportOrderMatchSkewMs = 5000;

std::string gm_order_status_to_string(int status)
{
    switch (status) {
    case GM_ORDER_STATUS_NEW:
    case GM_ORDER_STATUS_PENDING_NEW:
        return "SUBMITTED";
    case GM_ORDER_STATUS_PARTIALLY_FILLED:
        return "PARTIALLY_FILLED";
    case GM_ORDER_STATUS_FILLED:
        return "FILLED";
    case GM_ORDER_STATUS_CANCELED:
        return "CANCELLED";
    case GM_ORDER_STATUS_PENDING_CANCEL:
        return "PENDING_CANCEL";
    case GM_ORDER_STATUS_REJECTED:
        return "REJECTED";
    case GM_ORDER_STATUS_EXPIRED:
        return "EXPIRED";
    default:
        return "UNKNOWN";
    }
}

std::string gm_position_side_to_string(int side)
{
    switch (side) {
    case GM_POSITION_SIDE_LONG:
        return "LONG";
    case GM_POSITION_SIDE_SHORT:
        return "SHORT";
    default:
        return "UNKNOWN";
    }
}

std::string gm_order_side_to_string(int side)
{
    switch (side) {
    case GM_ORDER_SIDE_BUY:
        return "BUY";
    case GM_ORDER_SIDE_SELL:
        return "SELL";
    default:
        return "UNKNOWN";
    }
}

std::string gm_order_identity(const GmOrder& order)
{
    return string_from_cstr(order.order_id);
}

std::string gm_client_order_identity(const GmOrder& order)
{
    return string_from_cstr(order.cl_ord_id);
}

std::string gm_order_identity(const GmExecRpt& report)
{
    return string_from_cstr(report.order_id);
}

std::string gm_client_order_identity(const GmExecRpt& report)
{
    return string_from_cstr(report.cl_ord_id);
}

void cache_runtime_order_alias(const GmOrder& order,
                              std::map<std::string, std::string>& aliases,
                              std::map<std::string, std::map<std::string, std::string>>& contexts)
{
    const std::string client_id = gm_client_order_identity(order);
    const std::string actual_id = gm_order_identity(order);
    if (client_id.empty() || actual_id.empty() || client_id == actual_id) {
        return;
    }

    aliases[client_id] = actual_id;

    const auto client_context = contexts.find(client_id);
    if (client_context != contexts.end() && contexts.find(actual_id) == contexts.end()) {
        contexts[actual_id] = client_context->second;
    }
}

void cache_runtime_order_alias(const GmExecRpt& report,
                              std::map<std::string, std::string>& aliases,
                              std::map<std::string, std::map<std::string, std::string>>& contexts)
{
    const std::string client_id = gm_client_order_identity(report);
    const std::string actual_id = gm_order_identity(report);
    if (client_id.empty() || actual_id.empty() || client_id == actual_id) {
        return;
    }

    aliases[client_id] = actual_id;

    const auto client_context = contexts.find(client_id);
    if (client_context != contexts.end() && contexts.find(actual_id) == contexts.end()) {
        contexts[actual_id] = client_context->second;
    }
}

std::string resolve_cached_order_id(const GmOrder& order,
                                    const std::map<std::string, std::string>& aliases)
{
    const std::string client_id = gm_client_order_identity(order);
    if (!client_id.empty()) {
        return client_id;
    }

    const std::string actual_id = gm_order_identity(order);
    if (!actual_id.empty()) {
        for (const auto& entry : aliases) {
            if (entry.second == actual_id) {
                return entry.first;
            }
        }
    }

    return actual_id;
}

std::string resolve_cached_order_id(const GmExecRpt& report,
                                    const std::map<std::string, std::string>& aliases,
                                    const std::map<std::string, std::string>* broker_order_lookup = nullptr)
{
    std::string cache_id = gm_client_order_identity(report);
    const std::string actual_id = gm_order_identity(report);

    if (cache_id.empty() && !actual_id.empty() && broker_order_lookup != nullptr) {
        const auto broker_it = broker_order_lookup->find(actual_id);
        if (broker_it != broker_order_lookup->end()) {
            cache_id = broker_it->second;
        }
    }

    if (cache_id.empty() && !actual_id.empty()) {
        for (const auto& entry : aliases) {
            if (entry.second == actual_id) {
                cache_id = entry.first;
                break;
            }
        }
    }

    if (cache_id.empty()) {
        cache_id = !actual_id.empty() ? actual_id : gm_client_order_identity(report);
    }

    return cache_id;
}

struct ExecutionReportAggregate {
    int64_t cumulative_quantity = 0;
    double cumulative_notional = 0.0;
    double last_fill_price = 0.0;
    std::string last_timestamp;
    std::string broker_order_id;
    std::string symbol;
    std::string side;
    std::set<std::string> exec_ids;
};

template <typename FillProgress>
void merge_execution_report_into_order(thirdparty::OrderResult* order,
                                       FillProgress* progress,
                                       const ExecutionReportAggregate& aggregate)
{
    if (order == nullptr || progress == nullptr) {
        return;
    }

    progress->cumulative_quantity = std::max(progress->cumulative_quantity, aggregate.cumulative_quantity);
    progress->cumulative_notional = std::max(progress->cumulative_notional, aggregate.cumulative_notional);
    progress->exec_ids.insert(aggregate.exec_ids.begin(), aggregate.exec_ids.end());

    if (order->symbol.empty()) {
        order->symbol = aggregate.symbol;
        order->exchange = exchange_from_symbol(order->symbol);
    }
    if (order->side.empty()) {
        order->side = aggregate.side;
    }
    if (order->quantity <= 0 && aggregate.cumulative_quantity > 0) {
        order->quantity = aggregate.cumulative_quantity;
    }
    if (order->quantity > 0 && progress->cumulative_quantity > order->quantity) {
        progress->cumulative_quantity = order->quantity;
    }

    order->filled_quantity = std::max(order->filled_quantity, progress->cumulative_quantity);
    bool filled_quantity_clamped = false;
    if (order->quantity > 0 && order->filled_quantity > order->quantity) {
        order->filled_quantity = order->quantity;
        filled_quantity_clamped = true;
    }
    order->filled_notional = std::max(order->filled_notional, progress->cumulative_notional);

    if (filled_quantity_clamped && order->filled_quantity > 0) {
        const double effective_price = aggregate.last_fill_price > 0.0
            ? aggregate.last_fill_price
            : (order->avg_price > 0.0 ? order->avg_price : order->price);
        if (effective_price > 0.0) {
            const double clamped_notional = effective_price * static_cast<double>(order->filled_quantity);
            progress->cumulative_notional = std::min(progress->cumulative_notional, clamped_notional);
            order->filled_notional = clamped_notional;
        }
    }

    if (order->filled_quantity > 0) {
        if (order->filled_notional > 0.0) {
            order->avg_price = order->filled_notional / static_cast<double>(order->filled_quantity);
        } else if (aggregate.last_fill_price > 0.0) {
            order->avg_price = aggregate.last_fill_price;
        }
    }

    if (order->price <= 0.0 && aggregate.last_fill_price > 0.0) {
        order->price = aggregate.last_fill_price;
    }
    if (!aggregate.last_timestamp.empty()) {
        order->update_time = aggregate.last_timestamp;
        if (order->submit_time.empty()) {
            order->submit_time = aggregate.last_timestamp;
        }
    }

    order->status = resolved_order_status_from_fill_progress(order->status.empty() ? std::string("SUBMITTED") : order->status,
                                                             order->quantity,
                                                             order->filled_quantity);
}

template <typename PendingMap>
std::string resolve_pending_order_id_by_attributes(const PendingMap& pending_orders,
                                                   const std::map<std::string, thirdparty::OrderResult>& cached_orders,
                                                   const std::string& symbol,
                                                   const std::string& side,
                                                   int64_t quantity,
                                                   double price,
                                                   bool allow_partial_quantity,
                                                   int64_t reference_timestamp_ms = 0,
                                                   bool require_submit_time_match = false)
{
    std::string matched_order_id;
    int match_count = 0;

    for (const auto& pending_entry : pending_orders) {
        const auto cached_it = cached_orders.find(pending_entry.first);
        if (cached_it == cached_orders.end()) {
            continue;
        }

        const thirdparty::OrderResult& cached_order = cached_it->second;
        if (!symbol.empty() && cached_order.symbol != symbol) {
            continue;
        }
        if (!side.empty() && cached_order.side != side) {
            continue;
        }
        if (quantity > 0 && cached_order.quantity > 0) {
            if (allow_partial_quantity) {
                if (quantity > cached_order.quantity) {
                    continue;
                }
            } else if (cached_order.quantity != quantity) {
                continue;
            }
        }
        if (price > 0.0 && cached_order.price > 0.0 && std::fabs(cached_order.price - price) > 1e-6) {
            continue;
        }
        if (require_submit_time_match && reference_timestamp_ms > 0) {
            const int64_t submit_time_ms = timestamp_ms_from_string(cached_order.submit_time);
            if (submit_time_ms > 0 && reference_timestamp_ms + kExecutionReportOrderMatchSkewMs < submit_time_ms) {
                continue;
            }
        }

        matched_order_id = pending_entry.first;
        ++match_count;
        if (match_count > 1) {
            return {};
        }
    }

    return match_count == 1 ? matched_order_id : std::string();
}

thirdparty::OrderResult to_runtime_order(const GmOrder& order, const std::string& cached_id)
{
    thirdparty::OrderResult result;
    result.order_id = cached_id.empty() ? gm_order_identity(order) : cached_id;
    result.symbol = internal_symbol_from_gm(string_from_cstr(order.symbol));
    result.exchange = exchange_from_symbol(result.symbol);
    result.side = gm_order_side_to_string(order.side);
    result.quantity = order.volume;
    result.filled_quantity = order.filled_volume;
    result.status = resolved_order_status_from_fill_progress(gm_order_status_to_string(order.status),
                                                             result.quantity,
                                                             result.filled_quantity);
    result.message = string_from_cstr(order.ord_rej_reason_detail);
    result.price = order.price;
    result.avg_price = order.filled_vwap > 0.0 ? order.filled_vwap : order.price;
    result.filled_notional = result.avg_price * static_cast<double>(result.filled_quantity);
    result.submit_time = timestamp_to_string(order.created_at);
    result.update_time = timestamp_to_string(order.updated_at);
    return result;
}

thirdparty::Position to_runtime_position(const GmPosition& position)
{
    thirdparty::Position result;
    result.symbol = internal_symbol_from_gm(string_from_cstr(position.symbol));
    result.name = string_from_cstr(position.account_name);
    result.quantity = position.volume;
    result.price = position.price;
    result.market_value = position.market_value;
    result.pnl = position.fpnl;
    result.pnl_percent = position.amount != 0.0 ? (position.fpnl / position.amount) : 0.0;
    result.direction = gm_position_side_to_string(position.side);
    result.entry_time = timestamp_to_string(position.created_at);
    result.update_time = timestamp_to_string(position.updated_at);
    return result;
}

thirdparty::AccountInfo to_runtime_account(const GmCash& cash)
{
    thirdparty::AccountInfo result;
    result.total_asset = cash.nav;
    result.cash = cash.balance;
    result.available = cash.available;
    result.buying_power = cash.available;
    result.frozen = cash.frozen + cash.order_frozen;
    result.market_value = cash.market_value;
    result.pnl = cash.pnl;
    result.pnl_percent = cash.cum_inout != 0.0 ? (cash.pnl / cash.cum_inout) : 0.0;
    result.update_time = timestamp_to_string(cash.updated_at);
    return result;
}

int gm_mode_from_config(const thirdparty::ConfigParams& config)
{
    const auto direct = config.extra_params.find("gm_mode");
    if (direct != config.extra_params.end() && !direct->second.empty()) {
        try {
            return std::stoi(direct->second);
        } catch (...) {
        }
    }

    const auto mode = config.extra_params.find("mode");
    if (mode != config.extra_params.end() && !mode->second.empty()) {
        const std::string value = mode->second;
        if (value == "backtest" || value == "BACKTEST") {
            return GM_MODE_BACKTEST;
        }
        if (value == "live" || value == "LIVE") {
            return GM_MODE_LIVE;
        }
        try {
            return std::stoi(value);
        } catch (...) {
        }
    }

    return config.platform == thirdparty::PlatformType::SIMULATION ? GM_MODE_BACKTEST : GM_MODE_LIVE;
}

int gm_order_side_from_runtime(thirdparty::OrderSide side)
{
    return side == thirdparty::OrderSide::SELL ? GM_ORDER_SIDE_SELL : GM_ORDER_SIDE_BUY;
}

int gm_order_type_from_runtime(thirdparty::OrderType type)
{
    return type == thirdparty::OrderType::MARKET ? GM_ORDER_TYPE_MARKET : GM_ORDER_TYPE_LIMIT;
}

int gm_position_effect_from_command(const thirdparty::TradingCommand& command)
{
    const int metadata_value = int_from_metadata(command.metadata, "position_effect", GM_POSITION_EFFECT_UNKNOWN);
    if (metadata_value != GM_POSITION_EFFECT_UNKNOWN) {
        return metadata_value;
    }

    return command.side == thirdparty::OrderSide::SELL ? GM_POSITION_EFFECT_CLOSE : GM_POSITION_EFFECT_OPEN;
}

int gm_credit_position_src_from_command(const thirdparty::TradingCommand& command)
{
    const int source = int_from_metadata(command.metadata, "position_src", PositionSrc_L1);
    return source > PositionSrc_Unknown ? source : PositionSrc_L1;
}

bool is_option_exercise_command(const thirdparty::TradingCommand& command)
{
    const std::string action = string_from_metadata(command.metadata, "action");
    return action == "optionExercise" || action == "exercise" || action == "option_exercise";
}

bool is_option_covered_open_command(const thirdparty::TradingCommand& command)
{
    const std::string action = string_from_metadata(command.metadata, "action");
    return action == "optionClose" || action == "coveredOpen"
        || action == "optionCoveredOpen" || action == "option_covered_open";
}

bool is_option_covered_close_command(const thirdparty::TradingCommand& command)
{
    const std::string action = string_from_metadata(command.metadata, "action");
    return action == "coveredClose" || action == "optionCoveredClose"
        || action == "option_covered_close";
}

bool is_credit_margin_buy_command(const thirdparty::TradingCommand& command)
{
    const std::string type = string_from_metadata(command.metadata, "type");
    const std::string action = string_from_metadata(command.metadata, "action");
    return type == "margin_buy" && (action.empty() || action == "marginBuy");
}

bool is_credit_margin_sell_command(const thirdparty::TradingCommand& command)
{
    const std::string type = string_from_metadata(command.metadata, "type");
    const std::string action = string_from_metadata(command.metadata, "action");
    return type == "margin_sell" && (action.empty() || action == "marginSell");
}

bool is_credit_margin_close_long_command(const thirdparty::TradingCommand& command)
{
    return string_from_metadata(command.metadata, "type") == "margin_buy"
        && string_from_metadata(command.metadata, "action") == "closeLong";
}

bool is_credit_margin_close_short_command(const thirdparty::TradingCommand& command)
{
    return string_from_metadata(command.metadata, "type") == "margin_sell"
        && string_from_metadata(command.metadata, "action") == "closeShort";
}

bool is_credit_repay_share_direct_command(const thirdparty::TradingCommand& command)
{
    const std::string action = string_from_metadata(command.metadata, "action");
    return action == "returnStock" || action == "repayShare" || action == "creditRepayShare";
}

bool is_credit_repay_cash_direct_command(const thirdparty::TradingCommand& command)
{
    const std::string action = string_from_metadata(command.metadata, "action");
    return action == "repay" || action == "cashRepay" || action == "creditRepayCash";
}

thirdparty::OrderResult build_credit_cash_repay_result(const thirdparty::TradingCommand& command,
                                                       int status_code,
                                                       double actual_repay_amount,
                                                       const std::string& message)
{
    thirdparty::OrderResult result;
    result.order_id = command.order_id.empty() ? build_order_id("credit_cash_repay") : command.order_id;
    result.symbol = command.symbol.empty() ? std::string("CASH_REPAY") : command.symbol;
    result.exchange = exchange_from_symbol(result.symbol);
    result.side = command.side == thirdparty::OrderSide::SELL ? "SELL" : "BUY";
    result.status = status_code == 0 ? "FILLED" : "REJECTED";
    result.message = message;
    result.quantity = 0;
    result.filled_quantity = 0;
    result.price = 0.0;
    result.avg_price = 0.0;
    result.filled_notional = actual_repay_amount > 0.0
        ? actual_repay_amount
        : double_from_metadata(command.metadata, "cashAmount", double_from_metadata(command.metadata, "cash_amount", 0.0));
    result.submit_time = now_string();
    result.update_time = result.submit_time;
    return result;
}

std::string subscription_key(const std::string& symbol, const std::string& frequency)
{
    return symbol + "@" + (frequency.empty() ? std::string("tick") : frequency);
}

std::string join_symbols_for_frequency(const std::vector<std::string>& subscriptions,
                                       const std::string& frequency)
{
    std::ostringstream symbols;
    bool first = true;
    const std::string suffix = "@" + frequency;
    for (const std::string& entry : subscriptions) {
        if (entry.size() <= suffix.size() || entry.substr(entry.size() - suffix.size()) != suffix) {
            continue;
        }

        const std::string symbol = entry.substr(0, entry.size() - suffix.size());
        if (symbol.empty()) {
            continue;
        }

        if (!first) {
            symbols << ',';
        }
        symbols << symbol;
        first = false;
    }
    return symbols.str();
}

template <typename T, typename Callback>
bool consume_array(::DataArray<T>* values, std::string* error_message, Callback callback)
{
    if (values == nullptr) {
        return true;
    }

    const int status = values->status();
    if (status != 0) {
        if (error_message != nullptr) {
            *error_message = string_from_cstr(values->errer_msg());
        }
        values->release();
        return false;
    }

    const int count = values->count();
    for (int index = 0; index < count; ++index) {
        callback(values->at(index));
    }

    values->release();
    return true;
}

void set_command_fields(engine::EventFormat& event, const thirdparty::TradingCommand* command)
{
    if (!command) {
        return;
    }

    event.correlation_id = command->correlation_id;
    event.set("symbol", command->symbol);
    event.set("frequency", command->frequency);
    event.set("order_id", command->order_id);
    event.set("price", command->price);
    event.set("quantity", command->quantity);
}

std::string position_effect_text_from_metadata(const std::map<std::string, std::string>& metadata)
{
    const std::string direct = string_from_metadata(metadata, "position_effect_text");
    if (!direct.empty()) {
        return direct;
    }

    const std::string camel = string_from_metadata(metadata, "positionEffect");
    if (!camel.empty()) {
        return camel;
    }

    const std::string raw = string_from_metadata(metadata, "position_effect");
    if (raw == "1" || raw == "OPEN") {
        return "OPEN";
    }
    if (raw == "2" || raw == "CLOSE") {
        return "CLOSE";
    }
    return {};
}

void set_event_context_field(engine::EventFormat& event,
                             const char* event_key,
                             const char* metadata_key,
                             const std::string& value)
{
    if (value.empty()) {
        return;
    }

    event.set(event_key, value);
    event.metadata[metadata_key] = value;
}

void set_event_context_numeric_field(engine::EventFormat& event,
                                     const char* event_key,
                                     const char* metadata_key,
                                     double value)
{
    if (!std::isfinite(value) || value <= 0.0) {
        return;
    }

    event.set(event_key, value);
    event.metadata[metadata_key] = QString::number(value, 'f', 6).toStdString();
}

void apply_order_context_to_event(engine::EventFormat& event,
                                  const std::map<std::string, std::string>& order_context)
{
    if (order_context.empty()) {
        return;
    }

    set_event_context_field(event, "type", "type", string_from_metadata(order_context, "type"));
    set_event_context_field(event, "action", "action", string_from_metadata(order_context, "action"));
    set_event_context_field(event, "position_effect", "position_effect", string_from_metadata(order_context, "position_effect"));
    set_event_context_field(event, "position_effect_text", "position_effect_text", position_effect_text_from_metadata(order_context));
    set_event_context_field(event, "underlying", "underlying", string_from_metadata(order_context, "underlying"));
    set_event_context_field(event, "option_type", "option_type", string_from_metadata(order_context, "option_type"));
    set_event_context_field(event, "expiry", "expiry", string_from_metadata(order_context, "expiry"));
    set_event_context_field(event, "strategy_id", "strategy_id", string_from_metadata(order_context, "strategy_id"));
    set_event_context_field(event, "business_strategy_id", "business_strategy_id", string_from_metadata(order_context, "business_strategy_id"));
    set_event_context_field(event, "runtime_strategy_id", "runtime_strategy_id", string_from_metadata(order_context, "runtime_strategy_id"));
    set_event_context_field(event, "batch_id", "batch_id", string_from_metadata(order_context, "batchId"));
    set_event_context_field(event, "batch_index", "batch_index", string_from_metadata(order_context, "batchIndex"));
    set_event_context_field(event, "execution_sequence", "execution_sequence", string_from_metadata(order_context, "executionSequence"));
    set_event_context_field(event, "batch_role", "batch_role", string_from_metadata(order_context, "batchRole"));
    set_event_context_field(event, "batch_phase", "batch_phase", string_from_metadata(order_context, "batchPhase"));
    set_event_context_field(event, "batch_order_count", "batch_order_count", string_from_metadata(order_context, "batchOrderCount"));
    set_event_context_field(event, "previous_batch_id", "previous_batch_id", string_from_metadata(order_context, "previousBatchId"));
    set_event_context_field(event, "previous_batch_order_count", "previous_batch_order_count", string_from_metadata(order_context, "previousBatchOrderCount"));
    set_event_context_field(event, "next_batch_id", "next_batch_id", string_from_metadata(order_context, "nextBatchId"));
    set_event_context_field(event, "execution_scope_id", "execution_scope_id", string_from_metadata(order_context, "executionScopeId"));
    set_event_context_field(event, "requires_previous_batch_filled", "requires_previous_batch_filled", string_from_metadata(order_context, "requiresPreviousBatchFilled"));
    set_event_context_field(event, "pause_on_conflict", "pause_on_conflict", string_from_metadata(order_context, "pauseOnConflict"));
    set_event_context_field(event, "pause_on_abnormal_reject", "pause_on_abnormal_reject", string_from_metadata(order_context, "pauseOnAbnormalReject"));
    set_event_context_field(event, "requires_manual_checkpoint", "requires_manual_checkpoint", string_from_metadata(order_context, "requiresManualCheckpoint"));
    set_event_context_field(event, "manual_checkpoint_batch_index", "manual_checkpoint_batch_index", string_from_metadata(order_context, "manualCheckpointBatchIndex"));
    set_event_context_field(event, "blocks_following_batches", "blocks_following_batches", string_from_metadata(order_context, "blocksFollowingBatches"));
    set_event_context_numeric_field(event,
                                    "cash_amount",
                                    "cash_amount",
                                    double_from_metadata(order_context, "cashAmount", double_from_metadata(order_context, "cash_amount", 0.0)));
}

const std::map<std::string, std::string>* find_order_context(const std::map<std::string, std::map<std::string, std::string>>& contexts,
                                                             const std::string& cache_order_id,
                                                             const std::string& broker_order_id)
{
    auto cache_it = contexts.find(cache_order_id);
    if (cache_it != contexts.end()) {
        return &cache_it->second;
    }

    auto broker_it = contexts.find(broker_order_id);
    if (broker_it != contexts.end()) {
        return &broker_it->second;
    }

    return nullptr;
}

void publish_runtime_order_status(const std::shared_ptr<engine::EventBus>& event_bus,
                                  const std::string& session_id,
                                  const thirdparty::ConfigParams& config,
                                  const thirdparty::OrderResult& order,
                                  const std::string& correlation_id,
                                  const std::string& broker_order_id,
                                  const std::string& source,
                                  const std::map<std::string, std::string>& order_context = {})
{
    if (!event_bus || !event_bus->is_running() || order.order_id.empty()) {
        return;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_ORDER_UPDATED, "TRADING_RUNTIME", 0);
    event.correlation_id = correlation_id;
    event.set("session_id", session_id);
    event.set("account_id", config.account_id);
    apply_strategy_identity_to_event(event, config);
    event.set("order_id", order.order_id);
    event.set("client_order_id", order.order_id);
    event.metadata["client_order_id"] = order.order_id;
    if (!broker_order_id.empty()) {
        event.set("broker_order_id", broker_order_id);
        event.metadata["broker_order_id"] = broker_order_id;
    }
    event.set("symbol", order.symbol);
    event.set("exchange", order.exchange);
    event.set("side", order.side);
    const std::string resolved_status = resolved_order_status_from_fill_progress(order.status,
                                                                                 order.quantity,
                                                                                 order.filled_quantity);
    event.set("status", resolved_status);
    event.set("price", order.price);
    event.set("quantity", order.quantity);
    event.set("filled_quantity", order.filled_quantity);
    event.set("filled_notional", order.filled_notional);
    event.set("avg_price", order.avg_price);
    event.set("message", order.message);
    event.set("created_at", order.submit_time);
    event.set("updated_at", order.update_time);
    event.metadata["order_id"] = order.order_id;
    event.metadata["symbol"] = order.symbol;
    event.metadata["side"] = order.side;
    event.metadata["status"] = resolved_status;
    event.metadata["status_origin"] = "runtime";
    event.metadata["source"] = source;
    apply_order_context_to_event(event, order_context);
    event_bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
}

void publish_runtime_trade_fill(const std::shared_ptr<engine::EventBus>& event_bus,
                                const std::string& session_id,
                                const thirdparty::ConfigParams& config,
                                const thirdparty::OrderResult& order,
                                const std::string& correlation_id,
                                const std::string& broker_order_id,
                                const std::string& source,
                                const std::map<std::string, std::string>& order_context = {})
{
    if (!event_bus || !event_bus->is_running() || order.order_id.empty() || order.filled_quantity <= 0) {
        return;
    }

    const double fill_price = order.avg_price > 0.0 ? order.avg_price : order.price;
    const double filled_notional = order.filled_notional > 0.0
        ? order.filled_notional
        : fill_price * static_cast<double>(order.filled_quantity);

    engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::ORDER_FILL, "TRADING_RUNTIME", 0);
    event.correlation_id = correlation_id;
    event.set("session_id", session_id);
    event.set("account_id", config.account_id);
    apply_strategy_identity_to_event(event, config);
    event.set("order_id", order.order_id);
    event.set("client_order_id", order.order_id);
    event.metadata["client_order_id"] = order.order_id;
    if (!broker_order_id.empty()) {
        event.set("broker_order_id", broker_order_id);
        event.metadata["broker_order_id"] = broker_order_id;
    }
    event.set("exec_id", std::string("sync:") + order.order_id);
    event.set("symbol", order.symbol);
    event.set("exchange", order.exchange);
    event.set("side", order.side);
    event.set("fill_price", fill_price);
    event.set("fill_quantity", order.filled_quantity);
    event.set("quantity", order.quantity);
    event.set("filled_quantity", order.filled_quantity);
    event.set("filled_notional", filled_notional);
    const std::string resolved_status = resolved_order_status_from_fill_progress(order.status,
                                                                                 order.quantity,
                                                                                 order.filled_quantity);
    event.set("status", resolved_status);
    event.set("created_at", order.update_time.empty() ? order.submit_time : order.update_time);
    event.metadata["order_id"] = order.order_id;
    event.metadata["symbol"] = order.symbol;
    event.metadata["side"] = order.side;
    event.metadata["status"] = resolved_status;
    event.metadata["status_origin"] = "runtime";
    event.metadata["source"] = source;
    apply_order_context_to_event(event, order_context);
    event_bus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
}

} // namespace

namespace thirdparty {

class RuntimeStrategy : public ::Strategy {
public:
    RuntimeStrategy(GmStrategySession* owner, const ConfigParams& config)
        : owner_(owner)
        , token_(config.token)
        , strategy_id_(runtime_strategy_id_from_config(config).empty() ? owner->session_id_ : runtime_strategy_id_from_config(config))
        , mode_(gm_mode_from_config(config))
    {
        set_token(token_.c_str());
        set_strategy_id(strategy_id_.c_str());
        set_mode(mode_);
    }

    std::string last_error_detail()
    {
        return string_from_cstr(get_last_error_detail());
    }

    void on_init() override
    {
        if (owner_ == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        owner_->mark_runtime_started_locked();
        owner_->sync_initial_state_locked();

        const std::string tick_symbols = join_symbols_for_frequency(owner_->subscriptions_, "tick");
        if (!tick_symbols.empty()) {
            subscribe(tick_symbols.c_str(), "tick", false);
        }

        const std::string bar_symbols = join_symbols_for_frequency(owner_->subscriptions_, "bar1m");
        if (!bar_symbols.empty()) {
            subscribe(bar_symbols.c_str(), "bar1m", false);
        }
    }

    void on_tick(GmTick* tick) override
    {
        if (owner_ == nullptr || tick == nullptr) {
            return;
        }

        std::shared_ptr<engine::EventBus> eventBus;
        std::string sessionId;
        std::string accountId;
        std::string strategyId;
        std::string businessStrategyId;
        std::string runtimeStrategyId;
        {
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            if (!owner_->event_bus_ || !owner_->event_bus_->is_running()) {
                return;
            }
            eventBus = owner_->event_bus_;
            sessionId = owner_->session_id_;
            accountId = owner_->config_.account_id;
            strategyId = display_strategy_id_from_config(owner_->config_);
            businessStrategyId = business_strategy_id_from_config(owner_->config_);
            runtimeStrategyId = runtime_strategy_id_from_config(owner_->config_);
        }

        const std::string symbol = internal_symbol_from_gm(string_from_cstr(tick->symbol));
        // 从 UTC 时间戳推导交易日 (YYYYMMDD int32)
        const auto tickTime = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(tick->created_at), Qt::UTC)
                                  .toLocalTime();
        const int64_t tradingDay = static_cast<int64_t>(tickTime.date().year()) * 10000
                                 + static_cast<int64_t>(tickTime.date().month()) * 100
                                 + static_cast<int64_t>(tickTime.date().day());
        std::vector<double> bidPrices;
        std::vector<double> bidVolumes;
        std::vector<double> askPrices;
        std::vector<double> askVolumes;
        bidPrices.reserve(5);
        bidVolumes.reserve(5);
        askPrices.reserve(5);
        askVolumes.reserve(5);

        for (size_t level = 0; level < DEPTH_OF_QUOTE; ++level) {
            const Quote& quote = tick->quotes[level];
            if (quote.bid_price > 0.0f || quote.bid_volume > 0) {
                bidPrices.push_back(static_cast<double>(quote.bid_price));
                bidVolumes.push_back(static_cast<double>(quote.bid_volume));
            }
            if (quote.ask_price > 0.0f || quote.ask_volume > 0) {
                askPrices.push_back(static_cast<double>(quote.ask_price));
                askVolumes.push_back(static_cast<double>(quote.ask_volume));
            }
        }

        engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_MARKET_TICK, "TRADING_RUNTIME", 0);
        event.set("session_id", sessionId);
        event.set("account_id", accountId);
        apply_strategy_identity_to_event(event, strategyId, businessStrategyId, runtimeStrategyId);
        event.set("symbol", symbol);
        event.set("exchange", exchange_from_symbol(symbol));
        event.set("price", static_cast<double>(tick->price));
        event.set("open", static_cast<double>(tick->open));
        event.set("high", static_cast<double>(tick->high));
        event.set("low", static_cast<double>(tick->low));
        event.set("cum_volume", tick->cum_volume);
        event.set("cum_amount", tick->cum_amount);
        event.set("cum_position", static_cast<int64_t>(tick->cum_position));
        event.set("last_amount", tick->last_amount);
        event.set("last_volume", static_cast<int64_t>(tick->last_volume));
        event.set("volume", static_cast<double>(tick->last_volume));   // 统一字段：瞬时成交量
        event.set("tradingDay", tradingDay);                            // 统一字段：交易日
        event.set("trade_type", static_cast<int64_t>(tick->trade_type));
        if (!bidPrices.empty()) {
            event.set("bid_price", bidPrices.front());
            event.set("bid_volume", static_cast<int64_t>(bidVolumes.front()));
            event.set("bid_prices", bidPrices);
            event.set("bid_volumes", bidVolumes);
        }
        if (!askPrices.empty()) {
            event.set("ask_price", askPrices.front());
            event.set("ask_volume", static_cast<int64_t>(askVolumes.front()));
            event.set("ask_prices", askPrices);
            event.set("ask_volumes", askVolumes);
        }
        event.set("created_at", timestamp_to_string(static_cast<long long>(tick->created_at)));
        eventBus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
        static int tickLogCount = 0;
        if (++tickLogCount <= 5)
            std::cerr << "[GmSession] on_tick #" << tickLogCount << " sym=" << symbol
                      << " price=" << tick->price << "\n" << std::flush;
    }

    void on_bar(GmBar* bar) override
    {
        if (owner_ == nullptr || bar == nullptr) {
            return;
        }

        std::shared_ptr<engine::EventBus> eventBus;
        std::string sessionId;
        std::string accountId;
        std::string strategyId;
        std::string businessStrategyId;
        std::string runtimeStrategyId;
        {
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            if (!owner_->event_bus_ || !owner_->event_bus_->is_running()) {
                return;
            }
            eventBus = owner_->event_bus_;
            sessionId = owner_->session_id_;
            accountId = owner_->config_.account_id;
            strategyId = display_strategy_id_from_config(owner_->config_);
            businessStrategyId = business_strategy_id_from_config(owner_->config_);
            runtimeStrategyId = runtime_strategy_id_from_config(owner_->config_);
        }

        const std::string symbol = internal_symbol_from_gm(string_from_cstr(bar->symbol));

        // 从 bar 起始时间 (UTC epoch) 推导交易日
        const auto barTime = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(bar->bob), Qt::UTC)
                                 .toLocalTime();
        const int64_t tradingDay = static_cast<int64_t>(barTime.date().year()) * 10000
                                 + static_cast<int64_t>(barTime.date().month()) * 100
                                 + static_cast<int64_t>(barTime.date().day());

        engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_MARKET_BAR, "TRADING_RUNTIME", 0);
        event.set("session_id", sessionId);
        event.set("account_id", accountId);
        apply_strategy_identity_to_event(event, strategyId, businessStrategyId, runtimeStrategyId);
        event.set("symbol", symbol);
        event.set("exchange", exchange_from_symbol(symbol));
        event.set("frequency", string_from_cstr(bar->frequency));
        event.set("open", static_cast<double>(bar->open));
        event.set("close", static_cast<double>(bar->close));
        event.set("high", static_cast<double>(bar->high));
        event.set("low", static_cast<double>(bar->low));
        event.set("volume", bar->volume);
        event.set("amount", bar->amount);
        event.set("tradingDay", tradingDay);                     // 统一字段：交易日
        event.set("bob", bar->bob);
        event.set("eob", bar->eob);
        eventBus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    }

    void on_order_status(GmOrder* order) override
    {
        if (owner_ == nullptr || order == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        cache_runtime_order_alias(*order, owner_->order_aliases_, owner_->order_contexts_);
        const std::string cache_id = resolve_cached_order_id(*order, owner_->order_aliases_);
        if (!cache_id.empty()) {
            owner_->cache_order_locked(cache_id, to_runtime_order(*order, cache_id));
            if (is_terminal_order_status(gm_order_status_to_string(order->status))) {
                owner_->pending_order_reconciliations_.erase(cache_id);
            }
        }

        if (!owner_->event_bus_ || !owner_->event_bus_->is_running()) {
            return;
        }

        const std::string symbol = internal_symbol_from_gm(string_from_cstr(order->symbol));
    const std::string status = resolved_order_status_from_fill_progress(gm_order_status_to_string(order->status),
                                        static_cast<int64_t>(order->volume),
                                        static_cast<int64_t>(order->filled_volume));
        const std::string message = string_from_cstr(order->ord_rej_reason_detail);
        const std::string broker_order_id = gm_order_identity(*order);
        const std::map<std::string, std::string>* order_context = find_order_context(owner_->order_contexts_, cache_id, broker_order_id);

        engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_ORDER_UPDATED, "TRADING_RUNTIME", 0);
        event.set("session_id", owner_->session_id_);
        event.set("account_id", owner_->config_.account_id);
        apply_strategy_identity_to_event(event, owner_->config_);
        event.set("order_id", cache_id.empty() ? gm_order_identity(*order) : cache_id);
        event.set("client_order_id", cache_id.empty() ? gm_order_identity(*order) : cache_id);
        event.metadata["client_order_id"] = cache_id.empty() ? gm_order_identity(*order) : cache_id;
        if (!broker_order_id.empty()) {
            event.set("broker_order_id", broker_order_id);
            event.metadata["broker_order_id"] = broker_order_id;
        }
        event.metadata["status_origin"] = "runtime";
        event.set("symbol", symbol);
        event.set("exchange", exchange_from_symbol(symbol));
        event.set("side", gm_order_side_to_string(order->side));
        event.set("status", status);
        event.set("price", order->price);
        event.set("quantity", static_cast<int64_t>(order->volume));
        event.set("filled_quantity", static_cast<int64_t>(order->filled_volume));
        event.set("filled_notional", (order->filled_vwap > 0.0 ? order->filled_vwap : order->price) * static_cast<double>(order->filled_volume));
        event.set("message", message);
        event.set("created_at", timestamp_to_string(order->created_at));
        event.set("avg_price", order->filled_vwap > 0.0 ? order->filled_vwap : order->price);
        event.set("updated_at", timestamp_to_string(order->updated_at));
        if (order_context != nullptr) {
            apply_order_context_to_event(event, *order_context);
        }
        owner_->event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    }

    void on_execution_report(GmExecRpt* report) override
    {
        if (owner_ == nullptr || report == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        if (!owner_->event_bus_ || !owner_->event_bus_->is_running()) {
            return;
        }

        std::string cache_id = string_from_cstr(report->cl_ord_id);
        const std::string actual_order_id = string_from_cstr(report->order_id);
        if (cache_id.empty() && !actual_order_id.empty()) {
            for (const auto& entry : owner_->order_aliases_) {
                if (entry.second == actual_order_id) {
                    cache_id = entry.first;
                    break;
                }
            }
        }
        if (cache_id.empty()) {
            cache_id = !actual_order_id.empty() ? actual_order_id : string_from_cstr(report->cl_ord_id);
        }

        const std::string symbol = internal_symbol_from_gm(string_from_cstr(report->symbol));
        const std::string side = gm_order_side_to_string(report->side);
        const std::string exec_identity = execution_report_identity(*report, cache_id);
        const int64_t fill_quantity = static_cast<int64_t>(report->volume);
        const double fill_price = report->price;
        const double filled_notional = report->amount > 0.0
            ? report->amount
            : fill_price * static_cast<double>(fill_quantity);
        const std::map<std::string, std::string>* order_context = find_order_context(owner_->order_contexts_, cache_id, actual_order_id);

        auto& fill_progress = owner_->order_fill_progress_[cache_id];
        if (!exec_identity.empty() && !fill_progress.exec_ids.insert(exec_identity).second) {
            return;
        }
        if (fill_quantity > 0) {
            fill_progress.cumulative_quantity += fill_quantity;
            fill_progress.cumulative_notional += filled_notional;
        }

        int64_t total_quantity = fill_quantity;
        int64_t cumulative_filled_quantity = fill_progress.cumulative_quantity > 0
            ? fill_progress.cumulative_quantity
            : fill_quantity;
        auto cached_order = owner_->orders_.find(cache_id);
        if (cached_order != owner_->orders_.end()) {
            total_quantity = cached_order->second.quantity > 0 ? cached_order->second.quantity : total_quantity;
            if (cached_order->second.filled_quantity > 0) {
                cumulative_filled_quantity = std::max(cumulative_filled_quantity,
                                                      cached_order->second.filled_quantity);
            }
        }
        if (total_quantity > 0 && cumulative_filled_quantity > total_quantity) {
            cumulative_filled_quantity = total_quantity;
        }

        const std::string execution_status = total_quantity > 0 && cumulative_filled_quantity >= total_quantity
            ? std::string("FILLED")
            : std::string("PARTIAL_FILLED");

        if (execution_status == "FILLED") {
            owner_->pending_order_reconciliations_.erase(cache_id);
        }

        engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_EXECUTION_REPORT, "TRADING_RUNTIME", 0);
        event.set("session_id", owner_->session_id_);
        event.set("account_id", owner_->config_.account_id);
        apply_strategy_identity_to_event(event, owner_->config_);
        event.set("order_id", cache_id);
        event.set("client_order_id", cache_id);
        event.metadata["client_order_id"] = cache_id;
        if (!actual_order_id.empty()) {
            event.set("broker_order_id", actual_order_id);
            event.metadata["broker_order_id"] = actual_order_id;
        }
        event.metadata["status_origin"] = "runtime";
        event.set("exec_id", string_from_cstr(report->exec_id));
        event.set("symbol", symbol);
        event.set("exchange", exchange_from_symbol(symbol));
        event.set("side", side);
        event.set("exec_type", static_cast<int64_t>(report->exec_type));
        event.set("price", report->price);
        event.set("volume", static_cast<int64_t>(report->volume));
        event.set("amount", report->amount);
        event.set("fill_price", fill_price);
        event.set("fill_quantity", fill_quantity);
        event.set("filled_quantity", cumulative_filled_quantity);
        event.set("filled_notional", filled_notional);
        event.set("status", execution_status);
        event.set("created_at", timestamp_to_string(report->created_at));
        if (order_context != nullptr) {
            apply_order_context_to_event(event, *order_context);
        }
        owner_->event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));

        if (report->exec_type == GM_EXEC_TYPE_TRADE && fill_price > 0.0 && fill_quantity > 0) {
            engine::EventFormat fill_event = engine::EventFormat::create_from_strings(engine::EventTypes::ORDER_FILL, "TRADING_RUNTIME", 0);
            fill_event.set("session_id", owner_->session_id_);
            fill_event.set("account_id", owner_->config_.account_id);
            apply_strategy_identity_to_event(fill_event, owner_->config_);
            fill_event.set("order_id", cache_id);
            fill_event.set("client_order_id", cache_id);
            fill_event.metadata["client_order_id"] = cache_id;
            if (!actual_order_id.empty()) {
                fill_event.set("broker_order_id", actual_order_id);
                fill_event.metadata["broker_order_id"] = actual_order_id;
            }
            fill_event.metadata["status_origin"] = "runtime";
            fill_event.set("exec_id", string_from_cstr(report->exec_id));
            fill_event.set("symbol", symbol);
            fill_event.set("exchange", exchange_from_symbol(symbol));
            fill_event.set("side", side);
            fill_event.set("fill_price", fill_price);
            fill_event.set("fill_quantity", fill_quantity);
            fill_event.set("quantity", total_quantity);
            fill_event.set("filled_quantity", cumulative_filled_quantity);
            fill_event.set("filled_notional", filled_notional);
            fill_event.set("status", execution_status);
            fill_event.set("created_at", timestamp_to_string(report->created_at));
            if (order_context != nullptr) {
                apply_order_context_to_event(fill_event, *order_context);
            }
            owner_->event_bus_->publish(fill_event, static_cast<int>(engine::EventPriority::HIGH));
        }
    }

    void on_cash(GmCash* cash) override
    {
        if (owner_ == nullptr || cash == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        owner_->cache_account_locked(to_runtime_account(*cash));

        if (!owner_->event_bus_ || !owner_->event_bus_->is_running()) {
            return;
        }

        engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_ACCOUNT_UPDATED, "TRADING_RUNTIME", 0);
        event.set("session_id", owner_->session_id_);
        event.set("account_id", owner_->config_.account_id);
        apply_strategy_identity_to_event(event, owner_->config_);
        event.set("available", cash->available);
        event.set("balance", cash->balance);
        event.set("nav", cash->nav);
        event.set("market_value", cash->market_value);
        event.set("updated_at", timestamp_to_string(cash->updated_at));
        owner_->event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    }

    void on_position(GmPosition* position) override
    {
        if (owner_ == nullptr || position == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        owner_->cache_position_locked(to_runtime_position(*position));

        if (!owner_->event_bus_ || !owner_->event_bus_->is_running()) {
            return;
        }

        engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_POSITION_UPDATED, "TRADING_RUNTIME", 0);
        event.set("session_id", owner_->session_id_);
        event.set("account_id", owner_->config_.account_id);
        apply_strategy_identity_to_event(event, owner_->config_);
        event.set("symbol", string_from_cstr(position->symbol));
        event.set("direction", gm_position_side_to_string(position->side));
        event.set("quantity", static_cast<int64_t>(position->volume));
        event.set("available", static_cast<int64_t>(position->available));
        event.set("price", position->price);
        event.set("market_value", position->market_value);
        event.set("updated_at", timestamp_to_string(position->updated_at));
        owner_->event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    }

    void on_timer(int timer_id) override
    {
        if (owner_ == nullptr) {
            return;
        }

        bool should_drain = false;
        {
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            should_drain = timer_id == owner_->command_timer_id_;
        }

        if (should_drain) {
            owner_->drain_pending_commands(64);

            std::lock_guard<std::mutex> lock(owner_->mutex_);
            owner_->reconcile_pending_orders_locked();
        }
    }

    void on_error(int error_code, const char* error_msg) override
    {
        if (owner_ == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        const std::string runtime_error = string_from_cstr(error_msg).empty()
            ? (std::string("GM runtime error: ") + std::to_string(error_code))
            : string_from_cstr(error_msg);
        owner_->set_error_locked(runtime_error);
        if (!owner_->event_bus_ || !owner_->event_bus_->is_running()) {
            return;
        }

        engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_SESSION_ERROR, "TRADING_RUNTIME", 0);
        event.set("session_id", owner_->session_id_);
        event.set("account_id", owner_->config_.account_id);
        apply_strategy_identity_to_event(event, owner_->config_);
        event.set("error_code", static_cast<int64_t>(error_code));
        event.set("error_message", string_from_cstr(error_msg));
        owner_->event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    }

    void on_stop() override
    {
        if (owner_ == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        owner_->stop_requested_ = true;
    }

    void on_market_data_connected(const char* connect_msg) override
    {
        if (owner_ == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        owner_->connected_ = true;
        owner_->publish_event("trading.connection.changed");
        if (owner_->event_bus_ && owner_->event_bus_->is_running()) {
            engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_MARKET_CONNECTED, "TRADING_RUNTIME", 0);
            event.set("session_id", owner_->session_id_);
            event.set("account_id", owner_->config_.account_id);
            apply_strategy_identity_to_event(event, owner_->config_);
            event.set("message", string_from_cstr(connect_msg));
            owner_->event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
        }
    }

    void on_trade_data_connected() override
    {
        if (owner_ == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        owner_->connected_ = true;
        owner_->sync_initial_state_locked();
        owner_->publish_event("trading.connection.changed");
    }

    void on_market_data_disconnected(const char* connect_msg) override
    {
        if (owner_ == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        owner_->connected_ = false;
        owner_->publish_event("trading.connection.changed");
        if (owner_->event_bus_ && owner_->event_bus_->is_running()) {
            engine::EventFormat event = engine::EventFormat::create_from_strings(engine::EventTypes::TRADING_MARKET_DISCONNECTED, "TRADING_RUNTIME", 0);
            event.set("session_id", owner_->session_id_);
            event.set("account_id", owner_->config_.account_id);
            apply_strategy_identity_to_event(event, owner_->config_);
            event.set("message", string_from_cstr(connect_msg));
            owner_->event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
        }
    }

    void on_trade_data_disconnected() override
    {
        if (owner_ == nullptr) {
            return;
        }

        std::lock_guard<std::mutex> lock(owner_->mutex_);
        owner_->connected_ = false;
        owner_->publish_event("trading.connection.changed");
    }

private:
    GmStrategySession* owner_ = nullptr;
    std::string token_;
    std::string strategy_id_;
    int mode_ = GM_MODE_LIVE;
};

GmStrategySession::GmStrategySession(std::string session_id)
    : session_id_(std::move(session_id))
    , command_queue_(std::make_shared<TradingCommandQueue>())
{
}

GmStrategySession::~GmStrategySession() = default;

bool GmStrategySession::initialize(const ConfigParams& config)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (config.token.empty()) {
        set_error_locked("Missing GM token");
        return false;
    }

    if (config.account_id.empty()) {
        set_error_locked("Missing account id");
        return false;
    }

    if (!config.server_url.empty()) {
        ::set_serv_addr(config.server_url.c_str());
    }

    if (config_flag_enabled(config.extra_params, "simtrade_only")) {
        ::set_simtrade_only();
        qDebug() << "GmStrategySession: simtrade_only enabled for account"
                 << QString::fromStdString(config.account_id);
    }

    config_ = config;
    initialized_ = true;
    connected_ = false;
    stop_requested_ = false;
    state_ = TradingSessionState::Initialized;
    last_error_.clear();
    subscriptions_.clear();
    positions_.clear();
    orders_.clear();
    order_contexts_.clear();
    order_aliases_.clear();
    pending_order_reconciliations_.clear();
    reconciliation_tick_ = 0;
    has_account_snapshot_ = false;
    account_snapshot_ = AccountInfo{};
    publish_event("trading.session.initialized");
    return true;
}

bool GmStrategySession::start()
{
    std::thread previous_thread;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            set_error_locked("Session is not initialized");
            return false;
        }

        if (state_ == TradingSessionState::Running || state_ == TradingSessionState::Starting) {
            return true;
        }

        if (runtime_thread_.joinable()) {
            previous_thread = std::move(runtime_thread_);
        }

        strategy_ = std::make_unique<RuntimeStrategy>(this, config_);
        stop_requested_ = false;
        connected_ = false;
        command_timer_id_ = 0;
        state_ = TradingSessionState::Starting;
        last_error_.clear();
        publish_event("trading.session.starting");

        runtime_thread_ = std::thread([this]() {
            RuntimeStrategy* runtime = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                runtime_thread_id_ = std::this_thread::get_id();
                runtime = strategy_.get();
            }

            int run_result = -1;
            if (runtime != nullptr) {
                run_result = runtime->run();
            }

            std::string runtime_error;
            if (runtime != nullptr && run_result != 0) {
                runtime_error = runtime->last_error_detail();
                if (runtime_error.empty()) {
                    runtime_error = "GM Strategy run failed: " + std::to_string(run_result);
                }
            }

            std::lock_guard<std::mutex> lock(mutex_);
            runtime_thread_id_ = std::thread::id();
            strategy_.reset();

            if (state_ == TradingSessionState::Error) {
                connected_ = false;
                command_timer_id_ = 0;
                publish_event("trading.connection.changed");
                publish_event("trading.session.error");
            } else {
                mark_runtime_stopped_locked((stop_requested_ || state_ == TradingSessionState::Stopping) ? std::string() : runtime_error);
            }
        });
    }

    if (previous_thread.joinable()) {
        previous_thread.join();
    }

    return true;
}

bool GmStrategySession::stop()
{
    std::thread runtime_thread;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == TradingSessionState::Stopped || state_ == TradingSessionState::Created) {
            connected_ = false;
            return true;
        }

        stop_requested_ = true;
        state_ = TradingSessionState::Stopping;
        publish_event("trading.session.stopping");

        if (strategy_) {
            if (command_timer_id_ > 0) {
                strategy_->timer_stop(command_timer_id_);
                command_timer_id_ = 0;
            }
            strategy_->stop();
        }

        if (runtime_thread_.joinable()) {
            runtime_thread = std::move(runtime_thread_);
        }
    }

    if (runtime_thread.joinable()) {
        runtime_thread.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (strategy_) {
        strategy_.reset();
    }
    runtime_thread_id_ = std::thread::id();
    if (state_ != TradingSessionState::Error && state_ != TradingSessionState::Stopped) {
        mark_runtime_stopped_locked(std::string());
    }
    return true;
}

bool GmStrategySession::enqueue_command(const TradingCommand& command)
{
    return command_queue_->enqueue(command);
}

size_t GmStrategySession::drain_pending_commands(size_t max_count)
{
    const std::vector<TradingCommand> commands = command_queue_->dequeue_all(max_count);
    if (commands.empty()) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const TradingCommand& command : commands) {
        apply_command_locked(command);
    }
    return commands.size();
}

void GmStrategySession::set_event_bus(std::shared_ptr<engine::EventBus> event_bus)
{
    std::lock_guard<std::mutex> lock(mutex_);
    event_bus_ = std::move(event_bus);
}

bool GmStrategySession::is_initialized() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

bool GmStrategySession::is_connected() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_;
}

bool GmStrategySession::is_running() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == TradingSessionState::Running;
}

std::string GmStrategySession::last_error_message() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_error_;
}

ConfigParams GmStrategySession::config() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

std::string GmStrategySession::session_id() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return session_id_;
}

TradingSessionSnapshot GmStrategySession::snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return TradingSessionSnapshot{
        session_id_,
        config_.account_id,
        display_strategy_id_from_config(config_),
        state_,
        initialized_,
        connected_,
        last_error_,
        subscriptions_
    };
}

std::shared_ptr<TradingCommandQueue> GmStrategySession::command_queue() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return command_queue_;
}

std::vector<Position> GmStrategySession::snapshot_positions() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Position> positions;
    positions.reserve(positions_.size());
    for (const auto& entry : positions_) {
        positions.push_back(entry.second);
    }
    return positions;
}

AccountInfo GmStrategySession::snapshot_account() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return account_snapshot_;
}

OrderResult GmStrategySession::snapshot_order(const std::string& order_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = orders_.find(order_id);
    if (it != orders_.end()) {
        return it->second;
    }

    return OrderResult{};
}

std::vector<OrderResult> GmStrategySession::snapshot_orders() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<OrderResult> orders;
    orders.reserve(orders_.size());
    for (const auto& entry : orders_) {
        orders.push_back(entry.second);
    }
    return orders;
}

void GmStrategySession::apply_command_locked(const TradingCommand& command)
{
    switch (command.type) {
    case TradingCommandType::Subscribe: {
        if (!command.symbol.empty()) {
            const std::string frequency = command.frequency.empty() ? std::string("tick") : command.frequency;
            const std::string key = subscription_key(command.symbol, frequency);
            if (std::find(subscriptions_.begin(), subscriptions_.end(), key) == subscriptions_.end()) {
                subscriptions_.push_back(key);
            }
            if (strategy_) {
                std::string gmSym = gm_symbol_from_internal(command.symbol);
                int ret = strategy_->subscribe(gmSym.c_str(), frequency.c_str(), false);
                static int subLogCount = 0;
                if (++subLogCount <= 5)
                    std::cerr << "[GmSession] subscribe(" << gmSym << "," << frequency
                              << ") ret=" << ret << " subsTotal=" << subscriptions_.size() << "\n" << std::flush;
            }
            publish_event("trading.subscription.changed", &command);
        }
        break;
    }
    case TradingCommandType::Unsubscribe: {
        if (!command.symbol.empty()) {
            const std::string frequency = command.frequency.empty() ? std::string("tick") : command.frequency;
            const std::string key = subscription_key(command.symbol, frequency);
            const auto it = std::remove(subscriptions_.begin(), subscriptions_.end(), key);
            if (it != subscriptions_.end()) {
                subscriptions_.erase(it, subscriptions_.end());
            }
            if (strategy_) {
                strategy_->unsubscribe(gm_symbol_from_internal(command.symbol).c_str(), frequency.c_str());
            }
            publish_event("trading.subscription.changed", &command);
        }
        break;
    }
    case TradingCommandType::PlaceOrder: {
        OrderResult order;
        std::string broker_order_id;
        const bool option_exercise = is_option_exercise_command(command);
        const bool option_covered_open = is_option_covered_open_command(command);
        const bool option_covered_close = is_option_covered_close_command(command);
        const bool credit_margin_buy = is_credit_margin_buy_command(command);
        const bool credit_margin_sell = is_credit_margin_sell_command(command);
        const bool credit_margin_close_long = is_credit_margin_close_long_command(command);
        const bool credit_margin_close_short = is_credit_margin_close_short_command(command);
        const bool credit_repay_share_direct = is_credit_repay_share_direct_command(command);
        const bool credit_repay_cash_direct = is_credit_repay_cash_direct_command(command);
        order.order_id = command.order_id.empty() ? build_order_id(session_id_) : command.order_id;
        order.symbol = command.symbol;
        order.status = "REJECTED";
        order.message = "Trading runtime is not ready";
        order.submit_time = now_string();
        order.update_time = order.submit_time;

        const double cash_amount = double_from_metadata(command.metadata,
                                                        "cashAmount",
                                                        double_from_metadata(command.metadata, "cash_amount", 0.0));
        const bool can_submit_credit_cash_repay = credit_repay_cash_direct && cash_amount > 0.0;

        if (strategy_ && (can_submit_credit_cash_repay || (!command.symbol.empty() && command.quantity > 0.0))) {
            const int volume = (std::max)(0, static_cast<int>(std::llround(command.quantity)));
            const int position_src = gm_credit_position_src_from_command(command);
            if (option_exercise) {
                qDebug() << "GmStrategySession: option_exercise submit"
                         << "symbol=" << QString::fromStdString(command.symbol)
                         << "volume=" << volume
                         << "account=" << QString::fromStdString(config_.account_id);
            } else if (option_covered_open) {
                qDebug() << "GmStrategySession: option_covered_open submit"
                         << "symbol=" << QString::fromStdString(command.symbol)
                         << "volume=" << volume
                         << "orderType=" << gm_order_type_from_runtime(command.order_type)
                         << "price=" << command.price
                         << "account=" << QString::fromStdString(config_.account_id);
            } else if (option_covered_close) {
                qDebug() << "GmStrategySession: option_covered_close submit"
                         << "symbol=" << QString::fromStdString(command.symbol)
                         << "volume=" << volume
                         << "orderType=" << gm_order_type_from_runtime(command.order_type)
                         << "price=" << command.price
                         << "account=" << QString::fromStdString(config_.account_id);
            } else if (credit_margin_buy) {
                qDebug() << "GmStrategySession: credit_buying_on_margin submit"
                         << "symbol=" << QString::fromStdString(command.symbol)
                         << "volume=" << volume
                         << "price=" << command.price
                         << "account=" << QString::fromStdString(config_.account_id);
            } else if (credit_margin_sell) {
                qDebug() << "GmStrategySession: credit_short_selling submit"
                         << "symbol=" << QString::fromStdString(command.symbol)
                         << "volume=" << volume
                         << "price=" << command.price
                         << "account=" << QString::fromStdString(config_.account_id);
            } else if (credit_margin_close_long) {
                qDebug() << "GmStrategySession: credit_repay_cash_by_selling_share submit"
                         << "symbol=" << QString::fromStdString(command.symbol)
                         << "volume=" << volume
                         << "price=" << command.price
                         << "account=" << QString::fromStdString(config_.account_id);
            } else if (credit_margin_close_short) {
                qDebug() << "GmStrategySession: credit_repay_share_by_buying_share submit"
                         << "symbol=" << QString::fromStdString(command.symbol)
                         << "volume=" << volume
                         << "price=" << command.price
                         << "account=" << QString::fromStdString(config_.account_id);
            } else if (credit_repay_share_direct) {
                qDebug() << "GmStrategySession: credit_repay_share_directly submit"
                         << "symbol=" << QString::fromStdString(command.symbol)
                         << "volume=" << volume
                         << "account=" << QString::fromStdString(config_.account_id);
            } else if (credit_repay_cash_direct) {
                qDebug() << "GmStrategySession: credit_repay_cash_directly submit"
                         << "cashAmount=" << cash_amount
                         << "account=" << QString::fromStdString(config_.account_id);
            }

            const std::string cache_id = command.order_id.empty()
                ? build_order_id(session_id_)
                : command.order_id;
            if (!command.metadata.empty()) {
                order_contexts_[cache_id] = command.metadata;
                if (!command.order_id.empty()) {
                    order_contexts_[command.order_id] = command.metadata;
                }
            }

            if (credit_repay_cash_direct) {
                double actual_repay_amount = 0.0;
                char error_buffer[512] = {0};
                const int status_code = strategy_->credit_repay_cash_directly(position_src,
                                                                              cash_amount,
                                                                              config_.account_id.c_str(),
                                                                              &actual_repay_amount,
                                                                              error_buffer,
                                                                              static_cast<int>(sizeof(error_buffer)));
                std::string message = status_code == 0
                    ? std::string("Credit cash repay submitted")
                    : string_from_cstr(error_buffer);
                if (status_code == 0 && actual_repay_amount > 0.0) {
                    std::ostringstream stream;
                    stream << "Credit cash repaid amount=" << actual_repay_amount;
                    message = stream.str();
                }
                if (message.empty()) {
                    message = strategy_->last_error_detail();
                }
                order = build_credit_cash_repay_result(command, status_code, actual_repay_amount, message);
                order.order_id = cache_id;
            } else {
                GmOrder gm_order = option_exercise
                    ? strategy_->option_exercise(gm_symbol_from_internal(command.symbol).c_str(),
                                                 volume,
                                                 config_.account_id.c_str())
                    : option_covered_open
                        ? strategy_->option_covered_open(gm_symbol_from_internal(command.symbol).c_str(),
                                                         volume,
                                                         gm_order_type_from_runtime(command.order_type),
                                                         command.price,
                                                         config_.account_id.c_str())
                        : option_covered_close
                            ? strategy_->option_covered_close(gm_symbol_from_internal(command.symbol).c_str(),
                                                              volume,
                                                              gm_order_type_from_runtime(command.order_type),
                                                              command.price,
                                                              config_.account_id.c_str())
                            : credit_margin_buy
                                ? strategy_->credit_buying_on_margin(position_src,
                                                                     gm_symbol_from_internal(command.symbol).c_str(),
                                                                     volume,
                                                                     command.price,
                                                                     gm_order_type_from_runtime(command.order_type),
                                                                     GM_ORDER_DURATION_GFD,
                                                                     GM_ORDER_QUALIFIER_UNKNOWN,
                                                                     config_.account_id.c_str())
                                : credit_margin_sell
                                    ? strategy_->credit_short_selling(position_src,
                                                                      gm_symbol_from_internal(command.symbol).c_str(),
                                                                      volume,
                                                                      command.price,
                                                                      gm_order_type_from_runtime(command.order_type),
                                                                      GM_ORDER_DURATION_GFD,
                                                                      GM_ORDER_QUALIFIER_UNKNOWN,
                                                                      config_.account_id.c_str())
                                    : credit_margin_close_long
                                        ? strategy_->credit_repay_cash_by_selling_share(position_src,
                                                                                         gm_symbol_from_internal(command.symbol).c_str(),
                                                                                         volume,
                                                                                         command.price,
                                                                                         gm_order_type_from_runtime(command.order_type),
                                                                                         GM_ORDER_DURATION_GFD,
                                                                                         GM_ORDER_QUALIFIER_UNKNOWN,
                                                                                         config_.account_id.c_str())
                                        : credit_margin_close_short
                                            ? strategy_->credit_repay_share_by_buying_share(position_src,
                                                                                             gm_symbol_from_internal(command.symbol).c_str(),
                                                                                             volume,
                                                                                             command.price,
                                                                                             gm_order_type_from_runtime(command.order_type),
                                                                                             GM_ORDER_DURATION_GFD,
                                                                                             GM_ORDER_QUALIFIER_UNKNOWN,
                                                                                             config_.account_id.c_str())
                                            : credit_repay_share_direct
                                                ? strategy_->credit_repay_share_directly(position_src,
                                                                                         gm_symbol_from_internal(command.symbol).c_str(),
                                                                                         volume,
                                                                                         config_.account_id.c_str())
                                                : strategy_->place_order(gm_symbol_from_internal(command.symbol).c_str(),
                                                                         volume,
                                                                         gm_order_side_from_runtime(command.side),
                                                                         gm_order_type_from_runtime(command.order_type),
                                                                         gm_position_effect_from_command(command),
                                                                         command.price,
                                                                         GM_ORDER_DURATION_GFD,
                                                                         GM_ORDER_QUALIFIER_UNKNOWN,
                                                                         0.0,
                                                                         0,
                                                                         config_.account_id.c_str());

                const std::string actual_id = gm_order_identity(gm_order);
                broker_order_id = actual_id;
                if (!command.order_id.empty() && !actual_id.empty() && command.order_id != actual_id) {
                    order_aliases_[command.order_id] = actual_id;
                }
                if (!actual_id.empty()) {
                    order_contexts_[actual_id] = command.metadata;
                }

                order = to_runtime_order(gm_order, cache_id);
                if (actual_id.empty() && !strategy_->last_error_detail().empty()) {
                    order.status = "REJECTED";
                    order.message = strategy_->last_error_detail();
                }

                if (is_error_order_status(order.status)) {
                    const char* reject_log_text = "GmStrategySession: place_order rejected";
                    if (option_exercise) {
                        reject_log_text = "GmStrategySession: option_exercise rejected";
                    } else if (option_covered_open) {
                        reject_log_text = "GmStrategySession: option_covered_open rejected";
                    } else if (option_covered_close) {
                        reject_log_text = "GmStrategySession: option_covered_close rejected";
                    } else if (credit_margin_buy) {
                        reject_log_text = "GmStrategySession: credit_buying_on_margin rejected";
                    } else if (credit_margin_sell) {
                        reject_log_text = "GmStrategySession: credit_short_selling rejected";
                    } else if (credit_margin_close_long) {
                        reject_log_text = "GmStrategySession: credit_repay_cash_by_selling_share rejected";
                    } else if (credit_margin_close_short) {
                        reject_log_text = "GmStrategySession: credit_repay_share_by_buying_share rejected";
                    } else if (credit_repay_share_direct) {
                        reject_log_text = "GmStrategySession: credit_repay_share_directly rejected";
                    }

                    qWarning() << reject_log_text
                               << "cacheOrderId=" << QString::fromStdString(order.order_id)
                               << "gmOrderId=" << QString::fromStdString(actual_id)
                               << "status=" << QString::fromStdString(order.status)
                               << "quantity=" << static_cast<qint64>(order.quantity)
                               << "price=" << order.price
                               << "message=" << QString::fromStdString(order.message);
                }

                if (should_schedule_order_reconciliation(order)) {
                    schedule_order_reconciliation_locked(order.order_id, broker_order_id, command.correlation_id);
                }
            }
        }

        cache_order_locked(order.order_id, order);
        publish_runtime_order_status(event_bus_,
                                     session_id_,
                                     config_,
                                     order,
                                     command.correlation_id,
                                     broker_order_id,
                                                                         "place_order.sync",
                                                                         command.metadata);
        publish_runtime_trade_fill(event_bus_,
                                   session_id_,
                                   config_,
                                   order,
                                   command.correlation_id,
                                   broker_order_id,
                                                                     "place_order.sync",
                                                                     command.metadata);
        publish_event("trading.command.place_order.accepted", &command);
        break;
    }
    case TradingCommandType::CancelOrder: {
        if (strategy_ && !command.order_id.empty()) {
            const auto alias = order_aliases_.find(command.order_id);
            const std::string actual_id = alias == order_aliases_.end() ? command.order_id : alias->second;
            strategy_->order_cancel(actual_id.c_str(), config_.account_id.c_str());
        }

        auto it = orders_.find(command.order_id);
        if (it != orders_.end()) {
            it->second.status = "PENDING_CANCEL";
            it->second.update_time = now_string();
        }
        publish_event("trading.command.cancel_order.accepted", &command);
        break;
    }
    case TradingCommandType::QueryOrders:
        sync_initial_state_locked();
        publish_event("trading.command.query_orders.accepted", &command);
        break;
    case TradingCommandType::QueryPositions:
        sync_initial_state_locked();
        publish_event("trading.command.query_positions.accepted", &command);
        break;
    case TradingCommandType::QueryAccount:
        sync_initial_state_locked();
        publish_event("trading.command.query_account.accepted", &command);
        break;
    case TradingCommandType::AddTimer:
        if (strategy_) {
            const int timer_id = strategy_->timer(command.timer_period_ms, command.timer_delay_ms);
            if (event_bus_ && event_bus_->is_running()) {
                engine::EventFormat event = engine::EventFormat::create_from_strings("trading.command.timer.added", "TRADING_RUNTIME", 0);
                event.set("session_id", session_id_);
                event.set("account_id", config_.account_id);
                apply_strategy_identity_to_event(event, config_);
                event.set("timer_id", static_cast<int64_t>(timer_id));
                set_command_fields(event, &command);
                event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
            }
        }
        publish_event("trading.command.add_timer.accepted", &command);
        break;
    case TradingCommandType::StopTimer:
        if (strategy_ && command.timer_id > 0) {
            strategy_->timer_stop(command.timer_id);
        }
        publish_event("trading.command.stop_timer.accepted", &command);
        break;
    }
}

void GmStrategySession::publish_event(const std::string& event_type, const TradingCommand* command) const
{
    if (!event_bus_ || !event_bus_->is_running()) {
        return;
    }

    engine::EventFormat event = engine::EventFormat::create_from_strings(event_type, "TRADING_RUNTIME", 0);
    event.set("session_id", session_id_);
    event.set("account_id", config_.account_id);
    apply_strategy_identity_to_event(event, config_);
    event.set("state", session_state_to_string(state_));
    event.set("connected", static_cast<int64_t>(connected_ ? 1 : 0));
    set_command_fields(event, command);
    event_bus_->publish(event, static_cast<int>(engine::EventPriority::HIGH));
}

void GmStrategySession::set_error_locked(const std::string& message)
{
    last_error_ = message;
    state_ = TradingSessionState::Error;
}

void GmStrategySession::mark_runtime_started_locked()
{
    connected_ = true;
    state_ = TradingSessionState::Running;
    last_error_.clear();
    qDebug() << "GmStrategySession: runtime entered running state"
             << "sessionId=" << QString::fromStdString(session_id_)
             << "accountId=" << QString::fromStdString(config_.account_id);
    if (strategy_ && command_timer_id_ <= 0) {
        command_timer_id_ = strategy_->timer(250, 0);
    }
    publish_event("trading.session.started");
    publish_event("trading.connection.changed");
}

void GmStrategySession::mark_runtime_stopped_locked(const std::string& error_message)
{
    connected_ = false;
    command_timer_id_ = 0;
    if (!error_message.empty()) {
        last_error_ = error_message;
        state_ = TradingSessionState::Error;
    } else {
        state_ = TradingSessionState::Stopped;
    }
    publish_event("trading.connection.changed");
    publish_event(error_message.empty() ? "trading.session.stopped" : "trading.session.error");
}

void GmStrategySession::sync_initial_state_locked()
{
    if (!strategy_) {
        std::cerr << "[GmSession] sync_initial_state: strategy_ is null\n";
        return;
    }

    std::string error_message;
    auto* cashArr = strategy_->get_cash(config_.account_id.c_str());
    std::cerr << "[GmSession] get_cash(" << config_.account_id << ") -> "
              << (cashArr ? std::to_string(cashArr->count()) + " records, status=" + std::to_string(cashArr->status()) : "null") << "\n";
    consume_array(cashArr, &error_message, [this](const GmCash& cash) {
        cache_account_locked(to_runtime_account(cash));
    });
    if (!error_message.empty()) {
        std::cerr << "[GmSession] get_cash error: " << error_message << "\n";
    }

    auto* posArr = strategy_->get_position(config_.account_id.c_str());
    std::cerr << "[GmSession] get_position(" << config_.account_id << ") -> "
              << (posArr ? std::to_string(posArr->count()) + " records, status=" + std::to_string(posArr->status()) : "null") << "\n";
    consume_array(posArr, &error_message, [this](const GmPosition& position) {
        cache_position_locked(to_runtime_position(position));
    });
    if (!error_message.empty()) {
        std::cerr << "[GmSession] get_position error: " << error_message << "\n";
    }
    std::cerr << "[GmSession] sync done: account=" << (has_account_snapshot_ ? "yes" : "no")
              << " positions=" << positions_.size() << "\n";

    consume_array(strategy_->get_orders(config_.account_id.c_str()), &error_message, [this](const GmOrder& order) {
        cache_runtime_order_alias(order, order_aliases_, order_contexts_);
        const std::string cache_id = resolve_cached_order_id(order, order_aliases_);
        cache_order_locked(cache_id, to_runtime_order(order, cache_id));
    });

    std::map<std::string, ExecutionReportAggregate> execution_aggregates;
    consume_array(strategy_->get_execution_reports(config_.account_id.c_str()), &error_message, [this, &execution_aggregates](const GmExecRpt& report) {
        cache_runtime_order_alias(report, order_aliases_, order_contexts_);
        const std::string cache_id = resolve_cached_order_id(report, order_aliases_);
        if (cache_id.empty()) {
            return;
        }

        ExecutionReportAggregate& aggregate = execution_aggregates[cache_id];
        const std::string exec_id = execution_report_identity(report, cache_id);
        if (!exec_id.empty() && !aggregate.exec_ids.insert(exec_id).second) {
            return;
        }

        const int64_t fill_quantity = std::max<int64_t>(0, static_cast<int64_t>(report.volume));
        const double fill_price = report.price;
        const double fill_notional = report.amount > 0.0
            ? report.amount
            : fill_price * static_cast<double>(fill_quantity);

        aggregate.cumulative_quantity += fill_quantity;
        aggregate.cumulative_notional += fill_notional;
        aggregate.last_fill_price = fill_price > 0.0 ? fill_price : aggregate.last_fill_price;
        aggregate.last_timestamp = timestamp_to_string(report.created_at);
        aggregate.broker_order_id = gm_order_identity(report);
        aggregate.symbol = internal_symbol_from_gm(string_from_cstr(report.symbol));
        aggregate.side = gm_order_side_to_string(report.side);
    });

    for (const auto& entry : execution_aggregates) {
        const std::string& cache_id = entry.first;
        const ExecutionReportAggregate& aggregate = entry.second;
        if (aggregate.cumulative_quantity <= 0) {
            continue;
        }

        OrderResult order;
        const auto existing_it = orders_.find(cache_id);
        if (existing_it != orders_.end()) {
            order = existing_it->second;
        }
        if (order.order_id.empty()) {
            order.order_id = cache_id;
            order.symbol = aggregate.symbol;
            order.exchange = exchange_from_symbol(order.symbol);
            order.side = aggregate.side;
            order.status = "SUBMITTED";
            order.quantity = aggregate.cumulative_quantity;
            order.price = aggregate.last_fill_price;
        }

        auto& fill_progress = order_fill_progress_[cache_id];
        merge_execution_report_into_order(&order, &fill_progress, aggregate);
        cache_order_locked(cache_id, order);
    }

    if (!error_message.empty()) {
        last_error_ = error_message;
        qWarning() << "GmStrategySession: initial state sync failed"
                   << "sessionId=" << QString::fromStdString(session_id_)
                   << "accountId=" << QString::fromStdString(config_.account_id)
                   << "message=" << QString::fromStdString(error_message);
    }
}

void GmStrategySession::schedule_order_reconciliation_locked(const std::string& order_id,
                                                             const std::string& broker_order_id,
                                                             const std::string& correlation_id)
{
    if (order_id.empty()) {
        return;
    }

    PendingOrderReconciliation& pending = pending_order_reconciliations_[order_id];
    pending.broker_order_id = broker_order_id;
    pending.correlation_id = correlation_id;
    pending.attempts = 0;
    pending.next_due_tick = reconciliation_tick_ + 4;

}

void GmStrategySession::reconcile_pending_orders_locked()
{
    if (!strategy_ || pending_order_reconciliations_.empty()) {
        return;
    }

    ++reconciliation_tick_;

    std::vector<std::string> due_order_ids;
    due_order_ids.reserve(pending_order_reconciliations_.size());
    for (const auto& entry : pending_order_reconciliations_) {
        if (entry.second.next_due_tick <= reconciliation_tick_) {
            due_order_ids.push_back(entry.first);
        }
    }

    if (due_order_ids.empty()) {
        return;
    }

    std::string error_message;
    std::map<std::string, OrderResult> refreshed_orders;
    std::map<std::string, std::string> refreshed_broker_order_ids;
    std::map<std::string, std::string> due_order_ids_by_broker_id;
    for (const std::string& due_order_id : due_order_ids) {
        const auto pending_it = pending_order_reconciliations_.find(due_order_id);
        if (pending_it == pending_order_reconciliations_.end()) {
            continue;
        }
        if (!pending_it->second.broker_order_id.empty()) {
            due_order_ids_by_broker_id[pending_it->second.broker_order_id] = due_order_id;
        }
    }

    consume_array(strategy_->get_orders(config_.account_id.c_str()), &error_message, [this, &refreshed_orders, &refreshed_broker_order_ids](const GmOrder& order) {
        cache_runtime_order_alias(order, order_aliases_, order_contexts_);
        std::string cache_id = resolve_cached_order_id(order, order_aliases_);
        if (cache_id.empty() || pending_order_reconciliations_.find(cache_id) == pending_order_reconciliations_.end()) {
            cache_id = resolve_pending_order_id_by_attributes(pending_order_reconciliations_,
                                                              orders_,
                                                              internal_symbol_from_gm(string_from_cstr(order.symbol)),
                                                              gm_order_side_to_string(order.side),
                                                              static_cast<int64_t>(order.volume),
                                                              order.price,
                                                              false);
        }
        if (cache_id.empty()) {
            return;
        }

        if (pending_order_reconciliations_.find(cache_id) == pending_order_reconciliations_.end()) {
            return;
        }

        refreshed_orders[cache_id] = to_runtime_order(order, cache_id);
        refreshed_broker_order_ids[cache_id] = string_from_cstr(order.order_id);
    });

    std::string execution_report_error;
    std::map<std::string, ExecutionReportAggregate> execution_aggregates;
    consume_array(strategy_->get_execution_reports(config_.account_id.c_str()), &execution_report_error, [this, &due_order_ids_by_broker_id, &execution_aggregates](const GmExecRpt& report) {
        cache_runtime_order_alias(report, order_aliases_, order_contexts_);
        std::string cache_id = resolve_cached_order_id(report, order_aliases_, &due_order_ids_by_broker_id);
        if (cache_id.empty() || pending_order_reconciliations_.find(cache_id) == pending_order_reconciliations_.end()) {
            cache_id = resolve_pending_order_id_by_attributes(pending_order_reconciliations_,
                                                              orders_,
                                                              internal_symbol_from_gm(string_from_cstr(report.symbol)),
                                                              gm_order_side_to_string(report.side),
                                                              static_cast<int64_t>(report.volume),
                                                              report.price,
                                                              true,
                                                              static_cast<int64_t>(report.created_at),
                                                              true);
        }
        if (cache_id.empty() || pending_order_reconciliations_.find(cache_id) == pending_order_reconciliations_.end()) {
            return;
        }

        ExecutionReportAggregate& aggregate = execution_aggregates[cache_id];
        const std::string exec_id = execution_report_identity(report, cache_id);
        if (!exec_id.empty() && !aggregate.exec_ids.insert(exec_id).second) {
            return;
        }

        const int64_t fill_quantity = std::max<int64_t>(0, static_cast<int64_t>(report.volume));
        const double fill_price = report.price;
        const double fill_notional = report.amount > 0.0
            ? report.amount
            : fill_price * static_cast<double>(fill_quantity);

        aggregate.cumulative_quantity += fill_quantity;
        aggregate.cumulative_notional += fill_notional;
        aggregate.last_fill_price = fill_price > 0.0 ? fill_price : aggregate.last_fill_price;
        aggregate.last_timestamp = timestamp_to_string(report.created_at);
        aggregate.broker_order_id = gm_order_identity(report);
        aggregate.symbol = internal_symbol_from_gm(string_from_cstr(report.symbol));
        aggregate.side = gm_order_side_to_string(report.side);
    });

    if (!error_message.empty()) {
        last_error_ = error_message;
        qWarning() << "GmStrategySession: order reconciliation query failed"
                   << QString::fromStdString(error_message);
        for (const std::string& order_id : due_order_ids) {
            auto it = pending_order_reconciliations_.find(order_id);
            if (it == pending_order_reconciliations_.end()) {
                continue;
            }

            ++it->second.attempts;
            if (it->second.attempts >= 20) {
                qWarning() << "GmStrategySession: giving up order reconciliation after query failures"
                           << "cacheOrderId=" << QString::fromStdString(order_id);
                pending_order_reconciliations_.erase(it);
            } else {
                it->second.next_due_tick = reconciliation_tick_ + 4;
            }
        }
        return;
    }

    if (!execution_report_error.empty()) {
        qWarning() << "GmStrategySession: execution report reconciliation query failed"
                   << QString::fromStdString(execution_report_error);
    }

    for (const auto& entry : execution_aggregates) {
        const std::string& cache_id = entry.first;
        const ExecutionReportAggregate& aggregate = entry.second;
        if (aggregate.cumulative_quantity <= 0) {
            continue;
        }

        OrderResult order;
        const auto refreshed_it = refreshed_orders.find(cache_id);
        if (refreshed_it != refreshed_orders.end()) {
            order = refreshed_it->second;
        } else {
            const auto cached_it = orders_.find(cache_id);
            if (cached_it != orders_.end()) {
                order = cached_it->second;
            }
        }

        if (order.order_id.empty()) {
            order.order_id = cache_id;
            order.symbol = aggregate.symbol;
            order.exchange = exchange_from_symbol(order.symbol);
            order.side = aggregate.side;
            order.status = "SUBMITTED";
            order.quantity = aggregate.cumulative_quantity;
            order.price = aggregate.last_fill_price;
        }

        auto& fill_progress = order_fill_progress_[cache_id];
        merge_execution_report_into_order(&order, &fill_progress, aggregate);
        refreshed_orders[cache_id] = order;
        if (!aggregate.broker_order_id.empty()) {
            refreshed_broker_order_ids[cache_id] = aggregate.broker_order_id;
        }
    }

    for (const std::string& order_id : due_order_ids) {
        auto pending_it = pending_order_reconciliations_.find(order_id);
        if (pending_it == pending_order_reconciliations_.end()) {
            continue;
        }

        ++pending_it->second.attempts;
        const auto refreshed_it = refreshed_orders.find(order_id);
        if (refreshed_it == refreshed_orders.end()) {
            if (pending_it->second.attempts >= 20) {
                qWarning() << "GmStrategySession: stopping order reconciliation without remote update"
                           << "cacheOrderId=" << QString::fromStdString(order_id)
                           << "accountId=" << QString::fromStdString(config_.account_id);
                pending_order_reconciliations_.erase(pending_it);
            } else {
                pending_it->second.next_due_tick = reconciliation_tick_ + 4;
            }
            continue;
        }

        const auto previous_it = orders_.find(order_id);
        const OrderResult previous = previous_it == orders_.end() ? OrderResult{} : previous_it->second;
        const OrderResult current = refreshed_it->second;
        const bool changed = previous.order_id.empty() || order_state_changed(previous, current);
        const int64_t previous_filled_quantity = previous.filled_quantity;
        const std::string broker_order_id = !pending_it->second.broker_order_id.empty()
            ? pending_it->second.broker_order_id
            : refreshed_broker_order_ids[order_id];

        cache_order_locked(order_id, current);

        if (changed) {
            if (is_error_order_status(current.status)) {
                qWarning() << "GmStrategySession: reconciled terminal error order"
                           << "cacheOrderId=" << QString::fromStdString(order_id)
                           << "gmOrderId=" << QString::fromStdString(broker_order_id)
                           << "status=" << QString::fromStdString(current.status)
                           << "filled=" << static_cast<qint64>(current.filled_quantity)
                           << "quantity=" << static_cast<qint64>(current.quantity)
                           << "message=" << QString::fromStdString(current.message);
            }

            publish_runtime_order_status(event_bus_,
                                         session_id_,
                                         config_,
                                         current,
                                         pending_it->second.correlation_id,
                                         broker_order_id,
                                                                                 "place_order.reconcile",
                                                                                 order_contexts_[order_id]);

            if (current.filled_quantity > previous_filled_quantity) {
                publish_runtime_trade_fill(event_bus_,
                                           session_id_,
                                           config_,
                                           current,
                                           pending_it->second.correlation_id,
                                           broker_order_id,
                                                                                     "place_order.reconcile",
                                                                                     order_contexts_[order_id]);
            }
        }

        if (is_terminal_order_status(current.status) || pending_it->second.attempts >= 20) {
            pending_order_reconciliations_.erase(pending_it);
        } else {
            pending_it->second.next_due_tick = reconciliation_tick_ + 4;
        }
    }
}

void GmStrategySession::cache_order_locked(const std::string& order_id, const OrderResult& order)
{
    if (!order_id.empty()) {
        orders_[order_id] = order;
    }
}

void GmStrategySession::cache_position_locked(const Position& position)
{
    if (!position.symbol.empty()) {
        positions_[position.symbol] = position;
    }
}

void GmStrategySession::cache_account_locked(const AccountInfo& account)
{
    account_snapshot_ = account;
    has_account_snapshot_ = true;
}

} // namespace thirdparty







