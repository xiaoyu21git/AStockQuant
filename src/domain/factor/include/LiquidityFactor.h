#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class LiquidityFactor final : public ConfigurableFactorBase {
public:
    LiquidityFactor();

    CalculationResult calculate(const CalculationContext& context) override;

    static std::shared_ptr<LiquidityFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);
};

} // namespace factor