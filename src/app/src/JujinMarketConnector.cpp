#include "JujinMarketConnector.h"
#include "../../engine/include/GmSessionEngine.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include "../../engine/include/TradeEngine.h"
#include "../../engine/include/AccountEngine.h"
#include "../../engine/include/OrderManager.h"
#include "../../domain/trading/include/OrderBuilder.h"
#include "../../domain/trading/TradeExecutionEngine.h"
#include "../../domain/strategy/include/RiskManager.h"
#include "foundation/thread/ThreadPoolExecutor.h"

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
// JujinApi removed
#include "JujinTypes.h"
#include "../../../thirdparty/gmsdk/gmapi.h"
#include "MarketSubscriptionStatusRegistry.h"
#include "../../../infrastructure/include/database/NativePgConnectionPool.h"
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

    auto& cfgMgr = foundation::config::ConfigManager::instance();
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

    // ── 初始化 GmSessionEngine（唯一 gmsdk 连接）──
    if (!engine::GmSessionEngine::instance().initialize(token, accountId)) {
        m_lastError = "GmSessionEngine 初始化失败";
        return false;
    }
    // 上层引擎共享 Strategy
    auto* s = engine::GmSessionEngine::instance().strategy();
    engine::TradeEngine::instance().initialize(s);
    engine::AccountEngine::instance().initialize(s);
    engine::OrderManager::instance().initialize(s);
    INTERNAL_INFO_STREAM << "[JMC] GmSessionEngine 初始化成功";

    m_started = true;
    m_lastError.clear();

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
    publishExistingOrders(eventBus, token, accountId, "", boundIds);

    m_patrolExecutor = std::make_shared<foundation::thread::ThreadPoolExecutor>(
        1, 1, std::chrono::seconds(60), "JmcRiskPatrol");
    m_patrolExecutor->post([this]() { riskPatrolLoop(); });

    INTERNAL_INFO_STREAM << "[JujinMarketConnector] start completed";
    return true;
}

void JujinMarketConnector::stop()
{
    m_stopRequested.store(true);
    if (m_patrolExecutor)
        m_patrolExecutor->shutdown();

    if (m_initialOrderSyncThread.joinable()) {
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] waiting for initial order sync thread";
        m_initialOrderSyncThread.join();
    }

    engine::GmSessionEngine::instance().shutdown();
    m_started = false;
    INTERNAL_INFO_STREAM << "[JujinMarketConnector] stopped";
}

