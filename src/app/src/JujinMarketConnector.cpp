#include "JujinMarketConnector.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "../../engine/include/JujinApi.h"
#include "JujinTypes.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
#include "MarketSubscriptionStatusRegistry.h"
#include "../../../infrastructure/include/database/NativeMySQLConnectionPool.h"
#include "system/TradingSystem.h"
#include "foundation/json/json_facade.h"
#include "foundation/log/logging.hpp"
#include "foundation/market/AStockSymbol.h"
#include <ctime>
#include "foundation/config/ConfigManager.hpp"

namespace {

constexpr const char* kRuntimeSubscriptionStatusEvent = "trading.market.subscription.status";

// ========== 纯 C++ 字符串工具 ==========
std::string trim(std::string_view s) noexcept
{
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(b, e - b + 1));
}

std::string toLower(std::string s) noexcept
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string toUpper(std::string s) noexcept
{
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// ========== 市场时段检测（纯时间判断，不依赖桥接层） ==========
bool marketSessionAllowsSubscriptions()
{
    // A股交易时段：周一至周五 9:30-11:30 / 13:00-15:00
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm* local = std::localtime(&t);

    if (local->tm_wday < 1 || local->tm_wday > 5) return false; // 周末

    int minutes = local->tm_hour * 60 + local->tm_min;
    return (minutes >= 570 && minutes < 690)   // 9:30-11:30
        || (minutes >= 780 && minutes < 900);   // 13:00-15:00
}

std::string marketSessionPhaseText()
{
    return marketSessionAllowsSubscriptions() ? "交易中" : "已收盘";
}

// ========== 配置读取（纯 C++） ==========
namespace cfg {

foundation::json::JsonFacade loadConfigObject()
{
    try {
        auto& mgr = foundation::config::ConfigManager::instance();
        std::string json = mgr.get_app_config_string("jujin.config", "");
        if (json.empty()) return foundation::json::JsonFacade::createObject();
        return foundation::json::JsonFacade::parse(json);
    } catch (...) {
        return foundation::json::JsonFacade::createObject();
    }
}

std::string readString(const foundation::json::JsonFacade& obj, const char* key,
                       const char* envName)
{
    if (obj.has(key)) {
        std::string v = obj.get(key).asString();
        if (!v.empty()) return v;
    }
    if (const char* e = std::getenv(envName)) return std::string(e);
    return {};
}

bool readBool(const foundation::json::JsonFacade& obj, const char* key,
              const char* envName)
{
    if (obj.has(key)) {
        auto v = obj.get(key);
        if (v.isBool()) return v.asBool();
        std::string s = toLower(trim(v.asString()));
        return s == "1" || s == "true";
    }
    if (const char* e = std::getenv(envName)) {
        std::string s = toLower(trim(std::string(e)));
        return s == "1" || s == "true";
    }
    return false;
}

int readInt(const foundation::json::JsonFacade& obj, const char* key,
            const char* envName, int fallback)
{
    if (obj.has(key)) {
        auto v = obj.get(key);
        if (v.isNumber()) return v.asInt();
        try { return std::stoi(trim(v.asString())); } catch (...) {}
    }
    if (const char* e = std::getenv(envName)) {
        try { return std::stoi(trim(std::string(e))); } catch (...) {}
    }
    return fallback;
}

std::unordered_set<std::string> readBoundStrategyIds(const foundation::json::JsonFacade& obj)
{
    std::unordered_set<std::string> ids;
    if (obj.has("boundStrategyId")) {
        std::string v = trim(obj.get("boundStrategyId").asString());
        if (!v.empty()) ids.insert(v);
    }
    if (obj.has("boundStrategies") && obj.get("boundStrategies").isArray()) {
        auto arr = obj.get("boundStrategies");
        for (size_t i = 0; i < arr.size(); ++i) {
            auto entry = arr.at(i);
            if (entry.isObject()) {
                std::string v = trim(entry.get("strategyId").asString());
                if (!v.empty()) ids.insert(v);
            } else if (entry.isString()) {
                std::string v = trim(entry.asString());
                if (!v.empty()) ids.insert(v);
            }
        }
    }
    return ids;
}

} // namespace cfg

// ========== 掘金市场符号标准化 ==========
std::string toGmMarketSymbol(std::string symbol)
{
    symbol = trim(symbol);
    if (symbol.empty()) return symbol;
    symbol = toUpper(symbol);

    // 已是掘金格式 → 直接返回（含期货前缀）
    if (symbol.rfind("SHSE.", 0) == 0 || symbol.rfind("SZSE.", 0) == 0 || symbol.rfind("BSE.", 0) == 0
        || symbol.rfind("CFFEX.", 0) == 0 || symbol.rfind("SHFE.", 0) == 0 || symbol.rfind("DCE.", 0) == 0
        || symbol.rfind("CZCE.", 0) == 0 || symbol.rfind("INE.", 0) == 0 || symbol.rfind("GFEX.", 0) == 0)
        return symbol;

    // 股票符号 → AStockSymbol 统一处理
    auto sym = foundation::market::AStockSymbol::fromString(symbol);
    if (sym.isValid()) return sym.gmSymbol();

    // 期货后缀 "CFFEX" / "SHFE" 等 → 交易所前缀格式
    auto dot = symbol.find('.');
    if (dot != std::string::npos) {
        std::string code = symbol.substr(0, dot);
        std::string exchange = symbol.substr(dot + 1);
        return exchange + "." + code;
    }
    return symbol;
}

// ========== 订单字段规范化 ==========
std::string normalizeOrderSide(std::string side) noexcept
{
    side = toUpper(trim(side));
    if (side == "1" || side == "BUY" || side == "LONG") return "BUY";
    if (side == "2" || side == "SELL" || side == "SHORT") return "SELL";
    return side;
}

std::string normalizeOrderStatus(std::string status) noexcept
{
    status = toUpper(trim(status));
    if (status.empty()) return "SUBMITTED";
    if (status == "0" || status == "UNKNOWN") return "PENDING";
    if (status == "1" || status == "10" || status == "13") return "SUBMITTED";
    if (status == "2") return "PARTIAL_FILLED";
    if (status == "3") return "FILLED";
    if (status == "4" || status == "5" || status == "12") return "CANCELLED";
    if (status == "8") return "REJECTED";
    if (status == "PENDINGNEW" || status == "NEW") return "SUBMITTED";
    return status;
}

int64_t toEpochUs(const std::chrono::system_clock::time_point& tp) noexcept
{
    if (tp == std::chrono::system_clock::time_point()) return 0;
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

} // namespace

JujinMarketConnector::JujinMarketConnector() = default;

JujinMarketConnector::~JujinMarketConnector()
{
    stop();
}

bool JujinMarketConnector::isEnabledByEnvironment() const
{
    // 编译时已启用 ASTOCK_ENABLE_JUJIN_MARKET
    return true;
}

bool JujinMarketConnector::start()
{
    if (m_started) {
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] start skipped: already started";
        return true;
    }

    auto* eventBus = engine::get_engine_event_bus();
    if (!eventBus) {
        m_lastError = "engine event bus is not initialized";
        return false;
    }

    if (thirdparty::JujinApi* sharedApi = engine::get_shared_jujin_api()) {
        if (sharedApi != m_api.get()) {
            m_lastError = "shared jujin trading session already exists";
            return false;
        }
    }

    auto& cfgMgr = foundation::config::ConfigManager::instance();
    m_maxMarketSubscriptions = static_cast<size_t>(
        (std::max)(1, cfgMgr.get_config_file_int(foundation::config::ConfigFile::Jujin,
            "maxMarketSubscriptions", 500)));
    m_marketSubscriptionBatchSize = static_cast<size_t>(
        (std::max)(1, cfgMgr.get_config_file_int(foundation::config::ConfigFile::Jujin,
            "marketSubscriptionBatchSize", 4)));
    INTERNAL_INFO_STREAM << "[JujinMarketConnector] start requested";

    // 通过 ConfigManager 统一读取 trading_connection.json
    std::string token, accountId, gmStrategyId, accountRuntimeId;
    {
        auto cfg = cfgMgr.loadConfigFile(foundation::config::ConfigFile::TradingConnection);
        if (cfg && !cfg->isNull()) {
            token    = cfg->has("token")    ? cfg->get("token").asString()    : "";
            accountId= cfg->has("accountId")? cfg->get("accountId").asString(): "";
            gmStrategyId = cfg->has("gmStrategyId") ? cfg->get("gmStrategyId").asString() : "";
            accountRuntimeId = cfg->has("accountRuntimeStrategyId") ? cfg->get("accountRuntimeStrategyId").asString() : "";
        }
    }

    if (token.empty()) {
        m_lastError = "jujin token is empty";
        return false;
    }

    std::string resolvedId = trim(accountRuntimeId);
    if (resolvedId.empty()) resolvedId = trim(gmStrategyId);
    if (resolvedId.empty()) {
        m_lastError = "JMC: missing gmStrategyId";
        return false;
    }

    thirdparty::ConfigParams config;
    config.platform = thirdparty::PlatformType::JUJIN;
    config.token = token;
    config.account_id = accountId;
    config.server_url = "";

    config.extra_params["runtime_strategy_id"] = resolvedId;
    config.extra_params["mode"] = "1";
    config.extra_params["simtrade_only"] = "false";
    config.extra_params["read_only"] = "false";

    INTERNAL_INFO_STREAM << "[JujinMarketConnector] accountId=" << config.account_id
                         << " gmStrategyId=" << gmStrategyId
                         << " resolvedId=" << resolvedId
                         << " mode=" << config.extra_params["mode"]
                         << " simtradeOnly=" << config.extra_params["simtrade_only"]
                         << " readOnly=" << config.extra_params["read_only"];

    m_api = std::make_unique<thirdparty::JujinApi>();
    m_api->set_event_bus(std::shared_ptr<engine::EventBus>(eventBus, [](engine::EventBus*) {}));

    if (!m_api->initialize(config)) { m_lastError = "initialize failed"; m_api.reset(); return false; }
    if (!m_api->connect()) { m_lastError = "connect failed"; m_api.reset(); return false; }

    engine::register_shared_jujin_api(m_api.get());
    m_stopRequested.store(false);

    if (m_marketSubscriptionThread.joinable()) m_marketSubscriptionThread.join();
    m_marketSubscriptionThread = std::thread([this, eventBus]() { processSubscriptionRequests(eventBus); });

    INTERNAL_INFO_STREAM << "[JMC] API 已连接，等待策略请求订阅";

    m_watchRequestSubscription = eventBus->subscribe("market.watch.ensure",
        [this](const engine::EventFormat& event) {
            if (!m_api || !marketSessionAllowsSubscriptions()) return;
            auto symbolValue = event.get<std::string>("symbol");
            if (symbolValue.has_value() && !symbolValue->empty())
                enqueueWatchSymbol(*symbolValue);
        });

    // ── 订阅 C++ SDK 行情事件 → 推送到策略引擎 ──
    // GmStrategySession::on_tick() 发布 "trading.market.tick"
    // 字段: symbol, price(double), volume(double, 瞬时成交量), tradingDay(int64, YYYYMMDD)
    m_tradingTickSubscription = eventBus->subscribe("trading.market.tick",
        [this](const engine::EventFormat& event) {
            auto sym   = event.get<std::string>("symbol");
            auto price = event.get<double>("price");
            auto vol   = event.get<double>("volume");
            auto date  = event.get<std::int64_t>("tradingDay");
            if (sym.has_value() && price.has_value()) {
                static int totalTicks = 0;
                static auto lastReport = std::chrono::steady_clock::now();
                totalTicks++;
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastReport).count();
                if (elapsed >= 60) {
                    INTERNAL_INFO_STREAM << "[行情] tick: " << totalTicks << "/" << elapsed
                                         << "s ≈ " << (totalTicks / (elapsed > 0 ? elapsed : 1)) << "/s";
                    totalTicks = 0;
                    lastReport = now;
                }
                app::system::TradingSystem::instance().pushMarketData(
                    *sym, *price, vol.value_or(0.0),
                    date.has_value() ? static_cast<std::int32_t>(*date) : 0);
            }
        });

    // GmStrategySession::on_bar() 发布 "trading.market.bar"
    // 字段: symbol, close(double), volume(double, bar成交量), tradingDay(int64, YYYYMMDD)
    m_tradingBarSubscription = eventBus->subscribe("trading.market.bar",
        [this](const engine::EventFormat& event) {
            auto sym   = event.get<std::string>("symbol");
            auto price = event.get<double>("close");
            auto vol   = event.get<double>("volume");
            auto date  = event.get<std::int64_t>("tradingDay");
            if (sym.has_value() && price.has_value()) {
                app::system::TradingSystem::instance().pushMarketData(
                    *sym, *price, vol.value_or(0.0),
                    date.has_value() ? static_cast<std::int32_t>(*date) : 0);
            }
        });

    m_started = true;
    m_lastError.clear();
    publishSubscriptionStatus(eventBus, true);

    std::unordered_set<std::string> boundIds;
    {
        auto cfg = cfgMgr.loadConfigFile(foundation::config::ConfigFile::TradingConnection);
        if (cfg && !cfg->isNull()) {
            if (cfg->has("boundStrategyId")) {
                std::string v = trim(cfg->get("boundStrategyId").asString());
                if (!v.empty()) boundIds.insert(v);
            }
            if (cfg->has("boundStrategies")) {
                auto arr = cfg->get("boundStrategies");
                if (arr.isArray()) {
                    for (size_t i = 0; i < arr.size(); ++i) {
                        auto entry = arr.at(i);
                        if (entry.isObject()) {
                            std::string v = trim(entry.get("strategyId").asString());
                            if (!v.empty()) boundIds.insert(v);
                        } else if (entry.isString()) {
                            std::string v = trim(entry.asString());
                            if (!v.empty()) boundIds.insert(v);
                        }
                    }
                }
            }
        }
    }
    publishExistingOrders(eventBus, token, config.account_id, resolvedId, boundIds);
    INTERNAL_INFO_STREAM << "[JujinMarketConnector] start completed";
    return true;
}

