#pragma once

#include "BacktestRuleTemplateEvaluator.h"
#include "Bar.h"

#include <foundation.h>

#include <QString>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace domain::backtest {
class FactorDataProvider;
}

namespace domain::backtest::runtime {

enum class TradingSignal {
    Hold,
    Buy,
    Sell
};

struct PositionState {
    double quantity{0.0};
    double entryPrice{0.0};
    foundation::Timestamp entryTime;

    bool hasPosition() const {
        return quantity > 0.0 && entryPrice > 0.0;
    }
};

struct SymbolRuntimeState {
    std::vector<double> closes;
    PositionState position;
};

struct BacktestRuntimeState {
    double cash{0.0};
    std::unordered_map<std::string, SymbolRuntimeState> symbolStates;
    std::unordered_map<std::string, double> latestPrices;
    std::unordered_map<std::string, foundation::Timestamp> latestTimestamps;
};

struct FactorOverlayAllocation {
    std::string factorId;
    double weight{0.0};
};

struct FactorOverlayRuntimeSupport {
    bool enabled{false};
    int targetPositionCount{0};
    double minimumCompositeScore{0.0};
    std::vector<FactorOverlayAllocation> allocations;
    std::map<std::string, std::map<std::string, std::map<std::string, double>>> factorSeriesByFactor;

    bool active() const {
        return enabled && !allocations.empty() && !factorSeriesByFactor.empty();
    }
};

struct PendingBuyCandidate {
    domain::model::Bar bar;
    foundation::Timestamp timestamp;
    domain::backtest::rules::RuntimeRuleTemplateEvaluationResult templateEntryResult;
    double ruleSelectionScore{0.0};
    double factorCompositeScore{0.0};
    double combinedSelectionScore{0.0};
    std::size_t orderIndex{0};
};

struct StrategyProfile {
    std::string subtype;
    double positionSizeRatio{1.0};
    int fastPeriod{10};
    int slowPeriod{30};
    int bollPeriod{20};
    double bollStd{2.0};
    double reversionThreshold{0.5};
    int momentumPeriod{60};
    double spreadThreshold{0.02};
    double entryZScore{2.0};
    double exitZScore{0.5};
    bool autoStopEnabled{true};
    double stopLossRate{0.05};
    double takeProfitRate{0.15};
};

QString backtestDateKey(long long epochMs);

double resultSelectionScore(const domain::backtest::rules::RuntimeRuleTemplateEvaluationResult& result);

int openPositionCount(const BacktestRuntimeState& state);

FactorOverlayRuntimeSupport buildFactorOverlayRuntimeSupport(
    const std::map<std::string, std::string>& strategyOptions,
    const std::vector<domain::model::Bar>& bars,
    const std::shared_ptr<domain::backtest::FactorDataProvider>& factorDataProvider);

StrategyProfile buildStrategyProfile(
    const std::string& strategyName,
    double maxPositionRatio,
    const std::map<std::string, double>& strategyParams,
    const std::map<std::string, std::string>& strategyOptions);

TradingSignal evaluateSignal(
    const StrategyProfile& profile,
    const std::vector<double>& closes,
    const PositionState& position);

double calculatePortfolioEquity(const BacktestRuntimeState& state);

void applyFactorOverlayScores(
    const FactorOverlayRuntimeSupport& support,
    std::vector<PendingBuyCandidate>& candidates);

int resolveAvailablePendingBuySlots(
    const FactorOverlayRuntimeSupport& support,
    const BacktestRuntimeState& state,
    int pendingCandidateCount);

} // namespace domain::backtest::runtime