std::string JujinMarketConnector::symbolName(const std::string& gmSymbol) const {
    std::lock_guard<std::mutex> lk(m_symbolNameMutex);
    auto it = m_symbolNames.find(gmSymbol);
    return it != m_symbolNames.end() ? it->second : "";
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
    const std::string configuredRuntimeId = trim(runtimeStrategyId);
    const std::unordered_set<std::string> configuredBoundIds = boundStrategyIds;

    m_initialOrderSyncThread = std::thread([this, rawEventBus,
                                            configuredRuntimeId, configuredBoundIds]() {
        try {
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] initial unfinished-order sync started (C++ gmsdk)";

        auto& engine = engine::GmSessionEngine::instance();
        auto* s = static_cast<::Strategy*>(engine.strategy());
        if (!s) {
            INTERNAL_ERROR_STREAM << "[JujinMarketConnector] gmsdk strategy not initialized";
            return;
        }

        auto* arr = s->get_unfinished_orders(nullptr);
        if (!arr || arr->status() != 0) {
            if (arr) arr->release();
            INTERNAL_ERROR_STREAM << "[JujinMarketConnector] get_unfinished_orders failed";
            return;
        }

        std::size_t publishedCount = 0, filteredCount = 0;
        for (size_t i = 0; i < arr->count() && !m_stopRequested.load()
             && rawEventBus && rawEventBus->is_running(); ++i) {
            auto& o = arr->at(i);

            std::string orderId = o.cl_ord_id ? o.cl_ord_id : "";
            std::string sym     = engine.fromGmSymbol(o.symbol);
            std::string bizId   = o.strategy_id ? o.strategy_id : "";
            std::string rtId;   // gmsdk Order 无 runtime_strategy_id，预留
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

            event.set("side", std::to_string(o.side));
            event.set("price", static_cast<double>(o.price));
            event.set("quantity", static_cast<int64_t>(o.volume));
            event.set("filled_quantity", static_cast<int64_t>(o.filled_volume));
            event.set("filled_notional", static_cast<double>(o.filled_vwap));
            event.set("status", std::to_string(o.status));
            event.set("message", o.ord_rej_reason_detail ? o.ord_rej_reason_detail : "");

            event.metadata["order_id"] = orderId;
            event.metadata["symbol"]   = sym;
            event.metadata["side"]     = std::to_string(o.side);
            event.metadata["status"]   = std::to_string(o.status);
            event.metadata["source"]   = "snapshot.async";
            event.metadata["event_contract"] = "canonical";

            rawEventBus->publish(event, static_cast<int>(engine::EventPriority::HIGH));
            ++publishedCount;
        }
        arr->release();
        INTERNAL_INFO_STREAM << "[JujinMarketConnector] sync done published=" << publishedCount
                             << " filtered=" << filteredCount;
        } catch (const std::exception& e) {
            INTERNAL_ERROR_STREAM << "[JujinMarketConnector] sync exception: " << e.what();
        } catch (...) {
            INTERNAL_ERROR_STREAM << "[JujinMarketConnector] sync unknown exception";
        }
    });
}

void JujinMarketConnector::riskPatrolLoop()
{
    INTERNAL_INFO_STREAM << "[JMC] 全局风控巡检启动";
    domain::trading::OrderBuilder builder;
    while (!m_stopRequested.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        try {
            auto& accEng = engine::AccountEngine::instance();
            builder.setAccountId(accEng.account().accountId);
            auto stopOrders = domain::strategy::RiskManager::instance().patrolPositions(builder);
            auto& tradeEng = engine::TradeEngine::instance();
            for (const auto& o : stopOrders) {
                INTERNAL_WARN_STREAM << "[JMC] 止损/止盈触发: " << o.symbol()
                                     << " price=" << o.price() << " qty=" << o.quantity();
                if (tradeEng.initialized()) {
                    auto result = tradeEng.submitOrder(o);
                    INTERNAL_INFO_STREAM << "[JMC] 止损/止盈提交: " << o.symbol()
                                         << " accepted=" << result.accepted
                                         << " msg=" << result.message;
                    if (result.accepted) {
                        // 注册到 TradeExecutionEngine 以供状态追踪和 UI 更新
                        domain::trading::TradeOrder tOrder;
                        tOrder.setSymbol(o.symbol());
                        tOrder.setSide(domain::strategy::OrderDirection::Sell);
                        tOrder.setQuantity(static_cast<std::int64_t>(o.quantity()));
                        tOrder.setPrice(o.price());
                        tOrder.setClOrdId(o.clOrdId());
                        tOrder.setAccountId(o.accountId());
                        tOrder.setOrderType(domain::trading::OrderType::Limit);
                        tOrder.setPositionEffect(domain::strategy::PositionEffect::Close);
                        tOrder.setSignalStrength(0.8);
                        domain::trading::TradeExecutionEngine::instance().registerOrder(tOrder);
                    }
                } else {
                    INTERNAL_ERROR_STREAM << "[JMC] TradeEngine 未初始化, 止损/止盈订单未提交: "
                                          << o.symbol();
                }
            }
        } catch (const std::exception& e) {
            INTERNAL_WARN_STREAM << "[JMC] 巡检异常: " << e.what();
        }
    }
    INTERNAL_INFO_STREAM << "[JMC] 全局风控巡检结束";
}