void JujinMarketConnector::stop()
{
    m_stopRequested.store(true);
    m_pendingWatchCv.notify_all();

    if (m_initialOrderSyncThread.joinable()) {
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] waiting for initial order sync thread";
        m_initialOrderSyncThread.join();
    }

    if (m_marketSubscriptionThread.joinable()) {
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] waiting for market subscription thread";
        m_marketSubscriptionThread.join();
    }

    if (engine::EventBus* bus = engine::get_engine_event_bus()) {
        if (m_watchRequestSubscription) {
            bus->unsubscribe(m_watchRequestSubscription);
            m_watchRequestSubscription = foundation::utils::Uuid();
        }
        if (m_tradingTickSubscription) {
            bus->unsubscribe(m_tradingTickSubscription);
            m_tradingTickSubscription = foundation::utils::Uuid();
        }
        if (m_tradingBarSubscription) {
            bus->unsubscribe(m_tradingBarSubscription);
            m_tradingBarSubscription = foundation::utils::Uuid();
        }
    }

    if (!m_api) {
        m_started = false;
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] stop skipped: api not initialized";
        return;
    }

    INTERNAL_INFO_STREAM << "[JujinMarketConnector] stopping";
    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        m_subscribedSymbols.clear();
    }
    {
        std::lock_guard<std::mutex> lock(m_pendingWatchMutex);
        m_pendingWatchQueue.clear();
        m_pendingWatchSymbols.clear();
    }

    publishSubscriptionStatus(engine::get_engine_event_bus(), false);

    if (engine::get_shared_jujin_api() == m_api.get()) {
        engine::register_shared_jujin_api(nullptr);
    }

    m_api->disconnect();
    m_api.reset();
    m_started = false;
    INTERNAL_INFO_STREAM << "[JujinMarketConnector] stopped";
}

