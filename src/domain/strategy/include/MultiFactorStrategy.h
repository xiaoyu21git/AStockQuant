#pragma once
// MultiFactorStrategy — 标准量化多因子选股策略
//
// 流水线: 因子快照 → 截面Z-score归一化 → 加权合成评分 → 截面排名 → Top-N选股 → 信号生成
//
// 职责边界:
//   - 纯策略逻辑，不持有因子计算职责（因子值由 RuntimeFactorSvc::copySnapshots 注入）
//   - 零 Qt 依赖，纯 C++17
//   - 直接实现 IRuntimeStrategy，无中间适配层

#include "IStrategyService.h"
#include "StrategyServiceTypes.h"
#include "../../factor/include/factor_compute/IMarketDataView.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace domain::strategy {

/// @brief 多因子策略不可变配置 — 构造后只读
struct MultiFactorConfig final {
    /// 因子实例 ID 列表（与 weights 的 key 对应）
    std::vector<std::string> factorIds;

    /// factorId → 归一化权重（总和 = 1.0），构造时由调用方归一化
    std::unordered_map<std::string, double> weights;

    /// 最大持仓数 — 硬上限，超出时按信号强度择优保留
    int maxPositions{30};

    /// 买入候选数量上限（每轮调仓最多新买入的标的数）
    int topN{50};

    /// 最低合成评分阈值 — 低于此值的标的即使排名靠前也不买入
    double minCompositeScore{0.0};

    /// 持仓卖出阈值 — 已持仓标的合成评分低于此值时生成卖出信号
    double sellThreshold{0.2};

    /// 是否启用行业中性化（截面 Z-score 后减去行业内均值）
    bool industryNeutral{false};

    /// 组合权重分配方案
    ::domain::strategies::WeightScheme weightScheme{
        ::domain::strategies::WeightScheme::EQUAL};

    /// 单标的最大权重
    double maxWeightPerStock{0.10};

    /// 单标的最小权重
    double minWeightPerStock{0.0};

    /// 排名驱动的卖出倍率: 持仓排名 > maxPositions * sellRankMultiplier 时强制卖出
    /// 默认 2.0 = 跌出 2×maxPositions 即卖。调大可容忍更多持仓，调小换手更频繁
    double sellRankMultiplier{2.0};

    /// 跳过截面 Z-score 归一化的因子 ID 集合（如 SupplyChain 同组同值因子）
    std::unordered_set<std::string> skipNormalizeFactorIds;

    [[nodiscard]] bool isValid() const noexcept
    {
        return maxPositions > 0 && topN > 0 && !factorIds.empty() && !weights.empty();
    }
};

/// @brief 标准多因子量化选股策略
///
/// 实现 IRuntimeStrategy，直接消费因子快照产出交易信号。
/// 与 StrategyBase（非因子技术指标策略）并列，均直接实现 IRuntimeStrategy。
///
/// 核心流水线:
///   1. normalizeCrossSectional  — 按因子截面 Z-score 归一化
///   2. computeCompositeScores   — 加权合成评分
///   3. emitExitSignals          — 因子分跌破阈值的持仓 → 卖出
///   4. rankAndSelect            — 降序排名 + Top-N 截断
///   5. allocateWeights          — 按配置方案分配组合权重
///   6. emitBuySignals           — 买入信号输出
class MultiFactorStrategy final : public IRuntimeStrategy {
public:
    MultiFactorStrategy(StrategyInstanceId instanceId, MultiFactorConfig config);

    // ── IRuntimeStrategy 接口 ──

    [[nodiscard]] StrategyInstanceId instanceId() const noexcept override;
    [[nodiscard]] bool isEnabled() const noexcept override;
    [[nodiscard]] rules::RuleSetId ruleSetId() const noexcept override;

    /// @brief 多因子策略依赖因子计算 — 始终返回 true
    [[nodiscard]] bool usesFactors() const noexcept override;

    /// @brief 消费因子快照，产出 StrategySignal 列表
    void evaluate(const std::vector<RuntimeFactorSnapshot>& factorSnapshots,
                  const RuntimeStrategyContext& context,
                  std::vector<StrategySignal>& outputSignals) override;

private:
    // ── 内部统计类型 ──

    /// 单因子截面统计量（用于 Z-score 归一化）
    struct FactorStats final {
        double sum{0.0};
        double sumSquares{0.0};
        std::size_t count{0};
    };

    /// 单标的的多因子 Z-score 快照
    struct SymbolFactorScore final {
        std::uint32_t symbolId{0};
        /// factorId → zScore
        std::unordered_map<std::string, double> factorZScores;
    };

    /// 标的合成评分记录
    struct CompositeRecord final {
        std::uint32_t symbolId{0};
        double compositeScore{0.0};
    };

    // ── 流水线阶段 ──

    /// @brief 阶段1: 截面 Z-score 归一化
    /// 对每个因子跨全市场标的计算均值/标准差，产出 (symbolId → {factorId: zScore})
    [[nodiscard]] std::vector<SymbolFactorScore> normalizeCrossSectional(
        const std::vector<RuntimeFactorSnapshot>& snapshots) const;

    /// @brief 阶段2: 加权合成评分
    /// compositeScore = Σ(weight_i × zScore_i)，缺因子数据的标的跳过
    [[nodiscard]] std::vector<CompositeRecord> computeCompositeScores(
        const std::vector<SymbolFactorScore>& normalized) const;

    /// @brief 阶段3: 持仓退出信号（因子分跌破阈值 或 排名跌出 topN*sellRankMultiplier）
    void emitExitSignals(
        const std::vector<CompositeRecord>& allScores,
        const std::unordered_map<std::uint32_t, int>& rankById,
        const RuntimeStrategyContext& context,
        const factor::compute::IMarketDataView* view,
        std::vector<StrategySignal>& outputSignals) const;

    /// @brief 阶段4: 降序排名 + minCompositeScore 过滤 + Top-N 截断
    [[nodiscard]] std::vector<CompositeRecord> rankAndSelect(
        std::vector<CompositeRecord>& scores) const;

    /// @brief 阶段5: 按配置方案分配组合权重
    /// 支持 Equal / SignalStrength / MarketCap / RiskParity
    [[nodiscard]] std::vector<double> allocateWeights(
        const std::vector<CompositeRecord>& selected,
        const RuntimeStrategyContext& context,
        const factor::compute::IMarketDataView* view) const;

    /// @brief 阶段6: 买入信号输出
    void emitBuySignals(
        const std::vector<CompositeRecord>& selected,
        const std::vector<double>& weights,
        const std::unordered_map<std::uint32_t, const std::string*>& idToFullSymbol,
        const RuntimeStrategyContext& context,
        std::vector<StrategySignal>& outputSignals) const;

    // ── 辅助方法 ──

    /// @brief 从上下文解析行情视图（用于 symbol 映射和权重方案数据）
    [[nodiscard]] static const factor::compute::IMarketDataView* resolveMarketView(
        const RuntimeStrategyContext& context) noexcept;

    /// @brief 构建 instrumentId → fullSymbol 映射（从行情视图）
    [[nodiscard]] static std::unordered_map<std::uint32_t, const std::string*>
    buildIdToSymbolMap(const factor::compute::IMarketDataView* view);

    /// @brief 计算日波动率（用于 RiskParity 权重方案）
    [[nodiscard]] static double computeDailyVolatility(
        const factor::compute::NumericConstMatrixView& closeMat,
        int lastRow, int col, int lookbackBars);

    // ── 数据成员 ──

    StrategyInstanceId m_instanceId{0};
    MultiFactorConfig m_config;
};

} // namespace domain::strategy
