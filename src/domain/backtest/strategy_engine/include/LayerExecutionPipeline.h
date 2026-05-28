#pragma once

#include "StrategyBacktestEngineInterfaces.h"

namespace domain::backtest::strategy_engine {

class LayerExecutionPipeline final {
public:
    LayerExecutionPipeline(const ILayerSelectionStrategy& layerSelectionStrategy,
                           const IRuleChecker& ruleChecker,
                           const IPortfolioOptimizer& portfolioOptimizer,
                           const IExecutionPolicyStrategy& executionPolicyStrategy,
                           const IExecutionSimulator& executionSimulator);

    [[nodiscard]] LayerExecutionState execute(const DecisionLayer& decisionLayer,
                                              const StrategyContext& context,
                                              const MarketDataSlice& marketData) const;

private:
    const ILayerSelectionStrategy& layerSelectionStrategy_;
    const IRuleChecker& ruleChecker_;
    const IPortfolioOptimizer& portfolioOptimizer_;
    const IExecutionPolicyStrategy& executionPolicyStrategy_;
    const IExecutionSimulator& executionSimulator_;
};

} // namespace domain::backtest::strategy_engine