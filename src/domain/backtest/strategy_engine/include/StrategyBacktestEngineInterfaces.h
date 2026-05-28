#pragma once

#include <functional>

#include "StrategyBacktestAsyncTypes.h"
#include "StrategyBacktestEngineFailure.h"
#include "StrategyBacktestEngineTypes.h"

namespace domain::backtest::strategy_engine {

struct MarketBar final {
    SymbolId symbolId;
    PriceValue openPrice;
    PriceValue highPrice;
    PriceValue lowPrice;
    PriceValue closePrice;
    ShareQuantity volume;
    TradingDayIndex tradingDay;

    [[nodiscard]] bool isValid() const
    {
        return symbolId.isValid()
            && openPrice.isPositive()
            && highPrice.isPositive()
            && lowPrice.isPositive()
            && closePrice.isPositive()
            && volume.isPositive()
            && tradingDay.isValid();
    }
};

using MarketBarList = ObjectList<MarketBar>;

struct FactorSnapshot final {
    FactorId factorId;
    SymbolId symbolId;
    ScoreValue value;
    TradingDayIndex tradingDay;

    [[nodiscard]] bool isValid() const
    {
        return factorId.isValid() && symbolId.isValid() && value.isValid() && tradingDay.isValid();
    }
};

using FactorSnapshotList = ObjectList<FactorSnapshot>;

struct MarketDataSlice final {
    TradingDayIndex tradingDay;
    UniverseId universeId;
    MarketBarList bars;
    FactorSnapshotList factorSnapshots;

    [[nodiscard]] bool isValid() const
    {
        if (!tradingDay.isValid() || !universeId.isValid()) {
            return false;
        }

        for (const MarketBar& bar : bars) {
            if (!bar.isValid()) {
                return false;
            }
        }

        for (const FactorSnapshot& factorSnapshot : factorSnapshots) {
            if (!factorSnapshot.isValid() || factorSnapshot.tradingDay != tradingDay) {
                return false;
            }
        }

        return true;
    }
};

struct LayerSelectionResult final {
    UniverseId outputUniverseId;
    SymbolIdList selectedSymbols;
    CandidateScoreList candidateScores;

    [[nodiscard]] bool isValid() const
    {
        if (!outputUniverseId.isValid()) {
            return false;
        }

        for (const SymbolId symbolId : selectedSymbols) {
            if (!symbolId.isValid()) {
                return false;
            }
        }

        for (const CandidateScore& candidateScore : candidateScores) {
            if (!candidateScore.isValid()) {
                return false;
            }
        }

        return true;
    }
};

struct ExecutionPolicyDecision final {
    bool rebalanceRequired{false};
    bool intradayEnabled{false};
    OrderType orderType{OrderType::MarketOnClose};
    CandidateCount executionBatchCount;
    LayerId layerId;

    [[nodiscard]] bool isValid() const
    {
        return executionBatchCount.isPositive() && layerId.isValid();
    }
};

class IMarketDataCache {
public:
    virtual ~IMarketDataCache() = default;

    [[nodiscard]] virtual MarketDataSlice sliceForDay(OverlayBindingScopeId overlayBindingScopeId,
                                                      TradingDayIndex tradingDay,
                                                      const SymbolIdList& symbols,
                                                      const FactorIdList& factorIds) const = 0;

    [[nodiscard]] virtual CandidateCount warmupDayCount() const = 0;
};

class IRuleChecker {
public:
    virtual ~IRuleChecker() = default;

    [[nodiscard]] virtual RuleCheckResult checkRules(const StrategyContext& context,
                                                     SymbolId symbolId,
                                                     DecisionType decisionType) const = 0;
};

class ILayerSelectionStrategy {
public:
    virtual ~ILayerSelectionStrategy() = default;

    [[nodiscard]] virtual LayerSelectionResult select(const DecisionLayer& decisionLayer,
                                                      const StrategyContext& context,
                                                      const MarketDataSlice& marketData) const = 0;
};

class IExecutionSimulator {
public:
    virtual ~IExecutionSimulator() = default;

    [[nodiscard]] virtual ExecutionFill execute(const ExecutionOrder& order,
                                                const MarketDataSlice& marketData) const = 0;
};

class IPortfolioOptimizer {
public:
    virtual ~IPortfolioOptimizer() = default;

    [[nodiscard]] virtual TargetWeightList computeWeights(const SymbolIdList& candidateSymbols,
                                                          const CandidateScoreList& candidateScores,
                                                          const PortfolioState& portfolioState,
                                                          const RiskSpec& riskSpec) const = 0;
};

class IExecutionPolicyStrategy {
public:
    virtual ~IExecutionPolicyStrategy() = default;

    [[nodiscard]] virtual ExecutionPolicyDecision evaluate(const ExecutionSpec& executionSpec,
                                                           const StrategyContext& context) const = 0;

    [[nodiscard]] virtual ExecutionOrderList buildOrders(const TargetWeightList& targetWeights,
                                                         const PortfolioState& portfolioState,
                                                         const MarketDataSlice& marketData,
                                                         const ExecutionPolicyDecision& decision) const = 0;
};

class IBacktestProgressSink {
public:
    virtual ~IBacktestProgressSink() = default;

    virtual void publish(const BacktestExecutionProgress& progress) = 0;
};

class IBacktestCancellationObserver {
public:
    virtual ~IBacktestCancellationObserver() = default;

    [[nodiscard]] virtual bool isCancellationRequested() const = 0;
};

struct BacktestExecutionCallbacks final {
    std::optional<std::reference_wrapper<IBacktestProgressSink>> progressSink;
    std::optional<std::reference_wrapper<IBacktestCancellationObserver>> cancellationObserver;

    [[nodiscard]] bool hasProgressSink() const
    {
        return progressSink.has_value();
    }

    [[nodiscard]] bool hasCancellationObserver() const
    {
        return cancellationObserver.has_value();
    }
};

class IStrategyBacktestEngine {
public:
    virtual ~IStrategyBacktestEngine() = default;

    [[nodiscard]] BacktestResultDto execute(const BacktestRequest& request) const
    {
        return execute(request, BacktestExecutionCallbacks{});
    }

    [[nodiscard]] virtual BacktestResultDto execute(const BacktestRequest& request,
                                                    const BacktestExecutionCallbacks& callbacks) const = 0;
};

class IAsyncBacktestScheduler {
public:
    virtual ~IAsyncBacktestScheduler() = default;

    [[nodiscard]] virtual AsyncBacktestHandle submit(const BacktestRequest& request) = 0;
    [[nodiscard]] virtual BacktestProgressSnapshot progress(const AsyncBacktestHandle& handle) const = 0;
    [[nodiscard]] virtual std::optional<BacktestResultDto> tryCollect(const AsyncBacktestHandle& handle) = 0;
    virtual void requestCancel(const CancellationRequest& request) = 0;
};

} // namespace domain::backtest::strategy_engine