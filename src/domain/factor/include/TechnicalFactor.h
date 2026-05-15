#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class TechnicalFactor final : public ConfigurableFactorBase {
public:
    TechnicalFactor();

    CalculationResult calculate(const CalculationContext& context) override;

    static std::shared_ptr<TechnicalFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);
};

} // namespace factor