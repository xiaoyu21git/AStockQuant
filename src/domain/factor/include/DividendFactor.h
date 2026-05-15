#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class DividendFactor final : public ConfigurableFactorBase {
public:
    DividendFactor();

    CalculationResult calculate(const CalculationContext& context) override;

    static std::shared_ptr<DividendFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);
};

} // namespace factor