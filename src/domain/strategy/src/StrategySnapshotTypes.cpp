#include "../include/StrategySnapshotTypes.h"

#include <cmath>

namespace domain::strategy {

bool DatasetId::isValid() const
{
    return value >= 0;
}

bool Quantity::isPositive() const
{
    return value > 0;
}

bool Money::isFinite() const
{
    return std::isfinite(value);
}

bool Money::isPositive() const
{
    return isFinite() && value > 0.0;
}

bool Ratio::isValid() const
{
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

bool RebalanceFrequencyDays::isPositive() const
{
    return value > 0;
}

bool StrategyIdentity::isValid() const
{
    return strategyId.isValid()
        && strategyCode.isValid()
        && strategyName.isValid()
        && storedType != domain::backtest::StrategyStoredType::Unknown;
}

bool StrategyLifecycle::isValid() const
{
    return isKnownStrategyLifecycleStatus(status);
}

bool StrategyLifecycle::allowsSignalEmission() const
{
    return status == StrategyLifecycleStatus::Active
        || status == StrategyLifecycleStatus::Testing;
}

bool StrategyRuntimeProfile::hasAny() const
{
    return assetTypeIndex > 0 || timeFrameIndex > 0 || riskLevelIndex > 0;
}

bool RuleTemplateBinding::isValid() const
{
    return templateId.isValid() || filePath.isValid();
}

bool RuleComposerRule::isValid() const
{
    return binding.isValid();
}

bool RuleComposerGroup::isValid() const
{
    return !rules.empty();
}

bool RuleComposerStage::isValid() const
{
    return !groups.empty();
}

bool RuleComposerState::isEmpty() const
{
    return stages.empty();
}

bool FactorOverlayAllocation::isValid() const
{
    return factorId.isValid() && std::isfinite(weightPercent) && weightPercent > 0.0;
}

bool RuleProfileSnapshot::isValid() const
{
    return maxPositionRatio.isValid()
        && maxTotalExposureRatio.isValid()
        && stopLossRatio.isValid()
        && takeProfitRatio.isValid();
}

bool ExecutionPolicySnapshot::isValid() const
{
    return rebalanceFrequencyDays.isPositive();
}

bool UniverseSpec::isValid() const
{
    switch (universeMode) {
    case UniverseMode::ExplicitSymbols:
        return !explicitSymbols.empty();
    case UniverseMode::SavedUniverse:
    case UniverseMode::LinkedWatchlist:
    case UniverseMode::IndexConstituents:
        return sourceId.isValid() || !resolvedSymbols.empty();
    default:
        return false;
    }
}

bool FactorOverlaySpec::isValid() const
{
    if (!enabled) {
        return true;
    }

    if (selectedFactors.empty()) {
        return false;
    }

    for (const FactorOverlayAllocation& allocation : allocations) {
        if (!allocation.isValid()) {
            return false;
        }
    }

    return targetPositionCount > 0 && std::isfinite(minimumCompositeScore);
}

bool StrategyScopeContextSnapshot::isValid() const
{
    return universe.isValid();
}

bool StrategySpec::isValid() const
{
    return ruleProfile.isValid()
        && executionPolicy.isValid()
        && strategyScopeContext.isValid()
        && factorOverlay.isValid();
}

bool TimeSeriesSnapshot::isValid() const
{
    return dates.size() == portfolioValues.size()
        && dates.size() == returns.size()
        && dates.size() == drawdowns.size();
}

} // namespace domain::strategy