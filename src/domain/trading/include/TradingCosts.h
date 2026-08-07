// TradingCosts.h — A股交易成本统一常量
// 避免佣金/印花税率在 PnlCalculator 与 BacktestFillSimulator 中重复定义
#pragma once

namespace domain::trading {

/// @brief A股交易成本默认参数
struct TradingCosts {
    // 佣金率（双向收取），默认万三
    static constexpr double kDefaultCommissionRate = 0.0003;
    // 最低佣金（每笔），默认 5 元
    static constexpr double kDefaultMinCommission = 5.0;
    // 印花税率（仅卖方收取），默认千一
    static constexpr double kDefaultStampTaxRate = 0.001;
    // 滑点率（回测模拟用），默认千一
    static constexpr double kDefaultSlippageRate = 0.001;

    /// @brief 计算买入总成本（含佣金）
    static double buyCost(double price, std::int64_t quantity,
                          double commissionRate = kDefaultCommissionRate,
                          double slippageRate = kDefaultSlippageRate) {
        const double notional = price * static_cast<double>(quantity);
        return notional * (1.0 + commissionRate + slippageRate);
    }

    /// @brief 计算卖出净收入（扣除佣金+印花税）
    static double sellIncome(double price, std::int64_t quantity,
                             double commissionRate = kDefaultCommissionRate,
                             double slippageRate = kDefaultSlippageRate,
                             double stampTaxRate = kDefaultStampTaxRate) {
        const double notional = price * static_cast<double>(quantity);
        return notional * (1.0 - commissionRate - slippageRate - stampTaxRate);
    }
};

} // namespace domain::trading
