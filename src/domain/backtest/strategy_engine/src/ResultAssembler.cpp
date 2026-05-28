#include "ResultAssembler.h"

namespace domain::backtest::strategy_engine {

BacktestResultDto ResultAssembler::assemble(const BacktestRequest& request,
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
                                            const Diagnostics& diagnostics) const
{
    (void)runtimeState;

    BacktestResultDto result;
    result.runMetadata = runMetadata;
    result.configSnapshot = request;
    result.performance = performance;
    result.tradeStatistics = tradeStatistics;
    result.riskMetrics = riskMetrics;
    result.timeSeries = timeSeries;
    result.tradeRecords = tradeRecords;
    result.universeResolution = universeResolution;
    result.ruleSummary = ruleSummary;
    result.layerAttribution = layerAttribution;
    result.benchmark = benchmark;
    result.diagnostics = diagnostics;
    return result;
}

} // namespace domain::backtest::strategy_engine