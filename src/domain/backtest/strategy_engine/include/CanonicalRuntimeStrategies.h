#pragma once

#include "StrategyBacktestEngineInterfaces.h"

namespace domain::backtest::strategy_engine {

class CanonicalLayerSelectionStrategy final : public ILayerSelectionStrategy {
public:
    [[nodiscard]] LayerSelectionResult select(const DecisionLayer& decisionLayer,
                                              const StrategyContext& context,
                                              const MarketDataSlice& marketData) const override;
};

class CanonicalRuleChecker final : public IRuleChecker {
public:
    [[nodiscard]] RuleCheckResult checkRules(const StrategyContext& context,
                                             SymbolId symbolId,
                                             DecisionType decisionType) const override;
};

class CanonicalExecutionSimulator final : public IExecutionSimulator {
public:
    [[nodiscard]] ExecutionFill execute(const ExecutionOrder& order,
                                        const MarketDataSlice& marketData) const override;
};

class CanonicalPortfolioOptimizer final : public IPortfolioOptimizer {
public:
    [[nodiscard]] TargetWeightList computeWeights(const SymbolIdList& candidateSymbols,
                                                  const CandidateScoreList& candidateScores,
                                                  const PortfolioState& portfolioState,
                                                  const RiskSpec& riskSpec) const override;
};

class CanonicalExecutionPolicyStrategy final : public IExecutionPolicyStrategy {
public:
    [[nodiscard]] ExecutionPolicyDecision evaluate(const ExecutionSpec& executionSpec,
                                                   const StrategyContext& context) const override;

    [[nodiscard]] ExecutionOrderList buildOrders(const TargetWeightList& targetWeights,
                                                 const PortfolioState& portfolioState,
                                                 const MarketDataSlice& marketData,
                                                 const ExecutionPolicyDecision& decision) const override;
};

} // namespace domain::backtest::strategy_engine