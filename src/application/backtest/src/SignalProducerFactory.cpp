#include "SignalProducerFactory.hpp"

namespace application::backtest {

SignalProducerFactoryResult SignalProducerFactory::create(const ExistingModuleSlots& slots)
{
    SignalProducerFactoryResult result;

    if (slots.factorComputeEngine != nullptr) {
        result.factorSignalProducer =
            std::make_unique<FactorSignalProducerAdapter>(*slots.factorComputeEngine);
    }

    if (slots.strategyService != nullptr) {
        result.strategySignalProducer =
            std::make_unique<StrategySignalProducerAdapter>(*slots.strategyService);
    } else {
        result.strategySignalProducer = std::make_unique<MissingStrategySignalProducerAdapter>();
    }
    return result;
}

} // namespace application::backtest