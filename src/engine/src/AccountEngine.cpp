// AccountEngine.cpp — 账户引擎实现，共享 GmSdkSingleton 的 ::Strategy
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

void AccountEngine::shutdown() { m_strategy = nullptr; }
bool AccountEngine::initialized() const { return m_strategy != nullptr; }

AccountInfo AccountEngine::account() const {
    AccountInfo a;
    auto* s = static_cast<::Strategy*>(m_strategy);
    if (!s) return a;
    auto* arr = s->get_cash(nullptr);
    if (!arr || arr->status() != 0 || arr->count() == 0) {
        if (arr) arr->release(); return a;
    }
    auto& cash = arr->at(0);
    a.accountId = cash.account_id; a.totalAsset = cash.nav;
    a.availableCash = cash.available; a.marketValue = cash.market_value;
    a.frozenCash = cash.frozen;
    arr->release();
    return a;
}

std::vector<Position> AccountEngine::positions() const {
    std::vector<Position> result;
    auto* s = static_cast<::Strategy*>(m_strategy);
    if (!s) return result;
    auto* arr = s->get_position(nullptr);
    if (!arr || arr->status() != 0) { if (arr) arr->release(); return result; }
    for (size_t i = 0; i < arr->count(); ++i) {
        auto& gp = arr->at(i);
        Position p;
        p.symbol        = fromGm(gp.symbol);
        p.quantity      = (gp.side == 2) ? -gp.volume : gp.volume;
        p.availableQty  = gp.available;
        p.costPrice     = gp.vwap;
        p.lastPrice     = gp.price;
        p.marketValue   = gp.market_value;
        p.unrealizedPnl = gp.fpnl;
        result.push_back(p);
    }
    arr->release();
    return result;
}

void AccountEngine::setOnAccountChanged(AccountFn cb)  { m_onAccountChanged  = std::move(cb); }
void AccountEngine::setOnPositionChanged(PositionFn cb) { m_onPositionChanged = std::move(cb); }

void AccountEngine::onCash(const AccountInfo& a) {
    if (m_onAccountChanged) m_onAccountChanged(a);
}
void AccountEngine::onPositionUpdate(const std::vector<Position>& positions) {
    if (m_onPositionChanged) m_onPositionChanged(positions);
}

} // namespace engine
