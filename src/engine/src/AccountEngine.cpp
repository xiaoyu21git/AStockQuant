// AccountEngine.cpp — 账户引擎实现，共享 GmSessionEngine 的 ::Strategy
#include "AccountEngine.h"
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
    return true;
}

void AccountEngine::shutdown() {
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
