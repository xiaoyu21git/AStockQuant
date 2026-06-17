#include "../include/FactorCompositor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace domain::strategy {

namespace {

/// @brief 三维展平索引 [time][instrument][signal]
inline std::size_t idx(
    std::int32_t t, std::int32_t i, std::int32_t s,
    std::int32_t timeStride, std::int32_t instrumentStride)
{
    return static_cast<std::size_t>(t) * static_cast<std::size_t>(timeStride)
        + static_cast<std::size_t>(i) * static_cast<std::size_t>(instrumentStride)
        + static_cast<std::size_t>(s);
}

} // anonymous namespace

CompositedSignal FactorCompositor::compose(
    const factor::compute::SignalSet& signalSet,
    const FactorCompositeSpec& spec) const
{
    if (!signalSet.isValid() || !spec.isValid()) {
        return CompositedSignal{};
    }

    const std::int32_t T = static_cast<std::int32_t>(signalSet.dates.size());
    const std::int32_t N = static_cast<std::int32_t>(signalSet.instruments.size());
    const std::uint32_t S = spec.signalCount;

    if (T == 0 || N == 0 || S == 0U) {
        return CompositedSignal{};
    }

    switch (spec.method) {
    case CompositeMethod::EqualWeight:
        return composeEqualWeight(signalSet, T, N, S);
    case CompositeMethod::CustomWeight:
        return composeCustomWeight(signalSet, spec.customWeights, T, N);
    case CompositeMethod::ICRankWeight:
        // ICRankWeight 需要外部传入 IC 向量，暂回退到等权
        INTERNAL_WARN_STREAM << "[FactorCompositor] ICRankWeight requires external IC vector, "
                             << "falling back to EqualWeight — strategy signals will be unweighted";
        return composeEqualWeight(signalSet, T, N, S);
    }

    return CompositedSignal{};
}

CompositedSignal FactorCompositor::composeEqualWeight(
    const factor::compute::SignalSet& signalSet,
    std::int32_t timeCount,
    std::int32_t instrumentCount,
    std::uint32_t signalCount)
{
    CompositedSignal result;
    result.timeCount = timeCount;
    result.instrumentCount = instrumentCount;
    const std::size_t flatSize = static_cast<std::size_t>(timeCount) * static_cast<std::size_t>(instrumentCount);
    result.values.resize(flatSize, factor::compute::signal_value_t{0});

    if (signalCount == 0U) {
        return result;
    }

    const double invSignalCount = 1.0 / static_cast<double>(signalCount);

    for (std::int32_t t = 0; t < timeCount; ++t) {
        for (std::int32_t i = 0; i < instrumentCount; ++i) {
            double sum = 0.0;
            for (std::uint32_t s = 0U; s < signalCount; ++s) {
                const std::size_t flatIdx = idx(t, i, static_cast<std::int32_t>(s),
                    signalSet.index.timeStride, signalSet.index.instrumentStride);
                if (flatIdx < signalSet.values.size() && signalSet.mask[flatIdx] == 0U) {
                    sum += static_cast<double>(signalSet.values[flatIdx]);
                }
            }
            const std::size_t outIdx = static_cast<std::size_t>(t) * static_cast<std::size_t>(instrumentCount) + static_cast<std::size_t>(i);
            result.values[outIdx] = static_cast<factor::compute::signal_value_t>(sum * invSignalCount);
        }
    }

    return result;
}

CompositedSignal FactorCompositor::composeCustomWeight(
    const factor::compute::SignalSet& signalSet,
    const std::vector<double>& weights,
    std::int32_t timeCount,
    std::int32_t instrumentCount)
{
    CompositedSignal result;
    result.timeCount = timeCount;
    result.instrumentCount = instrumentCount;
    const std::size_t flatSize = static_cast<std::size_t>(timeCount) * static_cast<std::size_t>(instrumentCount);
    result.values.resize(flatSize, factor::compute::signal_value_t{0});

    const std::uint32_t signalCount = static_cast<std::uint32_t>(weights.size());
    if (signalCount == 0U) {
        return result;
    }

    // 验证权重总和是否有效
    double weightSum = 0.0;
    for (double w : weights) {
        weightSum += std::abs(w);
    }
    if (weightSum < std::numeric_limits<double>::epsilon()) {
        return result;
    }
    const double invWeightSum = 1.0 / weightSum;

    for (std::int32_t t = 0; t < timeCount; ++t) {
        for (std::int32_t i = 0; i < instrumentCount; ++i) {
            double sum = 0.0;
            for (std::uint32_t s = 0U; s < signalCount; ++s) {
                const std::size_t flatIdx = idx(t, i, static_cast<std::int32_t>(s),
                    signalSet.index.timeStride, signalSet.index.instrumentStride);
                if (flatIdx < signalSet.values.size() && signalSet.mask[flatIdx] == 0U) {
                    sum += static_cast<double>(signalSet.values[flatIdx]) * weights[s];
                }
            }
            const std::size_t outIdx = static_cast<std::size_t>(t) * static_cast<std::size_t>(instrumentCount) + static_cast<std::size_t>(i);
            result.values[outIdx] = static_cast<factor::compute::signal_value_t>(sum * invWeightSum);
        }
    }

    return result;
}

} // namespace domain::strategy