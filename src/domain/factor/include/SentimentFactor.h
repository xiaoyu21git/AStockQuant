#pragma once

#include "ConfigurableFactor.h"

namespace factor {

class SentimentFactor final : public ConfigurableFactorBase {
public:
    SentimentFactor();

    CalculationResult calculate(const CalculationContext& context) override;

    static std::shared_ptr<SentimentFactor> create(
        const FactorInstanceInfo& info,
        std::shared_ptr<DataAvailabilityChecker> dataChecker);
};

} // namespace factor