// AccountEngine.cpp — 账户引擎实现，共享 GmSessionEngine 的 ::Strategy
#include "AccountEngine.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "foundation/market/AStockSymbol.h"
#include "../../../thirdparty/gmsdk/strategy.h"
#include <foundation/log/logging.hpp>
#include <algorithm>
#include <chrono>

namespace engine {

AccountEngine& AccountEngine::instance() {
    static AccountEngine engine;
    return engine;
}

AccountEngine::AccountEngine() {
    auto bus = get_engine_event_bus();
    if (bus) {
        m_accountSub = bus->subscribe("trading.account.updated",
            [this](const EventFormat& e) {
                AccountInfo a;
                a.accountId     = e.get<std::string>("account_id").value_or("");
                a.availableCash = e.get<double>("available").value_or(0.0);
                a.totalAsset    = e.get<double>("total_asset").value_or(0.0);
                a.marketValue   = e.get<double>("market_value").value_or(0.0);
                a.frozenCash    = e.get<double>("frozen").value_or(0.0);
                a.unrealizedPnl = e.get<double>("unrealized_pnl").value_or(0.0);
                a.realizedPnl   = e.get<double>("realized_pnl").value_or(0.0);
                onCash(a);
            });
        m_positionSub = bus->subscribe("trading.position.updated",
            [this](const EventFormat& e) {
                Position p;
                p.symbol        = e.get<std::string>("symbol").value_or("");
                p.quantity      = e.get<std::int64_t>("quantity").value_or(0);
                p.availableQty  = e.get<std::int64_t>("available_qty").value_or(0);
                p.costPrice     = e.get<double>("cost_price").value_or(0.0);
                p.lastPrice     = e.get<double>("last_price").value_or(0.0);
                p.marketValue   = e.get<double>("market_value").value_or(0.0);
                p.unrealizedPnl = e.get<double>("unrealized_pnl").value_or(0.0);
                onPositionUpdate({p});
            });
        m_tickSub = bus->subscribe("trading.market.tick",
            [this](const EventFormat& e) {
                auto sym   = e.get<std::string>("symbol");
                auto price = e.get<double>("price");
                if (!sym || !price) return;
                std::unique_lock<std::shared_mutex> lock(m_mutex);
                auto it = m_cachedPositions.find(*sym);
                if (it != m_cachedPositions.end())
                    it->second.lastPrice = *price;
            });
    }
    INTERNAL_INFO_STREAM << "[AccountEngine] 已初始化, 已订阅事件总线";
}

bool AccountEngine::initialize(::Strategy* strategy) {
    if (!strategy) return false;
    m_strategy = strategy;
    INTERNAL_INFO_STREAM << "[AccountEngine] 已使用策略初始化";
    return true;
}

void AccountEngine::shutdown() {
    auto bus = get_engine_event_bus();
    if (bus) {
        if (!m_accountSub.is_null())  bus->unsubscribe(m_accountSub);
        if (!m_positionSub.is_null()) bus->unsubscribe(m_positionSub);
        if (!m_tickSub.is_null())     bus->unsubscribe(m_tickSub);
    }
    m_strategy = nullptr;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_cacheValid = false;
    m_cachedPositions.clear();
    m_firstSeenSec.clear();
    INTERNAL_INFO_STREAM << "[AccountEngine] 关闭完成";
}

bool AccountEngine::initialized() const { return m_strategy != nullptr; }

AccountInfo AccountEngine::account() {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_cachedAccount;
}

std::vector<Position> AccountEngine::positions() {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    std::vector<Position> result;
    result.reserve(m_cachedPositions.size());
    for (const auto& [sym, p] : m_cachedPositions)
        result.push_back(p);
    return result;
}

AccountEngine::Snapshot AccountEngine::snapshot() {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    Snapshot s;
    s.account = m_cachedAccount;
    s.positions.reserve(m_cachedPositions.size());
    for (const auto& [sym, p] : m_cachedPositions) {
        Position withEntry = p;
        if (const auto it = m_firstSeenSec.find(sym); it != m_firstSeenSec.end())
            withEntry.firstHeldAtEpochSec = it->second;
        s.positions.push_back(withEntry);
    }
    return s;
}

void AccountEngine::updatePosition(const Position& p) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_cachedPositions[p.symbol] = p;
    if (p.quantity == 0) {
        // 券商推送清零 → 持仓已平, 抹除首次时间 (再次出现视为新持仓)
        m_firstSeenSec.erase(p.symbol);
    } else if (m_firstSeenSec.find(p.symbol) == m_firstSeenSec.end()) {
        m_firstSeenSec[p.symbol] = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}

std::unordered_map<std::string, int64_t> AccountEngine::Snapshot::posQtyByCode() const {
    std::unordered_map<std::string, int64_t> map;
    for (const auto& p : positions)
        map[foundation::market::AStockSymbol::codeOnly(p.symbol)] = p.quantity;
    return map;
}

AccountEngine::CallbackToken AccountEngine::addOnDataChanged(DataFn cb) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    const CallbackToken token = m_nextCbToken++;
    m_onDataChanged.emplace_back(token, std::move(cb));
    return token;
}

void AccountEngine::removeOnDataChanged(CallbackToken token) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_onDataChanged.erase(
        std::remove_if(m_onDataChanged.begin(), m_onDataChanged.end(),
                       [token](const auto& entry) { return entry.first == token; }),
        m_onDataChanged.end());
}

void AccountEngine::notifyDataChanged() {
    // 拷贝订阅列表后在锁外执行 (回调可能再次进出本引擎)
    std::vector<DataFn> cbs;
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        cbs.reserve(m_onDataChanged.size());
        for (const auto& entry : m_onDataChanged)
            cbs.push_back(entry.second);
    }
    for (const auto& cb : cbs)
        if (cb) cb();
}

void AccountEngine::onCash(const AccountInfo& a) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cachedAccount = a;
        m_cacheValid = true;
    }
    m_positionLogThrottle++;
    if (m_positionLogThrottle % 50 == 1) {
        INTERNAL_DEBUG_STREAM << "[AccountEngine] 资金更新: available=" << a.availableCash
                              << " 总资产=" << a.totalAsset
                              << " 市值=" << a.marketValue
                              << " (节流 #" << m_positionLogThrottle << ")";
    }
    notifyDataChanged();
}

void AccountEngine::onPositionUpdate(const std::vector<Position>& positions) {
    for (const auto& p : positions)
        updatePosition(p);
    // 节流: 每 50 次才打印一次日志
    m_positionLogThrottle++;
    if (m_positionLogThrottle % 50 == 1) {
        INTERNAL_DEBUG_STREAM << "[AccountEngine] 持仓更新: " << positions.size()
                              << " 个持仓, 缓存大小=" << m_cachedPositions.size()
                              << " (节流 #" << m_positionLogThrottle << ")";
    }
    notifyDataChanged();
}

void AccountEngine::applyAccountEvent(const AccountInfo& a) {
    onCash(a);
}

void AccountEngine::applyPositionEvent(const std::string& symbol, const Position& p) {
    updatePosition(p);
    notifyDataChanged();
}

} // namespace engine
