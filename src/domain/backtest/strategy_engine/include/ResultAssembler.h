#pragma once

#include "StrategyBacktestEngineInterfaces.h"

namespace domain::backtest::strategy_engine {

class ResultAssembler final {
public:
    [[nodiscard]] BacktestResultDto assemble(const BacktestRequest& request,
                                             const BacktestRuntimeSessionState& runtimeState,
                                             const RunMetadata& runMetadata,
                                             const PerformanceSummary& performance,
                                             const TradeStatistics& tradeStatistics,
                                             const RiskMetrics& riskMetrics,
                                             const TimeSeries& timeSeries,
                                             const TradeRecordList& tradeRecords,
                                             const UniverseResolutionSummary& universeResolution,
                                             const RuleTemplateSummary& ruleSummary,
                                             const LayerAttribution& layerAttribution,
                                             const BenchmarkComparison& benchmark,
                                             const Diagnostics& diagnostics) const;
};

} // namespace domain::backtest::strategy_engine