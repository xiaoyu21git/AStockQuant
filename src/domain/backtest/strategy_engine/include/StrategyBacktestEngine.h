#pragma once

#include "BacktestRequestValidator.h"
#include "BacktestRuntimeSession.h"
#include "DiagnosticsRecorder.h"
#include "LayerExecutionPipeline.h"
#include "ResultAssembler.h"

namespace domain::backtest::strategy_engine {

class StrategyBacktestEngine final : public IStrategyBacktestEngine {
public:
    StrategyBacktestEngine(const IMarketDataCache& marketDataCache,
                           const ILayerSelectionStrategy& layerSelectionStrategy,
                           const IRuleChecker& ruleChecker,
                           const IExecutionSimulator& executionSimulator,
                           const IPortfolioOptimizer& portfolioOptimizer,
                           const IExecutionPolicyStrategy& executionPolicyStrategy,
                           BacktestRequestValidator requestValidator,
                           ResultAssembler resultAssembler);

    [[nodiscard]] BacktestResultDto execute(const BacktestRequest& request,
                                            const BacktestExecutionCallbacks& callbacks) const override;

private:
    [[nodiscard]] BacktestRuntimeSession createSession(const BacktestRequest& request) const;
    [[nodiscard]] PortfolioState createInitialPortfolioState(const BacktestRequest& request) const;
    [[nodiscard]] RunMetadata buildRunMetadata(const BacktestRuntimeSession& session) const;
    [[nodiscard]] UniverseResolutionSummary buildUniverseResolution(const BacktestRequest& request) const;
    [[nodiscard]] RuleTemplateSummary buildRuleSummary(const BacktestRuntimeSessionState& runtimeState) const;
    [[nodiscard]] LayerAttribution buildLayerAttribution(const BacktestRuntimeSessionState& runtimeState) const;
    [[nodiscard]] BenchmarkComparison buildBenchmarkComparison(const BacktestRequest& request,
                                                               const PerformanceSummary& performanceSummary) const;
    [[nodiscard]] PerformanceSummary buildPerformanceSummary(const BacktestRuntimeSessionState& runtimeState) const;
    [[nodiscard]] TradeStatistics buildTradeStatistics(const TradeRecordList& tradeRecords) const;
    [[nodiscard]] RiskMetrics buildRiskMetrics(const BacktestRuntimeSessionState& runtimeState) const;
    [[nodiscard]] TimeSeries buildTimeSeries(const BacktestRuntimeSessionState& runtimeState) const;
    [[nodiscard]] TradeRecordList buildTradeRecords(const BacktestRuntimeSessionState& runtimeState) const;

    const IMarketDataCache& marketDataCache_;
    const ILayerSelectionStrategy& layerSelectionStrategy_;
    const IRuleChecker& ruleChecker_;
    const IExecutionSimulator& executionSimulator_;
    const IPortfolioOptimizer& portfolioOptimizer_;
    const IExecutionPolicyStrategy& executionPolicyStrategy_;
    BacktestRequestValidator requestValidator_;
    ResultAssembler resultAssembler_;
};

} // namespace domain::backtest::strategy_engine