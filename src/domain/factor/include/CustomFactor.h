#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class CustomFactor final : public ConfigurableFactorBase {
public:
    CustomFactor();

    CalculationResult calculate(const CalculationContext& context) override;

    static std::shared_ptr<CustomFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);
};

} // namespace factor