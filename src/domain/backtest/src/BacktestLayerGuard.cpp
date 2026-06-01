#include "BacktestLayerGuard.h"

#include <algorithm>
#include <cmath>

namespace domain::backtest {

BacktestLayerGuardResult StrictBacktestLayerGuard::validate(const BacktestRequest& request) const
{
    BacktestLayerGuardResult result;
    if (!request.isValid()) {
        result.violations.push_back(BacktestLayerViolationCode::InvalidRequest);
        return result;
    }

    const bool strategyOverlayEnabled = request.strategySpec.factorOverlay.enabled;
    const bool sessionOverlayEnabled = request.factorOverlaySpec.enabled;
    if (strategyOverlayEnabled != sessionOverlayEnabled) {
        result.violations.push_back(BacktestLayerViolationCode::OverlayEnableFlagMismatch);
    }

    const int strategyRebalanceDays = request.strategySpec.ruleProfile.rebalanceDays;
    const int strategyPolicyRebalanceDays = request.strategySpec.executionPolicy.rebalanceFrequencyDays.value;
    if (strategyRebalanceDays > 0 && strategyPolicyRebalanceDays != strategyRebalanceDays) {
        result.violations.push_back(BacktestLayerViolationCode::StrategyExecutionPolicyRebalanceDaysMismatch);
    }

    if (strategyRebalanceDays > 0 && request.executionSpec.rebalanceFrequencyDays != strategyRebalanceDays) {
        result.violations.push_back(BacktestLayerViolationCode::RebalanceDaysMustComeFromStrategyDefinition);
    }

    if (request.executionSpec.positionSizingMethod != request.strategySpec.executionPolicy.positionSizingMethod) {
        result.violations.push_back(BacktestLayerViolationCode::ExecutionPositionSizingMethodMismatch);
    }

    const bool strategyAllowsShort =
        request.strategySpec.executionPolicy.shortSellingMode == strategy::ShortSellingMode::Enabled;
    if (request.executionSpec.enableShortSelling != strategyAllowsShort) {
        result.violations.push_back(BacktestLayerViolationCode::ExecutionShortSellingModeMismatch);
    }

    if (!nearEqual(request.riskSpec.stopLossRate.value,
                   request.strategySpec.ruleProfile.stopLossRatio.value,
                   kRatioTolerance)) {
        result.violations.push_back(BacktestLayerViolationCode::RiskStopLossMustAlignRuleProfile);
    }

    if (!nearEqual(request.riskSpec.maxPositionRatio.value,
                   request.strategySpec.ruleProfile.maxPositionRatio.value,
                   kRatioTolerance)) {
        result.violations.push_back(BacktestLayerViolationCode::RiskMaxPositionMustAlignRuleProfile);
    }

    if (!strategyOverlayEnabled) {
        return result;
    }

    if (request.factorOverlaySpec.targetPositionCount != request.strategySpec.factorOverlay.targetPositionCount) {
        result.violations.push_back(BacktestLayerViolationCode::OverlayTargetPositionCountMismatch);
    }

    if (!nearEqual(request.factorOverlaySpec.minimumCompositeScore,
                   request.strategySpec.factorOverlay.minimumCompositeScore,
                   kMinimumCompositeScoreTolerance)) {
        result.violations.push_back(BacktestLayerViolationCode::OverlayMinimumCompositeScoreMismatch);
    }

    auto requestSelectedFactors = request.factorOverlaySpec.selectedFactors;
    auto strategySelectedFactors = request.strategySpec.factorOverlay.selectedFactors;
    if (requestSelectedFactors.size() != strategySelectedFactors.size()) {
        result.violations.push_back(BacktestLayerViolationCode::OverlaySelectedFactorsMismatch);
    } else {
        const auto lessByFactorId = [](const strategy::FactorId& left, const strategy::FactorId& right) {
            return left.text() < right.text();
        };
        std::sort(requestSelectedFactors.begin(), requestSelectedFactors.end(), lessByFactorId);
        std::sort(strategySelectedFactors.begin(), strategySelectedFactors.end(), lessByFactorId);
        if (requestSelectedFactors != strategySelectedFactors) {
            result.violations.push_back(BacktestLayerViolationCode::OverlaySelectedFactorsMismatch);
        }
    }

    auto requestAllocations = request.factorOverlaySpec.allocations;
    auto strategyAllocations = request.strategySpec.factorOverlay.allocations;
    if (requestAllocations.size() != strategyAllocations.size()) {
        result.violations.push_back(BacktestLayerViolationCode::OverlayAllocationsMismatch);
    } else {
        const auto lessByFactorId = [](const strategy::FactorOverlayAllocation& left,
                                       const strategy::FactorOverlayAllocation& right) {
            return left.factorId.text() < right.factorId.text();
        };
        std::sort(requestAllocations.begin(), requestAllocations.end(), lessByFactorId);
        std::sort(strategyAllocations.begin(), strategyAllocations.end(), lessByFactorId);

        for (int index = 0; index < requestAllocations.size(); ++index) {
            const auto& requestAllocation = requestAllocations.at(index);
            const auto& strategyAllocation = strategyAllocations.at(index);
            if (requestAllocation.factorId != strategyAllocation.factorId
                || !nearEqual(requestAllocation.weightPercent,
                              strategyAllocation.weightPercent,
                              kAllocationWeightTolerance)) {
                result.violations.push_back(BacktestLayerViolationCode::OverlayAllocationsMismatch);
                break;
            }
        }
    }

    return result;
}

bool StrictBacktestLayerGuard::nearEqual(double left, double right, double tolerance)
{
    return std::fabs(left - right) <= tolerance;
}

} // namespace domain::backtest
