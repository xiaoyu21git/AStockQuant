#pragma once
// PnlCalculator.h — 盈亏计算器（纯 C++，零 Qt）
// 统一手续费/印花税/已实现盈亏/未实现盈亏的计算逻辑

#include <cstdint>

namespace domain::trading {

// ── 费率参数 ──
class FeeParams final {
public:
    FeeParams() = default;
    FeeParams(double commissionRate, double minCommission, double stampTaxRate);

    [[nodiscard]] double commissionRate() const noexcept { return m_commissionRate; }
    [[nodiscard]] double minCommission()  const noexcept { return m_minCommission; }
    [[nodiscard]] double stampTaxRate()   const noexcept { return m_stampTaxRate; }

private:
    double m_commissionRate{0.0003};
    double m_minCommission{5.0};
    double m_stampTaxRate{0.001};
};

// ── 已实现盈亏结果 ──
class PnlResult final {
public:
    [[nodiscard]] double grossPnl()   const noexcept { return m_grossPnl; }
    [[nodiscard]] double commission() const noexcept { return m_commission; }
    [[nodiscard]] double stampTax()   const noexcept { return m_stampTax; }
    [[nodiscard]] double netPnl()     const noexcept { return m_netPnl; }

private:
    friend class PnlCalculator;
    double m_grossPnl{0.0};
    double m_commission{0.0};
    double m_stampTax{0.0};
    double m_netPnl{0.0};
};

// ── 持仓盈亏结果 ──
class PositionPnl final {
public:
    [[nodiscard]] double costBasis()      const noexcept { return m_costBasis; }
    [[nodiscard]] double marketValue()    const noexcept { return m_marketValue; }
    [[nodiscard]] double unrealizedPnl()  const noexcept { return m_unrealizedPnl; }
    [[nodiscard]] double pnlPercent()     const noexcept { return m_pnlPercent; }

private:
    friend class PnlCalculator;
    double m_costBasis{0.0};
    double m_marketValue{0.0};
    double m_unrealizedPnl{0.0};
    double m_pnlPercent{0.0};
};

// ── 盈亏计算器 ──
class PnlCalculator final {
public:
    explicit PnlCalculator(const FeeParams& params);

    /// @brief 已实现盈亏 = 毛盈亏 - 手续费 - 印花税
    [[nodiscard]] PnlResult realizedPnl(double buyPrice, double sellPrice, std::int64_t qty) const;

    /// @brief 未实现盈亏 = (现价 - 成本) × 数量
    [[nodiscard]] PositionPnl unrealizedPnl(double avgCost, double lastPrice, std::int64_t qty) const;

    /// @brief 手续费 = max(成交额 × 费率, 最低手续费)
    [[nodiscard]] double calcCommission(double notional) const;

    /// @brief 印花税 = 成交额 × 税率（仅卖出；买入返回 0）
    [[nodiscard]] double calcStampTax(double notional, bool isSell) const;

private:
    FeeParams m_params;
};

} // namespace domain::trading
