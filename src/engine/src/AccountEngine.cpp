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

bool AccountEngine::initialize(void* strategy) {
    if (!strategy) return false;
    m_strategy = strategy;

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
                m_cachedPositions[*sym].lastPrice = *price;
                if (m_onDataChanged) m_onDataChanged();
            });
    }
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
    m_cacheValid = false;
    m_cachedPositions.clear();
}
bool AccountEngine::initialized() const { return m_strategy != nullptr; }

AccountInfo AccountEngine::account() {
    if (m_cacheValid) return m_cachedAccount;
    auto* s = static_cast<::Strategy*>(m_strategy);
    if (!s) return {};
    auto* arr = s->get_cash(nullptr);
    if (!arr || arr->status() != 0 || arr->count() == 0) {
        if (arr) arr->release(); return {};
    }
    auto& cash = arr->at(0);
    m_cachedAccount.accountId = cash.account_id;
    m_cachedAccount.totalAsset = cash.nav;
    m_cachedAccount.availableCash = cash.available;
    m_cachedAccount.marketValue = cash.market_value;
    m_cachedAccount.frozenCash = cash.frozen;
    arr->release();
    m_cacheValid = true;
    return m_cachedAccount;
}

std::vector<Position> AccountEngine::positions() {
    if (m_cacheValid && !m_cachedPositions.empty()) {
        std::vector<Position> result;
        for (auto& [sym, p] : m_cachedPositions) result.push_back(p);
        return result;
    }
    auto* s = static_cast<::Strategy*>(m_strategy);
    if (!s) return {};
    auto* arr = s->get_position(nullptr);
    if (!arr || arr->status() != 0) { if (arr) arr->release(); return {}; }
    m_cachedPositions.clear();
    for (size_t i = 0; i < arr->count(); ++i) {
        auto& gp = arr->at(i);
        Position p;
        p.symbol = fromGm(gp.symbol);
        p.quantity = (gp.side == 2) ? -gp.volume : gp.volume;
        p.availableQty = gp.available;
        p.costPrice = gp.vwap;
        p.lastPrice = gp.price;
        p.marketValue = gp.market_value;
        p.unrealizedPnl = gp.fpnl;
        m_cachedPositions[p.symbol] = p;
    }
    arr->release();
    m_cacheValid = true;
    std::vector<Position> result;
    for (auto& [sym, p] : m_cachedPositions) result.push_back(p);
    return result;
}

void AccountEngine::setOnDataChanged(DataFn cb) { m_onDataChanged = std::move(cb); }

void AccountEngine::onCash(const AccountInfo& a) {
    m_cachedAccount = a;
    m_cacheValid = true;
    if (m_onDataChanged) m_onDataChanged();
}

void AccountEngine::onPositionUpdate(const std::vector<Position>& positions) {
    for (auto& p : positions)
        m_cachedPositions[p.symbol] = p;
    if (m_onDataChanged) m_onDataChanged();
}

void AccountEngine::applyAccountEvent(const AccountInfo& a) {
    onCash(a);
}

void AccountEngine::applyPositionEvent(const std::string& symbol, const Position& p) {
    m_cachedPositions[symbol] = p;
    if (m_onDataChanged) m_onDataChanged();
}

} // namespace engine