std::string JujinMarketConnector::symbolName(const std::string& gmSymbol) const {
    std::lock_guard<std::mutex> lk(m_symbolNameMutex);
    auto it = m_symbolNames.find(gmSymbol);
    return it != m_symbolNames.end() ? it->second : "";
}

void JujinMarketConnector::enqueueWatchSymbol(const std::string& symbol)
{
    const std::string normalizedSymbol = toGmMarketSymbol(symbol);
    if (normalizedSymbol.empty()) {
        return;
    }

    {
        std::lock_guard<std::mutex> subscriptionLock(m_subscriptionMutex);
        if (m_subscribedSymbols.find(normalizedSymbol) != m_subscribedSymbols.end()) {
            return;
        }
    }

    {
        std::lock_guard<std::mutex> pendingLock(m_pendingWatchMutex);
        if (m_pendingWatchSymbols.find(normalizedSymbol) != m_pendingWatchSymbols.end()) {
            return;
        }
        m_pendingWatchQueue.push_back(normalizedSymbol);
        m_pendingWatchSymbols.insert(normalizedSymbol);
    }
    m_pendingWatchCv.notify_one();
}

void JujinMarketConnector::processSubscriptionRequests(engine::EventBus* eventBus)
{
    INTERNAL_INFO_STREAM << "[JMC] 订阅线程启动";

    if (!marketSessionAllowsSubscriptions()) {
        INTERNAL_INFO_STREAM << "[JMC] 当前非交易时段，跳过订阅";
        return;
    }

    // 仅按需订阅：策略通过 "market.watch.ensure" 事件请求的标的
    while (true) {
        std::vector<std::string> batch;

        {
            std::unique_lock<std::mutex> lock(m_pendingWatchMutex);
            m_pendingWatchCv.wait(lock, [this]() {
                return m_stopRequested.load() || !m_pendingWatchQueue.empty();
            });

            if (m_stopRequested.load() && m_pendingWatchQueue.empty()) {
                break;
            }

            while (!m_pendingWatchQueue.empty() && batch.size() < m_marketSubscriptionBatchSize) {
                const std::string symbol = m_pendingWatchQueue.front();
                m_pendingWatchQueue.pop_front();
                m_pendingWatchSymbols.erase(symbol);
                batch.push_back(symbol);
            }
        }

        if (batch.empty()) {
            continue;
        }

        if (!subscribeSymbolBatch(batch, eventBus)) {
            INTERNAL_ERROR_STREAM << "[JMC] subscribe batch failed size=" << batch.size();
        }
    }
}

