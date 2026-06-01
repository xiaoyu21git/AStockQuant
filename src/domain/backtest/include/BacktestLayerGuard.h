#pragma once

#include "BacktestRequest.h"

#include <QVector>

namespace domain::backtest {

enum class BacktestLayerViolationCode {
    None,
    InvalidRequest,
    OverlayEnableFlagMismatch,
    RebalanceDaysMustComeFromStrategyDefinition,
    StrategyExecutionPolicyRebalanceDaysMismatch,
    ExecutionPositionSizingMethodMismatch,
    ExecutionShortSellingModeMismatch,
    OverlayTargetPositionCountMismatch,
    OverlayMinimumCompositeScoreMismatch,
    OverlaySelectedFactorsMismatch,
    OverlayAllocationsMismatch,
    RiskStopLossMustAlignRuleProfile,
    RiskMaxPositionMustAlignRuleProfile
};

struct BacktestLayerGuardResult final {
    QVector<BacktestLayerViolationCode> violations;

    [[nodiscard]] bool ok() const
    {
        return violations.isEmpty();
    }
};

class IBacktestLayerGuard {
public:
    virtual ~IBacktestLayerGuard() = default;

    virtual BacktestLayerGuardResult validate(const BacktestRequest& request) const = 0;
};

class StrictBacktestLayerGuard final : public IBacktestLayerGuard {
public:
    static constexpr double kMinimumCompositeScoreTolerance = 1e-9;
    static constexpr double kAllocationWeightTolerance = 1e-9;
    static constexpr double kRatioTolerance = 1e-9;

    BacktestLayerGuardResult validate(const BacktestRequest& request) const override;

private:
    [[nodiscard]] static bool nearEqual(double left, double right, double tolerance);
};

} // namespace domain::backtest
