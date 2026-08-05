#pragma once
// ---------------------------------------------------------------------------
// TradingSystem — domain trading 公开 facade
// 头文件零外部依赖（不 include 任何已有 trading 头文件，避免类型冲突）
// ---------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace domain::trading {

// ── IAccountProvider ──
struct CashSnapshot {
    double totalAsset{0.0};
    double availableCash{0.0};
    double frozenCash{0.0};
    double marketValue{0.0};
    double totalLiability{0.0};
};

struct HoldingPosition {
    std::string symbol;
    std::int64_t availableQty{0};
    std::int64_t longQty{0};
    std::int64_t shortQty{0};
    double avgCost{0.0};
    double lastPrice{0.0};
    double marketValue{0.0};
    double unrealizedPnL{0.0};
};

class IAccountProvider {
public:
    virtual ~IAccountProvider() = default;
    virtual CashSnapshot snapshot() const = 0;
    virtual double availableCash() const = 0;
    virtual std::vector<HoldingPosition> positions() const = 0;
    virtual void refresh() = 0;
    virtual bool hasReceivedData() const = 0;
};

// ── facade ──
class TradingSystem {
public:
    static TradingSystem& instance();

    TradingSystem();
    ~TradingSystem();

    void initialize(IAccountProvider* accountProvider = nullptr);
    bool isReady() const;

    bool isTradingSession() const;
    bool isAfterHoursSession() const;
    bool isInLockPeriod() const;

    double availableCash() const;
    double closingPrice() const;

    void initCallbacks();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    TradingSystem(const TradingSystem&) = delete;
    TradingSystem& operator=(const TradingSystem&) = delete;
};

} // namespace domain::trading
