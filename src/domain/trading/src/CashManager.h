#pragma once
// ---------------------------------------------------------------------------
// CashManager — 唯一现金核算权威
// 依赖注入 IAccountProvider，调不到 engine::AccountEngine::instance()
// ---------------------------------------------------------------------------

#include "../include/TradingSystem.h"  // for IAccountProvider

namespace domain::trading {

class CashManager {
public:
    explicit CashManager(IAccountProvider& provider);

    double availableCash(bool forceRefresh = false);
    // forceRefresh: 盘后由调用方传入 true，触发 provider.refresh()
    // CashManager 自身不判断时段，零外部单例依赖

    bool hasReceivedData() const;

private:
    IAccountProvider& m_provider;
};

} // namespace domain::trading
