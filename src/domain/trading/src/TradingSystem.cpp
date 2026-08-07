#include "../include/TradingSystem.h"

#include "../../../engine/include/GmSessionEngine.h"
#include "../../../engine/include/AccountEngine.h"

#include <memory>

namespace domain::trading {

// ── 默认 AccountProvider（生产环境，包装 AccountEngine 单例）──
namespace {
class AccountEngineAdapter : public IAccountProvider {
public:
    CashSnapshot snapshot() const override {
        CashSnapshot snap;
        auto& ae = engine::AccountEngine::instance();
        auto acc = ae.account();
        snap.totalAsset    = acc.totalAsset;
        snap.availableCash = acc.availableCash;
        snap.frozenCash    = acc.frozenCash;
        snap.marketValue   = acc.marketValue;
        snap.totalLiability = 0.0;
        return snap;
    }
    double availableCash() const override {
        return engine::AccountEngine::instance().account().availableCash;
    }
    std::vector<HoldingPosition> positions() const override {
        std::vector<HoldingPosition> result;
        auto posList = engine::AccountEngine::instance().positions();
        for (const auto& ep : posList) {
            HoldingPosition p;
            p.symbol        = ep.symbol;
            p.availableQty  = ep.availableQty;
            p.longQty       = ep.quantity;
            p.shortQty      = 0;
            p.avgCost       = ep.costPrice;
            p.lastPrice     = ep.lastPrice;
            p.marketValue   = ep.marketValue;
            p.unrealizedPnL = ep.unrealizedPnl;
            result.push_back(std::move(p));
        }
        return result;
    }
    void refresh() override {
        engine::AccountEngine::instance().account();
    }
    bool hasReceivedData() const override {
        return engine::AccountEngine::instance().account().totalAsset > 0;
    }
};
} // anonymous namespace

struct TradingSystem::Impl {
    std::unique_ptr<IAccountProvider> m_ownedProvider;
    IAccountProvider* m_accountProvider{nullptr};
    bool m_initialized{false};
    double m_cachedClosingPrice{0.0};
};

TradingSystem& TradingSystem::instance() {
    static TradingSystem ts;
    return ts;
}

TradingSystem::TradingSystem() : m_impl(std::make_unique<Impl>()) {}
TradingSystem::~TradingSystem() = default;

void TradingSystem::initialize(IAccountProvider* provider) {
    if (m_impl->m_initialized) return;
    if (provider) {
        m_impl->m_accountProvider = provider;
    } else {
        m_impl->m_ownedProvider = std::make_unique<AccountEngineAdapter>();
        m_impl->m_accountProvider = m_impl->m_ownedProvider.get();
    }
    m_impl->m_initialized = true;
}

bool TradingSystem::isReady() const {
    return m_impl->m_initialized
        && engine::GmSessionEngine::instance().isSessionReady()
        && m_impl->m_accountProvider
        && m_impl->m_accountProvider->hasReceivedData();
}

bool TradingSystem::isTradingSession() const {
    return engine::GmSessionEngine::instance().isSessionReady();
}
bool TradingSystem::isAfterHoursSession() const {
    return engine::GmSessionEngine::instance().isAfterHoursSession();
}
bool TradingSystem::isInLockPeriod() const {
    return engine::GmSessionEngine::instance().isInLockPeriod();
}

double TradingSystem::availableCash() const {
    return m_impl->m_accountProvider
        ? m_impl->m_accountProvider->availableCash() : 0.0;
}

double TradingSystem::closingPrice() const {
    return m_impl->m_cachedClosingPrice;
}

void TradingSystem::initCallbacks() {
    // engine 层回调已有 EventBus 机制
}

} // namespace domain::trading
