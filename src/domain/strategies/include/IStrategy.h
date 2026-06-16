#pragma once

#include "StrategyDefinitionTypes.h"

namespace domain::strategies {

class IStrategy {
public:
    IStrategy(const StrategyCommonConfig& commonConfig, const StrategyMetadata& metadata);

    virtual ~IStrategy();

    virtual StrategyType strategyType() const = 0;

    virtual bool isConfigured() const noexcept;

    const StrategyCommonConfig& commonConfig() const noexcept;

    [[nodiscard]] bool allowsShort() const noexcept;

    [[nodiscard]] bool hasPositionCapacity(int openPositionCount) const noexcept;

    [[nodiscard]] int maxPositions() const noexcept;

    [[nodiscard]] bool hasValidPositionLimit() const noexcept;

    [[nodiscard]] double maxWeightPerStock() const noexcept;

    [[nodiscard]] double minWeightPerStock() const noexcept;

    [[nodiscard]] bool hasValidWeightRange() const noexcept;

    [[nodiscard]] WeightScheme weightScheme() const noexcept;

    [[nodiscard]] RebalanceFrequency rebalanceFrequency() const noexcept;

    [[nodiscard]] bool hasValidRebalanceFrequency() const noexcept;

    [[nodiscard]] bool hasValidCommonConfig() const noexcept;

    [[nodiscard]] int rebalanceStepInterval() const noexcept;

    [[nodiscard]] bool shouldRebalanceOnStep(int observedStepCount) const noexcept;

    [[nodiscard]] const std::string& strategyName() const noexcept;

    [[nodiscard]] const std::string& strategyDescription() const noexcept;

    [[nodiscard]] const StrategyUuid& strategyUuid() const noexcept;

    [[nodiscard]] const StrategyMetadata& metadata() const noexcept;

    [[nodiscard]] const std::vector<std::string>& factorIds() const noexcept;

    [[nodiscard]] const std::vector<RuleId>& ruleIds() const noexcept;

    [[nodiscard]] bool isEnabled() const noexcept;

    void setMetadata(const StrategyMetadata& metadata);

    virtual void reset();

private:
    StrategyCommonConfig commonConfig_;
    StrategyMetadata metadata_;
};

} // namespace domain::strategies
