// AccountEngine.h — 账户引擎（engine 层，零 Qt，不依赖其他引擎）
// 职责：账户/持仓查询，接收 gmsdk 回调。共享 GmSdkSingleton 的 Strategy。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace engine {

struct AccountInfo {
    std::string accountId;
    double      totalAsset   = 0.0;
    double      availableCash = 0.0;
    double      marketValue   = 0.0;
    double      frozenCash    = 0.0;
};

struct Position {
    std::string symbol;
    int64_t     quantity      = 0;
    int64_t     availableQty  = 0;
    double      costPrice     = 0.0;
    double      lastPrice     = 0.0;
    double      marketValue   = 0.0;
    double      unrealizedPnl = 0.0;
};

class AccountEngine {
public:
    static AccountEngine& instance();

    bool initialize(void* strategy);
    void shutdown();
    bool initialized() const;

    // 同步查询
    AccountInfo           account() const;
    std::vector<Position> positions() const;

    // 回调（由 GmSdkSingleton::SessionStrategy 调用）
    using AccountFn  = std::function<void(const AccountInfo&)>;
    using PositionFn = std::function<void(const std::vector<Position>&)>;
    void setOnAccountChanged(AccountFn cb);
    void setOnPositionChanged(PositionFn cb);

    // gmsdk 回调入口
    void onCash(const AccountInfo& a);
    void onPositionUpdate(const std::vector<Position>& positions);

private:
    AccountEngine() = default;
    ~AccountEngine() = default;

    void* m_strategy = nullptr;
    AccountFn  m_onAccountChanged;
    PositionFn m_onPositionChanged;
};

} // namespace engine
