#pragma once
// ══════════════════════════════════════════════════════════════════════════════
// FactorAttributionCalculator — 因子收益法归因计算器 (组合因子模式)
// P0 铁律: 只接收 Orchestrator 预聚合的标量序列, 禁止任何 per-child 全截面输入
// 输入规模 ≈ 子因子数 × 调仓期数 × 8B, <1MB
// ══════════════════════════════════════════════════════════════════════════════

#include "FactorAttributionTypes.h"

#include <string>
#include <vector>

namespace factor {

/// @brief 因子归因计算器 (纯函数式, 领域层零 Qt 依赖)
class FactorAttributionCalculator final {
public:
    /// @brief 单个子因子的预聚合标量序列 (由 Orchestrator 循环内累积)
    struct PerChildSeries final {
        std::string instanceId;
        double weight{0.0};
        bool ascending{true};                // 方向已在多空收益采集时折算, 此处仅透传记录
        std::vector<double> longShortReturns;  // 逐调仓期多空收益 (无成本口径, 与组合 raw 对齐)
        std::vector<double> icSeries;          // 逐期 Rank IC (含非调仓日, 仅统计用)
    };

    /// @brief 归因输入 (全部为预聚合序列)
    struct Inputs final {
        std::vector<PerChildSeries> children;
        std::vector<double> compositeRawReturns;      // rawLongShortReturns (扣费前, 子因子同口径)
        std::vector<double> compositeCostAdjReturns;  // costAdjustedLongShortReturns (扣费后, 锚点)
    };

    /// @brief 计算因子归因报告
    /// 恒等式: totalLongShortReturn = Σ contribution + residualRankingInteraction + residualCosts
    [[nodiscard]] FactorAttributionReport compute(const Inputs& inputs) const;

private:
    static double seriesMean(const std::vector<double>& values);
    static double seriesStd(const std::vector<double>& values, double mean);
};

} // namespace factor
