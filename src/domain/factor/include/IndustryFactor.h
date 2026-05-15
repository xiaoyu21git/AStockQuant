#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class IndustryFactor final : public ConfigurableFactorBase {
public:
    IndustryFactor();

    CalculationResult calculate(const CalculationContext& context) override;

    static std::shared_ptr<IndustryFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);
};

} // namespace factor