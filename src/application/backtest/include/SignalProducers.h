#pragma once

#include "BacktestInterfaces.hpp"

#include "../../../domain/factor/include/factor_compute/IFactorComputeEngine.h"
#include "../../../domain/factor/include/factor_compute/ISignalEngine.h"

#include <optional>
#include <variant>

namespace domain::strategy {
class IStrategyService;
}

namespace application::backtest {

/// @brief P2-T2：统一后半链路 - 因子信号生产器适配器
///
/// 将因子服务输出适配为统一 ISignalProducer 接口，接入统一后半链：
/// SignalSet → 构仓 → 风控 → 构单 → 成交 → 状态更新 → 指标聚合 → 诊断
///
/// 支持两种因子引擎：
/// - IFactorComputeEngine（旧引擎，向后兼容）
/// - ISignalEngine（新门面接口）
using FactorEngineVariant = std::variant<
    factor::compute::IFactorComputeEngine*,
    factor::compute::ISignalEngine*>;

class FactorSignalProducerAdapter final : public ISignalProducer {
public:
    /// @brief 从 IFactorComputeEngine 构造（旧引擎兼容）
    explicit FactorSignalProducerAdapter(factor::compute::IFactorComputeEngine& factorComputeEngine);

    /// @brief 从 ISignalEngine 构造（新门面接口）
    explicit FactorSignalProducerAdapter(factor::compute::ISignalEngine& signalEngine);

    [[nodiscard]] StageResult generateSignal(RunContext& context) const override;

private:
    [[nodiscard]] std::optional<factor::compute::GenerateSpec>
    buildGenerateSpec(const RunContext& context) const;

    /// @brief 通过 ISignalEngine 生成信号（新路径）
    [[nodiscard]] StageResult generateViaSignalEngine(RunContext& context) const;

    /// @brief 通过 IFactorComputeEngine 生成信号（旧路径，向后兼容）
    [[nodiscard]] StageResult generateViaComputeEngine(RunContext& context) const;

    static constexpr int32_t kMinimumInstrumentUniverseSize = 1;
    static constexpr int32_t kMinimumRequestedFactorCount = 1;
    static constexpr std::uint32_t kDateChunkSize = 64U;
    static constexpr std::uint32_t kInstrumentChunkSize = 1024U;
    static constexpr double kWinsorizeStdBand = 3.0;
    static constexpr double kStdEpsilon = 1e-12;
    static constexpr int32_t kMinimumValidSampleCount = 2;

    FactorEngineVariant engine_;
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