bool JujinMarketConnector::subscribeSymbolBatch(const std::vector<std::string>& symbols, engine::EventBus* eventBus)
{
    if (!m_api || !eventBus || !eventBus->is_running() || symbols.empty()) {
        return false;
    }

    std::vector<std::string> normalizedSymbols;
    normalizedSymbols.reserve(symbols.size());

    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        for (const std::string& rawSymbol : symbols) {
            const std::string normalizedSymbol = toGmMarketSymbol(rawSymbol);
            if (normalizedSymbol.empty()) {
                continue;
            }
            if (m_subscribedSymbols.find(normalizedSymbol) != m_subscribedSymbols.end()) {
                continue;
            }
            if (m_subscribedSymbols.size() + normalizedSymbols.size() >= m_maxMarketSubscriptions) {
                continue;
            }
            normalizedSymbols.push_back(normalizedSymbol);
        }
    }

    if (normalizedSymbols.empty()) {
        return true;
    }

    INTERNAL_INFO_STREAM << "[JMC] 批量订阅行情: " << normalizedSymbols.size() << " 只标的";

    if (!m_api->subscribe_market_data(normalizedSymbols, thirdparty::MarketDataType::TICK, {})) {
        for (const std::string& symbol : normalizedSymbols) {
            if (!subscribeSymbol(symbol, eventBus)) {
                INTERNAL_ERROR_STREAM << "[JMC] JujinMarketConnector: failed to subscribe tick fallback symbol" << (symbol);
            }
        }
        return false;
    }
    if (!m_api->subscribe_market_data(normalizedSymbols, thirdparty::MarketDataType::BAR_1M, {})) {
        for (const std::string& symbol : normalizedSymbols) {
            if (!subscribeSymbol(symbol, eventBus)) {
                INTERNAL_ERROR_STREAM << "[JMC] JujinMarketConnector: failed to subscribe bar fallback symbol" << (symbol);
            }
        }
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        for (const std::string& symbol : normalizedSymbols) {
            m_subscribedSymbols.insert(symbol);
        }
    }

    publishSubscriptionStatus(eventBus, true);

    return true;
}

