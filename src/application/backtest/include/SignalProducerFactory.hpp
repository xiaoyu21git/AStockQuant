#pragma once

#include "SignalProducers.h"

#include <memory>

namespace application::backtest {

struct SignalProducerFactoryResult final {
    std::unique_ptr<ISignalProducer> factorSignalProducer;
    std::unique_ptr<ISignalProducer> strategySignalProducer;

    [[nodiscard]] bool hasFactorProducer() const noexcept
    {
        return factorSignalProducer != nullptr;
    }

    [[nodiscard]] bool hasStrategyProducer() const noexcept
    {
        return strategySignalProducer != nullptr;
    }
};

class SignalProducerFactory final {
public:
    [[nodiscard]] static SignalProducerFactoryResult create(const ExistingModuleSlots& slots);
};

} // namespace application::backtest