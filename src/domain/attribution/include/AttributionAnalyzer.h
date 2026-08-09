// AttributionAnalyzer.h — 绩效归因分析器
// 领域层纯算法，零 Qt 依赖，外部数据通过 std::function 回调注入
#pragma once

#include "AttributionTypes.h"

#include "../../strategy/include/StrategySnapshotTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace domain::attribution {

/// @brief 绩效归因分析器
///
/// 从 StrategyBacktestResult 计算三维归因:
///   1. 板块归因: 按行业分组 tradeLog 的 realizedPnl
///   2. 因子归因: 基于因子覆盖 + 权重 + rankIC 估算贡献
///   3. 择时归因: Brinson 分解 (Phase 1 简化 → Phase 2 完整)
///
/// 外部数据注入模式 (参考 IOptimizer::TrialEvaluator):
///   - sectorLookup: symbol → 行业名称 (Bridge 层查 ref.symbol_info)
///   - factorWeightLookup: factorId → 权重 (Bridge 层读策略模板配置)
///   - benchmarkLookup: → 基准行业权重/收益率 (Phase 2)
class AttributionAnalyzer final {
public:
    /// @brief 符号→行业 查询回调
    /// 实现要求: 调用方应在 Bridge 层做内存缓存 (std::unordered_map),
    /// 避免对同一 symbol 重复查询 PG
    using SectorLookupFn = std::function<std::string(const std::string& fullSymbol)>;

    /// @brief 因子权重查询回调 (Bridge 层注入, 如读策略模板 FactorOverlayAllocation)
    /// 缺失因子返回 0.0
    using FactorWeightLookupFn = std::function<double(const std::string& factorId)>;

    /// @brief 基准行业数据查询回调 (Phase 2 实现, 可选)
    /// 若返回 empty vector, Brinson 使用简化模式 (仅计算超额收益)
    using BenchmarkLookupFn = std::function<std::vector<BenchmarkSectorDatum>()>;

    struct Config {
        SectorLookupFn sectorLookup;              // 必填: symbol → sector name
        FactorWeightLookupFn factorWeightLookup;   // 可选: factorId → weight (缺失返回 0.0)
        BenchmarkLookupFn benchmarkLookup;        // 可选: → benchmark sector data (Phase 2)
    };

    explicit AttributionAnalyzer(Config config);
    ~AttributionAnalyzer() = default;

    /// @brief 主入口: 从回测结果计算全部归因
    /// @param result StrategyEngine::backtest() 返回的完整结果
    /// @return 归因报告 (isValid=true 表示足够数据可计算)
    [[nodiscard]] AttributionReport analyze(
        const domain::strategy::StrategyBacktestResult& result) const;

private:
    Config m_config;

    /// 板块归因: 按行业分组 tradeLog 的 realizedPnl
    [[nodiscard]] std::vector<SectorAttribution> computeSectorAttribution(
        const std::vector<domain::strategy::BacktestTradeRecord>& tradeLog,
        double totalPnl) const;

    /// 因子归因: 基于因子覆盖 + 权重 + rankIC 估算贡献
    [[nodiscard]] std::vector<FactorAttribution> computeFactorAttribution(
        const std::vector<domain::strategy::HybridFactorCoverage>& factorCoverage,
        double rankIC,
        int totalDays) const;

    /// Brinson 择时归因: 对标基准做配置/选股/交互拆解
    /// Phase 1: benchmarkLookup 为空 → isSimplified=true, 仅 excessReturn
    [[nodiscard]] TimingAttribution computeTimingAttribution(
        const std::vector<SectorAttribution>& sectorData,
        double portfolioReturn,
        double benchmarkReturn) const;
};

} // namespace domain::attribution
