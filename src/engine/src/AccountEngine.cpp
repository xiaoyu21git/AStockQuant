// AccountEngine.cpp — 账户引擎实现，共享 GmSessionEngine 的 ::Strategy
#include "AccountEngine.h"
#include "Event/EventBus.hpp"
#include "Event/EventFormat.hpp"
#include "GlobalEventBusRegistry.h"
#include "../../../thirdparty/gmsdk/strategy.h"

namespace engine {

namespace {
std::string fromGm(const std::string& gm) {
    if (gm.compare(0, 5, "SHSE.") == 0) return gm.substr(5) + ".SH";
    if (gm.compare(0, 5, "SZSE.") == 0) return gm.substr(5) + ".SZ";
    if (gm.compare(0, 4, "BSE.")  == 0) return gm.substr(4) + ".BJ";
    return gm;
}
}

AccountEngine& AccountEngine::instance() {
    static AccountEngine engine;
    return engine;
}

AccountEngine::AccountEngine() {
    auto* bus = get_engine_event_bus();
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
}

bool AccountEngine::initialize(void* strategy) {
    if (!strategy) return false;
    m_strategy = strategy;
    return true;
}

void AccountEngine::shutdown() {
    auto* bus = get_engine_event_bus();
    if (bus) {
        if (!m_accountSub.is_null())  bus->unsubscribe(m_accountSub);
        if (!m_positionSub.is_null()) bus->unsubscribe(m_positionSub);
        if (!m_tickSub.is_null())     bus->unsubscribe(m_tickSub);
    }
    m_strategy = nullptr;
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_cacheValid = false;
    m_cachedPositions.clear();
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

void AccountEngine::setOnDataChanged(DataFn cb) {
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    m_onDataChanged = std::move(cb);
}

void AccountEngine::onCash(const AccountInfo& a) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cachedAccount = a;
        m_cacheValid = true;
    }
    if (m_onDataChanged) m_onDataChanged();
}

void AccountEngine::onPositionUpdate(const std::vector<Position>& positions) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (const auto& p : positions)
            m_cachedPositions[p.symbol] = p;
    }
    if (m_onDataChanged) m_onDataChanged();
}

void AccountEngine::applyAccountEvent(const AccountInfo& a) {
    onCash(a);
}

void AccountEngine::applyPositionEvent(const std::string& symbol, const Position& p) {
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_cachedPositions[symbol] = p;
    }
    if (m_onDataChanged) m_onDataChanged();
}

} // namespace engine