bool JujinMarketConnector::subscribeSymbol(const std::string& symbol, engine::EventBus* eventBus)
{
    if (!m_api || !eventBus || !eventBus->is_running()) {
        return false;
    }

    const std::string normalizedSymbol = toGmMarketSymbol(symbol);
    if (normalizedSymbol.empty()) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        if (m_subscribedSymbols.find(normalizedSymbol) != m_subscribedSymbols.end()) {
            return true;
        }
    }

    INTERNAL_ERROR_STREAM << "[JMC] JujinMarketConnector: subscribe market symbol" << (symbol)
                         << "->" << (normalizedSymbol);

    const std::vector<std::string> symbols{normalizedSymbol};
    if (!m_api->subscribe_market_data(symbols, thirdparty::MarketDataType::TICK, {})) {
        return false;
    }
    if (!m_api->subscribe_market_data(symbols, thirdparty::MarketDataType::BAR_1M, {})) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        m_subscribedSymbols.insert(normalizedSymbol);
    }

    publishSubscriptionStatus(eventBus, true);

    return true;
}

void JujinMarketConnector::publishSubscriptionStatus(engine::EventBus* eventBus, bool active)
{
    if (!eventBus || !eventBus->is_running()) {
        return;
    }

    std::size_t subscribedCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_subscriptionMutex);
        subscribedCount = m_subscribedSymbols.size();
    }

    MarketSubscriptionStatusRegistry::update(
        static_cast<int>(subscribedCount),
        static_cast<int>(m_maxMarketSubscriptions),
        active);

    engine::EventFormat event = engine::EventFormat::create_from_strings(
        kRuntimeSubscriptionStatusEvent,
        "JUJIN_MARKET_CONNECTOR",
        0);
    event.set("subscription_count", static_cast<int64_t>(subscribedCount));
    event.set("subscription_limit", static_cast<int64_t>(m_maxMarketSubscriptions));
    event.set("active", active);
    event.metadata["subscription_count"] = std::to_string(subscribedCount);
    event.metadata["subscription_limit"] = std::to_string(m_maxMarketSubscriptions);
    event.metadata["active"] = active ? "true" : "false";
    const auto result = eventBus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
    if (!result) {
        INTERNAL_ERROR_STREAM << "[JMC] JujinMarketConnector: failed to publish subscription status"
                              << (result.message);
    }
}

