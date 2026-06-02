#include "../include/IStrategySignalEngine.h"

#include <algorithm>

namespace domain::strategy {

/// @brief P3-T1：默认策略信号引擎实现
///
/// 当前为占位实现，实际使用时由具体策略规则引擎注入。
/// 输出符合统一 SignalSet 合同，确保可通过 ISignalProducer 接入后半链。
class DefaultStrategySignalEngine final : public IStrategySignalEngine {
public:
    DefaultStrategySignalEngine() = default;

    [[nodiscard]] factor::compute::FactorResult<factor::compute::SignalSet>
    evaluateBatch(
        factor::compute::DateRange dateRange,
        const std::vector<factor::compute::InstrumentId>& universe) override;
};

factor::compute::FactorResult<factor::compute::SignalSet>
DefaultStrategySignalEngine::evaluateBatch(
    factor::compute::DateRange dateRange,
    const std::vector<factor::compute::InstrumentId>& universe)
{
    if (!dateRange.isValid() || universe.empty()) {
        return factor::compute::FactorResult<factor::compute::SignalSet>::failure(
            factor::compute::FactorError::InvalidUniverse);
    }

    // 构造最小 SignalSet 以满足统一合同（P3-T2 约束）
    factor::compute::SignalSet result;
    result.dates.push_back(dateRange.from);
    if (dateRange.from.value != dateRange.to.value) {
        result.dates.push_back(dateRange.to);
    }
    result.instruments = universe;

    // 策略信号使用固定 SignalId = 1（单一策略信号维度）
    factor::compute::SignalId strategySignal;
    strategySignal.value = 1U;
    result.signals.push_back(strategySignal);

    // 预分配平展数据
    const size_t dateCount = result.dates.size();
    const size_t instCount = result.instruments.size();
    const size_t signalCount = result.signals.size();
    const size_t flatSize = dateCount * instCount * signalCount;

    result.values.resize(flatSize, 0.0);
    result.mask.resize(flatSize, 0U);
    result.isPartial = false;

    // 设置 SignalSetIndex（用于三维访问）
    result.index.timeStride = static_cast<int32_t>(instCount * signalCount);
    result.index.instrumentStride = static_cast<int32_t>(signalCount);
    result.index.factorStride = 1;

    result.progress.plannedFactorCount = 1U;
    result.progress.completedFactorCount = 1U;

    return factor::compute::FactorResult<factor::compute::SignalSet>::success(result);
}

} // namespace domain::strategy