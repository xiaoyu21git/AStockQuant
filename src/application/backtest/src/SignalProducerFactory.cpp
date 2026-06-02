#include "SignalProducerFactory.hpp"

namespace application::backtest {

SignalProducerFactoryResult SignalProducerFactory::create(const ExistingModuleSlots& slots)
{
    SignalProducerFactoryResult result;

    // 优先使用新 ISignalEngine 路径（性能版：集成线程池+预算闸门+池化）
    if (slots.signalEngine != nullptr) {
        result.factorSignalProducer =
            std::make_unique<FactorSignalProducerAdapter>(*slots.signalEngine);
    } else if (slots.factorComputeEngine != nullptr) {
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
