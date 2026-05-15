#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class MacroFactor final : public ConfigurableFactorBase {
public:
    MacroFactor();

    CalculationResult calculate(const CalculationContext& context) override;

    static std::shared_ptr<MacroFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);
};

} // namespace factor