const std::string& JujinMarketConnector::lastError() const
{
    return m_lastError;
}

void JujinMarketConnector::publishExistingOrders(engine::EventBus* eventBus,
                                                const std::string& token,
                                                const std::string& accountId,
                                                const std::string& runtimeStrategyId,
                                                const std::unordered_set<std::string>& boundStrategyIds)
{
    if (!eventBus || !eventBus->is_running() || token.empty()) return;

    if (m_initialOrderSyncThread.joinable())
        m_initialOrderSyncThread.join();

    auto* rawEventBus = eventBus;
    const std::string requestToken = token;
    const std::string requestAccountId = accountId;
    const std::string configuredRuntimeId = trim(runtimeStrategyId);
    const std::unordered_set<std::string> configuredBoundIds = boundStrategyIds;

    m_initialOrderSyncThread = std::thread([this, rawEventBus, requestToken, requestAccountId,
                                            configuredRuntimeId, configuredBoundIds]() {
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] initial unfinished-order sync started asynchronously";

        // 1. Write Python script to temp file
#ifdef _WIN32
        std::string tmpPath = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".")
                              + "\\astock_jujin_sync_" + std::to_string(std::rand()) + ".py";
#else
        std::string tmpPath = "/tmp/astock_jujin_sync_" + std::to_string(std::rand()) + ".py";
#endif
        std::ofstream scriptFile(tmpPath, std::ios::out | std::ios::trunc);
        if (!scriptFile.is_open()) {
            INTERNAL_ERROR_STREAM << "[JujinMarketConnector] failed to create temp python script file";
            return;
        }

        const char* kPyScript = R"py(
import json, sys
import gm.api as gm
token = sys.argv[1]
account_id = sys.argv[2]
gm.set_token(token)
if account_id:
    gm.set_account_id(account_id)
orders = gm.get_unfinished_orders() or []

def pick(obj, *keys):
    for key in keys:
        value = obj.get(key) if isinstance(obj, dict) else None
        if value is not None and value != '':
            return value
    return None

result = []
for order in orders:
    item = order if isinstance(order, dict) else {}
    result.append({
        'order_id': pick(item, 'cl_ord_id', 'order_id', 'orderId'),
        'business_strategy_id': pick(item, 'business_strategy_id'),
        'runtime_strategy_id': pick(item, 'runtime_strategy_id'),
        'symbol': pick(item, 'symbol'),
        'side': pick(item, 'side', 'position_side'),
        'price': pick(item, 'price'),
        'quantity': pick(item, 'volume', 'quantity'),
        'filled_quantity': pick(item, 'filled_volume', 'filledQuantity'),
        'filled_notional': pick(item, 'filled_amount', 'filledNotional'),
        'status': pick(item, 'status'),
        'message': pick(item, 'ord_rej_reason_detail', 'status_msg', 'message'),
        'created_at': str(pick(item, 'created_at', 'createdAt') or ''),
        'updated_at': str(pick(item, 'updated_at', 'updatedAt') or '')
    })
print(json.dumps(result, ensure_ascii=True))
)py";

        scriptFile << kPyScript;
        scriptFile.close();

        // 2. Execute Python
        std::string cmd = "python \"" + tmpPath + "\" \"" + requestToken + "\" \"" + requestAccountId + "\"";
