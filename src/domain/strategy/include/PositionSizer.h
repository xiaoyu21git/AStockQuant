#pragma once
// PositionSizer — 仓位计算器
// 将所有手数/股数/权重转换逻辑集中封装，不散落各处。
// maxOrderQuantity 必须显式传入，不写死默认值。
//
// 职责: weight → quantity 映射 + 买卖增量计算 + 买方敞口压缩
// 零状态: 不持有持仓引用，纯数学计算

#include "StrategyServiceTypes.h"  // SignalIntent
#include "../../trading/TradingTypes.h"

#include <cstdint>
#include <vector>

namespace domain::strategy {

/// @brief 买卖增量结果
struct OrderDelta {
    SignalIntent intent = SignalIntent::KEEP;
    std::int64_t  deltaQty = 0;
};

/// @brief A股最小交易单位
inline constexpr std::int64_t kMinLot = 100;

class PositionSizer {
public:
    /// @brief 默认构造: baseQty=0, 调用 generate() 前必须通过 setBaseQty() 或带参构造设置
    PositionSizer() noexcept = default;

    /// @param maxOrderQuantity 权重建仓基数 (targetWeight × base = 目标股数)
    explicit PositionSizer(std::uint32_t maxOrderQuantity) noexcept
        : m_baseQty(maxOrderQuantity) {}

    // ── 访问器 / 设置器 ──
    std::uint32_t baseQty() const noexcept { return m_baseQty; }
    void setBaseQty(std::uint32_t qty) noexcept { m_baseQty = qty; }

    // ════════════════════════════════════════════════════════
    // 核心计算
    // ════════════════════════════════════════════════════════

    /// @brief 权重 → 目标股数（取整到整手，最低 1 手）
    [[nodiscard]] std::int64_t weightToQty(double weight) const noexcept;

    /// @brief 买入增量：新开仓 → OPEN，加仓 → ADD，不需操作 → deltaQty=0
    [[nodiscard]] OrderDelta buyDelta(std::int64_t currentQty, double targetWeight) const;

    /// @brief 卖出减量：
    ///   targetWeight > 0 → REDUCE/CLOSE（策略信号）
    ///   requestedQty > 0 → 尊重显式数量（规则出场）
    ///   否则 → CLOSE（全卖）
    [[nodiscard]] OrderDelta sellDelta(std::int64_t currentQty, double targetWeight,
                                       std::int64_t requestedQty = 0) const;

    /// @brief 买单总敞口压缩：总权重 > 1.0 时等比缩放所有买单 quantity
    void compressBuys(std::vector<domain::trading::OrderRequest>& orders) const;

private:
    std::uint32_t m_baseQty;
};

} // namespace domain::strategy
