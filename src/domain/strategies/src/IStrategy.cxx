#include "IStrategy.h"

namespace domain::strategies {

IStrategy::IStrategy(const StrategyCommonConfig& commonConfig, const StrategyMetadata& metadata)
    : commonConfig_(commonConfig)
    , metadata_(metadata)
{
}

IStrategy::~IStrategy() = default;

bool IStrategy::isConfigured() const noexcept
{
    return hasValidCommonConfig();
}

const StrategyCommonConfig& IStrategy::commonConfig() const noexcept
{
    return commonConfig_;
}

bool IStrategy::allowsShort() const noexcept
{
    return commonConfig_.allowShort;
}

bool IStrategy::hasPositionCapacity(int openPositionCount) const noexcept
{
    return commonConfig_.maxPositions > 0 && openPositionCount < commonConfig_.maxPositions;
}

int IStrategy::maxPositions() const noexcept
{
    return commonConfig_.maxPositions;
}

bool IStrategy::hasValidPositionLimit() const noexcept
{
    return commonConfig_.maxPositions > 0;
}

double IStrategy::maxWeightPerStock() const noexcept
{
    return commonConfig_.maxWeightPerStock;
}

double IStrategy::minWeightPerStock() const noexcept
{
    return commonConfig_.minWeightPerStock;
}

bool IStrategy::hasValidWeightRange() const noexcept
{
    return commonConfig_.minWeightPerStock >= 0.0
        && commonConfig_.maxWeightPerStock >= commonConfig_.minWeightPerStock
        && commonConfig_.maxWeightPerStock <= 1.0;
}

WeightScheme IStrategy::weightScheme() const noexcept
{
    return commonConfig_.weightScheme;
}

RebalanceFrequency IStrategy::rebalanceFrequency() const noexcept
{
    return commonConfig_.rebalanceFrequency;
}

bool IStrategy::hasValidRebalanceFrequency() const noexcept
{
    return isValidRebalanceFrequency(commonConfig_.rebalanceFrequency);
}

bool IStrategy::hasValidCommonConfig() const noexcept
{
    return hasValidPositionLimit()
        && hasValidWeightRange()
        && hasValidRebalanceFrequency();
}

int IStrategy::rebalanceStepInterval() const noexcept
{
    return rebalanceFrequencyStepInterval(commonConfig_.rebalanceFrequency);
}

bool IStrategy::shouldRebalanceOnStep(int observedStepCount) const noexcept
{
    const int interval = rebalanceStepInterval();
    return interval > 0 && observedStepCount > 0 && observedStepCount % interval == 0;
}

const std::string& IStrategy::strategyName() const noexcept
{
    return metadata_.name;
}

const std::string& IStrategy::strategyDescription() const noexcept
{
    return metadata_.description;
}

const StrategyUuid& IStrategy::strategyUuid() const noexcept
{
    return metadata_.uuid;
}

const StrategyMetadata& IStrategy::metadata() const noexcept
{
    return metadata_;
}

const std::vector<FactorId>& IStrategy::factorIds() const noexcept
{
    return metadata_.factorIds;
}

const std::vector<RuleId>& IStrategy::ruleIds() const noexcept
{
    return metadata_.ruleIds;
}

bool IStrategy::isEnabled() const noexcept
{
    return metadata_.enabled;
}

void IStrategy::setMetadata(const StrategyMetadata& metadata)
{
    metadata_ = metadata;
}

void IStrategy::reset()
{
}

} // namespace domain::strategies