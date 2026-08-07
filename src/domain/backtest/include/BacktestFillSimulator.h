#pragma once
// ═════════════════════════════════════════════════════════════════════════
// BacktestFillSimulator — 回测成交模拟 (纯 C++，零 Qt)
// 策略回测和因子回测共用：佣金/滑点/印花税计算
// ═════════════════════════════════════════════════════════════════════════

#include <cstdint>
#include "../../trading/include/TradingCosts.h"

namespace domain::backtest {

struct FillSimulatorParams {
    double commissionRate{trading::TradingCosts::kDefaultCommissionRate};
    double slippageRate{trading::TradingCosts::kDefaultSlippageRate};
    double taxRate{trading::TradingCosts::kDefaultStampTaxRate};   // A股印花税(仅卖出)
};

struct FillSimulatorResult {
    double cost{0.0};             // 买方总成本(含佣金+滑点)
    double income{0.0};           // 卖方净收入(扣除佣金+滑点+税)
    double commission{0.0};
    double slippage{0.0};
    double tax{0.0};
};

class BacktestFillSimulator final {
public:
    explicit BacktestFillSimulator(const FillSimulatorParams& params) : p_(params) {}

    /// @brief 模拟买入成交
    FillSimulatorResult simulateBuy(double price, std::int64_t quantity) const {
        FillSimulatorResult r;
        const double notional = price * static_cast<double>(quantity);
        r.commission = notional * p_.commissionRate;
        r.slippage   = notional * p_.slippageRate;
        r.cost       = notional + r.commission + r.slippage;
        return r;
    }

    /// @brief 模拟卖出成交
    FillSimulatorResult simulateSell(double price, std::int64_t quantity) const {
        FillSimulatorResult r;
        const double notional = price * static_cast<double>(quantity);
        r.commission = notional * p_.commissionRate;
        r.slippage   = notional * p_.slippageRate;
        r.tax        = notional * p_.taxRate;
        r.income     = notional - r.commission - r.slippage - r.tax;
        return r;
    }

    /// @brief 买入后剩余现金; 若不足则返回 -1
    double cashAfterBuy(double availableCash, double price, std::int64_t quantity) const {
        auto r = simulateBuy(price, quantity);
        return (availableCash >= r.cost) ? (availableCash - r.cost) : -1.0;
    }

private:
    FillSimulatorParams p_;
};

} // namespace domain::backtest
