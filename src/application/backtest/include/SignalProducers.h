#pragma once

#include "BacktestInterfaces.hpp"

#include "../../../domain/factor/include/factor_compute/IFactorComputeEngine.h"

#include <optional>

namespace domain::strategy {
class IStrategyService;
}

namespace application::backtest {

class FactorSignalProducerAdapter final : public ISignalProducer {
public:
    explicit FactorSignalProducerAdapter(factor::compute::IFactorComputeEngine& factorComputeEngine);

    [[nodiscard]] StageResult generateSignal(RunContext& context) const override;

private:
    [[nodiscard]] std::optional<factor::compute::GenerateSpec>
    buildGenerateSpec(const RunContext& context) const;

private:
    static constexpr int32_t kMinimumInstrumentUniverseSize = 1;
    static constexpr int32_t kMinimumRequestedFactorCount = 1;
    static constexpr std::uint32_t kDateChunkSize = 64U;
    static constexpr std::uint32_t kInstrumentChunkSize = 1024U;
    static constexpr double kWinsorizeStdBand = 3.0;
    static constexpr double kStdEpsilon = 1e-12;
    static constexpr int32_t kMinimumValidSampleCount = 2;

    factor::compute::IFactorComputeEngine& factorComputeEngine_;
};

class MissingStrategySignalProducerAdapter final : public ISignalProducer {
public:
    [[nodiscard]] StageResult generateSignal(RunContext& context) const override;
};

class StrategySignalProducerAdapter final : public ISignalProducer {
public:
    explicit StrategySignalProducerAdapter(const domain::strategy::IStrategyService& strategyService);

    [[nodiscard]] StageResult generateSignal(RunContext& context) const override;

private:
    const domain::strategy::IStrategyService& strategyService_;
};

} // namespace application::backtest