#ifdef _WIN32
        FILE* pipe = _popen(cmd.c_str(), "r");
#else
        FILE* pipe = popen(cmd.c_str(), "r");
#endif
        if (!pipe) {
            std::remove(tmpPath.c_str());
            INTERNAL_ERROR_STREAM << "[JujinMarketConnector] python process start failed";
            return;
        }

        std::string output;
        char buf[4096];
        while (fgets(buf, sizeof(buf), pipe)) output += buf;
        int ret = 
#ifdef _WIN32
            _pclose(pipe);
#else
            pclose(pipe);
#endif
        std::remove(tmpPath.c_str());

        if (m_stopRequested.load()) return;
        if (ret != 0) {
            INTERNAL_ERROR_STREAM << "[JujinMarketConnector] python exited code=" << ret
                                  << " output=" << output.substr(0, 256);
            return;
        }

        // 3. Parse JSON
        auto doc = foundation::json::JsonFacade::parse(output);
        if (!doc.isArray()) {
            INTERNAL_ERROR_STREAM << "[JujinMarketConnector] invalid JSON array";
            return;
        }
        if (doc.size() == 0) {
            INTERNAL_INFO_STREAM << "[JujinMarketConnector] no orders found";
            return;
        }

        std::size_t publishedCount = 0, filteredCount = 0;
        for (std::size_t i = 0; i < doc.size() && !m_stopRequested.load()
             && rawEventBus && rawEventBus->is_running(); ++i) {
            auto order = doc.at(i);
            if (!order.isObject()) continue;

            std::string orderId    = order.has("order_id") ? trim(order.get("order_id").asString()) : "";
            std::string bizId      = order.has("business_strategy_id") ? trim(order.get("business_strategy_id").asString()) : "";
            std::string rtId       = order.has("runtime_strategy_id") ? trim(order.get("runtime_strategy_id").asString()) : "";
            std::string sym        = order.has("symbol") ? trim(order.get("symbol").asString()) : "";
            if (orderId.empty() || sym.empty()) continue;

            // Strategy filter
            bool hasRt = !configuredRuntimeId.empty();
            bool hasBiz = !configuredBoundIds.empty();
            bool rtMatch = hasRt && !rtId.empty() && rtId == configuredRuntimeId;
            bool bizMatch = hasBiz && !bizId.empty() && configuredBoundIds.find(bizId) != configuredBoundIds.end();
            if ((hasRt || hasBiz) && !(rtMatch || bizMatch)) { ++filteredCount; continue; }

            engine::EventFormat event = engine::EventFormat::create_from_strings(
                engine::EventTypes::TRADING_ORDER_UPDATED,
                "TRADING_SNAPSHOT",
                toEpochUs(std::chrono::system_clock::now()));
            event.set("order_id", orderId);
            event.set("symbol", sym);
            if (!bizId.empty()) { event.set("business_strategy_id", bizId); event.metadata["business_strategy_id"] = bizId; }
            if (!rtId.empty())  { event.set("runtime_strategy_id", rtId); event.metadata["runtime_strategy_id"] = rtId; }

            std::string rawSide = order.has("side") ? trim(order.get("side").asString()) : "1";
            event.set("side", normalizeOrderSide(rawSide));
            double price = order.has("price") ? order.get("price").asDouble() : 0.0;
            event.set("price", price);
            int64_t qty = order.has("quantity") ? static_cast<int64_t>(order.get("quantity").asDouble()) : 0;
            event.set("quantity", qty);
            int64_t fqty = order.has("filled_quantity") ? static_cast<int64_t>(order.get("filled_quantity").asDouble()) : 0;
            event.set("filled_quantity", fqty);
            double fnotional = order.has("filled_notional") ? order.get("filled_notional").asDouble() : 0.0;
            event.set("filled_notional", fnotional);
            std::string rawStatus = order.has("status") ? trim(order.get("status").asString()) : "";
            event.set("status", normalizeOrderStatus(rawStatus));
            std::string msg = order.has("message") ? trim(order.get("message").asString()) : "";
            event.set("message", msg);

            if (order.has("created_at")) { std::string v = trim(order.get("created_at").asString()); event.set("created_at", v); event.metadata["created_at"] = v; }
            if (order.has("updated_at")) { std::string v = trim(order.get("updated_at").asString()); event.set("updated_at", v); event.metadata["updated_at"] = v; }

            event.metadata["order_id"] = orderId;
            event.metadata["symbol"] = sym;
            event.metadata["side"] = normalizeOrderSide(rawSide);
            event.metadata["status"] = normalizeOrderStatus(rawStatus);
            event.metadata["source"] = "snapshot.async";
            event.metadata["event_contract"] = "canonical";

            rawEventBus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
            ++publishedCount;
        }
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] sync done published=" << publishedCount
                             << " filtered=" << filteredCount;
    });
}




