#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class GrowthFactor final : public ConfigurableFactorBase {
public:
    GrowthFactor();

    CalculationResult calculate(const CalculationContext& context) override;

    static std::shared_ptr<GrowthFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);
};

} // namespace factor